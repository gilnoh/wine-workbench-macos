import os,subprocess,json
from pathlib import Path
r=Path('/private/tmp/TFA portable validation tpzsgkyf');env=os.environ.copy()
for k in list(env):
 if k.startswith(('WINE','DYLD_','GST_','MESA_','VK_')):env.pop(k)
b=r/'runtime/lib/bundled'
env.update(PATH='/usr/bin:/bin:/usr/sbin:/sbin',WINEPREFIX=str(r/'prefix'),WINEDEBUG='-all,+loaddll',WINEDLLOVERRIDES='mscoree,mshtml=',GST_PLUGIN_SYSTEM_PATH=str(b/'gstreamer-1.0'),GST_PLUGIN_PATH=str(b/'gstreamer-1.0'),GST_PLUGIN_SCANNER=str(b/'gst-plugin-scanner'),GST_REGISTRY=str(r/'gst-registry.bin'))
wrap=['/usr/bin/sandbox-exec','-f',str(r/'isolation.sb'),str(r/'runtime/bin/wine')];results=[]
for name,args in [('https',['https.exe']),('media',['mfprobe.exe',str(r/'splash.mp4')]),('missing-media',['mfprobe.exe',str(r/'absent.mp4')])]:
 with (r/(name+'.log')).open('w') as f:
  try:p=subprocess.run(wrap+[str(r/args[0]),*args[1:]],env=env,cwd=r,stdout=f,stderr=subprocess.STDOUT,timeout=150);status=p.returncode
  except subprocess.TimeoutExpired:status='timeout'
 text=(r/(name+'.log')).read_text(errors='replace');ok=status==0 if name!='missing-media' else status!=0 and status!='timeout'
 if name=='media':ok=ok and 'decoded_frames=30' in text
 results.append(dict(test=name,exit_status=status,passed=ok));print(results[-1],flush=True)
(r/'media-network-results.json').write_text(json.dumps(results,indent=2))
subprocess.run(['/usr/bin/sandbox-exec','-f',str(r/'isolation.sb'),str(r/'runtime/bin/wineserver'),'-k'],env=env,timeout=20)
