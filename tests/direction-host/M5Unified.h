#pragma once
#include "Arduino.h"
struct Button{bool down=false;bool clicked=false;bool isPressed(){return down;}bool wasPressed(){bool r=clicked;clicked=false;return r;}};
struct Screen{void setRotation(int){}void setTextSize(int){}void setCursor(int,int){}template<class...A>void printf(const char*,A...){} };
struct M5Mock{Button BtnA,BtnB;Screen Display;int config(){return 0;}void begin(int){}void update(){} };extern M5Mock M5;
