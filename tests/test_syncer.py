import hashlib
import json
from pathlib import Path
import socket
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'syncer'))
from media import normalize_lyrics, digest, source_digest
from transfer import deliver, capacity, report_failure
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer

class SyncTests(unittest.TestCase):
    def test_publication_changes_and_empty_selection(self):
        from publisher import publish
        empty={'format':1,'server_id':'test','playlists':[],'tracks':{}}
        changed=dict(empty,server_id='changed')
        with tempfile.TemporaryDirectory() as directory:
            cache=Path(directory);(cache/'head.json').write_bytes(b'previous publication')
            with patch('publisher.snapshot',side_effect=[empty,changed]):
                with self.assertRaises(ValueError):publish(None,Path('/source'),Path('/music'),cache)
            self.assertEqual((cache/'head.json').read_bytes(),b'previous publication')
            with patch('publisher.snapshot',return_value=empty):
                head=publish(None,Path('/source'),Path('/music'),cache)
            self.assertEqual(head['objects'],0)
            self.assertEqual((cache/'catalogs'/head['catalog']).read_bytes(),b'{"format":1}\n')

    def test_source_cache_invalidation(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'source';source.write_bytes(b'NAS source identity')
            cache=root/'cache';cache.mkdir()
            first=source_digest(source,cache)
            with patch('media.digest',side_effect=AssertionError('unchanged source rehashed')):
                self.assertEqual(source_digest(source,cache),first)
            source.write_bytes(b'Changed source identity')
            self.assertNotEqual(source_digest(source,cache),first)

    def test_capacity(self):
        required={'same.mp3':100,'new.mp3':1000,'partial.mp3':2000}
        existing={'same.mp3':100,'partial.mp3':1}
        needed=capacity(required,existing,8*1024*1024,120)
        self.assertEqual(needed,3000+2*32768+120+4*1024*1024)
        with self.assertRaises(OSError):capacity(required,existing,needed-1,120)
        with self.assertRaises(ValueError):capacity(required,existing,True,120)

    def test_lyrics(self):
        ext,data=normalize_lyrics(b'[offset:-500]\n[00:02.00][00:04.000]hello\n','.lrc')
        self.assertEqual(ext,'.lrc');self.assertIn(b'[00:01.500]hello',data);self.assertIn(b'[00:03.500]hello',data)
        ext,data=normalize_lyrics(b'1\n00:00:01,100 --> 00:00:02,200\n<b>Hello</b>\n\n','.srt')
        self.assertIn(b'[00:01.100]Hello',data);self.assertIn(b'[00:02.200]\n',data)
        with self.assertRaises(ValueError):normalize_lyrics(b'x'*(256*1024+1),'.txt')
        with self.assertRaises(ValueError):normalize_lyrics(b'[00:99]bad','.lrc')
        self.assertIn(b'Keep [these words] too',normalize_lyrics(b'[00:01.000]Keep [these words] too','.lrc')[1])
    def test_prepared_media(self):
        """Validate the actual NAS export; never synthesize music for this check."""
        import subprocess
        from mutagen.id3 import ID3
        cache=Path('.sync-state/cache')
        if not (cache/'head.json').exists():
            self.skipTest('Prepare a real NAS playlist before checking exported media')
        head=json.loads((cache/'head.json').read_text())
        records=[json.loads(row) for row in (cache/'catalogs'/head['catalog']).read_text().splitlines()]
        tracks=[row for row in records if 'track' in row]
        self.assertTrue(tracks)
        for track in tracks:
            audio=cache/'objects'/track['track']
            self.assertEqual(digest(audio),track['track'][:64])
            tags=ID3(audio)
            self.assertTrue(tags.getall('TIT2'))
            self.assertTrue(tags.getall('TALB'))
            if track.get('art'):
                self.assertTrue(tags.getall('APIC'))
                subprocess.run(['/tmp/pearl-export-compat',str(audio)],check=True)

    def test_native_receiver(self):
        import subprocess, time, ftplib
        connection=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);connection.connect(('192.0.2.2',32400));ip=connection.getsockname()[0];connection.close()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);cache=root/'cache';pod=root/'pod';pod.mkdir();(cache/'objects').mkdir(parents=True);(cache/'catalogs').mkdir()
            data=b'Native FTP fixture'*100000;name=hashlib.sha256(data).hexdigest()+'.mp3';(cache/'objects'/name).write_bytes(data)
            catalog=(json.dumps({'file':name,'bytes':len(data)})+'\n').encode();sha=hashlib.sha256(catalog).hexdigest();(cache/'catalogs'/sha).write_bytes(catalog)
            process=subprocess.Popen(['/tmp/pearl-ftp-host',str(pod)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            try:
                time.sleep(.2);deliver(cache,{'catalog':sha},ip,2121,30)
                self.assertEqual((pod/'objects'/name).read_bytes(),data)
                old=(pod/'objects'/name).stat().st_mtime_ns;deliver(cache,{'catalog':sha},ip,2121,30,free_bytes=8*1024*1024)
                with self.assertRaises(OSError):deliver(cache,{'catalog':sha},ip,2121,30,free_bytes=0)
                report_failure(ip,2121,'card_full')
                self.assertEqual((pod/'error.txt').read_text(),'card_full\n')
                self.assertEqual((pod/'objects'/name).stat().st_mtime_ns,old)
                ftp=ftplib.FTP();ftp.connect(ip,2121,timeout=5)
                self.assertEqual(ftp.pwd(),'/')  # No login/password handshake required
                with self.assertRaises(ftplib.error_perm):ftp.cwd('../../')
                ftp.quit()
            finally:
                process.terminate();out,err=process.communicate(timeout=5)
                self.assertNotIn(b'AddressSanitizer',err);self.assertNotIn(b'runtime error:',err)
                self.assertGreaterEqual(int(out.split(b'RECEIVED ')[-1].splitlines()[0]),len(data))

    def test_interrupted_and_full_transfer(self):
        import subprocess,time,os
        connection=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);connection.connect(('192.0.2.2',32400));ip=connection.getsockname()[0];connection.close()
        for mode in ('interrupted','full'):
            with tempfile.TemporaryDirectory() as directory:
                root=Path(directory);cache=root/'cache';pod=root/'pod';pod.mkdir();(pod/'existing-music').write_bytes(b'Keep me');(cache/'objects').mkdir(parents=True);(cache/'catalogs').mkdir()
                data=b'fixture'*600000;name=hashlib.sha256(data).hexdigest()+'.mp3';(cache/'objects'/name).write_bytes(data)
                catalog=(json.dumps({'file':name,'bytes':len(data)})+'\n').encode();sha=hashlib.sha256(catalog).hexdigest();(cache/'catalogs'/sha).write_bytes(catalog)
                env=dict(os.environ);env['PEARL_FTP_DELAY_US']='20000' if mode=='interrupted' else '5000'
                if mode=='full':env['PEARL_FTP_LIMIT']='1'
                process=subprocess.Popen(['/tmp/pearl-ftp-host',str(pod)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                try:
                    time.sleep(.1)
                    expected=subprocess.TimeoutExpired if mode=='interrupted' else subprocess.CalledProcessError
                    with self.assertRaises(expected):deliver(cache,{'catalog':sha},ip,2121,5 if mode=='interrupted' else 10)
                    self.assertFalse((pod/'ready').exists());self.assertEqual((pod/'existing-music').read_bytes(),b'Keep me')
                    partial=pod/'objects'/name;self.assertTrue(partial.is_file());self.assertLess(partial.stat().st_size,len(data))
                    if mode=='interrupted':
                        deliver(cache,{'catalog':sha},ip,2121,30);self.assertEqual(partial.read_bytes(),data)
                finally:
                    process.terminate();out,err=process.communicate(timeout=5);self.assertNotIn(b'AddressSanitizer',err)

    def test_incremental_ftp(self):
        connection=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);connection.connect(('192.0.2.2',32400));ip=connection.getsockname()[0];connection.close()
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory);cache=base/'cache';remote=base/'pod';remote.mkdir();(cache/'objects').mkdir(parents=True);(cache/'catalogs').mkdir()
            data=b'fixture audio'*10000;name=hashlib.sha256(data).hexdigest()+'.mp3';(cache/'objects'/name).write_bytes(data)
            catalog=(json.dumps({'file':name,'bytes':len(data)})+'\n').encode();sha=hashlib.sha256(catalog).hexdigest();(cache/'catalogs'/sha).write_bytes(catalog)
            transfers=[]
            class Handler(FTPHandler):
                def on_file_received(self,file):transfers.append(Path(file).name)
            authorizer=DummyAuthorizer();authorizer.add_anonymous(str(remote),perm='elradfmwMT');Handler.authorizer=authorizer
            server=FTPServer((ip,0),Handler);port=server.socket.getsockname()[1]
            thread=threading.Thread(target=server.serve_forever,kwargs={'timeout':.1},daemon=True);thread.start()
            try:
                deliver(cache,{'catalog':sha},ip,port,15)
                self.assertEqual((remote/'objects'/name).read_bytes(),data);self.assertEqual((remote/'ready').read_text(),sha+'\n')
                first=transfers.count(name);deliver(cache,{'catalog':sha},ip,port,15);self.assertEqual(transfers.count(name),first)
                # Truncated object is retransferred; no active data is deleted.
                (remote/'objects'/name).write_bytes(b'bad');deliver(cache,{'catalog':sha},ip,port,15)
                self.assertEqual((remote/'objects'/name).read_bytes(),data)
            finally:
                server.ioloop.call_later(0,server.close_all)
                thread.join(2)

if __name__=='__main__':
    import transfer
    original=transfer.subprocess.run
    def diagnostic(*args,**kwargs):
        try:return original(*args,**kwargs)
        except transfer.subprocess.CalledProcessError as e:
            print(e.stderr.decode(),file=sys.stderr);raise
    transfer.subprocess.run=diagnostic
    unittest.main()
