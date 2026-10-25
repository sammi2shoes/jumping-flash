#define _GNU_SOURCE
#define JF_UART_CORE_TEST
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include "../../android/ax12/app/src/main/cpp/uart_probe.c"
int main(void){
 int master=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK);assert(master>=0);assert(!grantpt(master));assert(!unlockpt(master));char *path=ptsname(master);assert(path);
 int setup=open(path,O_RDWR|O_NOCTTY);assert(setup>=0);struct termios initial;assert(!tcgetattr(setup,&initial));cfmakeraw(&initial);assert(!tcsetattr(setup,TCSANOW,&initial));close(setup);
 int fd=probe_open(path);assert(fd>=0);assert((fcntl(fd,F_GETFL)&O_ACCMODE)==O_RDONLY);
 struct termios after;assert(!tcgetattr(fd,&after));assert(initial.c_iflag==after.c_iflag&&initial.c_oflag==after.c_oflag&&initial.c_cflag==after.c_cflag&&initial.c_lflag==after.c_lflag);
 char info[300];assert(!probe_info(fd,info,sizeof(info)));assert(strstr(info,"query only"));
 errno=0;assert(write(fd,"x",1)==-1&&errno==EBADF);
 errno=0;int competing=open(path,O_RDONLY|O_NOCTTY|O_NONBLOCK);assert(competing==-1&&errno==EBUSY);
 unsigned char sample[]={0xa6,0,0x7f,0xff},data[16];assert(write(master,sample,sizeof(sample))==(int)sizeof(sample));assert(probe_read(fd,data,sizeof(data))==4);assert(!memcmp(data,sample,4));assert(probe_read(fd,data,sizeof(data))==0);
 probe_close(fd);int reopened=open(path,O_RDONLY|O_NOCTTY|O_NONBLOCK);assert(reopened>=0);close(reopened);
 fd=probe_open(path);assert(fd>=0);close(master);assert(probe_read(fd,data,sizeof(data))<0);probe_close(fd);
 assert(probe_open("/not/a/real/port")==-ENOENT);
 puts("PASS: Linux pseudo-TTY reader is read-only, exclusive to subsequent ordinary opens, preserves termios, reads exact bytes, times out, releases ownership, and reports hangup. Not AX12 hardware validation.");
}
