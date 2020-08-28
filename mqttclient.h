#ifndef _MQTTCLIENT_H_
#define _MQTTCLIENT_H_



enum returncode{
OVERFLOW = -2,
ERROR =-1,
SUCCESS = 0,
};

typedef struct{
int my_socket;
char *ip;
int port;
char ssl;
int (*read)(int,unsigned char *,int,int);
int (*write)(int,unsigned char *,int,int);
int (*disconn)(int);
} iot_network;

typedef struct{
char *cstr;
int lenght;
} mqttstring;

typedef union{
unsigned char byte;
struct{
		unsigned char retain : 1;
		unsigned char Qos : 2;
		unsigned char dup : 1;
		unsigned char type : 4;
		}bits;
} head;

typedef struct{
head Mqtt_Head;

char struct_id[4];
unsigned int Mqtt_Version;
union{
	unsigned char byte;
	struct{
		unsigned char reserve : 1;				//low to tall
		unsigned char Clean_Session : 1;
		unsigned char Will_Flage : 1;
		unsigned char Will_Qos : 2;
		unsigned char Will_Retain : 1;
		unsigned char Pass_Flage : 1;
		unsigned char Name_Flage : 1;
		}bits;
	}conn_flage;
int Keep_Alive;

mqttstring client_id;
mqttstring will_topic;
mqttstring will_message;
mqttstring user_name;
mqttstring passworld;
} iot_mqttconn_parament;

typedef struct{
	union{
			char byte;
			struct{
					char Sessionpresent : 1;
					char reserved : 7;
				}bits;
		}first_byte;
	char recode;
} iot_mqttconn_ack;
#define MQTT_CONNECT_Initializer {{0},{'M','Q','T','T'},4,{0},60,{"2019072028",10},{NULL,0},{NULL,0},{NULL,0},{NULL,0}}

typedef struct{
iot_network PNetwork;
short client_state;
unsigned char *read_buf;
int read_size;
unsigned char *write_buf;
int write_size;
unsigned int timeout;
} iot_client_parament;

enum mqtt_type{
CONNECT = 1,CONNACK,PUBLISH,PUBACK,PUBREC,PUBREL,PUBCOMP,SUBSCRIBE,
SUBACK,UNSUBSCRIBE,UNSUBACK,PINGREQ,PINGRESP,DISCONNECT,OVER
};	//packet type



typedef struct{
unsigned char dup;
unsigned char qos;
unsigned char retain;
char *topic;
unsigned short topic_len;
char *message;
unsigned int mesg_len;
unsigned short mesg_id;
} iot_mqtt_parament;

#include "pthread.h"

typedef pthread_mutex_t* mutex_type;

extern iot_client_parament* MQTTClient_Creat();
extern int MQTT_CONNECT(iot_client_parament *Client,iot_mqttconn_parament option);
extern int MQTT_DISCONN(iot_client_parament *Client);
extern int MQTT_PUBLISH(iot_client_parament *Client,iot_mqtt_parament option);

int MQTTSerialise_ack(unsigned char *buf,int size,char packettype,int packetid);
int MQTTDeSerialise_ack(unsigned char *buff,unsigned short *packetid);
int MQTTDeSerialise_Publish(unsigned char *buff,iot_mqtt_parament *mesg);


#endif
