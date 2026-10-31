#include <dlfcn.h>
#include <stdio.h>
int main(int n,char **v){int failures=0;for(int i=1;i<n;i++){void *h=dlopen(v[i],RTLD_NOW|RTLD_LOCAL);if(!h){fprintf(stderr,"FAIL %s: %s\n",v[i],dlerror());failures++;}}printf("checked=%d failures=%d\n",n-1,failures);return failures?1:0;}
