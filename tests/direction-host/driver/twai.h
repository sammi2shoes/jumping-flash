#pragma once
#include "Arduino.h"
using esp_err_t=int;
constexpr int TWAI_STATE_STOPPED=0,TWAI_STATE_BUS_OFF=2,TWAI_STATE_RECOVERING=3;
extern int simulatedBusState;
inline int twai_initiate_recovery(){simulatedBusState=TWAI_STATE_STOPPED;return 0;}
constexpr int ESP_OK=0,TWAI_MODE_NORMAL=0,TWAI_STATE_RUNNING=1;
inline const char* esp_err_to_name(int){return "mock";}
struct twai_message_t {uint32_t identifier=0;unsigned extd=0,rtr=0,ss=0;uint8_t data_length_code=0,data[8]={};};
struct twai_general_config_t {int rx_queue_len=0,tx_queue_len=0;};
struct twai_timing_config_t {};
struct twai_filter_config_t {};
struct twai_status_info_t {int state=1; uint32_t msgs_to_tx=0,tx_error_counter=0,rx_error_counter=0,tx_failed_count=0,rx_missed_count=0;};
#define TWAI_GENERAL_CONFIG_DEFAULT(a,b,c) {}
#define TWAI_TIMING_CONFIG_1MBITS() {}
#define TWAI_FILTER_CONFIG_ACCEPT_ALL() {}
int twai_transmit(const twai_message_t*,int);
int twai_receive(twai_message_t*,int);
inline int twai_driver_install(void*,void*,void*){return 0;}
inline int twai_start(){simulatedBusState=TWAI_STATE_RUNNING;return 0;}
int twai_get_status_info(twai_status_info_t*);
