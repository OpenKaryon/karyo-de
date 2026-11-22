#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real cold Chromium/WebKit PKCE in the fixed disposable Work context."""
import base64,hashlib,json,os,pwd,re,shutil,socket,stat,subprocess,sys,time,urllib.error,urllib.parse,urllib.request
from pathlib import Path
sys.dont_write_bytecode=True
if socket.gethostname() not in {'karyo-de-acceptance','karyo-de-ubuntu-acceptance'}:
    raise SystemExit('Use only the fixed disposable hosts')
BASE='debian' if socket.gethostname()=='karyo-de-acceptance' else 'ubuntu'
HOME=Path('/home/kq-switch-work')
DATA=HOME/'.karyo-qa-pkce'
ROOT=Path('/var/lib/karyo-qa-netbird-browser-admission')
DRIVER='http://127.0.0.1:9515'
REALM='http://192.168.230.163:18443'
INFO=DATA/'session.json'
OPENER='/usr/local/libexec/karyo-qa-pkce-opener'
SCRIPT='/usr/local/libexec/karyo-qa-netbird-pkce'


def require(value,message):
    if not value:raise RuntimeError(message)


def wait(predicate,message,seconds=45):
    deadline=time.monotonic()+seconds
    while not predicate():
        require(time.monotonic()<deadline,message);time.sleep(.1)


def wd(method,path,data=None):
    request=urllib.request.Request(DRIVER+path,method=method,
        data=None if data is None else json.dumps(data).encode(),headers={'Content-Type':'application/json'})
    client=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with client.open(request,timeout=50) as response:value=json.load(response)
    except urllib.error.HTTPError:
        raise RuntimeError('Native browser driver rejected operation '+path.split('/')[1]) from None
    require(isinstance(value,dict) and 'value' in value,'Invalid browser driver response')
    return value['value']


def user(*args):
    return ['/usr/sbin/runuser','-u','kq-switch-work','--','/usr/bin/env','XDG_RUNTIME_DIR=/run/user/1002',
        'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1002/bus','WAYLAND_DISPLAY=wayland-0',*args]


def ordinary():
    require(os.getuid()==os.geteuid()==os.getgid()==os.getegid()==1002,'Use only the ordinary QA Work user')
    sys.path.insert(0,'/usr/libexec/karyo/profiles')
    from karyo_profiles.netbird_client import capture
    capture(os.environ.get('KARYO_NETBIRD_ENDPOINT'))
    s=DATA.lstat()
    require(stat.S_ISDIR(s.st_mode) and s.st_uid==1002 and stat.S_IMODE(s.st_mode)==0o700,'Unsafe browser fixture')


def session():
    s=INFO.lstat()
    require(stat.S_ISREG(s.st_mode) and s.st_uid==1002 and stat.S_IMODE(s.st_mode)==0o600 and s.st_nlink==1,'Unsafe browser session fixture')
    value=json.loads(INFO.read_bytes());require(re.fullmatch('[A-Za-z0-9-]{1,128}',value['id']) is not None,'Invalid browser session')
    return '/session/'+value['id']


def evaluate(script,args=[]):
    return wd('POST',session()+'/execute/sync',{'script':script,'args':args})


