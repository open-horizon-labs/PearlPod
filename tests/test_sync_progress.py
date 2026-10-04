"""Content progress and interruption checks using existing NAS exports."""
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'syncer'))
from progress import message, plan
from transfer import deliver, quote


def address():
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
        s.connect(('192.0.2.2',32400));return s.getsockname()[0]


class ProgressTests(unittest.TestCase):
    def test_control_line_bounds_and_escaping(self):
        info=dict(p='"\\$`雪'*40,t='"\\$`雪'*40,a='雪'*40,r='雪'*40,k='song',b=100,d=0,f=100,n=1,s=0,i=1,c=1,q=0)
        text=message(info)
        self.assertLessEqual(len(text.encode()),491)
        row=json.loads(text[len('SITE PEARL '):]);self.assertIn('雪',row['p'])
        self.assertEqual(row['f'],100)

    def exported(self, directory):
        source=Path('.sync-state/cache')
        if not (source/'head.json').is_file():self.skipTest('Prepare a real NAS playlist before checking interruption recovery')
        head=json.loads((source/'head.json').read_text())
        rows=[json.loads(x) for x in (source/'catalogs'/head['catalog']).read_text().splitlines()]
        tracks=[row for row in rows if 'track' in row][:2]
        if len(tracks)<2:self.skipTest('Two real exported songs needed')
        files={row['file']:row for row in rows if 'file' in row}
        cache=directory/'cache';(cache/'objects').mkdir(parents=True);(cache/'catalogs').mkdir()
        required=set()
        for track in tracks:
            required.add(track['track'])
            if track.get('art'):required.add(track['art'])
            for lyric in track.get('lyrics',[]):required.update((lyric['file'],lyric['original']))
        records=[{'format':2}]+[files[name] for name in sorted(required)]+tracks
        for name in required:
            os.link(source/'objects'/files[name]['cache_source'],cache/'objects'/files[name]['cache_source'])
        for title,selected in (('After school',[tracks[0],tracks[0]]),('Weekend',[tracks[0],tracks[1]])):
            name='Playlists/'+title+'.m3u8'
            data=('#EXTM3U\n'+''.join('../'+track['track']+'\n' for track in selected)).encode()
            digest=hashlib.sha256(data).hexdigest()+'.m3u8';(cache/'objects'/digest).write_bytes(data)
            records.extend([dict(file=name,bytes=len(data),cache_source=digest),dict(playlist=name,title=title)])
        data=b''.join(json.dumps(row).encode()+b'\n' for row in records)
        sha=hashlib.sha256(data).hexdigest();(cache/'catalogs'/sha).write_bytes(data)
        return cache,dict(format=2,catalog=sha),records,tracks

    def test_crash_checkpoint_and_resume(self):
        """First complete playlist is readable, partial second one isn't published,
        and retry preserves the first playlist's media timestamps."""
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);cache,head,records,tracks=self.exported(root)
            pod=root/'pod';pod.mkdir();log=root/'progress.jsonl'
            env=dict(os.environ,PEARL_PROGRESS_LOG=str(log),PEARL_FTP_CRASH_AFTER_PLAYLIST='1')
            receiver=subprocess.Popen(['/tmp/pearl-ftp-host',str(pod)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            try:
                time.sleep(.2)
                with self.assertRaises((subprocess.CalledProcessError,subprocess.TimeoutExpired)):
                    deliver(cache,head,address(),2121,6,free_bytes=1024**3,progress=True)
                receiver.wait(timeout=3)
            finally:
                if receiver.poll() is None:receiver.terminate()
                _,errors=receiver.communicate(timeout=3)
                self.assertNotIn(b'AddressSanitizer',errors);self.assertNotIn(b'runtime error:',errors)
            self.assertTrue((pod/'Playlists/After school.m3u8').is_file())
            self.assertFalse((pod/'Playlists/Weekend.m3u8').exists())
            before=(pod/tracks[0]['track']).stat().st_mtime_ns
            receipt=json.loads((pod/'.pearl/delivered.json').read_text())
            expected=next(row['cache_source'] for row in records if row.get('file')==tracks[0]['track'])
            self.assertEqual(receipt[tracks[0]['track']],expected)
            messages=[json.loads(line) for line in log.read_text().splitlines()]
            self.assertEqual(messages[-1]['k'],'saved');self.assertEqual(messages[-1]['q'],1)
            self.assertEqual(sum(row['k']=='song' for row in messages),1)  # shared entries not recopied
            # An interrupted upload uses a hidden name; it must never become
            # a playable entry, even though its bytes come from real music.
            source=next(row['cache_source'] for row in records if row.get('file')==tracks[1]['track'])
            with (cache/'objects'/source).open('rb') as f:(pod/'.in.interrupted.mp3').write_bytes(f.read(32768))
            subprocess.run(['/tmp/pearl-resume-library',str(pod)],check=True)
            env.pop('PEARL_FTP_CRASH_AFTER_PLAYLIST');log.unlink()
            receiver=subprocess.Popen(['/tmp/pearl-ftp-host',str(pod)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            try:
                time.sleep(.2);deliver(cache,head,address(),2121,30,free_bytes=1024**3,progress=True)
                self.assertEqual((pod/tracks[0]['track']).stat().st_mtime_ns,before)
                self.assertTrue((pod/tracks[1]['track']).exists())
                self.assertTrue((pod/'Playlists/Weekend.m3u8').exists())
                self.assertTrue((pod/'.pearl/ready').exists())
                messages=[json.loads(line) for line in log.read_text().splitlines()]
                songs=[row for row in messages if row['k']=='song']
                self.assertEqual(len(songs),1);self.assertEqual(songs[0]['p'],'Weekend')
                self.assertEqual(songs[0]['n'],1)
                self.assertEqual(messages[-1]['k'],'finishing')
                mtimes={row['file']:(pod/row['file']).stat().st_mtime_ns for row in records if 'file' in row}
                log.unlink();deliver(cache,head,address(),2121,30,free_bytes=1024**3,progress=True)
                self.assertEqual(mtimes,{name:(pod/name).stat().st_mtime_ns for name in mtimes})
                self.assertFalse(any(json.loads(line)['k']=='song' for line in log.read_text().splitlines()))
            finally:
                receiver.terminate();_,errors=receiver.communicate(timeout=3)
                self.assertNotIn(b'AddressSanitizer',errors);self.assertNotIn(b'runtime error:',errors)

    def test_fragmented_and_pipelined_control_lines(self):
        with tempfile.TemporaryDirectory() as directory:
            receiver=subprocess.Popen(['/tmp/pearl-ftp-host',directory],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            try:
                time.sleep(.2)
                with socket.create_connection((address(),2121),timeout=5) as sock:
                    reply=sock.makefile('rb');self.assertTrue(reply.readline().startswith(b'220'))
                    payload=message(dict(p='After school',t='Quotes " and \\ and $`',a='Album',r='Artist',k='song',b=100,d=0,f=100,n=1,s=0,i=1,c=1,q=0)).encode()+b'\r\n'
                    for at in range(0,len(payload),7):sock.sendall(payload[at:at+7]);time.sleep(.001)
                    self.assertTrue(reply.readline().startswith(b'200'))
                    sock.sendall(b'NOOP\r\nNOOP\r\n')
                    self.assertTrue(reply.readline().startswith(b'200'));self.assertTrue(reply.readline().startswith(b'200'))
                    sock.sendall(b'SITE PEARL {}\r\n');self.assertTrue(reply.readline().startswith(b'502'))
                    sock.sendall(b'NOOP\r\n');self.assertTrue(reply.readline().startswith(b'200'))
                    sock.sendall(b'QUIT\r\n');self.assertTrue(reply.readline().startswith(b'221'))
                    reply.close()
                # Exercise lftp's own quote language, not just a raw socket.
                hostile=message(dict(p='"\\$`雪'*40,t='Escaped "song"',a='Album',r='Artist',k='song',b=100,d=0,f=100,n=1,s=0,i=1,c=1,q=0))
                script=Path(directory)/'probe.lftp'
                script.write_text('set cmd:fail-exit yes\nset ftp:ssl-allow no\nopen -u anonymous,pearlpod ftp://'+address()+':2121\nquote '+quote(hostile)+'\nbye\n')
                subprocess.run(['lftp','-f',str(script)],check=True,timeout=10,capture_output=True)
            finally:
                receiver.terminate();_,errors=receiver.communicate(timeout=3)
                self.assertNotIn(b'AddressSanitizer',errors);self.assertNotIn(b'runtime error:',errors)


if __name__=='__main__':unittest.main()
