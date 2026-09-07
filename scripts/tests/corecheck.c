/* Guest regression tests and interactive PS/2 TTY check. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <termios.h>
#define CHECK(x) do { if (!(x)) { printf("CORECHECK_FAIL line %d: %s (errno=%d)\n", __LINE__, #x, errno); return 1; } } while (0)
static int expect_fault(volatile unsigned char *p, int operation) {
    pid_t pid=fork();
    if (pid < 0) return 0;
    if (!pid) {
        if (operation==0) *p=42;
        else if (operation==1) { volatile unsigned char v=*p; (void)v; }
        else ((void (*)(void))(uintptr_t)p)();
        _exit(0);
    }
    int status=0;
    return waitpid(pid,&status,0)==pid && WEXITSTATUS(status)==142;
}
int main(void) {
    void *kernel=(void *)0xffffffff80000000ULL;
    CHECK(__syscall3(SYS_write,1,(long)kernel,1)==-EFAULT);
    CHECK(__syscall3(SYS_read,0,(long)kernel,1)==-EFAULT);
    CHECK(__syscall3(SYS_write,1,0x400000000000ULL,1)==-EFAULT);
    CHECK(munmap(kernel,4096)==-1 && errno==EINVAL);
    CHECK(mprotect(kernel,4096,PROT_READ)==-1 && errno==EINVAL);
    CHECK(mmap(kernel,4096,PROT_READ,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==MAP_FAILED);
    CHECK(mmap(NULL,SIZE_MAX,PROT_READ,MAP_PRIVATE|MAP_ANONYMOUS,-1,0)==MAP_FAILED);
    CHECK(malloc(SIZE_MAX)==NULL && errno==ENOMEM);
    unsigned char *small=malloc(32); CHECK(small); small[0]=123;
    CHECK(realloc(small,SIZE_MAX)==NULL && small[0]==123); free(small);
    volatile unsigned char *p=mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(p!=MAP_FAILED); p[0]=0xc3; /* RET instruction */
    CHECK(munmap((void *)(p+4096),4096)==0);
    CHECK(__syscall3(SYS_write,1,(long)(p+4095),2)==-EFAULT);
    CHECK(mprotect((void *)p,4096,PROT_READ)==0);
    CHECK(__syscall3(SYS_read,0,(long)p,1)==-EFAULT);
    CHECK(expect_fault(p,0)); /* fork must preserve read-only */
    CHECK(expect_fault(p,2)); /* fork must preserve NX */
    CHECK(mprotect((void *)p,4096,PROT_NONE)==0);
    CHECK(__syscall3(SYS_write,1,(long)p,1)==-EFAULT);
    CHECK(expect_fault(p,1));
    CHECK(mprotect((void *)p,4096,PROT_READ|PROT_WRITE)==0);
    p[0]=23; CHECK(p[0]==23);
    CHECK(munmap((void *)p,4096)==0);
    /* More than the guest RAM: failure to free frames cannot pass this loop. */
    for (int i=0;i<768;i++) {
        void *q=mmap(NULL,1024*1024,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        CHECK(q!=MAP_FAILED); CHECK(munmap(q,1024*1024)==0);
    }
    puts("MEMORY_CHECK_PASS");
    struct termios saved,t;
    CHECK(tcgetattr(0,&saved)==0); t=saved;
    t.c_lflag=ICANON|ISIG|ECHO|ECHOE;
    t.c_iflag=ICRNL;
    CHECK(tcsetattr(0,TCSANOW,&t)==0);
    puts("TTY_CANON_READY: type a, Shift+B, Backspace, c, Enter");
    char buf[16]={0};
    ssize_t n=read(0,buf,sizeof(buf));
    CHECK(n==3 && memcmp(buf,"ac\n",3)==0);
    puts("TTY_CANON_PASS");
    t.c_lflag=0; t.c_iflag=0; t.c_cc[VMIN]=1; t.c_cc[VTIME]=0;
    CHECK(tcsetattr(0,TCSANOW,&t)==0);
    puts("TTY_RAW_READY: press Ctrl+C, then Up");
    size_t got=0;
    while(got<4) { n=read(0,buf+got,4-got); CHECK(n>0); got+=n; }
    CHECK(tcsetattr(0,TCSANOW,&saved)==0);
    CHECK(memcmp(buf,"\003\033[A",4)==0);
    puts("CORECHECK_PASS");
    return 0;
}
