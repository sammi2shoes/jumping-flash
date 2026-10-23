#pragma once
#include "Arduino.h"
constexpr int TFT_WHITE=1,TFT_BLACK=0;
struct Button {bool wasPressed(){return false;}};
struct Screen {void setRotation(int){} void setTextSize(int){} void setTextColor(int,int){} void fillScreen(int){} void setCursor(int,int){}void println(const char*){} template<class... T> void printf(const char*,T...){} };
struct M5Mock {Screen Display;Button BtnA;int config(){return 0;}void begin(int){}void update(){} };
extern M5Mock M5;
