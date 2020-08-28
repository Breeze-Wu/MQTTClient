#ifndef _MQTT_TIME_H_
#define _MQTT_TIME_H_

#include "time.h"

//typedef struct timeval time_val;

typedef struct timval{
	unsigned long tv_sec;
	unsigned long tv_usec;
	unsigned long t_end;
} time_val;

extern void MQTTTime_now(time_val *start);
extern unsigned long MQTTTime_elapsed(time_val start);

extern unsigned long MQTTTime_left(time_val start_time,unsigned long timeout);

extern void MQTTTime_init(time_val *start_time,unsigned long time_out);


#endif
