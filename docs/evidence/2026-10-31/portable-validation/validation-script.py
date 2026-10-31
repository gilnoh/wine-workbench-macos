import os, pathlib, subprocess, json, shutil, tempfile
root=pathlib.Path(tempfile.mkdtemp(prefix='TFA portable validation ',dir='/private/tmp'))
wb=pathlib.Path('/Users/tailblues/tfa-release-repro/wine-workbench-macos'); gl=wb.parent/'mesa-wine-gl'
for src,dst in [(wb/'runtimes/winecx-26.3.0-tfa-portable-x86_64',root/'runtime'),(gl/'build/graphics-bundle-swap-fix',root/'graphics')]:
 subprocess.run(['/bin/cp','-Rc',str(src),str(dst)],check=True)
shutil.copy2(gl/'build/wine-gl-swap.exe',root/'probe.exe')
print(root,flush=True)
issues=[];count=0
for base in [root/'runtime',root/'graphics']:
 for p in base.rglob('*'):
  if p.is_symlink():
   if not p.resolve().is_relative_to(root):issues.append([str(p),'external symlink',os.readlink(p)])
   continue
  if not p.is_file():continue
  with p.open('rb') as f:magic=f.read(4)
  if magic not in [b'\xcf\xfa\xed\xfe',b'\xce\xfa\xed\xfe',b'\xca\xfe\xba\xbe']:continue
  count+=1
  output=subprocess.check_output(['/usr/bin/otool','-L',str(p)],text=True)
  for line in output.splitlines()[1:]:
   dep=line.strip().split(' (compatibility')[0]
   if dep.startswith(('/usr/lib/','/System/')):continue
   if dep.startswith('@loader_path/'):
    if not (p.parent/dep[len('@loader_path/'):]).exists():issues.append([str(p),'missing',dep])
   elif dep.startswith('@rpath/'):pass
   else:issues.append([str(p),'unexpected link',dep])
(root/'static-audit.json').write_text(json.dumps({'mach_o_files':count,'issues':issues},indent=2))
print('Static audit:',count,'Mach-O files;',len(issues),'issues',flush=True)
policy='(version 1) (allow default)\n'+''.join('(deny file-read* (subpath "'+p+'"))\n' for p in ['/usr/local','/opt/homebrew','/Users/tailblues/tfa-release-repro','/Users/tailblues/mesa-wine-gl','/Users/tailblues/wine-workbench-macos'])
(root/'isolation.sb').write_text(policy)
wrapper=['/usr/bin/sandbox-exec','-f',str(root/'isolation.sb')]
control=subprocess.run(wrapper+['/bin/cat',str(wb/'AGENT_START_HERE.md')],capture_output=True,text=True)
(root/'isolation-control.txt').write_text(control.stderr)
assert control.returncode!=0 and 'Operation not permitted' in control.stderr,control
print('Blocked-path control passed',flush=True)
env=os.environ.copy()
for key in list(env):
 if key.startswith(('DYLD_','WINE','MESA_','VK_','ZINK_')):env.pop(key)
env.update(PATH='/usr/bin:/bin:/usr/sbin:/sbin',WINEPREFIX=str(root/'prefix'),WINEDEBUG='-all,+wgl',WINEDLLOVERRIDES='mscoree,mshtml=',WINEARCH='win64')
wine=str(root/'runtime/bin/wine');results=[]
for mode in ['bootstrap','positive','wrong-expected','missing-bridge','apple-control']:
 e=env.copy();cmd=[wine,'wineboot','-u'] if mode=='bootstrap' else [wine,str(root/'probe.exe')]
 if mode not in ['bootstrap','apple-control']:e.update(MESA_WINE_LIB=str(root/'graphics/lib/libmesa_wine.dylib'),VK_DRIVER_FILES=str(root/'graphics/kosmickrisp.json'),MESA_KK_EXPERIMENTAL='xfb_gs_probe,custom_border,rgb32_texel')
 if mode=='wrong-expected':cmd+=['--wrong-expected']
 if mode=='missing-bridge':e['MESA_WINE_LIB']=str(root/'graphics/absent.dylib')
 with (root/(mode+'.log')).open('w') as f:
  try:r=subprocess.run(wrapper+cmd,env=e,cwd=root,stdout=f,stderr=subprocess.STDOUT,timeout=150);status=r.returncode
  except subprocess.TimeoutExpired:status='timeout'
 content=(root/(mode+'.log')).read_text(errors='replace')
 expected={'bootstrap':0,'positive':0,'wrong-expected':1,'missing-bridge':2,'apple-control':2}[mode]
 ok=status==expected
 if mode=='positive':ok &= 'SUMMARY checks=49 failures=0' in content and 'first GPU frame presented' in content
 if mode=='wrong-expected':ok &= 'SUMMARY checks=49 failures=25' in content
 results.append(dict(mode=mode,status=status,passed=ok));print(results[-1],flush=True)
 (root/'results.json').write_text(json.dumps(results,indent=2))
 if mode=='bootstrap' and not ok:break
subprocess.run(wrapper+[str(root/'runtime/bin/wineserver'),'-k'],env=env,timeout=20)
print('EVIDENCE',root,flush=True)
