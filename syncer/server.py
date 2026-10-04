"""Pod HTTP trigger -> NAS preparation -> lftp upload. No sync authentication."""
import argparse
import copy
import ipaddress
import json
from pathlib import Path
import socket
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from orchestrator import Publications
from transfer import deliver, CapacityError, report_failure
from zeroconf import Zeroconf, ServiceInfo

DEFAULT_POLL_INTERVAL = 300


def refresh_all(publications, processing):
    """Refresh each configured publication sequentially in one checker cycle."""
    for publication in publications:
        try:
            with processing:
                publication.refresh(publication.args.poll_timeout)
        except Exception:
            # Each Publications instance records its own error for /status.
            continue


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--plex-url',default='http://192.0.2.2:32400')
    p.add_argument('--plex-token',type=Path,required=True)
    p.add_argument('--source-root',type=Path,default=Path('/share/Media/Music'))
    p.add_argument('--music',type=Path,required=True)
    p.add_argument('--cache',type=Path,required=True)
    p.add_argument('--port',type=int,default=8787)
    p.add_argument('--advertise-ip',required=True)
    p.add_argument('--service-name',default='PearlPod Syncer')
    p.add_argument('--poll-interval',type=float,default=DEFAULT_POLL_INTERVAL)
    p.add_argument('--poll-timeout',type=float,default=600)
    p.add_argument('--refresh-timeout',type=float,default=120)
    p.add_argument('--cache-limit',type=int,default=20*1024**3)
    p.add_argument('--pearl-output',type=Path,help='Open-share directory for PP: prepared files')
    p.add_argument('--jonah-output',type=Path,help='Open share directory for prepared J: playlists; excluded from Pod sync')
    p.add_argument('--jonah-cache',type=Path,help='Separate cache directory for the J: processor')
    a=p.parse_args()
    if a.poll_interval <= 0 or a.poll_timeout <= 0 or not 0 < a.refresh_timeout <= 120 or a.cache_limit <= 0:
        p.error('Invalid scheduler/cache bounds')
    a.playlist_prefix='PP:'
    a.open_share=a.pearl_output
    publications=Publications(a)
    jonah_publications=None
    if a.jonah_output is not None:
        pp_cache=a.cache.resolve()
        if a.jonah_cache is None:
            p.error('--jonah-output requires --jonah-cache')
        jonah_cache=a.jonah_cache.resolve()
        if pp_cache==jonah_cache or pp_cache in jonah_cache.parents or jonah_cache in pp_cache.parents:
            p.error('--jonah-output requires a separate --jonah-cache outside the PearlPod cache')
        jonah_args=copy.copy(a)
        jonah_args.cache=a.jonah_cache
        jonah_args.cache_limit=a.cache_limit
        jonah_args.poll_interval=a.poll_interval
        jonah_args.playlist_prefix='J:'
        jonah_args.open_share=a.jonah_output
        jonah_publications=Publications(jonah_args)
    gate=threading.Lock(); processing=threading.Lock(); poll_stop=threading.Event(); state={'status':'idle'}
    processors=[publications]
    if jonah_publications is not None:processors.append(jonah_publications)
    def poll_all():
        while not poll_stop.is_set():
            refresh_all(processors,processing)
            poll_stop.wait(a.poll_interval)
    threading.Thread(target=poll_all,daemon=True).start()
    def sync(address,port,free_bytes,progress=False):
        try:
            state['status']='preparing'
            def upload(head):
                state['status']='transferring'
                state['transfer_trace']={}
                sha=deliver(a.cache,head,address,port,free_bytes=free_bytes,trace=lambda metrics:state.update(transfer_trace=metrics),progress=progress)
                state.update(status='uploaded',catalog=sha)
            with processing:publications.refresh(a.refresh_timeout, consume=upload)
        except Exception as e:
            state.update(status='error',error=type(e).__name__)
            try:report_failure(address,port,'card_full' if isinstance(e,CapacityError) else 'transfer_failed',normal=True)
            except Exception:pass
        finally:gate.release()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def reply(self,status,value):
            data=json.dumps(value).encode();self.send_response(status)
            self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)))
            self.end_headers();self.wfile.write(data)
        def do_GET(self):
            if self.path=='/health':self.reply(200,{'status':'alive'});return
            if self.path!='/status':self.reply(404,{'error':'not found'});return
            response={**dict(state),**publications.status()}
            if jonah_publications is not None:response['jonah']=jonah_publications.status()
            self.reply(200,response)
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
            if not (a.cache/'head.json').is_file():
                self.reply(503,{'error':'library preparing; retry after first export'});return
            if not gate.acquire(blocking=False):self.reply(409,{'error':'sync busy'});return
            state.pop('error',None);state.update(status='queued')
            threading.Thread(target=sync,args=(address,port,free_bytes,type(data.get('progress_version')) is int and data.get('progress_version')==1),daemon=True).start()
            self.reply(202,{'status':'queued'})
    http=ThreadingHTTPServer(('0.0.0.0',a.port),Handler);http.daemon_threads=True
    z=Zeroconf()
    info=ServiceInfo('_pearlpod-sync._tcp.local.',a.service_name+'._pearlpod-sync._tcp.local.',
        addresses=[socket.inet_aton(a.advertise_ip)],port=a.port,properties={'version':'1','transport':'ftp'},server='pearlpod-syncer.local.')
    z.register_service(info)
    try:http.serve_forever()
    finally:
        poll_stop.set();publications.stop.set()
        if jonah_publications is not None:jonah_publications.stop.set()
        z.unregister_service(info);z.close();http.server_close()


if __name__=='__main__':
    try:main()
    except Exception as e:raise SystemExit('Sync service failed: '+type(e).__name__) from None
