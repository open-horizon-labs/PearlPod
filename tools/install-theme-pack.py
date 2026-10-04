#!/usr/bin/env python3
"""Copy a pack to a mounted card, or to the player's existing diagnostic FTP."""
import argparse,ftplib,shutil
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('pack',type=Path);g=p.add_mutually_exclusive_group(required=True);g.add_argument('--card',type=Path,help='Card music/ folder');g.add_argument('--ftp',help='Player IP while diagnostic FTP is active');a=p.parse_args()
files=[f for f in sorted(a.pack.rglob('*')) if f.is_file() and f.suffix in ('.toml','.rgb565')]
assert any(f.name=='Person.toml' for f in files),'Pack must include Person.toml'
# Publish the person's selection last, so interrupted installation retains the old selection.
files.sort(key=lambda f:f.name=='Person.toml')
if a.card:
 for f in files:
  target=a.card/f.relative_to(a.pack);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(f,target)
else:
 with ftplib.FTP() as ftp:
  ftp.connect(a.ftp,2121,timeout=30);ftp.login()
  for f in files:
   relative=f.relative_to(a.pack).as_posix();parent=Path(relative).parent
   if str(parent)!='.':
    parts=parent.parts
    for i in range(1,len(parts)+1):
     try:ftp.mkd('/'.join(parts[:i]))
     except ftplib.error_perm as e:
      if not str(e).startswith('550'):raise
   with f.open('rb') as stream:ftp.storbinary('STOR '+relative,stream,blocksize=32768)
   assert ftp.size(relative)==f.stat().st_size
   print('Installed',relative)
