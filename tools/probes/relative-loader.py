from pathlib import Path
import os,subprocess,shutil,tempfile,sys
root=Path(tempfile.mkdtemp(prefix='tfa relocated loader ',dir='/private/tmp'))
source=Path(sys.argv[1]).resolve()/'lib/bundled'
shutil.copytree(source,root/'lib/bundled');mods=root/'lib/wine/x86_64-unix';mods.mkdir(parents=True)
(root/'check.c').write_text(r'''
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
int check(const char *root) {
 const char *libs[]={"libvulkan.1.dylib","libfreetype.6.dylib","libgnutls.30.dylib","libSDL2-2.0.0.dylib"};
 const char *symbols[]={"vkGetInstanceProcAddr","FT_Init_FreeType","gnutls_global_init","SDL_Init"};
 int failures=0;
 for(int i=0;i<4;i++) {
  char name[256];snprintf(name,sizeof(name),"@loader_path/../../bundled/%s",libs[i]);
  void *h=dlopen(name,RTLD_NOW);if(!h){printf("FAIL %s: %s\n",name,dlerror());failures++;continue;}
  Dl_info info;void *sym=dlsym(h,symbols[i]);
  if(!sym || !dladdr(sym,&info) || strncmp(info.dli_fname,root,strlen(root))) {printf("FAIL wrong location %s\n",name);failures++;}
  else printf("PASS %s\n",info.dli_fname);
 }
 if(dlopen("@loader_path/../../bundled/absent-control.dylib",RTLD_NOW)){printf("FAIL missing library control\n");failures++;}
 else printf("PASS missing library rejected\n");
 return failures;
}
''')
(root/'main.c').write_text(r'''
#include <dlfcn.h>
#include <stdio.h>
int main(int argc,char **argv){if(argc!=3)return 2;void*h=dlopen(argv[1],RTLD_NOW);if(!h){puts(dlerror());return 2;}int(*f)(const char*)=dlsym(h,"check");return f?f(argv[2]):2;}
''')
env=os.environ.copy()
for key in ['CPATH','C_INCLUDE_PATH','LIBRARY_PATH','DYLD_LIBRARY_PATH','DYLD_FALLBACK_LIBRARY_PATH']:env.pop(key,None)
env['SDKROOT']=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
for args in [['-dynamiclib',str(root/'check.c'),'-o',str(mods/'check.dylib')],[str(root/'main.c'),'-o',str(root/'probe')]]:
 subprocess.run(['/usr/bin/clang','-arch','x86_64',*args],env=env,check=True)
subprocess.run([str(root/'probe'),str(mods/'check.dylib'),str(root)],env=env,cwd='/private/tmp',check=True)
print('Probe workspace:',root)
