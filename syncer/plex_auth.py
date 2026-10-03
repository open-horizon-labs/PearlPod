"""Interactive Plex link login. Tokens are written privately, never printed."""
import argparse
import os
from pathlib import Path
import tempfile
import time
from plexapi.myplex import MyPlexAccount, MyPlexPinLogin


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--token-file', type=Path, required=True)
    parser.add_argument('--profile', help='Optional managed Plex Home username')
    args = parser.parse_args()
    login = MyPlexPinLogin(headers={'X-Plex-Product': 'PearlPod Syncer'})
    login.run(timeout=240)
    print('Open https://plex.tv/link and enter: ' + str(login.pin), flush=True)
    deadline = time.monotonic() + 240
    while time.monotonic() < deadline:
        if login.token:
            break
        if login.expired:
            raise SystemExit('Login expired; run again.')
        time.sleep(2)
    else:
        raise SystemExit('Login timed out; run again.')
    account = MyPlexAccount(token=login.token)
    if args.profile:
        from getpass import getpass
        pin = getpass('Managed profile PIN (empty if none): ')
        account = account.switchHomeUser(args.profile, pin=pin or None)
    target = args.token_file.expanduser()
    target.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    fd, temp = tempfile.mkstemp(dir=target.parent, prefix='.plex-token-')
    try:
        with os.fdopen(fd, 'w') as out:
            out.write(account.authenticationToken + '\n')
            out.flush()
            os.fsync(out.fileno())
        os.replace(temp, target)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)
    print('Plex authorization saved privately. Token was not printed.', flush=True)


if __name__ == '__main__':
    try:
        main()
    except Exception:
        # Requests/Plex exceptions can include credential-bearing URLs.
        raise SystemExit('Plex authorization failed; credentials were not logged. Retry login or check profile access.') from None
