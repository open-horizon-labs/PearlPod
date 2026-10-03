#!/usr/bin/env python3
"""Collect PearlPod's SD sweep, TCP-to-RAM and TCP-to-card diagnostics.
Uses bytes from an existing NAS/exported music file; never activates a catalog.
"""
import argparse
import ftplib
import json
import socket
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import serial
from serial.tools import list_ports


def tcp_info(sock):
    """macOS TCP_CONNECTION_INFO; omit on other platforms."""
    if sys.platform != 'darwin':
        return {}
    try:
        data = sock.getsockopt(socket.IPPROTO_TCP, 0x106, 112)
        values = struct.unpack_from('=12I', data, 4)
        packets, sent, retransmitted, received_packets, received, out_of_order, retransmit_packets = struct.unpack_from('=7Q', data, 56)
        return dict(cwnd=values[5], send_window=values[6], buffered_send_bytes=values[7],
                    rtt_ms=values[9], smoothed_rtt_ms=values[10],
                    sent_bytes=sent, retransmitted_bytes=retransmitted, retransmit_packets=retransmit_packets)
    except (OSError, struct.error):
        return {}


class Capture:
    def __init__(self, device, path):
        self.port = serial.Serial(device, 115200, timeout=0.2)
        self.file = path.open('w')
        self.lock = threading.Lock()
        self.command_lock = threading.Lock()
        self.stop = threading.Event()
        self.ready = threading.Condition()
        self.lines = []
        self.start = time.monotonic()
        self.thread = threading.Thread(target=self.read, daemon=True)
        self.thread.start()

    def record(self, kind, **values):
        row = dict(kind=kind, elapsed=round(time.monotonic()-self.start, 6), **values)
        with self.lock:
            self.file.write(json.dumps(row)+'\n')
            self.file.flush()

    def read(self):
        while not self.stop.is_set():
            try:
                line = self.port.readline().decode('utf-8', errors='replace').strip()
            except (OSError, serial.SerialException) as error:
                self.record('serial_error', error=str(error))
                return
            if not line:
                continue
            self.record('serial', line=line)
            with self.ready:
                self.lines.append(line)
                self.ready.notify_all()

    def command(self, value):
        with self.ready:
            position = len(self.lines)
        with self.command_lock:
            self.port.write((value+'\n').encode())
        self.record('command', value=value)
        return position

    def wait(self, text, position, timeout=10):
        deadline = time.monotonic()+timeout
        with self.ready:
            while time.monotonic() < deadline:
                for line in self.lines[position:]:
                    if text in line:
                        return line
                self.ready.wait(min(0.5, max(0, deadline-time.monotonic())))
        raise TimeoutError(f'No {text!r} response')

    def sample(self):
        self.command('trace status')
        self.command('sync trace')

    def close(self):
        self.stop.set()
        self.thread.join(2)
        self.port.close()
        self.file.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--serial')
    parser.add_argument('--host-ip', required=True)
    parser.add_argument('--port', type=int, default=8877)
    parser.add_argument('--bytes', type=int, default=32*1024*1024)
    parser.add_argument('--output', type=Path, default=Path('/tmp/pearl-tracer.jsonl'))
    parser.add_argument('--mode', choices=['all', 'sd', 'ram', 'card'], default='all')
    args = parser.parse_args()
    if args.bytes <= 0 or args.bytes > 256*1024*1024:
        parser.error('--bytes must be between 1 and 256 MiB')
    # Load once, before any measurement; reuse actual existing music bytes.
    with args.source.open('rb') as stream:
        sample = stream.read(1024*1024)
    if not sample:
        parser.error('Source file is empty')
    device = args.serial
    if not device:
        ports = [p.device for p in list_ports.comports() if p.vid == 0x303a and p.pid == 0x1001]
        if len(ports) != 1:
            parser.error('Specify --serial; expected one connected ESP32 USB device')
        device = ports[0]
    destination = []
    triggered = threading.Event()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            if self.path != '/sync':
                self.send_error(404)
                return
            length = int(self.headers.get('Content-Length', '0'))
            if length > 4096:
                self.send_error(413)
                return
            self.rfile.read(length)
            destination.append(self.client_address[0])
            self.send_response(202)
            self.send_header('Content-Length', '0')
            self.end_headers()
            triggered.set()

    http = ThreadingHTTPServer(('0.0.0.0', args.port), Handler)
    threading.Thread(target=http.serve_forever, daemon=True).start()
    capture = Capture(device, args.output)
    sampler_stop = threading.Event()
    sampler = None
    try:
        capture.wait('PEARL status', capture.command('status'))
        position = capture.command('status')
        status = capture.wait('PEARL status', position)
        if 'paused=1' not in status:
            raise RuntimeError('Pause music before running diagnostics')
        capture.command('wifi off')
        deadline = time.monotonic()+15
        while time.monotonic() < deadline:
            line = capture.wait('PEARL wifi', capture.command('wifi status'))
            if 'enabled=0' in line:
                break
            time.sleep(0.2)
        else:
            raise TimeoutError('WiFi did not turn off before SD sweep')
        if args.mode in ('all', 'sd'):
            capture.wait('start_ok=1', capture.command('trace start'))
            position = capture.command('trace sd')
            deadline = time.monotonic()+300
            while time.monotonic() < deadline:
                try:
                    capture.wait('tracer_sd_done', position, timeout=1)
                    break
                except TimeoutError:
                    capture.sample()
            else:
                raise TimeoutError('SD sweep timed out')
            capture.sample()
            capture.wait('tracer_events_end', capture.command('trace events 128'))
            capture.command('trace stop')
            with capture.ready:
                results = [line for line in capture.lines[position:] if 'PEARL tracer_sd mode=' in line]
            if not results:
                raise RuntimeError('SD sweep produced no results; inspect trace log')
            for line in results:
                print(line, flush=True)

        for mode in ('ram', 'card'):
            if args.mode not in ('all', mode):
                continue
            triggered.clear()
            capture.wait('start_ok=1', capture.command('trace start'))
            position = capture.command(f'trace net http://{args.host_ip}:{args.port}')
            capture.wait('probe_start_ok=1', position)
            if not triggered.wait(45):
                raise TimeoutError('Player did not reach diagnostic HTTP trigger')
            sampler_stop.clear()

            def sample_loop():
                while not sampler_stop.wait(1):
                    capture.sample()

            sampler = threading.Thread(target=sample_loop, daemon=True)
            sampler.start()
            remote = '/.pearl/.bench-ram' if mode == 'ram' else '/.pearl/.trace-card.tmp'
            ftp = ftplib.FTP()
            ftp.connect(destination[-1], 2121, timeout=120)
            ftp.login()
            ftp.voidcmd('TYPE I')
            # Do not overwrite a pre-existing diagnostic file.
            if mode == 'card':
                try:
                    if ftp.size(remote) is not None:
                        raise RuntimeError(f'Existing {remote}; remove stale diagnostic first')
                except ftplib.error_perm as error:
                    if not str(error).startswith('550'):
                        raise
            sent = 0
            longest_send_us = 0
            last_report = time.monotonic()
            begin = last_report
            try:
                with ftp.transfercmd('STOR '+remote) as data:
                    while sent < args.bytes:
                        offset = sent % len(sample)
                        block = sample[offset:offset+min(65536, args.bytes-sent)]
                        before = time.monotonic_ns()
                        data.sendall(block)
                        elapsed_us = (time.monotonic_ns()-before)//1000
                        longest_send_us = max(longest_send_us, elapsed_us)
                        sent += len(block)
                        now = time.monotonic()
                        if now-last_report >= 1:
                            capture.record('sender', mode=mode, bytes=sent, seconds=now-begin,
                                           send_max_us=longest_send_us, tcp=tcp_info(data))
                            last_report = now
                    capture.record('sender_final_socket', mode=mode, tcp=tcp_info(data))
                socket_done = time.monotonic()
                reply = ftp.voidresp()  # Includes receiver drain/close; never omit it from rate.
                end = time.monotonic()
                row = dict(mode=mode, bytes=sent, seconds=end-begin, MB_per_second=sent/(end-begin)/1e6,
                           sender_seconds=socket_done-begin, receiver_drain_seconds=end-socket_done,
                           longest_send_us=longest_send_us, reply=reply)
                capture.record('result', **row)
                print(json.dumps(row), flush=True)
            finally:
                if mode == 'card':
                    try:
                        ftp.delete(remote)
                    except ftplib.all_errors as error:
                        capture.record('cleanup_error', path=remote, error=str(error))
                ftp.close()
                sampler_stop.set()
                sampler.join(2)
                capture.sample()
                capture.wait('tracer_events_end', capture.command('trace events 128'))
                capture.command('trace cancel')
                # Bounded wait for FTP/network teardown, observing the device.
                deadline = time.monotonic()+15
                while time.monotonic() < deadline:
                    line = capture.wait('PEARL wifi', capture.command('wifi status'))
                    if 'enabled=0' in line:
                        break
                    time.sleep(0.5)
                else:
                    raise TimeoutError('Diagnostic network did not turn off')
                capture.command('trace stop')
        print(f'Trace saved to {args.output}', flush=True)
    finally:
        sampler_stop.set()
        if sampler:
            sampler.join(2)
        capture.command('trace cancel')
        capture.command('trace stop')
        time.sleep(0.3)
        capture.close()
        http.shutdown()
        http.server_close()


if __name__ == '__main__':
    main()
