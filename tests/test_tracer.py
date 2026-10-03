#!/usr/bin/env python3
"""Public tracer API checks with a simulated SD host, not throughput evidence."""
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]

class TracerTests(unittest.TestCase):
    def test_card_interceptor_bounds_resets_and_preserves_host_result(self):
        binary='/tmp/pearl-tracer-host'
        subprocess.run(['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-DPEARL_TRACER_HOST',
            '-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I','main','-I','tests/tracer_host',
            'tests/test_tracer.c','main/tracer.c','-pthread','-o',binary],cwd=ROOT,check=True)
        output=subprocess.run([binary],check=True,text=True,capture_output=True).stdout
        states={};name=None
        for line in output.splitlines():
            if line.startswith('CASE '):name=line[5:]
            elif line.startswith('PEARL tracer {'):states[name]=json.loads(line[len('PEARL tracer '):])
        self.assertEqual(states['classified']['commands'],5)
        self.assertEqual(states['classified']['writes'],2)
        self.assertEqual(states['classified']['reads'],1)
        self.assertEqual(states['classified']['status_commands'],2)
        self.assertEqual(states['classified']['busy_statuses'],1)
        self.assertEqual(states['classified']['status_gap_us'],100)
        self.assertEqual(states['classified']['errors'],1)
        self.assertEqual(states['classified']['write_bytes'],8704)
        self.assertEqual(states['classified']['read_bytes'],16384)
        self.assertEqual(sum(states['classified']['latency_bins']),5)
        self.assertEqual(states['wrapped']['commands'],2105)
        self.assertEqual(states['wrapped']['events_overwritten'],57)
        self.assertEqual(states['stopped']['commands'],2105)
        self.assertEqual(states['stopped']['enabled'],0)
        self.assertEqual(states['reset']['commands'],0)
        self.assertEqual(states['long']['command_us'],5200000000)
        self.assertEqual(len([l for l in output.splitlines() if l.startswith('PEARL tracer_event ')]),16)
        self.assertIn('PEARL tracer_slowest',output)

    def test_macos_tcp_abi_offsets_and_units(self):
        spec=importlib.util.spec_from_file_location('trace_sync',ROOT/'tools/trace_sync.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        class Sock:
            def getsockopt(self,*args):
                data=bytearray(112)
                struct.pack_into('=12I',data,4,*range(12))
                struct.pack_into('=7Q',data,56,100,10000,1500,200,20000,0,3)
                return data
        original=module.sys.platform
        try:
            module.sys.platform='darwin';values=module.tcp_info(Sock())
        finally:module.sys.platform=original
        self.assertEqual(values['cwnd'],5)
        self.assertEqual(values['send_window'],6)
        self.assertEqual(values['rtt_ms'],9)
        self.assertEqual(values['smoothed_rtt_ms'],10)
        self.assertEqual(values['retransmitted_bytes'],1500)
        self.assertEqual(values['retransmit_packets'],3)

if __name__=='__main__':unittest.main()
