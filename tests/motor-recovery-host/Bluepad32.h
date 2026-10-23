#pragma once
struct Controller {bool online=true,hold=false,gamepad=true,cycleButton=false,fastButton=false;int axis=0;bool isConnected(){return online;}bool isGamepad(){return gamepad;}bool hasData(){return true;}bool r1(){return hold;}bool y(){return cycleButton;}bool x(){return fastButton;}int axisX(){return axis;}};
using ControllerPtr=Controller*;
struct BP32Mock {bool reports=true;uint32_t disconnectAt=0;ControllerPtr disconnectTarget=nullptr;void setup(void(*)(ControllerPtr),void(*)(ControllerPtr)){}void enableVirtualDevice(bool){}void enableNewBluetoothConnections(bool){}bool update(){if(disconnectAt&&fakeTime>=disconnectAt){disconnectTarget->online=false;disconnectAt=0;}return reports;}};
extern BP32Mock BP32;
