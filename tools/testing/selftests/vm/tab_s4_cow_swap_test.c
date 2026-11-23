// Tab S4 bounded anonymous swap/COW stress and integrity check.
// Build: clang -O2 -Wall -std=gnu11 tab_s4_cow_swap_test.c -o cow_swap_test
// Run: ./cow_swap_test 8 64 128  (rounds, test MiB, pressure MiB)
// Never treats pressure as proof of swap: checks /proc/vmstat counters.
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static uint64_t vm_counter(const char *key) {
    FILE *f = fopen("/proc/vmstat", "r");
    char name[128];
    unsigned long long v;
    if (!f) return 0;
    while (fscanf(f, "%127s %llu", name, &v) == 2) {
        if (strcmp(name, key) == 0) { fclose(f); return (uint64_t)v; }
    }
    fclose(f);
    return 0;
}
static void touch(unsigned char *p, size_t n, size_t pg, unsigned char value) {
    size_t i;
    for (i=0; i<n; i+=pg) p[i]=value;
}
static int verify(unsigned char *p, size_t n, size_t pg, unsigned char value) {
    size_t i;
    for (i=0; i<n; i+=pg) if(p[i]!=value) return -1;
    return 0;
}
static unsigned long parse(const char *s, unsigned long fallback) {
    char *end; unsigned long v;
    if (!s) return fallback;
    errno=0; v=strtoul(s,&end,10);
    if(errno||*end||!v) { fprintf(stderr,"invalid integer: %s\n",s); exit(2); }
    return v;
}
int main(int argc, char **argv) {
    unsigned long rounds=parse(argc>1?argv[1]:NULL,8);
    unsigned long test_mb=parse(argc>2?argv[2]:NULL,64);
    unsigned long pressure_mb=parse(argc>3?argv[3]:NULL,128);
    size_t pg=(size_t)sysconf(_SC_PAGESIZE);
    unsigned long round;
    uint64_t in0=vm_counter("pswpin"), out0=vm_counter("pswpout");
    if(rounds>100 || test_mb>256 || pressure_mb>384 || test_mb+pressure_mb>512) {
        fprintf(stderr,"Refusing unbounded memory pressure (max 100 rounds, 512 MiB combined).\n");
        return 2;
    }
    for(round=0;round<rounds;round++) {
        size_t length=(size_t)test_mb*1024*1024, stress=(size_t)pressure_mb*1024*1024;
        unsigned char a=(unsigned char)(0x11+(round%40)), b=(unsigned char)(0xa0+(round%40));
        unsigned char *p=mmap(NULL,length,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        unsigned char *q;
        pid_t child; int status=0;
        if(p==MAP_FAILED) { perror("mmap test"); return 3; }
        touch(p,length,pg,a);
        child=fork();
        if(child<0) { perror("fork"); return 3; }
        if(child==0) {
            q=mmap(NULL,stress,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
            if(q==MAP_FAILED) _exit(5);
            touch(q,stress,pg,0x57);
            if(verify(p,length,pg,a)) _exit(6);
            touch(p,length,pg,b);
            if(verify(p,length,pg,b)) _exit(7);
            if(verify(q,stress,pg,0x57)) _exit(8);
            munmap(q,stress); munmap(p,length); _exit(0);
        }
        q=mmap(NULL,stress,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(q==MAP_FAILED) { perror("mmap pressure"); return 3; }
        touch(q,stress,pg,0x37);
        if(verify(p,length,pg,a) || verify(q,stress,pg,0x37)) {
            fprintf(stderr,"parent memory corruption at round %lu\n",round+1); return 4;
        }
        if(waitpid(child,&status,0)<0 || !WIFEXITED(status) || WEXITSTATUS(status)!=0) {
            fprintf(stderr,"child failure at round %lu status=%d\n",round+1,status); return 5;
        }
        munmap(q,stress); munmap(p,length);
        printf("PASS round %lu/%lu\n",round+1,rounds);
        fflush(stdout);
    }
    {
        uint64_t in1=vm_counter("pswpin"), out1=vm_counter("pswpout");
        printf("PASS: %lu swap/COW integrity rounds; system pswpin_delta=%llu pswpout_delta=%llu\n",
               rounds,(unsigned long long)(in1-in0),(unsigned long long)(out1-out0));
        if(in1==in0 && out1==out0) puts("NOTE: no observed system swap traffic; swap-cache COW path not established.");
        else puts("NOTE: system-wide swap observed; not proof the tested pages were swapped.");
    }
    return 0;
}
