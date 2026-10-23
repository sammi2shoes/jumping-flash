#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <string>
using gpio_num_t=int;
constexpr int GPIO_NUM_32=32,GPIO_NUM_33=33;
extern uint32_t fakeTime;
inline uint32_t millis(){return fakeTime;}
inline void delay(int){}
struct SerialMock {
 std::string output;
 void begin(int){}
 void println(const char* s){output+=s;output+='\n';}
 void printf(const char* f,...){char b[512];va_list a;va_start(a,f);vsnprintf(b,sizeof(b),f,a);va_end(a);output+=b;}
};
extern SerialMock Serial;
