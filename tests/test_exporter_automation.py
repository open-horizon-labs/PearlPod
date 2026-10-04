"""Adversarial scheduler, retention and same-length delivery checks."""
import hashlib
import json
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'syncer'))
from orchestrator import Publications, PreparationTimeout, run_publication
from cache import maintain
from publisher import publish
from transfer import deliver


class AutomationTests(unittest.TestCase):
    def args(self, cache):
        return SimpleNamespace(cache=cache, poll_interval=.01, poll_timeout=.2,
                               plex_url='http://127.0.0.1:1', plex_token=cache/'token',
                               source_root=cache, music=cache, cache_limit=1024**3)

    def test_request_checks_after_running_poll_and_pins_delivery(self):
        with tempfile.TemporaryDirectory() as directory:
            args=self.args(Path(directory));started=threading.Event();release=threading.Event()
            calls=[];active=[]
            def runner(args, timeout):
                calls.append(len(calls)+1);active.append(1)
                self.assertEqual(len(active),1)
                if len(calls)==1:started.set();release.wait(1)
                active.pop();return {'catalog':str(len(calls))}
            service=Publications(args,runner)
            poll=threading.Thread(target=service.refresh,args=(2,));poll.start();started.wait(1)
            result=[]
            job=threading.Thread(target=lambda:service.refresh(2, consume=lambda h:result.append(h['catalog'])))
            job.start();release.set();poll.join();job.join()
            self.assertEqual(calls,[1,2]);self.assertEqual(result,['2'])

    def test_delivery_holds_publication_lock(self):
        with tempfile.TemporaryDirectory() as directory:
            service=Publications(self.args(Path(directory)),lambda a,t:{'catalog':'pinned'})
            def consume(head):
                self.assertFalse(service.lock.acquire(blocking=False))
                self.assertEqual(head['catalog'],'pinned')
            service.refresh(1,consume)

    def test_failure_keeps_last_success_and_recovers(self):
        with tempfile.TemporaryDirectory() as directory:
            cache=Path(directory);(cache/'head.json').write_text(json.dumps({'catalog':'old','source_checked_at':1}))
            def fail(a,t):raise OSError('Plex unavailable')
            service=Publications(self.args(cache),fail)
            with self.assertRaises(OSError):service.refresh(.1)
            self.assertEqual(service.status()['published_catalog'],'old')
            self.assertEqual(service.status()['source_checked_at'],1)
            service.runner=lambda a,t:{'catalog':'new','source_checked_at':2}
            service.refresh(.1)
            self.assertIsNone(service.status()['publication_error'])
            self.assertEqual(service.status()['published_catalog'],'new')

    def test_waiting_for_poll_is_bounded_and_never_delivers_stale(self):
        with tempfile.TemporaryDirectory() as directory:
            service=Publications(self.args(Path(directory)))
            service.lock.acquire();called=[];before=time.monotonic()
            try:
                with self.assertRaises(PreparationTimeout):service.refresh(.03,lambda h:called.append(h))
            finally:service.lock.release()
            self.assertLess(time.monotonic()-before,.2);self.assertFalse(called)

    def test_process_timeout_kills_group(self):
        with tempfile.TemporaryDirectory() as directory:
            args=self.args(Path(directory))
            original=subprocess.Popen
            def sleeper(*a,**kw):return original([sys.executable,'-c','import time; time.sleep(60)'],**kw)
            before=time.monotonic()
            with patch('orchestrator.subprocess.Popen',side_effect=sleeper):
                with self.assertRaises(PreparationTimeout):run_publication(args,.03)
            self.assertLess(time.monotonic()-before,1)

    def test_retention_preserves_current_previous_and_refuses_full_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            cache=Path(directory);(cache/'catalogs').mkdir();(cache/'objects').mkdir()
            import os
            for i in range(3):
                (cache/'objects'/str(i)).write_bytes(b'asset')
                p=cache/'catalogs'/str(i);p.write_text(json.dumps({'file':'song','cache_source':str(i)})+'\n')
                os.utime(p,(1+i,1+i));os.utime(cache/'objects'/str(i),(1,1))
            (cache/'head.json').write_text('{"catalog":"2"}')
            (cache/'objects'/'abandoned.tmp').write_bytes(b'incomplete')
            maintain(cache,1024)
            self.assertFalse((cache/'objects'/'0').exists());self.assertTrue((cache/'objects'/'1').exists());self.assertTrue((cache/'objects'/'2').exists())
            self.assertFalse((cache/'objects'/'abandoned.tmp').exists())
            with self.assertRaises(OSError):maintain(cache,1)
            self.assertEqual(json.loads((cache/'head.json').read_text())['catalog'],'2')

    def test_failed_candidates_cannot_evict_previous_publication(self):
        import os
        with tempfile.TemporaryDirectory() as directory:
            cache=Path(directory);(cache/'catalogs').mkdir();(cache/'objects').mkdir()
            for i in range(5):
                (cache/'objects'/str(i)).write_bytes(b'asset')
                catalog=cache/'catalogs'/str(i)
                catalog.write_text(json.dumps({'file':'song','cache_source':str(i)})+'\n')
                os.utime(catalog,(i+1,i+1));os.utime(cache/'objects'/str(i),(1,1))
            (cache/'head.json').write_text('{"catalog":"0","previous_catalog":"1"}')
            maintain(cache,4096)
            self.assertTrue((cache/'objects'/'0').exists())
            self.assertTrue((cache/'objects'/'1').exists())
            self.assertFalse((cache/'objects'/'2').exists())

    def test_deadline_stops_child_encoder_too(self):
        import os
        with tempfile.TemporaryDirectory() as directory:
            args=self.args(Path(directory));pidfile=args.cache/'child-pid'
            original=subprocess.Popen
            code="import subprocess,sys,time; from pathlib import Path; p=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)']); Path(sys.argv[1]).write_text(str(p.pid)); time.sleep(60)"
            def sleeper(*a,**kw):return original([sys.executable,'-c',code,str(pidfile)],**kw)
            with patch('orchestrator.subprocess.Popen',side_effect=sleeper):
                with self.assertRaises(PreparationTimeout):run_publication(args,.3)
            pid=int(pidfile.read_text())
            result=subprocess.run(['ps','-o','stat=','-p',str(pid)],capture_output=True,text=True)
            if result.stdout.strip() and not result.stdout.strip().startswith('Z'):
                os.kill(pid,signal.SIGKILL)
                self.fail('Child encoder survived the preparation deadline')

    def test_sidecar_added_during_preparation_rejects_candidate(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'song.mp3';source.write_bytes(b'fixture')
            cache=root/'cache';cache.mkdir();(cache/'head.json').write_text('prior')
            selected={'tracks':{'1':{'source':str(source),'album_id':'2','bytes':7}},'playlists':[]}
            server=SimpleNamespace(fetchItem=lambda key:SimpleNamespace(thumb=None))
            def mutate(*args, **kwargs):
                (root/'song.lrc').write_text('[00:01]late')
                return {}
            with patch('publisher.snapshot',return_value=selected),patch('publisher.prepare',side_effect=mutate),patch('layout.readable',return_value=({},[],{},None,None)):
                with self.assertRaisesRegex(ValueError,'Source inputs'):publish(server,root,root,cache)
            self.assertEqual((cache/'head.json').read_text(),'prior')

    def test_art_changed_at_same_url_rejects_candidate(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'song.mp3';source.write_bytes(b'fixture')
            cache=root/'cache';cache.mkdir();(cache/'head.json').write_text('prior')
            selected={'tracks':{'1':{'source':str(source),'album_id':'2','bytes':7}},'playlists':[]}
            with patch('publisher.snapshot',return_value=selected),patch('publisher.album_art',side_effect=[b'old art',b'new art']),patch('publisher.prepare',return_value={}),patch('layout.readable',return_value=({},[],{},None,None)):
                with self.assertRaisesRegex(ValueError,'artwork changed'):publish(None,root,root,cache)
            self.assertEqual((cache/'head.json').read_text(),'prior')

    def test_track_cache_skips_metadata_work_and_not_source_changes(self):
        from publisher import prepare_cached
        with tempfile.TemporaryDirectory() as directory:
            cache=Path(directory);(cache/'objects').mkdir()
            name=hashlib.sha256(b'cached lyric').hexdigest()+'.txt'
            (cache/'objects'/name).write_bytes(b'cached lyric')
            track={'source':'/source/song.mp3','id':'1'}
            result={'track':name,'art':'','lyrics':[]}
            with patch('publisher.prepare',return_value=result) as prepare:
                prepare_cached(track,cache,b'',{'source':[1]},'cover',1024**3)
                prepare_cached(track,cache,b'',{'source':[1]},'cover',1024**3)
                self.assertEqual(prepare.call_count,1)
                prepare_cached(track,cache,b'',{'source':[2]},'cover',1024**3)
                self.assertEqual(prepare.call_count,2)

    def test_prepared_audio_identity_survives_new_worker(self):
        from media import prepare, _verified, digest
        candidates=list(Path('.sync-state/cache/objects').glob('*.mp3'))
        if not candidates:self.skipTest('Requires a real NAS-prepared MP3')
        track={'source':str(candidates[0].resolve()),'title':'Cache check','artist':'Artist','album':'Album',
               'album_id':'1','id':'1','track':1,'disc':1,'genres':[]}
        with tempfile.TemporaryDirectory() as directory:
            cache=Path(directory);first=prepare(track,cache)
            _verified.clear()  # A different worker has no process-local verification cache.
            def reject_audio_hash(path):
                if Path(path).suffix=='.mp3':raise AssertionError('Unchanged audio reread after worker restart')
                return digest(path)
            with patch('media.digest',side_effect=reject_audio_hash):
                self.assertEqual(prepare(track,cache)['track'],first['track'])
            audio=cache/'objects'/first['track']
            data=audio.read_bytes();audio.write_bytes(data[:-1]+bytes([data[-1]^1]))
            # A changed object fingerprint must not be trusted as the old identity.
            self.assertEqual(prepare(track,cache)['track'],first['track'])
            self.assertEqual(audio.read_bytes(),data)

    def test_http_stays_alive_with_plex_down_and_first_export_unavailable(self):
        import socket, urllib.request, urllib.error
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'token').write_text('invalid-test-token')
            with socket.socket() as listener:
                listener.bind(('',0));port=listener.getsockname()[1]
            code="import server; from types import SimpleNamespace; server.Zeroconf=lambda:SimpleNamespace(register_service=lambda x:None,unregister_service=lambda x:None,close=lambda:None); server.main()"
            process=subprocess.Popen([sys.executable,'-c',code,'--plex-url','http://127.0.0.1:1',
                '--plex-token',str(root/'token'),'--source-root',str(root),'--music',str(root),
                '--cache',str(root/'cache'),'--advertise-ip','192.0.2.101','--port',str(port)],
                cwd=Path(__file__).resolve().parents[1]/'syncer',stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            try:
                base='http://127.0.0.1:'+str(port)
                for _ in range(40):
                    try:
                        with urllib.request.urlopen(base+'/health',timeout=.2) as response:
                            self.assertEqual(response.status,200)
                        break
                    except urllib.error.URLError:time.sleep(.05)
                else:self.fail('HTTP unavailable when Plex is down')
                with urllib.request.urlopen(base+'/status',timeout=1) as response:
                    self.assertIn(json.load(response)['publisher'],('preparing','error','waiting'))
                self.assertIsNone(process.poll())
                with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as connection:
                    connection.connect(('192.0.2.2',32400));ip=connection.getsockname()[0]
                request=urllib.request.Request('http://'+ip+':'+str(port)+'/sync',data=b'{"ftp_port":2121}',headers={'Content-Type':'application/json'})
                with self.assertRaises(urllib.error.HTTPError) as rejected:
                    urllib.request.urlopen(request,timeout=1)
                self.assertEqual(rejected.exception.code,503)
            finally:
                process.terminate();process.communicate(timeout=3)

    def test_same_length_replacement_then_noop_native_receiver(self):
        import socket
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as connection:
            connection.connect(('192.0.2.2',32400));ip=connection.getsockname()[0]
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);cache=root/'cache';pod=root/'pod';pod.mkdir()
            (cache/'objects').mkdir(parents=True);(cache/'catalogs').mkdir()
            name='Artist/Album/01 - Song.txt'
            def head(data):
                source=hashlib.sha256(data).hexdigest()+'.txt';(cache/'objects'/source).write_bytes(data)
                catalog=(json.dumps({'format':2})+'\n'+json.dumps({'file':name,'cache_source':source,'bytes':len(data)})+'\n').encode()
                sha=hashlib.sha256(catalog).hexdigest();(cache/'catalogs'/sha).write_bytes(catalog)
                return {'format':2,'catalog':sha}
            process=subprocess.Popen(['/tmp/pearl-ftp-host',str(pod)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            try:
                time.sleep(.2)
                first=head(b'old lyric');deliver(cache,first,ip,2121,15,free_bytes=1024**3)
                second=head(b'new lyric');deliver(cache,second,ip,2121,15,free_bytes=1024**3)
                self.assertEqual((pod/name).read_bytes(),b'new lyric')
                mtime=(pod/name).stat().st_mtime_ns
                deliver(cache,second,ip,2121,15,free_bytes=1024**3)
                self.assertEqual((pod/name).stat().st_mtime_ns,mtime)
            finally:
                process.terminate();out,err=process.communicate(timeout=5)
                self.assertNotIn(b'AddressSanitizer',err)


if __name__=='__main__':unittest.main()
