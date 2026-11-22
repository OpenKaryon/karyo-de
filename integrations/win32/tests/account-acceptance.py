#!/usr/bin/python3 -I
# SPDX-License-Identifier: GPL-3.0-or-later
"""Native second-account selection and user MIME precedence, without Wine."""
import json,os,pwd,subprocess
from pathlib import Path
assert os.getuid()==1002 and pwd.getpwuid(os.getuid()).pw_name=='workqa'
home=Path(pwd.getpwuid(os.getuid()).pw_dir)
assert not (home/'.local').exists()
env={**os.environ,'HOME':'/home/win32qa','OPENKARYON_SESSION_SLOT':'1'}
result=subprocess.run(['/usr/bin/karyo-win32','--inspect','/usr/share/karyo-qa/runtime-probe.exe'],env=env,text=True,capture_output=True,check=True)
context=json.loads(result.stdout)
assert context['ownerUid']==1002 and context['storage']==str(home/'.local/share/karyo/windows')
assert context['prefix'].startswith(str(home)+'/')
assert not (home/'.local').exists()
env={**os.environ,'HOME':str(home),'XDG_CURRENT_DESKTOP':'Karyo','LANG':'C.UTF-8','LC_ALL':'C.UTF-8'}
def mime():return subprocess.run(['/usr/bin/gio','mime','application/x-ms-dos-executable'],env=env,text=True,capture_output=True,check=True).stdout.splitlines()[0]
assert mime().endswith(': karyo-win32.desktop')
data=home/'.local/share/applications';data.mkdir(parents=True)
(data/'qa-override.desktop').write_text('[Desktop Entry]\nType=Application\nName=QA override\nExec=true\nMimeType=application/x-ms-dos-executable;\n')
config=home/'.config';config.mkdir()
(config/'mimeapps.list').write_text('[Default Applications]\napplication/x-ms-dos-executable=qa-override.desktop;\n')
assert mime().endswith(': qa-override.desktop')
assert not (home/'.local/share/karyo/windows').exists()
print(json.dumps({'accepted':True,'uid':os.getuid(),'context':context,'no_prefix':True,'native_mime':'Karyo default and explicit user override'}))
