#!/usr/bin/env python3
"""Measure actual FTP/card transfer using an existing exported NAS MP3.
Run only while the ordinary sync service is stopped; the temporary probe is
never referenced by a catalog and is removed after verification.
"""
import argparse,ftplib,hashlib,json,threading,time
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('--port',type=int,default=8787);a=p.parse_args()
expected=hashlib.sha256(a.source.read_bytes()).hexdigest();destination=[];started=threading.Event()
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_POST(self):
  if self.path!='/sync':self.send_error(404);return
  n=int(self.headers.get('Content-Length',0));self.rfile.read(n)
  self.send_response(202);self.send_header('Content-Length','0');self.end_headers()
  destination.append(self.client_address[0]);started.set()
http=ThreadingHTTPServer(('0.0.0.0',a.port),Handler);threading.Thread(target=http.serve_forever,daemon=True).start()
print('Waiting for PearlPod sync trigger',flush=True)
try:
 if not started.wait(60):raise TimeoutError('No player trigger')
 remote='/objects/.throughput-probe.tmp'
 with ftplib.FTP() as ftp:
  ftp.connect(destination[0],2121,timeout=180);ftp.login();ftp.sock.settimeout(300)
  total=a.source.stat().st_size;count=0;last=time.monotonic();begin=last
  def progress(block):
   global count,last
   count+=len(block);now=time.monotonic()
   if now-last>=5:print(json.dumps({'sent':count,'total':total,'seconds':round(now-begin,2)}),flush=True);last=now
  try:
   with a.source.open('rb') as f:ftp.storbinary('STOR '+remote,f,blocksize=65536,callback=progress)
   elapsed=time.monotonic()-begin
   print(json.dumps({'upload_bytes':total,'upload_seconds':round(elapsed,3),'bytes_per_second':round(total/elapsed)}),flush=True)
   digest=hashlib.sha256();begin=time.monotonic();ftp.retrbinary('RETR '+remote,digest.update,blocksize=65536)
   assert digest.hexdigest()==expected,'Card copy hash mismatch'
   print(json.dumps({'card_readback_verified':True,'readback_seconds':round(time.monotonic()-begin,3)}),flush=True)
  finally:
   try:ftp.delete(remote)
   except ftplib.all_errors:pass
finally:http.shutdown();http.server_close()
