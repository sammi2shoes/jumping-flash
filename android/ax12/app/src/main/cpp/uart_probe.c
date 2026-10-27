// Read-only UART byte I/O. Passive capture preserves settings; live input sets/validates raw921600/8N1. No serial writes, shell, root, modem-line or RF commands.
#ifndef JF_UART_CORE_TEST
#include <jni.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <termios.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static int probe_open(const char *name) {
    int fd=open(name,O_RDONLY|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
    if(fd<0)return -errno;
    struct stat info;
    if(fstat(fd,&info)<0){int error=errno;close(fd);return -error;}
    if(!S_ISCHR(info.st_mode)){close(fd);return -ENOTTY;}
    if(ioctl(fd,TIOCEXCL)<0){int error=errno;close(fd);return -error;}
    return fd;
}
// Live configuration is deliberately separate from the passive probe path.
static int live_matches(const struct termios *t) {
    tcflag_t input_forbidden=ICRNL|INLCR|IGNCR|ISTRIP|IXON|IXOFF|IXANY|PARMRK;
    tcflag_t control_forbidden=PARENB|PARODD|CSTOPB|CRTSCTS;
    return cfgetispeed(t)==B921600 && cfgetospeed(t)==B921600 &&
        (t->c_cflag&CSIZE)==CS8 && (t->c_cflag&(CLOCAL|CREAD))==(CLOCAL|CREAD) &&
        !(t->c_cflag&control_forbidden) && !(t->c_iflag&input_forbidden) &&
        !(t->c_lflag&(ICANON|ECHO|ECHONL|ISIG|IEXTEN)) && !(t->c_oflag&OPOST) &&
        t->c_cc[VMIN]==0 && t->c_cc[VTIME]==0;
}
static int live_open(const char *name) {
    int fd=probe_open(name);
    if(fd<0)return fd;
    struct termios t,verified;
    int error=0;
    if(tcgetattr(fd,&t)<0){error=errno;goto failed;}
    cfmakeraw(&t);
    t.c_cflag &= ~(CSIZE|PARENB|PARODD|CSTOPB|CRTSCTS);
    t.c_cflag |= CS8|CLOCAL|CREAD;
    t.c_iflag &= ~(IXON|IXOFF|IXANY);
    t.c_cc[VMIN]=0;t.c_cc[VTIME]=0;
    if(cfsetispeed(&t,B921600)<0 || cfsetospeed(&t,B921600)<0){error=errno;goto failed;}
    if(tcsetattr(fd,TCSANOW,&t)<0){error=errno;goto failed;}
    if(tcgetattr(fd,&verified)<0){error=errno;goto failed;}
    if(!live_matches(&verified)){error=EPROTO;goto failed;}
    // Never treat reports buffered before this reader started as fresh controls.
    if(tcflush(fd,TCIFLUSH)<0){error=errno;goto failed;}
    return fd;
failed:
    ioctl(fd,TIOCNXCL);close(fd);return -error;
}
static int probe_info(int fd,char *buffer,size_t length) {
    struct termios t;
    if(tcgetattr(fd,&t)<0)return -errno;
    speed_t speed=cfgetispeed(&t);unsigned long baud=0;
    switch(speed){
        case B9600:baud=9600;break;case B19200:baud=19200;break;case B38400:baud=38400;break;
        case B57600:baud=57600;break;case B115200:baud=115200;break;case B230400:baud=230400;break;
        case B460800:baud=460800;break;case B500000:baud=500000;break;case B576000:baud=576000;break;
        case B921600:baud=921600;break;case B1000000:baud=1000000;break;case B1500000:baud=1500000;break;
        case B2000000:baud=2000000;break;default:break;
    }
    snprintf(buffer,length,"termios baud=%lu ispeed_code=%lu ospeed_code=%lu cflag=0x%lx iflag=0x%lx lflag=0x%lx oflag=0x%lx (query only)",baud,(unsigned long)speed,(unsigned long)cfgetospeed(&t),(unsigned long)t.c_cflag,(unsigned long)t.c_iflag,(unsigned long)t.c_lflag,(unsigned long)t.c_oflag);
    return 0;
}
static int probe_read(int fd,unsigned char *buffer,int size) {
    if(fd<0||!buffer||size<1||size>4096)return -EINVAL;
    struct pollfd p={.fd=fd,.events=POLLIN};
    int ready=poll(&p,1,100);
    if(ready<0)return errno==EINTR?0:-errno;
    if(!ready)return 0;
    if(p.revents&(POLLERR|POLLHUP|POLLNVAL))return -EIO;
    if(!(p.revents&POLLIN))return 0;
    ssize_t n=read(fd,buffer,(size_t)size);
    if(n<0)return errno==EAGAIN||errno==EINTR?0:-errno;
    if(n==0)return -EPIPE;
    return (int)n;
}
static void probe_close(int fd) {
    if(fd>=0){ioctl(fd,TIOCNXCL);close(fd);}
}
#ifndef JF_UART_CORE_TEST
JNIEXPORT jint JNICALL Java_com_jumpingflash_ax12_NativePort_openExclusive(JNIEnv *env,jclass cls,jstring path) {
    (void)cls;
    if(!path)return -EINVAL;
    const char *name=(*env)->GetStringUTFChars(env,path,0);
    if(!name)return -ENOMEM;
    int allowed=!strcmp(name,"/dev/ttyS0")||!strcmp(name,"/dev/ttyS4");
    int result=allowed?probe_open(name):-EINVAL;
    (*env)->ReleaseStringUTFChars(env,path,name);
    return result;
}
JNIEXPORT jint JNICALL Java_com_jumpingflash_ax12_NativePort_openLive(JNIEnv *env,jclass cls) {
    (void)env;(void)cls;
    return live_open("/dev/ttyS0");
}
JNIEXPORT jstring JNICALL Java_com_jumpingflash_ax12_NativePort_describeOpen(JNIEnv *env,jclass cls,jint fd) {
    (void)cls;char text[300];int result=probe_info(fd,text,sizeof(text));
    if(result<0)snprintf(text,sizeof(text),"termios query failed errno=%d",-result);
    return (*env)->NewStringUTF(env,text);
}
JNIEXPORT jint JNICALL Java_com_jumpingflash_ax12_NativePort_readBytes(JNIEnv *env,jclass cls,jint fd,jbyteArray data) {
    (void)cls;
    if(!data)return -EINVAL;
    jsize size=(*env)->GetArrayLength(env,data);
    if(size<1||size>4096)return -EINVAL;
    unsigned char buffer[4096];
    int n=probe_read(fd,buffer,(int)size);
    if(n>0)(*env)->SetByteArrayRegion(env,data,0,n,(jbyte *)buffer);
    return n;
}
JNIEXPORT void JNICALL Java_com_jumpingflash_ax12_NativePort_closePort(JNIEnv *env,jclass cls,jint fd) {
    (void)env;(void)cls;
    // Only the owning reader thread closes its fd, after leaving poll/read.
    probe_close(fd);
}
#endif
