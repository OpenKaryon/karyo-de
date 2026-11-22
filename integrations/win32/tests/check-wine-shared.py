#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check native libwine Multi-Arch identity and shared payload before installation."""
import argparse,hashlib,json,subprocess,tarfile
from email.parser import Parser
from pathlib import Path,PurePosixPath


def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def metadata(path):
    if path.is_symlink() or not path.is_file():raise ValueError('Expected an ordinary native package file')
    info=Parser().parsestr(subprocess.check_output(['dpkg-deb','--field',str(path)],text=True))
    fields=('Package','Version','Architecture','Multi-Arch')
    if any(len(info.get_all(key,[]))!=1 for key in fields):raise ValueError('Ambiguous native package identity')
    result={key:info[key].strip() for key in fields}
    if result['Package']!='libwine' or result['Multi-Arch']!='same':raise ValueError('Expected libwine Multi-Arch:same')
    return result


def payload(path):
    rows={}
    child=subprocess.Popen(['dpkg-deb','--fsys-tarfile',str(path)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    try:
        with tarfile.open(fileobj=child.stdout,mode='r|') as archive:
            for item in archive:
                name=item.name[2:] if item.name.startswith('./') else item.name
                parts=PurePosixPath(name)
                if parts.is_absolute() or '..' in parts.parts:raise ValueError('Unsafe native package path')
                if item.isdir():continue
                name=str(parts)
                if name in rows:raise ValueError('Duplicate native package path: '+name)
                attributes=(item.mode & 0o7777,item.uid,item.gid)
                if item.isfile():
                    stream=archive.extractfile(item);checksum=hashlib.sha256()
                    while chunk:=stream.read(1024*1024):checksum.update(chunk)
                    rows[name]=('file',item.size,checksum.hexdigest(),attributes)
                elif item.issym():rows[name]=('symlink',item.linkname,attributes)
                elif item.islnk():rows[name]=('hardlink',item.linkname,attributes)
                else:raise ValueError('Unsupported native payload member: '+name)
        while child.stdout.read(1024*1024):pass
        error=child.stderr.read().decode(errors='replace')
        if child.wait(timeout=30):raise ValueError('Native payload inspection failed: '+error[-2048:])
    except BaseException:
        if child.poll() is None:child.kill()
        child.communicate(timeout=5);raise
    finally:
        child.stdout.close();child.stderr.close()
    return rows


def check(amd64,i386):
    left=metadata(amd64);right=metadata(i386)
    if left['Architecture']!='amd64' or right['Architecture']!='i386':raise ValueError('Expected amd64 and i386 package identities')
    if left['Version']!=right['Version']:raise ValueError('Native Wine versions differ')
    a=payload(amd64);b=payload(i386);common=set(a)&set(b)
    if not common or not set(a)-set(b) or not set(b)-set(a):raise ValueError('Missing shared or architecture-specific payload')
    mismatches=sorted(name for name in common if a[name]!=b[name])
    if mismatches:raise ValueError('Shared native payload differs: '+', '.join(mismatches[:8]))
    return {'accepted':True,'version':left['Version'],'shared_files':len(common),
            'amd64_only_files':len(set(a)-set(b)),'i386_only_files':len(set(b)-set(a)),
            'shared_samples':sorted(common)[:5],
            'amd64_sha256':digest(amd64),'i386_sha256':digest(i386)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--amd64',type=Path,required=True);parser.add_argument('--i386',type=Path,required=True)
    parser.add_argument('--output',type=Path);args=parser.parse_args()
    report=check(args.amd64,args.i386)
    text=json.dumps(report,indent=2)+'\n'
    if args.output:args.output.write_text(text)
    print(text,end='')


if __name__=='__main__':main()