def open_browser(url):
    ordinary()
    value=urllib.parse.urlsplit(url);query=urllib.parse.parse_qs(value.query)
    require(value.scheme=='http' and value.netloc=='192.168.230.163:18443' and value.path.startswith('/oauth2/')
        and query.get('code_challenge_method')==['S256'] and query.get('state'),'Unexpected laboratory authorization request')
    require(not os.path.lexists(INFO),'Refuse an existing browser session')
    if BASE=='debian':
        caps={'browserName':'chrome','goog:chromeOptions':{'binary':'/usr/bin/chromium','args':[
            '--ozone-platform=wayland','--user-data-dir='+str(DATA/'profile'),'--no-first-run',
            '--no-default-browser-check','--disable-background-networking','--disable-dev-shm-usage','--password-store=basic']}}
    else:
        caps={'browserName':'MiniBrowser','webkitgtk:browserOptions':{'args':['--automation','--private','--enable-sandbox']}}
    result=wd('POST','/session',{'capabilities':{'alwaysMatch':caps}})
    identifier=result['sessionId'];require(re.fullmatch('[A-Za-z0-9-]{1,128}',identifier) is not None,'Invalid native session')
    wd('POST','/session/'+identifier+'/url',{'url':url})
    fd=os.open(INFO,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    try:os.write(fd,json.dumps({'id':identifier,'backend':BASE}).encode());os.fsync(fd)
    finally:os.close(fd)


def register_default():
    ordinary()
    apps=HOME/'.local/share/applications';apps.mkdir(parents=True,exist_ok=True)
    path=apps/'karyo-qa-browser.desktop'
    content='[Desktop Entry]\\nType=Application\\nName=Karyo QA Browser\\nExec='+OPENER+' %u\\nTerminal=false\\nMimeType=x-scheme-handler/http;x-scheme-handler/https;\\nNoDisplay=true\\n'
    require(not os.path.lexists(path),'Prior default-browser entry requires review')
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    try:os.write(fd,content.encode().replace(bytes([92,110]),bytes([10])));os.fsync(fd)
    finally:os.close(fd)
    for kind in ('http','https'):
        subprocess.run(['/usr/bin/xdg-mime','default',path.name,'x-scheme-handler/'+kind],check=True,timeout=8)
        result=subprocess.check_output(['/usr/bin/xdg-mime','query','default','x-scheme-handler/'+kind],text=True,timeout=8).strip()
        require(result==path.name,'Ordinary default browser association failed')
    print('PASS ordinary XDG HTTP/HTTPS default browser association',flush=True)


def safe_view():
    return evaluate("""return {
        origin:location.origin,path:location.pathname,
        success:location.hostname==='localhost' && document.body.innerText.includes('Login Successful'),
        fields:Array.from(document.querySelectorAll('input')).map(x=>({name:x.name,id:x.id,type:x.type})),
        buttons:Array.from(document.querySelectorAll('button,input[type=submit]')).map(x=>({type:x.type,text:(x.innerText||x.getAttribute('value')||'').slice(0,80)})),
        links:Array.from(document.querySelectorAll('a')).map(x=>({text:x.innerText.slice(0,80),path:new URL(x.href).pathname}))
    };""")


def find(selector):
    values=wd('POST',session()+'/elements',{'using':'css selector','value':selector})
    return [v['element-6066-11e4-a52e-4f735466cecf'] for v in values]


def keys(element,value):
    wd('POST',session()+'/element/'+element+'/value',{'text':value})


def authenticate():
    ordinary();wait(lambda:INFO.exists(),'Cold real browser never started',seconds=60)
    creds=DATA/'credentials.json';s=creds.lstat()
    require(stat.S_ISREG(s.st_mode) and s.st_uid==1002 and stat.S_IMODE(s.st_mode)==0o600 and s.st_nlink==1,'Unsafe browser credentials')
    credentials=json.loads(creds.read_bytes());require(set(credentials)=={'email','password'},'Invalid lab credentials')
    deadline=time.monotonic()+100;actions=set()
    while True:
        view=safe_view()
        if view['success']:break
        require(view['origin']==REALM,'Browser left the laboratory identity provider')
        require(time.monotonic()<deadline,'Real browser login did not complete')
        password=find('input[type=password]');email=find('input[type=email],input[name=login],input[name=email]')
        signature=(view['path'],bool(email),bool(password))
        if signature not in actions and (email or password):
            for element in email:keys(element,credentials['email'])
            for element in password:keys(element,credentials['password'])
            buttons=find('button[type=submit],input[type=submit]')
            require(bool(buttons),'Identity form has no submit button')
            wd('POST',session()+'/element/'+buttons[0]+'/click',{})
            actions.add(signature)
        elif not email and not password:
            candidates=[v for v in view['links'] if 'email' in v['text'].lower() or 'password' in v['text'].lower()]
            require(len(candidates)==1,'Identity provider needs a reviewed form choice')
            evaluate("""let target=Array.from(document.querySelectorAll('a')).find(x=>x.innerText===arguments[0]);if(!target)throw Error('choice missing');target.click();""",[candidates[0]['text']])
        time.sleep(.3)
    evaluate("history.replaceState({},'',location.pathname);")
    screenshot=base64.b64decode(wd('GET',session()+'/screenshot'))
    require(screenshot.startswith(bytes.fromhex('89504e470d0a1a0a')),'Invalid native browser screenshot')
    fd=os.open(DATA/'success.png',os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    try:os.write(fd,screenshot);os.fsync(fd)
    finally:os.close(fd)
    print(json.dumps({'real_browser_pkce':True,'backend':BASE,'successful_private_callback':True,'sandbox_disabled':False}),flush=True)


def run(*args):return subprocess.check_output(args,text=True,timeout=30).strip()


def state(name):
    return dict(x.split('=',1) for x in run(*user('/usr/bin/systemctl','--user','show',name,'-p','MainPID','-p','InvocationID','-p','ControlGroup','-p','ActiveState')).splitlines())


def user_job(name,*args):
    return user('/usr/bin/systemd-run','--user','--quiet','--collect','--unit='+name,'--property=StandardOutput=null',
                '--property=StandardError=null','--setenv=WAYLAND_DISPLAY=wayland-0',
                '--setenv=XDG_SESSION_TYPE=wayland','--setenv=GDK_BACKEND=wayland',
                '--setenv=WEBKIT_DISABLE_DMABUF_RENDERER=1','--','/usr/bin/setpriv','--inh-caps=-all','--ambient-caps=-all','--',*args)


def observer(command):
    result=subprocess.run(user('/usr/bin/systemd-run','--user','--wait','--pipe','--collect',
        '--unit=karyo-qa-pkce-observe-'+str(time.monotonic_ns())+'.service','--',SCRIPT,command),
        capture_output=True,text=True,timeout=150)
    require(result.returncode==0,'Ordinary browser actor failed; retained safe diagnostic requires review')
    return result.stdout


def root_main():
    require(os.getuid()==os.geteuid()==0 and not sys.argv[1:],'Use root orchestration without arguments')
    sys.path.insert(0,'/usr/libexec/karyo/profiles')
    from karyo_profiles.journal_session import stat_start
    record=json.loads(Path('/run/karyo/profile-host/state.json').read_bytes())['profiles']
    require(len(record)==1 and record[0]['account']['uid']==1002,'Ambiguous Work receipt');record=record[0]
    graph=record['session']['graphical'];network=record['network']['namespace']
    require(stat_start(graph['pid'])==graph['start'],'Captured graph changed')
    s=ROOT.lstat();require(stat.S_ISDIR(s.st_mode) and s.st_uid==0 and stat.S_IMODE(s.st_mode)==0o700,'Unsafe lab admission')
    source=ROOT/'credentials.json';s=source.lstat()
    require(stat.S_ISREG(s.st_mode) and s.st_uid==0 and stat.S_IMODE(s.st_mode)==0o600 and s.st_nlink==1,'Unsafe lab credentials')
    require(not os.path.lexists(DATA),'Prior browser fixture requires review');os.mkdir(DATA,0o700);os.chown(DATA,1002,1002)
    identity=(DATA.stat().st_dev,DATA.stat().st_ino)
    fd=os.open(DATA/'credentials.json',os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    try:os.fchown(fd,1002,1002);os.write(fd,source.read_bytes());os.fsync(fd)
    finally:os.close(fd)
    driver_name='karyo-qa-pkce-driver.service';client_name='karyo-qa-pkce-client.service'
    require(state(driver_name)['ActiveState']=='inactive','Prior browser driver requires review')
    program=['/usr/bin/chromedriver','--port=9515','--allowed-ips=127.0.0.1','--log-level=OFF'] if BASE=='debian' else ['/usr/bin/WebKitWebDriver','--port=9515','--host=local']
    subprocess.run(user_job(driver_name,*program),check=True)
    driver=state(driver_name);require(driver['ActiveState']=='active' and int(driver['MainPID'])>1,'Driver did not start')
    proof={'driver':driver,'directory':list(identity)}
    fd=os.open(DATA/'root-receipt.json',os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    try:os.write(fd,json.dumps(proof).encode())
    finally:os.close(fd)
    up=None
    try:
        # Driver is ready before the request, but no browser session exists yet.
        def driver_ready():
            result=subprocess.run(['/usr/bin/nsenter','--net=/proc/'+str(graph['pid'])+'/ns/net','--','/usr/bin/curl','-fsS',DRIVER+'/status'],capture_output=True,timeout=5)
            return result.returncode==0
        wait(driver_ready,'Driver unavailable')
        require(not INFO.exists(),'Browser was not cold')
        print(observer('--register-default'),end='',flush=True)
        command=user('/usr/bin/systemd-run','--user','--wait','--pipe','--collect','--unit='+client_name,
            '--setenv=WAYLAND_DISPLAY=wayland-0',
            '--','/usr/bin/karyo-netbird-cli','up','--management-url',REALM,'--hostname','karyo-qa-pkce-'+BASE)
        up=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        def listener():
            result=subprocess.run(['/usr/bin/nsenter','--net=/proc/'+str(graph['pid'])+'/ns/net','--','/usr/bin/ss','-H','-ltn','sport = :53000'],capture_output=True,text=True,timeout=4)
            return result.returncode==0 and bool(result.stdout.strip())
        wait(listener,'Actual provider did not start its private callback',seconds=60)
        require(not run('/usr/bin/ss','-H','-ltn','sport = :53000'),'Provider callback leaked to Home')
        output=observer('--authenticate');print(output,end='',flush=True)
        stdout,stderr=up.communicate(timeout=35);require(up.returncode==0,'Actual PKCE CLI failed without logging its authentication URL')
        require(state(driver_name)==driver,'Captured browser driver changed')
        report={'base':BASE,'private_callback':True,'cold_browser':True,'cli_exit':up.returncode,'browser_driver_pid':int(driver['MainPID'])}
        # Verify native renderer isolation rather than accepting a window alone.
        parent=os.stat('/proc/'+driver['MainPID']+'/ns/user');renderers=[]
        observations=[]
        for proc in Path('/proc').iterdir():
            if not proc.name.isdecimal():continue
            value=proc.name
            try:
                args=(proc/'cmdline').read_bytes().split(bytes([0]));status=dict(x.split(':',1) for x in (proc/'status').read_text().splitlines() if ':' in x)
                if '1002' not in status.get('Uid','').split():continue
                role=re.search(rb'(?:^| )--type=([a-z-]+)(?: |$)',b' '.join(args))
                renderer=role is not None and role[1]==b'renderer' or (proc/'comm').read_text().strip()=='WebKitWebProces'
                program=Path(os.fsdecode(args[0])).name if args and args[0] else ''
                if not renderer:
                    if 'chrom' in program.lower() or 'webkit' in program.lower() or 'MiniBrowser' in program:
                        observations.append({'pid':int(value),'program':program[:80],'role':None if role is None else role[1].decode(),'argument_count':len(args),'renderer':False})
                    continue
                ancestor=int(value);descendant=False
                for unused in range(64):
                    if ancestor==int(driver['MainPID']):descendant=True;break
                    ancestor_status=Path('/proc',str(ancestor),'status').read_text()
                    ancestor=int(next(x.split(':',1)[1].strip() for x in ancestor_status.splitlines() if x.startswith('PPid:')))
                    if ancestor<=1:break
                userns=(proc/'ns/user').stat()
                observations.append({'pid':int(value),'descendant':descendant,'cgroup':(proc/'cgroup').read_text().strip(),
                    'nnp':status['NoNewPrivs'].strip(),'seccomp':status['Seccomp'].strip(),'user_ns':[userns.st_dev,userns.st_ino]})
                if not descendant:continue
                require(status['NoNewPrivs'].strip()=='1' and status['Seccomp'].strip()=='2' and
                    (userns.st_dev,userns.st_ino)!=(parent.st_dev,parent.st_ino),'Browser renderer sandbox did not hold')
                require(int(status['CapAmb'],16)==0 and int(status['CapInh'],16)==0,'Browser renderer retained capabilities')
                renderers.append(int(value))
            except (FileNotFoundError,ProcessLookupError):continue
        (DATA/'sandbox-observation.json').write_bytes(json.dumps({'driver':driver,'renderers':observations}).encode())
        require(bool(renderers),'No sandboxed real browser renderer observed');report['sandboxed_renderers']=renderers;report['renderer_proofs']=observations;report['xdg_default_browser']=True
        print(json.dumps(report),flush=True)
        report_path=Path('/var/log/karyo-qa/netbird-pkce-'+BASE+'.json')
        fd=os.open(report_path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
        try:os.write(fd,(json.dumps(report,indent=2)+chr(10)).encode());os.fsync(fd)
        finally:os.close(fd)
        picture=DATA/'success.png';s=picture.lstat();require(stat.S_ISREG(s.st_mode) and s.st_uid==1002 and s.st_nlink==1,'Unsafe browser frame')
        out=Path('/var/log/karyo-qa/netbird-pkce-'+BASE+'.png');fd=os.open(out,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
        try:os.write(fd,picture.read_bytes());os.fsync(fd)
        finally:os.close(fd)
        print(observer('--close'),end='',flush=True)
    finally:
        if up is not None and up.poll() is None:
            subprocess.run(user('/usr/bin/systemctl','--user','stop',client_name),check=True)
            up.communicate(timeout=10)
        require(state(driver_name)==driver,'Browser driver identity changed; retained resources')
        subprocess.run(user('/usr/bin/systemctl','--user','stop',driver_name),check=True)
    require((DATA.stat().st_dev,DATA.stat().st_ino)==identity,'Browser directory changed')
    require(shutil.rmtree.avoids_symlink_attacks,'Native descriptor cleanup required')
    shutil.rmtree(DATA)
    print('PASS cold real browser PKCE, private callback and strict renderer sandbox',flush=True)


try:
    if sys.argv[1:2]==['--open'] and len(sys.argv)==3:open_browser(sys.argv[2])
    elif sys.argv[1:]==['--authenticate']:authenticate()
    elif sys.argv[1:]==['--register-default']:register_default()
    elif sys.argv[1:]==['--describe']:
        ordinary();print(json.dumps(safe_view()))
    elif sys.argv[1:]==['--close']:
        ordinary();wd('DELETE',session());print('PASS captured browser session closed')
    else:root_main()
except Exception as error:
    # Keep authentication URLs, codes, browser responses and credentials out of diagnostics.
    diagnostic={'phase':sys.argv[1] if sys.argv[1:] else 'root','error':type(error).__name__}
    if isinstance(error,RuntimeError):diagnostic['reason']=str(error)
    if DATA.is_dir():
        try:
            if os.geteuid()==1002 and INFO.exists():diagnostic['view']=safe_view()
            (DATA/('safe-diagnostic-'+diagnostic['phase'].replace('--','')+'.json')).write_bytes(json.dumps(diagnostic).encode())
        except Exception:pass
    print('karyo-qa-pkce: '+type(error).__name__+'; inspect the captured safe browser fixture',file=sys.stderr)
    raise SystemExit(1)
