# SPDX-License-Identifier: GPL-3.0-or-later
"""Private user storage; no symlink traversal or global configuration writes."""
import json,os,stat,time
from pathlib import Path
from .policy import APP_ID_RE,LaunchError,application_id


def open_directory(path,create=False):
    path=Path(path)
    if not path.is_absolute() or '..' in path.parts:raise LaunchError('Invalid application directory.')
    fd=os.open(path.anchor,os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC)
    try:
        for name in path.parts[1:]:
            if create:
                try:os.mkdir(name,0o700,dir_fd=fd)
                except FileExistsError:pass
            child=os.open(name,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW|os.O_CLOEXEC,dir_fd=fd)
            os.close(fd);fd=child
        if os.fstat(fd).st_uid!=os.getuid():raise LaunchError('Application directory is not owned by this user.')
        result=fd;fd=None;return result
    finally:
        if fd is not None:os.close(fd)


def directory(path,private=True):
    fd=open_directory(path,create=True)
    try:
        if private:os.fchmod(fd,0o700)
    finally:os.close(fd)


def secure_directory(path):directory(path)


def atomic_bytes(path,data):
    temporary=path.with_name(path.name+'.'+str(os.getpid())+'.'+str(time.monotonic_ns())+'.tmp')
    fd=None
    try:
        fd=os.open(temporary,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW|os.O_CLOEXEC,0o600)
        with os.fdopen(fd,'wb') as stream:
            fd=None;stream.write(data);stream.flush();os.fsync(stream.fileno())
        os.replace(temporary,path)
    finally:
        if fd is not None:os.close(fd)
        temporary.unlink(missing_ok=True)


def write_json(path,data):atomic_bytes(path,(json.dumps(data,ensure_ascii=False,indent=2)+'\n').encode())


def unique(pairs):
    result={}
    for key,value in pairs:
        if key in result:raise ValueError('Duplicate application record field')
        result[key]=value
    return result


def registered_application(identifier,home):
    application_id(Path('/'),identifier)
    path=home/'.local/share/karyo/windows/applications'/(identifier+'.json')
    try:
        parent=open_directory(path.parent)
        try:fd=os.open(path.name,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK|os.O_CLOEXEC,dir_fd=parent)
        finally:os.close(parent)
        with os.fdopen(fd,'rb') as stream:
            info=os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_uid!=os.getuid() or info.st_nlink!=1 or stat.S_IMODE(info.st_mode)!=0o600 or info.st_size>1048576:
                raise ValueError('Unsafe application record')
            data=json.loads(stream.read(1048577),object_pairs_hook=unique)
        if (not isinstance(data,dict) or set(data)!={'version','applicationId','ownerUid','file'}
                or type(data['version']) is not int or data['version']!=1 or data['applicationId']!=identifier
                or type(data['ownerUid']) is not int or data['ownerUid']!=os.getuid()
                or not isinstance(data['file'],str) or not Path(data['file']).is_absolute() or '\0' in data['file']):
            raise ValueError('Invalid application record')
        return data['file']
    except (OSError,ValueError) as error:raise LaunchError('Cannot read registered application '+identifier+': '+str(error)) from error
