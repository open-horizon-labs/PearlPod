#!/usr/bin/env python3
"""Deploy the private syncer image via Portainer, keeping the previous container for rollback."""
import argparse
import io
import json
from pathlib import Path
import ssl
import subprocess
import tarfile
import time
import urllib.parse
import urllib.request


class Portainer:
    def __init__(self, url, endpoint, reference, insecure=False):
        self.base=url.rstrip('/')+'/api/endpoints/'+str(endpoint)+'/docker'
        self.token=subprocess.check_output(['op','read',reference],text=True).strip()
        self.context=ssl._create_unverified_context() if insecure else ssl.create_default_context()

    def call(self, path, data=None, method=None, timeout=15, raw=False):
        body=data if isinstance(data,bytes) else json.dumps(data).encode() if data is not None else None
        request=urllib.request.Request(self.base+path,data=body,method=method,
            headers={'X-API-Key':self.token,'Content-Type':'application/x-tar' if isinstance(data,bytes) else 'application/json'})
        with urllib.request.urlopen(request,timeout=timeout,context=self.context) as response:
            result=response.read()
        return result if raw else json.loads(result) if result else None


def deploy(api, args):
    if args.source_dir:
        buffer=io.BytesIO()
        with tarfile.open(fileobj=buffer,mode='w') as archive:
            data=('FROM '+args.image+'\nCOPY *.py /app/\n').encode()
            info=tarfile.TarInfo('Dockerfile');info.size=len(data);info.mode=0o644
            archive.addfile(info,io.BytesIO(data))
            for path in sorted(args.source_dir.glob('*.py')):
                archive.add(path,arcname=path.name,recursive=False)
        result=api.call('/build?rm=true&t='+urllib.parse.quote(args.image,safe=''),buffer.getvalue(),timeout=90,raw=True)
        for line in result.splitlines():
            if json.loads(line).get('error'):raise RuntimeError('Incremental image build failed')
    if args.image_archive:
        api.call('/images/load',args.image_archive.read_bytes(),timeout=180,raw=True)
    containers=api.call('/containers/json?all=1')
    old=next((c for c in containers if '/pearlpod-syncer' in c['Names']),None)
    if old and old.get('Labels',{}).get('org.pearlpod.role')!='syncer':
        raise ValueError('Existing container is not managed by PearlPod')
    init=None
    try:
        init=api.call('/containers/create?name=pearlpod-syncer-init-'+str(int(time.time())),{
            'Image':args.image,'User':'0:0','Entrypoint':['python','-c'],
            'Cmd':["from pathlib import Path; import os; p=Path('/deployment'); [(p/x).mkdir(parents=True,exist_ok=True) for x in ('cache','secrets')]; [os.chown(p/x,1000,1000) for x in ('cache','secrets')]; os.chmod(p/'secrets',0o700); assert Path('/music').is_dir()"],
            'Labels':{'org.pearlpod.role':'deployment-init'},
            'HostConfig':{'Binds':[args.deployment_dir+':/deployment',args.music+':/music:ro']}})['Id']
        api.call('/containers/'+init+'/start',{},method='POST')
        for _ in range(30):
            state=api.call('/containers/'+init+'/json')['State']
            if not state['Running']:break
            time.sleep(.2)
        if state['Running'] or state['ExitCode']:
            raise RuntimeError('Deployment directory initialization failed')
        if args.plex_token_file:
            buffer=io.BytesIO()
            with tarfile.open(fileobj=buffer,mode='w') as archive:
                data=args.plex_token_file.read_bytes()
                info=tarfile.TarInfo('plex-token');info.size=len(data);info.mode=0o600;info.uid=1000;info.gid=1000
                archive.addfile(info,io.BytesIO(data))
            api.call('/containers/'+init+'/archive?path=/deployment/secrets',buffer.getvalue(),method='PUT',raw=True)
    finally:
        if init:api.call('/containers/'+init+'?force=true',method='DELETE')
    config={'Image':args.image,'User':'1000:1000',
        'Cmd':['--plex-token','/run/secrets/plex_token','--plex-url',args.plex_url,
               '--source-root',args.source_root,'--music','/music','--cache','/cache',
               '--advertise-ip',args.host_ip,'--poll-interval',str(args.poll_interval)],
        'Labels':{'org.pearlpod.role':'syncer'},
        'Healthcheck':{'Test':['CMD','python','-c',"import urllib.request; urllib.request.urlopen('http://127.0.0.1:8787/health',timeout=3).read()"],
                       'Interval':30000000000,'Timeout':5000000000,'Retries':3},
        'HostConfig':{'NetworkMode':'host','RestartPolicy':{'Name':'unless-stopped'},
                      'Binds':[args.music+':/music:ro',args.deployment_dir+'/cache:/cache',
                               args.deployment_dir+'/secrets/plex-token:/run/secrets/plex_token:ro']}}
    renamed=False;new=None
    try:
        if old:
            api.call('/containers/'+old['Id']+'/stop?t=10',{},method='POST')
            name='pearlpod-syncer-rollback-'+str(int(time.time()))
            api.call('/containers/'+old['Id']+'/rename?name='+name,{},method='POST')
            renamed=True
        new=api.call('/containers/create?name=pearlpod-syncer',config)['Id']
        api.call('/containers/'+new+'/start',{},method='POST')
        for _ in range(50):
            state=api.call('/containers/'+new+'/json')['State']
            if not state['Running']:raise RuntimeError('Syncer exited')
            if state.get('Health',{}).get('Status')=='healthy':break
            time.sleep(1)
        else:raise TimeoutError('Container liveness did not pass')
        print('Deployed pearlpod-syncer; liveness passed. Source readiness and device sync require separate checks.')
        if old:print('Rollback container retained:',name)
    except Exception:
        if new:api.call('/containers/'+new+'?force=true',method='DELETE')
        if renamed:api.call('/containers/'+old['Id']+'/rename?name=pearlpod-syncer',{},method='POST')
        if old:api.call('/containers/'+old['Id']+'/start',{},method='POST')
        raise


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--portainer-url',required=True)
    p.add_argument('--endpoint',type=int,required=True)
    p.add_argument('--portainer-token-reference',required=True,help='1Password op:// reference, read only at runtime')
    p.add_argument('--insecure',action='store_true',help='Use only for a known NAS with a self-signed certificate')
    p.add_argument('--image',default='pearlpod-syncer:automation')
    p.add_argument('--image-archive',type=Path)
    p.add_argument('--source-dir',type=Path,help='Incremental Python-only update of an already installed image')
    p.add_argument('--host-ip',required=True)
    p.add_argument('--music',required=True)
    p.add_argument('--source-root',required=True)
    p.add_argument('--plex-url',required=True)
    p.add_argument('--deployment-dir',required=True)
    p.add_argument('--plex-token-file',type=Path,help='First deployment only; otherwise reuse the existing host token')
    p.add_argument('--poll-interval',type=int,default=60)
    a=p.parse_args()
    if a.source_dir and a.image_archive:p.error('Choose a full image archive or an incremental source update')
    if not a.deployment_dir.startswith('/') or a.deployment_dir=='/' or ':' in a.deployment_dir or not a.music.startswith('/') or ':' in a.music:
        p.error('Expected absolute NAS paths without bind separators')
    deploy(Portainer(a.portainer_url,a.endpoint,a.portainer_token_reference,a.insecure),a)


if __name__=='__main__':
    try:main()
    except Exception as error:raise SystemExit('Deployment failed: '+type(error).__name__) from None
