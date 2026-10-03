"""Pod HTTP trigger -> NAS preparation -> lftp upload. No sync authentication."""
import argparse
import ipaddress
import json
from pathlib import Path
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from plexapi.server import PlexServer
from publisher import publish
from transfer import deliver, CapacityError, report_failure
from zeroconf import Zeroconf, ServiceInfo


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--plex-url',default='http://192.0.2.2:32400')
    p.add_argument('--plex-token',type=Path,required=True)
    p.add_argument('--source-root',type=Path,default=Path('/share/Media/Music'))
    p.add_argument('--music',type=Path,required=True)
    p.add_argument('--cache',type=Path,required=True)
    p.add_argument('--port',type=int,default=8787)
    p.add_argument('--advertise-ip',required=True)
    a=p.parse_args()
    server=PlexServer(a.plex_url,token=a.plex_token.read_text().strip(),timeout=15)
    gate=threading.Lock(); state={'status':'idle','publisher':'preparing'}
    def refresh():
        while True:
            state['publisher']='preparing'
            try:
                before=time.monotonic()
                head=publish(server,a.source_root,a.music,a.cache)
                state['publication_ms']=round((time.monotonic()-before)*1000,2)
                state.update(publisher='ready',source_checked_at=int(time.time()),published_catalog=head['catalog'])
            except Exception as e:
                state.update(publisher='error',publication_error=type(e).__name__)
            time.sleep(60)
    threading.Thread(target=refresh,daemon=True).start()
    def sync(address,port,free_bytes):
        try:
            head=json.loads((a.cache/'head.json').read_text())
            state['status']='transferring'
            state['transfer_trace']={}
            sha=deliver(a.cache,head,address,port,free_bytes=free_bytes,trace=lambda metrics:state.update(transfer_trace=metrics))
            state.update(status='uploaded',catalog=sha)
        except Exception as e:
            state.update(status='error',error=type(e).__name__)
            try:report_failure(address,port,'card_full' if isinstance(e,CapacityError) else 'transfer_failed',normal=head.get('format')==2)
            except Exception:pass
        finally:gate.release()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def reply(self,status,value):
            data=json.dumps(value).encode();self.send_response(status)
            self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)))
            self.end_headers();self.wfile.write(data)
        def do_GET(self):
            if self.path!='/status':self.reply(404,{'error':'not found'});return
            self.reply(200,dict(state))
        def do_POST(self):
            if self.path!='/sync':self.reply(404,{'error':'not found'});return
            try:
                self.connection.settimeout(5)
                length=int(self.headers.get('Content-Length','0'))
                if not 0<length<=256:raise ValueError()
                data=json.loads(self.rfile.read(length))
                if not isinstance(data,dict):raise ValueError()
                # Destination is the caller, never an arbitrary supplied host.
                address=self.client_address[0];ip=ipaddress.ip_address(address)
                if not ip.is_private or ip.is_loopback:raise ValueError()
                port=data.get('ftp_port',21)
                if type(port) is not int or not 1<=port<=65535:raise ValueError()
                free_bytes=data.get('free_bytes')
                if free_bytes is not None and (type(free_bytes) is not int or free_bytes<0):raise ValueError()
            except (ValueError,OSError):self.reply(400,{'error':'invalid sync trigger'});return
            if not (a.cache/'head.json').is_file() or state.get('publisher')=='error':self.reply(503,{'error':'library not ready; retry after preparation'});return
            if not gate.acquire(blocking=False):self.reply(409,{'error':'sync busy'});return
            state.pop('error',None);state.update(status='queued')
            threading.Thread(target=sync,args=(address,port,free_bytes),daemon=True).start()
            self.reply(202,{'status':'queued'})
    http=ThreadingHTTPServer(('0.0.0.0',a.port),Handler);http.daemon_threads=True
    z=Zeroconf()
    info=ServiceInfo('_pearlpod-sync._tcp.local.','PearlPod Syncer._pearlpod-sync._tcp.local.',
        addresses=[socket.inet_aton(a.advertise_ip)],port=a.port,properties={'version':'1','transport':'ftp'},server='pearlpod-syncer.local.')
    z.register_service(info)
    try:http.serve_forever()
    finally:z.unregister_service(info);z.close();http.server_close()


if __name__=='__main__':
    try:main()
    except Exception as e:raise SystemExit('Sync service failed: '+type(e).__name__) from None
