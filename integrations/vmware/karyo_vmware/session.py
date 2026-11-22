# SPDX-License-Identifier: GPL-3.0-or-later
"""Native process/logind identity; caller environment never chooses the session."""
import ctypes,errno,os,re

class Session:
    def __init__(self):
        self.lib=ctypes.CDLL('libsystemd.so.0');self.libc=ctypes.CDLL(None)
        self.libc.free.argtypes=(ctypes.c_void_p,);self.libc.free.restype=None
        self.lib.sd_pid_get_session.argtypes=(ctypes.c_int,ctypes.POINTER(ctypes.c_void_p))
        self.lib.sd_pid_get_session.restype=ctypes.c_int
        pointer=ctypes.c_void_p()
        if self.lib.sd_pid_get_session(0,ctypes.byref(pointer))<0:raise PermissionError('No native graphical login session')
        try:self.id=ctypes.string_at(pointer).decode('ascii')
        finally:self.libc.free(pointer)
        if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}',self.id):raise ValueError('Invalid native session')
        self.key=self.id.encode();self.bus=ctypes.c_void_p();self.path=ctypes.c_void_p()
        for name in ('sd_session_get_state','sd_session_get_type','sd_session_get_class','sd_session_get_seat'):
            function=getattr(self.lib,name);function.argtypes=(ctypes.c_char_p,ctypes.POINTER(ctypes.c_void_p));function.restype=ctypes.c_int
        for name in ('sd_session_is_active','sd_session_is_remote'):
            function=getattr(self.lib,name);function.argtypes=(ctypes.c_char_p,);function.restype=ctypes.c_int
        for name in ('sd_session_get_uid','sd_session_get_vt'):
            function=getattr(self.lib,name);function.argtypes=(ctypes.c_char_p,ctypes.POINTER(ctypes.c_uint));function.restype=ctypes.c_int
        self.lib.sd_bus_open_system.argtypes=(ctypes.POINTER(ctypes.c_void_p),);self.lib.sd_bus_open_system.restype=ctypes.c_int
        self.lib.sd_bus_set_method_call_timeout.argtypes=(ctypes.c_void_p,ctypes.c_uint64)
        self.lib.sd_bus_get_property_trivial.argtypes=(ctypes.c_void_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_void_p,ctypes.c_char,ctypes.c_void_p)
        self.lib.sd_bus_get_property_trivial.restype=ctypes.c_int
        self.lib.sd_bus_unref.argtypes=(ctypes.c_void_p,);self.lib.sd_bus_unref.restype=ctypes.c_void_p
        self.lib.sd_bus_path_encode.argtypes=(ctypes.c_char_p,ctypes.c_char_p,ctypes.POINTER(ctypes.c_void_p));self.lib.sd_bus_path_encode.restype=ctypes.c_int
        if self.lib.sd_bus_path_encode(b'/org/freedesktop/login1/session',self.key,ctypes.byref(self.path))<0:raise OSError('Cannot encode native session')
        if not self.connect_bus():
            self.close();raise OSError('Native session bus unavailable')

    def connect_bus(self):
        if self.bus:self.lib.sd_bus_unref(self.bus)
        self.bus=ctypes.c_void_p()
        if self.lib.sd_bus_open_system(ctypes.byref(self.bus))<0:return False
        self.lib.sd_bus_set_method_call_timeout(self.bus,300000)
        return True

    def text(self,name):
        value=ctypes.c_void_p();code=getattr(self.lib,'sd_session_get_'+name)(self.key,ctypes.byref(value))
        if code<0:raise OSError(-code,'Native session lookup failed')
        try:return ctypes.string_at(value).decode('ascii')
        finally:self.libc.free(value)

    def number(self,name):
        value=ctypes.c_uint();code=getattr(self.lib,'sd_session_get_'+name)(self.key,ctypes.byref(value))
        if code<0:raise OSError(-code,'Native session lookup failed')
        return value.value

    def state(self):
        try:
            state=self.text('state')
            if state=='closing':return None
            if (self.number('uid')!=os.getuid() or self.text('class')!='user' or self.text('seat')!='seat0'
                    or self.text('type') not in {'wayland','x11'} or not 1<=self.number('vt')<=63
                    or self.lib.sd_session_is_remote(self.key)!=0):raise PermissionError('Not the native local desktop')
            if state!='active' or self.lib.sd_session_is_active(self.key)!=1:return False
            if not self.bus and not self.connect_bus():return False
            locked=ctypes.c_int()
            result=self.lib.sd_bus_get_property_trivial(self.bus,b'org.freedesktop.login1',ctypes.string_at(self.path),
                b'org.freedesktop.login1.Session',b'LockedHint',None,b'b',ctypes.byref(locked))
            if result<0:
                self.connect_bus();return False
            return locked.value==0
        except OSError as error:
            if error.errno==errno.ENOENT:return None
            if isinstance(error,PermissionError):raise
            return False

    def close(self):
        if self.bus:self.lib.sd_bus_unref(self.bus);self.bus=ctypes.c_void_p()
        if self.path:self.libc.free(self.path);self.path=ctypes.c_void_p()
