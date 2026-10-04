"""Serialized, deadline-bounded publication with an independently live HTTP service."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading
import time


class PublicationFailed(RuntimeError):
    def __init__(self, code):
        self.code = code
        super().__init__(code)


class PreparationTimeout(TimeoutError):
    pass


def run_publication(args, timeout):
    # A process group bounds Python requests and ffmpeg together, without logging tokens.
    command = [sys.executable, str(Path(__file__).with_name('publisher.py')),
               '--plex-url', args.plex_url, '--plex-token', str(args.plex_token),
               '--source-root', str(args.source_root), '--music', str(args.music),
               '--cache', str(args.cache), '--cache-limit', str(args.cache_limit)]
    process = subprocess.Popen(command, start_new_session=True, stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL)
    try:
        output, _ = process.communicate(timeout=timeout)
        if process.returncode:
            try:
                code=json.loads(output)['error']
                if not isinstance(code,str) or not code.replace('_','').isalnum() or len(code)>64:raise ValueError()
            except (ValueError,KeyError,TypeError):code='PublicationFailed'
            raise PublicationFailed(code)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
        raise PreparationTimeout('Preparation deadline exceeded') from None
    finally:
        if process.stdout is not None:process.stdout.close()
    return json.loads((args.cache / 'head.json').read_text())


class Publications:
    def __init__(self, args, runner=run_publication):
        self.args = args
        self.runner = runner
        self.lock = threading.Lock()
        self.state_lock = threading.Lock()
        self.stop = threading.Event()
        self.state = {'publisher': 'waiting'}
        try:
            head = json.loads((args.cache / 'head.json').read_text())
            self.state.update(published_catalog=head['catalog'],
                              source_checked_at=head.get('source_checked_at'))
        except (OSError, ValueError, KeyError):
            pass

    def status(self):
        with self.state_lock:
            result = dict(self.state)
        checked = result.get('source_checked_at')
        result['source_age_seconds'] = max(0, int(time.time()) - checked) if checked else None
        return result

    def update(self, **values):
        with self.state_lock:
            self.state.update(values)

    def refresh(self, timeout, consume=None):
        deadline = time.monotonic() + timeout
        if not self.lock.acquire(timeout=timeout):
            raise PreparationTimeout('Publisher busy past deadline')
        prepared = False
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise PreparationTimeout('Preparation deadline exceeded')
            self.update(publisher='preparing')
            before = time.monotonic()
            head = self.runner(self.args, remaining)
            self.update(publisher='ready', publication_error=None,
                        publication_ms=round((time.monotonic()-before)*1000, 2),
                        source_checked_at=head.get('source_checked_at', int(time.time())),
                        published_catalog=head['catalog'])
            prepared = True
            if consume is not None:
                consume(head)
            return head
        except Exception as error:
            if not prepared:
                self.update(publisher='error', publication_error=getattr(error,'code',type(error).__name__))
            raise
        finally:
            self.lock.release()

    def poll(self):
        failures = 0
        while not self.stop.is_set():
            try:
                self.refresh(self.args.poll_timeout)
                failures = 0
            except Exception:
                failures += 1
            delay = min(self.args.poll_interval * 2 ** min(failures, 4), 900)
            self.stop.wait(delay)
