# SPDX-License-Identifier: GPL-3.0-or-later
"""Ordinary-user identity, PE/MSI validation and per-account launch policy."""
import ctypes,hashlib,os,pwd,re,shutil,stat,struct
from pathlib import Path

VERSION="0.1.0"
APP_ID_RE=re.compile(r"[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}")

class LaunchError(RuntimeError):
    def __init__(self, message, returncode=1):
        super().__init__(message)
        self.returncode = min(255, 128 - returncode) if returncode < 0 else min(255, returncode or 1)
def user_home():
    if os.getuid()==0 or os.getuid()!=os.geteuid() or os.getgid()!=os.getegid():
        raise LaunchError('Приложения Windows нельзя запускать от root. Войдите обычным пользователем.')
    return Path(pwd.getpwuid(os.getuid()).pw_dir).resolve(strict=True)
def application_id(path, requested=None):
    if requested is not None:
        if not APP_ID_RE.fullmatch(requested):
            raise LaunchError('Идентификатор приложения: 1–80 букв, цифр, точек, подчёркиваний или дефисов.')
        return requested
    return 'app-' + hashlib.sha256(os.fsencode(path)).hexdigest()[:24]
def validate_application(filename):
    try:
        path = Path(filename).expanduser().resolve(strict=True)
        with os.fdopen(os.open(path,os.O_RDONLY|os.O_NONBLOCK|os.O_NOFOLLOW|os.O_CLOEXEC),'rb') as stream:
            if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                raise LaunchError('Выберите файл приложения EXE или установщика MSI.')
            header = stream.read(64)
            if path.suffix.lower() == '.msi':
                if header[:8] != b'\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1':
                    raise LaunchError('Файл не является пакетом Windows Installer (MSI).')
                return path, 'msi'
            if path.suffix.lower() != '.exe' or header[:2] != b'MZ' or len(header) < 64:
                raise LaunchError('Поддерживаются Windows PE-приложения EXE и установщики MSI.')
            pe_offset = struct.unpack_from('<I', header, 60)[0]
            if pe_offset < 64 or pe_offset > os.fstat(stream.fileno()).st_size - 4:
                raise LaunchError('Некорректный заголовок Windows PE.')
            stream.seek(pe_offset)
            if stream.read(4) != b'PE\x00\x00':
                raise LaunchError('Файл EXE не содержит заголовок Windows PE.')
        return path, 'exe'
    except (OSError, ValueError) as error:
        raise LaunchError(f'Не удалось открыть приложение: {error}') from error

def launch_context(path,kind,home,environment,requested=None):
    # Home, Work and additional protected desktops have real separate UIDs/HOMEs.
    # A terminal environment or the old two-slot variable cannot select another
    # desktop's prefix. An installed executable keeps its installer's profile.
    storage=home/'.local/share/karyo/windows'
    inherited=None
    try:
        parts=path.relative_to(storage/'prefixes').parts
        if len(parts)>=3 and APP_ID_RE.fullmatch(parts[0]) and parts[1]=='drive_c':inherited=parts[0]
    except ValueError:pass
    identifier=application_id(path,requested if requested is not None else inherited)
    if inherited is not None and identifier!=inherited:
        raise LaunchError('An installed executable must use its existing application profile.')
    return {'version':1,'provider':'wine','applicationId':identifier,'ownerUid':os.getuid(),
        'file':str(path),'kind':kind,'prefix':str(storage/'prefixes'/identifier),'storage':str(storage)}


def drop_privileges():
    native=ctypes.CDLL(None,use_errno=True)
    class Header(ctypes.Structure):_fields_=[('version',ctypes.c_uint32),('pid',ctypes.c_int)]
    class Data(ctypes.Structure):_fields_=[('effective',ctypes.c_uint32),('permitted',ctypes.c_uint32),('inheritable',ctypes.c_uint32)]
    header=Header(0x20080522,0);data=(Data*2)()
    if native.prctl(47,4,0,0,0)!=0 or native.capset(ctypes.byref(header),data)!=0 or native.prctl(38,1,0,0,0)!=0:
        raise LaunchError('Cannot discard inherited runtime privileges.')

def wine_environment(context, environment):
    result = environment.copy()
    for name in tuple(result):
        if name.startswith(('QT_','QML_','QML2_','QSG_')):result.pop(name)
    # The Qt inside a Windows application must select qwindows, not the Linux
    # wayland plugin inherited from the Linux desktop. Display and private D-Bus remain intact.
    for name in (
        'WINEPREFIX', 'WINEARCH', 'WINESERVER', 'WINELOADER', 'WINEDLLPATH',
        'QT_QPA_PLATFORM', 'QT_QPA_PLATFORMTHEME', 'QT_PLUGIN_PATH',
        'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QUICK_BACKEND', 'QML_IMPORT_PATH',
        'QML2_IMPORT_PATH', 'LD_LIBRARY_PATH', 'LD_PRELOAD',
    ):
        result.pop(name, None)
    account=pwd.getpwuid(os.getuid())
    result.update(WINEPREFIX=context['prefix'],HOME=account.pw_dir,USER=account.pw_name,LOGNAME=account.pw_name)
    # WINEARCH selects a new prefix; forcing it on an existing prefix can
    # reject a valid legacy win32 installation. Let Wine validate its own
    # registry format and architecture without reading or rewriting it here.
    if not os.path.lexists(Path(context['prefix']) / 'system.reg'):
        result['WINEARCH'] = 'win64'
    # Wine-generated shortcuts call Wine directly and can reuse the wrong session.
    # Our registered menu entries resolve the current session on every launch.
    overrides = result.get('WINEDLLOVERRIDES', '').strip(';')
    result['WINEDLLOVERRIDES'] = (overrides + ';' if overrides else '') + 'winemenubuilder.exe=d'
    return result
def executable(name):
    value = shutil.which(name)
    if not value:
        raise LaunchError(f'Компонент {name} не установлен. Установите пакет karyo-win32.')
    return value
