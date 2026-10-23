#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <string>
using gpio_num_t=int;
constexpr float PI=3.14159265358979323846f;
constexpr int GPIO_NUM_32=32,GPIO_NUM_33=33;
extern uint32_t fakeTime, interruptAt;
inline uint32_t millis(){return fakeTime;}
inline void delay(int t){fakeTime+=t;}
#define pdMS_TO_TICKS(x) (x)
struct SerialMock {
 std::string output;
 void begin(int){}
 int available(){return interruptAt && fakeTime>=interruptAt;}
 char read(){interruptAt=0;return 'x';}
 void println(const char* s){output+=s;output+='\n';}
 void printf(const char* f,...){char b[512];va_list a;va_start(a,f);vsnprintf(b,sizeof(b),f,a);va_end(a);output+=b;}
};
extern SerialMock Serial;
