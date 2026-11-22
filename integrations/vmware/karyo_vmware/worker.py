# SPDX-License-Identifier: GPL-3.0-or-later
"""Owned foreground child: inherited channel lock and parent-death cleanup."""
import ctypes,os,signal,stat,sys

LOCK='/run/karyo-vmware/channel'

def validate_lock(fd):
    info=os.fstat(fd);path=os.stat(LOCK,follow_symlinks=False)
    if (not stat.S_ISREG(info.st_mode) or info.st_uid!=0 or info.st_gid!=0 or info.st_nlink!=1
            or stat.S_IMODE(info.st_mode)!=0o644 or info.st_size!=0 or (info.st_dev,info.st_ino)!=(path.st_dev,path.st_ino)):
        raise PermissionError('VMware channel lock changed')

def drop_privileges():
    lib=ctypes.CDLL(None,use_errno=True)
    class Header(ctypes.Structure):_fields_=[('version',ctypes.c_uint32),('pid',ctypes.c_int)]
    class Data(ctypes.Structure):_fields_=[('effective',ctypes.c_uint32),('permitted',ctypes.c_uint32),('inheritable',ctypes.c_uint32)]
    header=Header(0x20080522,0);data=(Data*2)()
    if lib.prctl(47,4,0,0,0)!=0 or lib.capset(ctypes.byref(header),data)!=0 or lib.prctl(38,1,0,0,0)!=0:
        raise PermissionError('Cannot discard inherited privileges')

def main():
    if (os.getuid()!=os.geteuid() or os.getgid()!=os.getegid() or not 1000<=os.getuid()<65534
            or not 4<=len(sys.argv)<=68):return 2
    parent,fd=int(sys.argv[1]),int(sys.argv[2]);command=sys.argv[3:]
    if not 0<parent<2**31 or not 3<=fd<1024 or not os.path.isabs(command[0]) or sum(map(len,command))>16384:return 2
    validate_lock(fd)
    lib=ctypes.CDLL(None,use_errno=True)
    if lib.prctl(1,signal.SIGTERM,0,0,0)!=0:raise OSError('Parent-death notification unavailable')
    if os.getppid()!=parent:return 1
    drop_privileges();os.set_inheritable(fd,True)
    os.execv(command[0],command)

if __name__=='__main__':raise SystemExit(main())
