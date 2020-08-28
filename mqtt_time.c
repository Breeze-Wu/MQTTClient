#include "mqtt_time.h"
#include "time.h"


void MQTTTime_now(time_val *start_time)
{
	static struct timespec start_ts;

	clock_gettime(CLOCK_MONOTONIC, &start_ts);  //counting time form the system  boot
	start_time->tv_sec = start_ts.tv_sec;
	start_time->tv_usec = start_ts.tv_nsec / 1000;
}

unsigned long MQTTTime_elapsed(time_val start_time)
{
	time_val now;
	static struct timespec now_ts;

	clock_gettime(CLOCK_MONOTONIC, &now_ts);
	now.tv_sec = now_ts.tv_sec;
	now.tv_usec = now_ts.tv_nsec / 1000;

	return ((now.tv_sec-start_time.tv_sec)*1000 + (now.tv_usec-start_time.tv_usec)/1000);	//elapse time will be retrun unit /ms
}

unsigned long MQTTTime_left(time_val start_time,unsigned long timeout)
{
	unsigned long left_time=0;

	if((left_time = (timeout - MQTTTime_elapsed(start_time))) < 0)
		return 0;
	else
		return left_time;
}

void MQTTTime_init(time_val *start_time,unsigned long time_out)
{
	MQTTTime_now(start_time);
	start_time->t_end = time_out;
}