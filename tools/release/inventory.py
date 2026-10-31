#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Map staged executable payload to source/notice records; reject unknown libraries."""
import argparse,hashlib,json,re,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('package',type=Path);p.add_argument('sources',type=Path);a=p.parse_args();pkg=a.package.resolve();sources=a.sources.resolve();wb=Path(__file__).resolve().parents[2]
records=json.loads((sources/'source-inventory.json').read_text());components={};files={}
for recipe in (pkg/'licenses').glob('homebrew-*/.brew/*.rb'):
 name=recipe.parent.parent.name;text=recipe.read_text();license_line=next((s.strip() for s in text.splitlines() if s.strip().startswith('license ')),'see upstream notices')
 components[name]={'license':license_line.removeprefix('license '),'notices':'licenses/'+name,'sources':[x['name'] for x in records if x['component']==name],'role':'source/build input only unless listed below'}
components.update({
 'wine':{'license':'LGPL-2.1-or-later; see individual file notices','notices':'licenses/wine','sources':['crossover-sources-26.3.0.tar.gz','wine-workbench-macos.bundle','mesa-wine-gl.bundle'],'role':'Wine runtime; build MinGW inputs separately included'},
 'mesa-bridge':{'license':'Mesa file-level licenses; bridge SPDX MIT','notices':'licenses/mesa','sources':['mesa-18b8ad0.tar.gz','mesa-wine-gl.bundle'],'role':'Mesa, Zink, KosmicKrisp and bridge'},
 'gstreamer':{'license':'GStreamer/subproject file-level licenses; see COPYING and notices','notices':'licenses/gstreamer','sources':['gstreamer-0701255.tar.gz','gstreamer-packagecache/','gstreamer-subprojects.json'],'role':'GStreamer 1.28.7 plus GLib2.86.3, libffi-meson3.2.9999.5, proxy-libintl0.5, PCRE2 10.47, ORC and gl-headers at recorded commits'},
 'winetricks':{'license':'LGPL-2.1-or-later','notices':'licenses/winetricks','sources':['winetricks-08304e81.tar.gz'],'role':'setup helper; its complete source is also tools/winetricks'},
 'release-wrapper':{'license':'MIT','notices':'licenses/release-wrapper','sources':['wine-workbench-macos.bundle'],'role':'launcher and packaging helpers'}})
(pkg/'licenses/release-wrapper').mkdir(exist_ok=True);shutil.copy2(wb/'release/LICENSE',pkg/'licenses/release-wrapper/LICENSE')
# Bridge declares MIT in its source; retain its attribution and full MIT text.
(pkg/'licenses/mesa/bridge').mkdir(exist_ok=True)
mit=(wb/'release/LICENSE').read_text();mit=mit[mit.index('Permission is hereby'):]
(pkg/'licenses/mesa/bridge/LICENSE').write_text('MIT License\n\nCopyright 2026 mesa_gl_to_metal project\n\n'+mit)
shutil.copy2(wb/'patches/LICENSE',pkg/'licenses/wine/WORKBENCH-PATCH-LICENSE')
shutil.copy2(sources/'source-inventory.json',pkg/'provenance/dependency-sources.json')
graphics=json.loads((pkg/'graphics/manifest.json').read_text())['libraries'];graphics={v['name']:k for k,v in graphics.items()}
for f in sorted(pkg.rglob('*')):
 if not f.is_file() or f.is_symlink():continue
 rel=f.relative_to(pkg);parts=rel.parts;owner=None
 if parts[0]=='runtime':
  if len(parts)>2 and parts[1:3]==('lib','bundled'):
   if f.suffix!='.dylib' and f.name!='gst-plugin-scanner':continue
   if f.name.startswith(('libgst','libglib','libgobject','libgio','libgirepository','libgmodule','libgthread','libffi','libintl-gst','liborc','libpcre')) or 'gstreamer-1.0' in parts or f.name=='gst-plugin-scanner':owner='gstreamer'
   else:
    q=(Path('/usr/local/Cellar/sdl2/2.32.10/lib') if f.name=='libSDL2-2.0.0.dylib' else Path('/usr/local/lib'))/f.name
    if not q.exists():
     matches=list(Path('/usr/local/Cellar').glob('*/**/lib/'+f.name));q=matches[0] if len(matches)==1 else q
    resolved=str(q.resolve())
    if '/Cellar/' not in resolved:raise SystemExit('No source owner: '+str(rel))
    formula,version=resolved.split('/Cellar/')[1].split('/')[:2];owner='homebrew-'+formula+'-'+version
    def code(path):return subprocess.check_output(['/usr/bin/otool','-s','__TEXT','__text',str(path)]).splitlines()[2:]
    assert code(f)==code(q),('bundled executable code differs from recorded source library',rel,q)
  else:owner='wine'
 elif parts[0]=='graphics' and f.suffix=='.dylib':
  origin=graphics[f.name]
  if '/Cellar/' in origin:
   formula,version=origin.split('/Cellar/')[1].split('/')[:2];owner='homebrew-'+formula+'-'+version
  else:owner='mesa-bridge'
 elif str(rel)=='tools/winetricks':owner='winetricks'
 elif str(rel)=='tools/cabextract':owner='homebrew-cabextract-1.11'
 elif str(rel)=='tfa':owner='release-wrapper'
 if owner:
  assert owner in components,(rel,owner)
  assert (pkg/components[owner]['notices']).is_dir(),owner
  assert components[owner]['sources'],owner
  files[str(rel)]={'component':owner,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
counts={c:sum(v['component']==c for v in files.values()) for c in components}
(pkg/'component-inventory.json').write_text(json.dumps({'components':components,'files':files,'scope':'Payload ownership plus nested/build inputs; source archive hashes in provenance/dependency-sources.json'},indent=2)+'\n')
lines=['# Components, sources and notices','','Every regular runtime payload file, bundled native library and shipped helper is mapped in `component-inventory.json`. Source-only inputs are retained separately from shipped components. Source filenames refer to the upstream or supplemental source release assets; project patches and build scripts are in the Git bundles.','','| Component | Payload files | License declaration | Notices | Source inputs |','| --- | ---: | --- | --- | --- |']
for name,c in sorted(components.items()):lines.append('| '+name+' | '+str(counts[name])+' | '+c['license'].replace('|','/')+' | ['+c['notices']+']('+c['notices']+') | '+', '.join(c['sources'])+' |')
lines+=['','Nested copyrights and terms remain in the original notice texts and complete sources. Mesa includes its in-tree utilities (including BLAKE3); LLVM includes its nested third-party notices; MoltenVK sources include the exact external revisions from the retained recipe. Windows CRT build inputs are covered by the MinGW/GCC source recipes, not by relabeling those files as Wine-only code.','','System libraries, frameworks, Metal and Rosetta are supplied by macOS and are not redistributed. Microsoft fonts, Visual C++ prerequisites and Steam are downloaded by setup and are not in the release archive. Game files and private bottles are excluded.','','Replaced library binaries can be used with this unpacked runtime; corresponding patched source and build recipes are supplied. This inventory records the release packaging review, not a claim of OpenGL conformance or a blanket license for all components.']
(pkg/'COMPONENTS.md').write_text('\n'.join(lines)+'\n');print('Mapped',len(files),'payload files;',sum(v>0 for v in counts.values()),'shipped component groups')
