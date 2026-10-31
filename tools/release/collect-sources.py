#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Collect exact installed dependency recipes, sources and notices for a staged release."""
import argparse,concurrent.futures,hashlib,json,re,shutil,subprocess,tarfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('package',type=Path);p.add_argument('sources',type=Path);a=p.parse_args();pkg=a.package.resolve();src=a.sources.resolve();src.mkdir(parents=True,exist_ok=True)
# Include graphics dependencies and build inputs used by their generators.
formulae={'llvm':'21.1.8_1','z3':'4.15.4','spirv-tools':'1.4.341.0','spirv-headers':'1.4.341.0','spirv-llvm-translator':'21.1.4','libclc':'21.1.8','mingw-w64':'13.0.0_2'}
entries=[(Path('/usr/local/Cellar'),name,v,'homebrew-') for name,v in formulae.items()]+[(Path('/opt/homebrew/Cellar'),'mingw-w64','14.0.0_1','homebrew-arm-')]
for cellar,name,v,prefix in entries:
 root=cellar/name/v;dest=pkg/'licenses'/(prefix+name+'-'+v);dest.mkdir(exist_ok=True)
 for f in root.rglob('*'):
  if f.is_file() and (f.name.upper().startswith(('LICENSE','COPYING','COPYRIGHT','NOTICE','AUTHORS')) or f.name=='INSTALL_RECEIPT.json' or '.brew' in f.parts):
   out=dest/f.relative_to(root);out.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f,out)
recipes=list((pkg/'licenses').glob('homebrew-*/.brew/*.rb'));jobs=[]
for recipe in recipes:
 text=recipe.read_text();component=recipe.parent.parent.name
 # First URL is stable source; all explicitly hashed patches/resources retained too.
 for i,m in enumerate(re.finditer(r'^\s+url "([^"]+)"[^\n]*\n(?:[^\n]*\n){0,3}?\s+sha256 "([a-f0-9]{64})"',text,re.M)):
  jobs.append(dict(component=component,url=m[1],expected_sha256=m[2],name=component+'-'+str(i)+'-'+m[1].split('/')[-1]))
 # Git resources are immutable revision inputs; use codeload archives.
 stable=text.split('  head do')[0]
 for m in re.finditer(r'url "https://github.com/([^" ]+)\.git",\s*revision: "([a-f0-9]{40})"',stable):
  jobs.append(dict(component=component,url='https://codeload.github.com/'+m[1]+'/tar.gz/'+m[2],commit=m[2],name=component+'-'+m[1].split('/')[-1]+'-'+m[2]+'.tar.gz'))
jobs.append(dict(component='winetricks',url='https://codeload.github.com/Winetricks/winetricks/tar.gz/08304e81f9ac9a83c552a6bd78689040d174bf95',commit='08304e81f9ac9a83c552a6bd78689040d174bf95',name='winetricks-08304e81.tar.gz'))
def fetch(j):
 dest=src/j['name'];expected=j.get('expected_sha256')
 if not dest.exists() or (expected and hashlib.sha256(dest.read_bytes()).hexdigest()!=expected):
  subprocess.run(['curl','-fL','--retry','3','--max-time','600',j['url'],'-o',str(dest)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 digest=hashlib.sha256(dest.read_bytes()).hexdigest()
 if expected and digest!=expected and j['url'].endswith('.diff'):
  # GitHub can change diff index abbreviation length. Accept only a result
  # matching the original recipe's exact SHA256, never a relaxed checksum.
  text=dest.read_text()
  for width in range(7,41):
   candidate=re.sub(r'(?m)^index ([0-9a-f]+)\.\.([0-9a-f]+)',lambda m:'index '+m[1][:width]+'..'+m[2][:width],text).encode()
   if hashlib.sha256(candidate).hexdigest()==expected:
    dest.write_bytes(candidate);digest=expected;j['diff_index_abbreviation']=width;break
 if expected and digest!=expected:raise RuntimeError('checksum mismatch '+j['name'])
 j['sha256']=digest;j['bytes']=dest.stat().st_size
 # Preserve upstream notices, including filenames not installed by Homebrew.
 try:
  with tarfile.open(dest) as tf:
   for member in tf:
    name=Path(member.name)
    if not member.isfile() or member.size>2000000 or not name.name.upper().startswith(('LICENSE','COPYING','COPYRIGHT','NOTICE','AUTHORS','FTL.TXT')):continue
    if '..' in name.parts or name.is_absolute():raise RuntimeError('unsafe source archive')
    out=pkg/'licenses'/j['component']/'upstream'/name;out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(tf.extractfile(member).read())
 except tarfile.ReadError:pass # patch files are retained as source inputs
 print('verified',j['name'],flush=True);return j
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:results=list(pool.map(fetch,jobs))
(src/'source-inventory.json').write_text(json.dumps(results,indent=2)+'\n')
