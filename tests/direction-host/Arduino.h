#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <cstdlib>
#include <cmath>
#define PI 3.14159265358979323846f
using gpio_num_t=int;
constexpr int GPIO_NUM_32=32,GPIO_NUM_33=33;
extern uint32_t fakeTime,interruptAt;
void advance(int);
inline uint32_t millis(){return fakeTime;}
inline void delay(int t){advance(t);}
#define pdMS_TO_TICKS(x) (x)
struct SerialMock{
 std::string output;
 void begin(int){} int available(){return interruptAt&&fakeTime>=interruptAt;}
 char read(){interruptAt=0;return 'x';}
 void println(const char*s){output+=s;output+='\n';}
 void printf(const char*fmt,...){char b[1024];va_list a;va_start(a,fmt);vsnprintf(b,sizeof b,fmt,a);va_end(a);output+=b;}
};extern SerialMock Serial;
