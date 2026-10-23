#pragma once
struct Controller {bool online=true,hold=false;int x=0;bool isConnected(){return online;}bool isGamepad(){return true;}bool hasData(){return true;}bool r1(){return hold;}int axisX(){return x;}};
using ControllerPtr=Controller*;
struct BP32Mock {bool reports=true;void setup(void(*)(ControllerPtr),void(*)(ControllerPtr)){}void enableVirtualDevice(bool){}bool update(){return reports;}};
extern BP32Mock BP32;
