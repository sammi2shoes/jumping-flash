#pragma once
#include "Arduino.h"
constexpr int TFT_WHITE=1,TFT_BLACK=0;
struct Button {bool pressed=false,clicked=false;bool wasPressed(){bool c=clicked;clicked=false;return c;}bool isPressed(){return pressed;}};
struct Screen {void setRotation(int){} void setTextSize(int){} void setTextColor(int,int){} void fillScreen(int){} void setCursor(int,int){}void println(const char*){} template<class... T> void printf(const char*,T...){assert(!fastCritical);} };
struct M5Mock {Screen Display;Button BtnA,BtnB;int config(){return 0;}void begin(int){}void update(){assert(!fastCritical);} };
extern M5Mock M5;
