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
 assert(live_link_probe(fd)==-EACCES);
 probe_close(fd);int reopened=open(path,O_RDONLY|O_NOCTTY|O_NONBLOCK);assert(reopened>=0);close(reopened);
 fd=probe_open(path);assert(fd>=0);close(master);assert(probe_read(fd,data,sizeof(data))<0);probe_close(fd);
 assert(probe_open("/not/a/real/port")==-ENOENT);
 // Simulate a cold-start terminal: wrong baud, canonical input, echo and software flow control.
 master=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK);assert(master>=0);assert(!grantpt(master));assert(!unlockpt(master));path=ptsname(master);
 setup=open(path,O_RDWR|O_NOCTTY);assert(setup>=0);assert(!tcgetattr(setup,&initial));
 initial.c_lflag|=ICANON|ECHO|ISIG;initial.c_iflag|=ICRNL|IXON|IXOFF;initial.c_oflag|=OPOST|ONLCR;
 assert(!cfsetispeed(&initial,B9600));assert(!cfsetospeed(&initial,B9600));assert(!tcsetattr(setup,TCSANOW,&initial));close(setup);
 assert(write(master,"stale\n",6)==6);
 fd=live_open(path);assert(fd>=0);assert((fcntl(fd,F_GETFL)&O_ACCMODE)==O_RDWR);
 assert(!tcgetattr(fd,&after));assert(live_matches(&after));assert(cfgetispeed(&after)==B921600);assert(cfgetospeed(&after)==B921600);
 assert(probe_read(fd,data,sizeof(data))==0); // Old buffered input was discarded.
 // Only the fixed discovery payload is exposed by the live JNI API.
 unsigned char discard[64];while(read(master,discard,sizeof discard)>0){}
 assert(live_link_probe(fd)==7);unsigned char probe[8];assert(read(master,probe,sizeof probe)==7);
 const unsigned char expected[]={0xa6,0x07,0x2b,0x04,0xff,0x01,0xf4};assert(!memcmp(probe,expected,7));
 errno=0;assert(open(path,O_RDONLY|O_NOCTTY|O_NONBLOCK)==-1&&errno==EBUSY);
 unsigned char binary[]={0,0xa6,0x13,0x11,0x0d,0x0a,0x80,0xff};assert(write(master,binary,sizeof binary)==sizeof binary);
 assert(probe_read(fd,data,sizeof data)==sizeof binary);assert(!memcmp(data,binary,sizeof binary));
 probe_close(fd);
 // Every open verifies its own settings, including after another owner changes them back.
 setup=open(path,O_RDWR|O_NOCTTY);assert(setup>=0);assert(!tcsetattr(setup,TCSANOW,&initial));close(setup);
 fd=live_open(path);assert(fd>=0);assert(!tcgetattr(fd,&after));assert(live_matches(&after));probe_close(fd);close(master);
 assert(live_open("/dev/null")<0);assert(live_open("/not/a/real/port")==-ENOENT);
 puts("PASS: live serial independently initializes/verifies raw921600/8N1 from canonical9600, flushes stale input, preserves binary bytes, uses exclusive live I/O and emits only the fixed discovery frame through its probe function; passive I/O remains read-only and reinitializes after settings change. Passive path preserves termios. Linux pseudo-TTY reader is read-only, exclusive to subsequent ordinary opens, preserves termios, reads exact bytes, times out, releases ownership, and reports hangup. Not AX12 hardware validation.");
}
