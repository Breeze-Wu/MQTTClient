#include "mqttclient.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

#include "sys/socket.h" //include socket parament socket/connect func
#include "sys/types.h" //include socket struct
#include "netdb.h" //include nework func 
#include "unistd.h" //include read/write func
#include "netinet/in.h"
#include "arpa/inet.h" //include addr convert func
#include "errno.h"  
#include "netinet/tcp.h"  //TCP_NODELAY

#include "mqtt_time.h"


#define address "183.239.240.40"
#define port_pc 12026
#define MaxSize (1024*2+1)
#define topicmax 129
#define messagemax 1024+256
#define MQTT_TIMEOUT 10000L


static unsigned short packet_id = 0;
char topic_filter[100][129];


static pthread_mutex_t mqttclient_mutex_store = PTHREAD_MUTEX_INITIALIZER;
static mutex_type mqttclient_mutex = &mqttclient_mutex_store;

static pthread_mutex_t socket_mutex_store = PTHREAD_MUTEX_INITIALIZER;
static mutex_type socket_mutex = &socket_mutex_store;

static pthread_mutex_t subscribe_mutex_store = PTHREAD_MUTEX_INITIALIZER;
static mutex_type subscribe_mutex = &subscribe_mutex_store;

static pthread_mutex_t unsubscribe_mutex_store = PTHREAD_MUTEX_INITIALIZER;
static mutex_type unsubscribe_mutex = &unsubscribe_mutex_store;

static pthread_mutex_t connect_mutex_store = PTHREAD_MUTEX_INITIALIZER;
static mutex_type connect_mutex = &connect_mutex_store;

mutex_type Thread_create_mutex(int* rc)
{
	mutex_type mutex = NULL;

	*rc = -1;
	#if defined(_WIN32) || defined(_WIN64)
		mutex = CreateMutex(NULL, 0, NULL);
		if (mutex == NULL)
			*rc = GetLastError();
	#else
		mutex = malloc(sizeof(pthread_mutex_t));
		if (mutex)
			*rc = pthread_mutex_init(mutex, NULL);
	#endif
	return mutex;
}


/**
 * Lock a mutex which has alrea
 * @return completion code, 0 is success
 */
int Thread_lock_mutex(mutex_type mutex)
{
	int rc = -1;

	/* don't add entry/exit trace points as the stack log uses mutexes - recursion beckons */
	#if defined(_WIN32) || defined(_WIN64)
		/* WaitForSingleObject returns WAIT_OBJECT_0 (0), on success */
		rc = WaitForSingleObject(mutex, INFINITE);
	#else
		rc = pthread_mutex_lock(mutex);
	#endif

	return rc;
}


/**
 * Unlock a mutex which has already been locked
 * @param mutex the mutex
 * @return completion code, 0 is success
 */
int Thread_unlock_mutex(mutex_type mutex)
{
	int rc = -1;

	/* don't add entry/exit trace points as the stack log uses mutexes - recursion beckons */
	#if defined(_WIN32) || defined(_WIN64)
		/* if ReleaseMutex fails, the return value is 0 */
		if (ReleaseMutex(mutex) == 0)
			rc = GetLastError();
		else
			rc = 0;
	#else
		rc = pthread_mutex_unlock(mutex);
	#endif

	return rc;
}


/**
 * Destroy a mutex which has already been created
 * @param mutex the mutex
 */
int Thread_destroy_mutex(mutex_type mutex)
{
	int rc = 0;

	#if defined(_WIN32) || defined(_WIN64)
		rc = CloseHandle(mutex);
	#else
		rc = pthread_mutex_destroy(mutex);
		free(mutex);
	#endif

	return rc;
}



int Network_Connect(int *fd,char *hostname,int port)
{

	struct sockaddr_in addr;
	struct hostent *host = NULL;
	char ipaddr[20];
	char opt = 1;
	struct linger m_linger;
	int socketfd;
	int rc;

	if(hostname == NULL || port == 0)
	{
		printf("network parament error\r\n");
		return ERROR;
	}

	host = gethostbyname(hostname);  //resolve domain
	if(host == NULL)
	{
		printf("resolve domain fail\r\n");
		return ERROR;
	}
	else
	{
		strcpy(ipaddr,inet_ntoa(*(struct in_addr *)host->h_addr));
		printf("first addr is %s\r\n",ipaddr);
	}

	socketfd = socket(AF_INET,SOCK_STREAM,0);

	if(socketfd < 0)
	{
		printf("socketfd error fd is %d\r\n",socketfd);
		return ERROR;
	}
	else
		*fd = socketfd;

	if((rc = setsockopt(socketfd,IPPROTO_TCP,TCP_NODELAY,(void *)&opt,sizeof(int))) == -1)	//disable nagle algorithm
	{
		printf("setsocket TCP_NODELAY err errno is %d\r\n",errno);
	//	return -1;
	}

	m_linger.l_onoff = 1;
	m_linger.l_linger = 0;
	if(setsockopt(socketfd,SOL_SOCKET,SO_LINGER,(void*)&m_linger,sizeof(m_linger)) == -1)	//close TIME_WAIT
	{
		printf("setsocket linger err errno is %d\r\n",errno);
	//	return -1;
	}

	memset(&addr,0,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = ((struct in_addr *)host->h_addr)->s_addr;
	#if 0
	if(inet_pton(AF_INET,ipaddr,&addr.sin_addr) == -1)
	{
		printf("transform error\r\n");
		return -1;
	}
	#endif
	while(connect(socketfd,(struct sockaddr *)&addr,sizeof(addr)) == -1)
	{
		printf("connect socket fail errno is %d\r\n",errno);
		//return -1;
	}
	return 0;
}

int HAL_Linux_read(int fd,unsigned char *buf,int len,unsigned long timeout_ms)
{
	int rc;
	unsigned long t_end;
	int len_read = 0;
	time_val start_time;
	char l_error;
	
	//MQTTTime_now(&start_time);
	MQTTTime_init(&start_time,timeout_ms);

	if(timeout_ms == 0)
		timeout_ms += 10000;
	if(!fd || !buf)
	{
		printf("fd or buf is null\r\n");
		return ERROR;
	}
	do{
		if((t_end = MQTTTime_left(start_time,start_time.t_end)) == 0)
			break;
		rc = read(fd,buf+len_read,len-len_read);
		l_error = errno;
		printf("read byte is:%d,l_errno:%d\r\n",rc,l_error);

		if(rc > 0)
			len_read += rc;
		else if(rc == 0 || 
			((rc < 0)&&(l_error == EINTR || l_error == EWOULDBLOCK || l_error == EAGAIN)))
			continue;
		else
		{
			printf("read err:%d\r\n",errno);
			return ERROR;
		}
	}while (len_read < len);

	printf("read byte is:%d,errno:%d\r\n",len_read,l_error);
	return len_read;
}
int HAL_Linux_write(int fd,unsigned char *buf,int len,unsigned long timeout_ms)
{
	int len_send = 0,rc ;
	unsigned long t_end;
	time_val start_time;
	char l_error;
	
	//MQTTTime_now(&start_time);
	MQTTTime_init(&start_time,timeout_ms);

	if(timeout_ms == 0)
		timeout_ms += 10000;
	if(!fd || !buf)
	{
		printf("fd or buf is null\r\n");
		return ERROR;
	}
	do{
		if((t_end = MQTTTime_left(start_time,start_time.t_end)) == 0)
		{
			printf("write timeout\r\n");
			break;
		}
		rc = write(fd,buf+len_send,len-len_send);
		l_error = errno;
		printf("send byte is:%d,l_errno:%d\r\n",rc,l_error);

		if(rc > 0)
			len_send += rc;
		else if(l_error == 4)
			continue;
		else
		{
			printf("send err:%d\r\n",rc);
			return ERROR;
		}
	}while(len_send < len);

	printf("send byte is:%d,errno:%d\r\n",len_send,l_error);
	return len_send;
}
int HAL_Linux_disconn(int fd)
{
	int rc;
	if(!fd)
	{
		printf("fd is null\r\n");
		return ERROR;
	}

	rc = close(fd);
	printf("close tcp rc:%d\r\n",rc);

	return 0;
}

int Network_Init(iot_network *Network)
{
	int result;

	if(Network == NULL)
	{
		printf("network is null\r\n");
		return -1;
	}
	Network->ip = address;
	Network->port = port_pc;
	Network->ssl = 0;
	Network->read = HAL_Linux_read;
	Network->write = HAL_Linux_write;
	Network->disconn = HAL_Linux_disconn;

	if((result = Network_Connect(&Network->my_socket,Network->ip,Network->port)) == -1)
	{
		printf("network connection fail\r\n");
		return -1;
	}

	printf("network connection success\r\n");
	return 0;
}

void Destory_network(iot_client_parament *Client)
{
	int ret;
	
	ret=Client->PNetwork.disconn(Client->PNetwork.my_socket);
	printf("destory_network status:%d\r\n",ret);
}

void Destory_client(iot_client_parament *Client)
{
	free(Client->read_buf);
	Client->read_buf = NULL;
	
	free(Client->write_buf);
	Client->read_buf = NULL;

	free(Client);
	Client = NULL;
}

int Send_packet(iot_client_parament *Client,int len,time_val start_time)
{
	int send_len = 0,rc = 0;

	if(!Client)
		return ERROR;
	printf("send packet len :%d,remain time :%ld\r\n",len,MQTTTime_left(start_time,start_time.t_end));
	while(send_len < len)
	{
		if(MQTTTime_elapsed(start_time) >= start_time.t_end)
		{
			printf("send conn packettimeout\r\n");
			return ERROR;
		}
		if((rc = Client->PNetwork.write(Client->PNetwork.my_socket, 
			Client->write_buf, len, MQTTTime_left(start_time,start_time.t_end))) < 0)
		{
			break;
		}
		send_len += rc;
	}

	if(send_len == len)
		return 0;
	else
	{
		printf("send_packet error :%d\r\n",send_len);
		return ERROR;
	}
}

int MQTTRemain_len(iot_mqttconn_parament *option)
{
	int len = 0;
	if(option == NULL)
	{
		printf("option is null\r\n");
		return ERROR;
	}
	if(option->Mqtt_Version == 4)  //mqtt version 3.1.1
		len += 10;
	len += strlen(option->client_id.cstr)+2;
	if(option->conn_flage.bits.Will_Flage == 1)
		len += strlen(option->will_topic.cstr)+2+strlen(option->will_message.cstr)+2;
	if(option->conn_flage.bits.Name_Flage == 1)
		len += strlen(option->user_name.cstr)+2;
	if(option->conn_flage.bits.Pass_Flage == 1)
		len += strlen(option->passworld.cstr)+2;

	return len;
}
int MQTTPacket_len(int remain_len)
{
	int packet_len = remain_len+1; //head byte

	if(remain_len < 128)
		packet_len += 1;
	else if(remain_len < 16384)
		packet_len += 2;
	else if(remain_len < 2097152)
		packet_len += 3;
	else
		packet_len += 4;

	return packet_len;
}

int MQTTPacket_encode(unsigned char *str,int remain_len)
{
	int rc=0;
	do
	{
		unsigned char d = remain_len%128;
		remain_len /= 128;
		if(remain_len > 0)
			d |= 0x80;
		str[rc++] = d;
	}
	while (remain_len > 0);

	return rc;
}
int MQTTPacket_decode(unsigned char *str,unsigned int *lenght)
{
	int len=0,multiplier=1;
	unsigned char i=0;
	do
	{
		if(++len > 4)
			goto exit_func;
		
		i = *str++;
			
		*lenght += (i&127 * multiplier);
		multiplier *= 128;
	}
	while(i&128);
	
exit_func:	
	return len;

}

int decodePacket(iot_client_parament *Client,int *lenght,time_val start_time)
{
	int len=0,multiplier=1;
	unsigned char i=0;
	do
	{
		if(++len > 4)
			goto exit_func;
		if(Client->PNetwork.read(Client->PNetwork.my_socket,
			&i,1,MQTTTime_left(start_time,start_time.t_end)) != 1)
			return 0;
		
		*lenght += (i&127 * multiplier);
		multiplier *= 128;
	}
	while (i&128);

exit_func:
	return len;
}

void writerchar(unsigned char **str,char data)
{
	**str = data;
	(*str)++;
}
void writerint(unsigned char **str,int data)
{
	**str = (data) / 256;	//hight byte
	(*str)++;
	**str = (data) % 256;	//low byte
	(*str)++;
}

void writerstring(unsigned char **str,char *data)
{
	int len = strlen(data);

	if(len > 0)
	{
		writerint(str,len);
		memcpy(*str,data,len);
		*str += len;
	}
}
char readchar(unsigned char **str)
{
	char c = **str;
	(*str)++;
	return c;
}

unsigned short readint(unsigned char **str)
{
	unsigned short data = 0;
	//printf("**str:%d,*(*str + 1):%d\r\n",**str,*(*str + 1));
	data += (**str) * 256;
	(*str)++;
	data += **str;
	(*str)++;
	
	return data;
}


int readpacket(iot_client_parament *Client,time_val start_time)
{
	int read_len = 0,remain_len = 0;
	int type = 0,rc = 0;
	head header;

	//1.read head
	if((rc = Client->PNetwork.read(Client->PNetwork.my_socket,
			Client->read_buf,1,MQTTTime_left(start_time,start_time.t_end))) != 1)
			return rc;
	read_len += rc;
	//2.read remaining length
	if((rc = decodePacket(Client,&remain_len,start_time)) == ERROR)
		return ERROR;
	//read_len += rc;
	read_len += MQTTPacket_encode(Client->read_buf+1, remain_len);

	//3.read buf overflow
	if((read_len + remain_len) > Client->read_size)
	{
		printf("mqtt read buf is too short read _buf:%d,remain_len:%d",Client->read_size,remain_len);

		int needread = 	Client->read_size - read_len;
		if((rc = Client->PNetwork.read(Client->PNetwork.my_socket,
			Client->read_buf+read_len,needread,MQTTTime_left(start_time,Client->timeout))) != needread)
		{
			//memset((void *)Client->read_buf,0,Client->read_size);
			rc = OVERFLOW;
			return rc;
		}

		int left =  remain_len - needread;
		char *leftbuf = (char *)malloc(left);
		if(!leftbuf)
		{
			//memset((void *)Client->read_buf,0,Client->read_size);
			rc = OVERFLOW;
			return rc;
		}

		if((rc = Client->PNetwork.read(Client->PNetwork.my_socket,
			leftbuf,left,MQTTTime_left(start_time,start_time.t_end))) != left)
		{
			free(leftbuf);
			leftbuf = NULL;
			//memset((void *)Client->read_buf,0,Client->read_size);
			rc = OVERFLOW;
			return rc;
		}

		printf("read sucess\r\n");
		free(leftbuf);
		leftbuf = NULL;

		return OVERFLOW;
	}

	//4.read left
	if((rc = Client->PNetwork.read(Client->PNetwork.my_socket,
			Client->read_buf+read_len,remain_len,MQTTTime_left(start_time,start_time.t_end))) != remain_len)
	{
		//memset((void *)Client->read_buf,0,Client->read_size);
		return ERROR;
	}

	read_len += rc;
	header.byte = Client->read_buf[0];
	type = header.bits.type;
	printf("recv byte:%d,packet type:%d\r\n",read_len,type);
	return type;
}
int deliverymsg(iot_mqtt_parament *mesg)
{
	unsigned short i;
	int ret = SUCCESS;

	for(i=0;i<100;i++)
	{
		if(strlen(topic_filter[i]) == 0)
		{
			printf("topic is not sub\r\n");
			ret = ERROR;
			break;
		}
		if(strcmp(topic_filter[i],mesg->topic) == 0)	//未知具体比较函数代码 可能存在被计时攻击风险或比较费时等问题
		{
			char *print_msg = (char *)malloc(MaxSize);
			char topic[topicmax+1]={0};
			int offset = 0;
			int ret = 0;
			
			memset(print_msg,0x0,MaxSize);
			memcpy(topic,mesg->topic,mesg->topic_len);
			ret = snprintf(print_msg,MaxSize,"\r\n+MQTTSUB:%d,%s,%d,",mesg->mesg_id,topic,mesg->topic_len);

			if(ret < 0)
			{
				printf("message arrive err\r\n");
				free(print_msg);
				print_msg = NULL;
				return ERROR;
			}
			//offset = strlen(print_msg);
			memcpy(print_msg+ret,mesg->message,mesg->mesg_len);
			offset = strlen(print_msg);
			
			snprintf(print_msg+offset,3,"\r\n");

			printf("%s",print_msg);
			free(print_msg);
			print_msg = NULL;

			break;
		}
	}

	return ret;
}

int cycle(iot_client_parament *Client,time_val start_time)
{
	int packet_type = 0;
	char ret = 0;
	printf("left time :%ld\r\n",MQTTTime_left(start_time,start_time.t_end));

	//Thread_lock_mutex(mqttclient_mutex);
	packet_type = readpacket(Client,start_time);

	switch(packet_type)
	{
		default:
			printf("INVAILD TYPE\r\n");
			ret = ERROR;
			goto exit_func;
		case 0:
			break;
		case CONNACK:
		case PUBACK:
		case SUBACK:
		case PUBCOMP:
		case UNSUBACK: 
			break;
		case PUBLISH:
			{
				iot_mqtt_parament msg;
				unsigned int len;
				time_val sec_time;
				MQTTTime_init(&sec_time, Client->timeout);
				
				if((ret == MQTTDeSerialise_Publish(Client->read_buf,&msg)) == ERROR)
					goto exit_func;

				deliverymsg(&msg);

				if(msg.qos != 0)
				{
					if(msg.qos == 1)
						if((len = MQTTSerialise_ack(Client->write_buf,Client->write_size,
												 PUBACK,msg.mesg_id)) <= 0)
							{
								printf("MQTTSerialise_ack fail!!!\r\n");
								ret = ERROR;
								goto exit_func;
							}
					if(msg.qos == 2)
						if((len = MQTTSerialise_ack(Client->write_buf,Client->write_size,
												 PUBREC,msg.mesg_id)) <= 0)
							{
								printf("MQTTSerialise_ack fail!!!\r\n");
								ret = ERROR;
								goto exit_func;
							}
					if((ret = Send_packet(Client,len,sec_time)) == ERROR)
								goto exit_func;
				}
			}
			break;
		case PUBREC:
		case PUBREL:
			{
				unsigned short packedid;
				unsigned len;
				time_val thr_time;
				MQTTTime_init(&thr_time, Client->timeout);

				if((ret = MQTTDeSerialise_ack(Client->read_buf,&packedid)) == ERROR)
					goto exit_func;
				else if((len = MQTTSerialise_ack(Client->write_buf,Client->write_size,
										 (packet_type == PUBREC)?PUBREL:PUBCOMP,packedid)) <= 0)
					{
						printf("MQTTSerialise_ack fail!!!\r\n");
						ret = ERROR;
						goto exit_func;
					}
				else if((ret = Send_packet(Client,len,thr_time)) == ERROR)
						goto exit_func;
			}
			break;
		case PINGRESP:
			break;
	}
	
exit_func:
if(ret == ERROR)
	Destory_network(Client);
		
//Thread_unlock_mutex(mqttclient_mutex);
return packet_type;
}
int Wait_ack(iot_client_parament *Client,unsigned char head_type,time_val start_time)
{
	int type = ERROR;
	printf("waitfor enter type:%d\r\n",head_type);
	
	do
	{
		if(MQTTTime_left(start_time,Client->timeout) == 0)
		{
			printf("waiting timeout\r\n");
			break;
		}

		type = cycle(Client,start_time);
	}
	while (type != head_type && type >= 0);

	printf("waitfor end type:%d\r\n",type);
	return type;
}
int MQTTSerialise_zero(unsigned char *buf,int size,int packet_type)
{
	unsigned char *str = buf;
	head header;

	header.byte = 0;
	header.bits.type = packet_type;
	writerchar(&str, header.byte);

	MQTTPacket_encode(str,0);
	
	return (str - buf);
}

int MQTTSerialise_CONNECT(unsigned char *buf,int size,iot_mqttconn_parament option)
{
	unsigned char *str = buf;
	int len = 0;

	if(MQTTPacket_len(len = MQTTRemain_len(&option)) > size) //packet len over size
	{
		printf("buf_size is overflow\r\n");
		return ERROR;
	}
	
	option.Mqtt_Head.byte = 0;
	option.Mqtt_Head.bits.type = CONNECT; 
	//writerchar(&str,option.Mqtt_Head.byte);			//fixed header
	*str = option.Mqtt_Head.byte;
	str++;

	str += MQTTPacket_encode(str,len);
	//MQTTPacket_encode(str,len);
	
	if(option.Mqtt_Version == 4)					//variable header
	{
		writerstring(&str,"MQTT");
		writerchar(&str,4);
	}

	writerchar(&str,option.conn_flage.byte);
	writerint(&str,option.Keep_Alive);

	writerstring(&str,option.client_id.cstr);			//payload
	if(option.conn_flage.bits.Will_Flage == 1)
	{
		writerstring(&str,option.will_topic.cstr);
		writerstring(&str,option.will_message.cstr);
	}

	if(option.conn_flage.bits.Name_Flage == 1)
		writerstring(&str,option.user_name.cstr); 
	if(option.conn_flage.bits.Pass_Flage == 1)
		writerstring(&str,option.passworld.cstr);

	return (str - buf);
}
int MQTTDeSerialise_CONNECT(iot_mqttconn_ack *ack,unsigned char *buff)
{
	unsigned char *str = buff;
	head header;
	unsigned int lenght;

	if(!str)
	{
		printf("read buff is null\r\n");
		return ERROR;
	}
	header.byte = readchar(&str);
	if(header.bits.type != CONNACK)
		return ERROR;
	str += MQTTPacket_decode(str,&lenght);

	if(lenght < 2)
		return ERROR;
	ack->first_byte.byte = readchar(&str);
	ack->recode = readchar(&str);
	if(ack->recode != 0X00)
	{
		printf("recv code :0X%02x\r\n",ack->recode);
		return ERROR;
	}

	return 0;
}

int MQTT_CONNECT(iot_client_parament *Client,iot_mqttconn_parament option)
{
	int len = 0,ret = 0;
	time_val start_time;
	iot_mqttconn_ack ack;
	
	//MQTTTime_now(&start_time);
	MQTTTime_init(&start_time,Client->timeout);

	Thread_lock_mutex(mqttclient_mutex);
	Thread_lock_mutex(connect_mutex);
	if(Client == NULL)
	{
		printf("client is null\r\n");
		ret = ERROR;
		goto exit_func;
	}
	if(Client->timeout == 0)
		Client->timeout = MQTT_TIMEOUT;
		
	#if 0
	if(option.user_name.lenght <= 0 || option.user_name.lenght >= topicmax) 
	{
		printf("user_name is too long\r\n");
		return ERROR
	}
	if(option.passworld.lenght <= 0 || option.passworld.lenght >= topicmax) 
	{
		printf("user_name is too long\r\n");
		return ERROR;
	}
	#endif
	if((len = MQTTSerialise_CONNECT(Client->write_buf,Client->write_size,option)) == ERROR)	//组包
	{
		printf("serialise fail\r\n");
		ret = ERROR;
		goto exit_func;
	}

	if((ret = Send_packet(Client,len,start_time)) == ERROR)
	{
		printf("send packet fail\r\n");
		goto exit_func;
	}

	if(Wait_ack(Client,CONNACK,start_time) == CONNACK)
	{
		printf("wait recv CONNACK\r\n");

		if((ret = MQTTDeSerialise_CONNECT(&ack,Client->read_buf)) == ERROR)
		{
			printf("server reject connection request\r\n");
		}
	}
	else
	{
		printf("waitfor not connack\r\n");
		return ERROR;
	}
exit_func:
	if(ret == ERROR)
		Destory_network(Client);
	Thread_unlock_mutex(mqttclient_mutex);
	Thread_unlock_mutex(connect_mutex);
	printf("creat mqttconn success!!!\r\n");
	return ret;
}
int MQTTPubRemain_len(iot_mqtt_parament *option)
{
	int len = 0;
	if(option == NULL)
	{
		printf("option is null\r\n");
		return ERROR;
	}
	if(option->topic != NULL)
		len += strlen(option->topic)+2;
	if(option->qos > 0)
		len += 2;	//packetid
	if(option->message != NULL)
		len += strlen(option->message);

	
	return len;
}

int MQTTSubRemain_len(iot_mqtt_parament *option)
{
	int len = 2;
	if(option == NULL)
	{
		printf("option is null\r\n");
		return ERROR;
	}
	if(option->topic != NULL)
		len += strlen(option->topic)+3;

	return len;
}

int MQTTSerialise_ack(unsigned char *buf,int size,char packettype,int packetid)
{
	unsigned char *str = buf;
	head head_ack;

	head_ack.byte = 0;
	head_ack.bits.type = packettype;
	
	writerchar(&str,head_ack.byte);
	str += MQTTPacket_encode(str, (int)2);
					
	writerint(&str,packetid);

	return (str-buf);
}

int MQTTSerialise_PUBLISH(unsigned char *buf,int size,iot_mqtt_parament option)
{
	unsigned char *str = buf;
	int len = 0;
	head head_pub;
	if(MQTTPacket_len(len = MQTTPubRemain_len(&option)) > size) //packet len over size
	{
		printf("buf_size is overflow\r\n");
		return ERROR;
	}
	
	head_pub.byte = 0;
	head_pub.bits.retain = option.retain;
	head_pub.bits.Qos = option.qos;
	head_pub.bits.dup = option.dup;
	head_pub.bits.type = PUBLISH;
	
	writerchar(&str,head_pub.byte);			//fixed header

	str += MQTTPacket_encode(str,len);
	//MQTTPacket_encode(str,len);

	writerstring(&str,option.topic);
	if(option.qos > 0)
	{
		writerint(&str,packet_id);
		packet_id++;
	}

	memcpy(str,option.message,strlen(option.message));
	str += strlen(option.message);

	return (str - buf);
}

int MQTTSerialise_SUBSCRIBE(unsigned char *buf,int size,iot_mqtt_parament option)
{
	unsigned char *str = buf;
	int len = 0;
	head head_sub;
	if(MQTTPacket_len(len = MQTTSubRemain_len(&option)) > size) //packet len over size
	{
		printf("buf_size is overflow\r\n");
		return ERROR;
	}
	
	head_sub.byte = 0;
	head_sub.bits.Qos = 1;
	head_sub.bits.type = SUBSCRIBE;
	
	writerchar(&str,head_sub.byte);			//fixed header

	str += MQTTPacket_encode(str,len);
	//MQTTPacket_encode(str,len);
	
	writerint(&str,packet_id);
	packet_id++;

	writerstring(&str,option.topic);
	writerchar(&str,option.qos);	

	return (str - buf);
}

int MQTTDeSerialise_ack(unsigned char *buff,unsigned short *packetid)
{
	unsigned char *str = buff;
	head header;
	unsigned int lenght;
	//int packetid=0;

	if(!str)
	{
		printf("read buff is null\r\n");
		return ERROR;
	}
	header.byte = readchar(&str);

	str += MQTTPacket_decode(str,&lenght);

	if(lenght < 2)
		return ERROR;

	*packetid = readint(&str);
	if(*packetid > packet_id)
	{
		printf("packet_id err:%d\r\n",*packetid);
		return ERROR;
	}

	return SUCCESS;
}


int MQTTDeSerialise_Publish(unsigned char *buff,iot_mqtt_parament *mesg)
{
	unsigned char *str = buff;
	head header;
	unsigned int lenght;
	//unsigned short packetid=0;

	if(!str)
	{
		printf("read buff is null\r\n");
		return ERROR;
	}
	header.byte = readchar(&str);

	mesg->dup = header.bits.dup;
	mesg->qos = header.bits.Qos;
	mesg->retain = header.bits.retain;

	str += MQTTPacket_decode(str,&lenght);

	if(lenght < 2)
		return ERROR;

	mesg->topic_len = readint(&str);
	if(lenght < mesg->topic_len)
	{
		printf("topic_len > reamin_len\r\n");
		return ERROR;
	}

	mesg->topic = str;
	str += mesg->topic_len;
	if(mesg->qos > 0)
		mesg->mesg_id = readint(&str);

	mesg->message = str;
	mesg->mesg_len = buff + lenght - str;
	return SUCCESS;
}

int MQTTDeSerialise_Subscribe(unsigned char *buff,unsigned char *maxqos)
{
	unsigned char *str = buff;
	head header;
	unsigned int lenght;
	unsigned short packetid = 0;
	//int packetid=0;

	if(!str)
	{
		printf("read buff is null\r\n");
		return ERROR;
	}
	header.byte = readchar(&str);

	str += MQTTPacket_decode(str,&lenght);

	if(lenght < 2)
		return ERROR;

	packetid = readint(&str);
	//printf("packet_id err:%d\r\n",packetid);

	*maxqos = readchar(&str);
	return SUCCESS;
}

int MQTT_SUBSCRIBE(iot_client_parament *Client,iot_mqtt_parament option)
{
	int len = 0,ret = 0;
	time_val start_time;
	iot_mqttconn_ack ack;
	
	//MQTTTime_now(&start_time);
	MQTTTime_init(&start_time,Client->timeout);

	Thread_lock_mutex(mqttclient_mutex);
	Thread_lock_mutex(subscribe_mutex);
	if(Client == NULL)
	{
		printf("client is null\r\n");
		ret = ERROR;
		goto exit_func;
	}
	if(Client->timeout == 0)
		Client->timeout = MQTT_TIMEOUT;
		
	#if 0
	if(option.user_name.lenght <= 0 || option.user_name.lenght >= topicmax) 
	{
		printf("user_name is too long\r\n");
		return ERROR
	}
	if(option.passworld.lenght <= 0 || option.passworld.lenght >= topicmax) 
	{
		printf("user_name is too long\r\n");
		return ERROR;
	}
	#endif
	if((len = MQTTSerialise_SUBSCRIBE(Client->write_buf,Client->write_size,option)) == ERROR)	//组包
	{
		printf("serialise fail\r\n");
		ret = ERROR;
		goto exit_func;
	}

	if((ret = Send_packet(Client,len,start_time)) == ERROR)
	{
		printf("send packet fail\r\n");
		goto exit_func;
	}

	{
		if(Wait_ack(Client,SUBACK,start_time) == SUBACK)
		{
			printf("wait recv SUBACK\r\n");
			unsigned char grantqos;
			if((ret = MQTTDeSerialise_Subscribe(Client->read_buf,&grantqos)) == ERROR)
			{
				goto exit_func;
			}

			if(grantqos != 0x80)
			{
				int i;
				for(i=0;i<100;i++)
				{
					if(strlen(topic_filter[i]) == 0)
					{
						//topic_filter[i] = (char *)malloc(topicmax);
						memcpy(topic_filter[i],option.topic,option.topic_len);
						break;
					}
				}
			}
			else
			{
				printf("subscribe fail\r\n");
				ret = ERROR;
				goto exit_func;
			}
		}
		else
			ret = ERROR;
	}

exit_func:
	if(ret == ERROR)
		Destory_network(Client);
	
	Thread_unlock_mutex(mqttclient_mutex);
	Thread_unlock_mutex(subscribe_mutex);
	printf("mqttsub success!!!\r\n");
	return ret;
}

int MQTT_PUBLISH(iot_client_parament *Client,iot_mqtt_parament option)
{
	int len = 0,ret = 0;
	time_val start_time;
	iot_mqttconn_ack ack;
	
	//MQTTTime_now(&start_time);
	MQTTTime_init(&start_time,Client->timeout);

	Thread_lock_mutex(mqttclient_mutex);

	if(Client == NULL)
	{
		printf("client is null\r\n");
		ret = ERROR;
		goto exit_func;
	}
	if(Client->timeout == 0)
		Client->timeout = MQTT_TIMEOUT;
		
	#if 0
	if(option.user_name.lenght <= 0 || option.user_name.lenght >= topicmax) 
	{
		printf("user_name is too long\r\n");
		return ERROR
	}
	if(option.passworld.lenght <= 0 || option.passworld.lenght >= topicmax) 
	{
		printf("user_name is too long\r\n");
		return ERROR;
	}
	#endif
	if((len = MQTTSerialise_PUBLISH(Client->write_buf,Client->write_size,option)) == ERROR)	//组包
	{
		printf("serialise fail\r\n");
		ret = ERROR;
		goto exit_func;
	}

	if((ret = Send_packet(Client,len,start_time)) == ERROR)
	{
		printf("send packet fail\r\n");
		goto exit_func;
	}

	if(option.qos == 1)
	{
		if(Wait_ack(Client,PUBACK,start_time) == PUBACK)
		{
			printf("wait recv PUBACK\r\n");
			unsigned short packetid;
			if((ret = MQTTDeSerialise_ack(Client->read_buf,&packetid)) == ERROR)
			{
				goto exit_func;
			}
		}
		else
			ret = ERROR;
	}
	if(option.qos == 2)
	{
		Client->timeout += 20000;
		if(Wait_ack(Client,PUBCOMP,start_time) == PUBCOMP)
		{
			printf("wait recv PUBCOMP\r\n");
			unsigned short packetid = 0;
			if((ret = MQTTDeSerialise_ack(Client->read_buf,&packetid)) == ERROR)
			{
				goto exit_func;
			}
		}
		else
			ret = ERROR;
	}

exit_func:
	if(ret == ERROR)
		Destory_network(Client);

	Thread_lock_mutex(mqttclient_mutex);
	printf("mqttpub success!!!\r\n");
	return ret;
}

int MQTT_DISCONN(iot_client_parament *Client)
{
	int len = 0;
	time_val start_time;
	
	//MQTTTime_now(&start_time);
	MQTTTime_init(&start_time,Client->timeout);
	
	if(Client == NULL)
	{
		printf("client is null\r\n");
		return ERROR;
	}
	if(Client->timeout == 0)
		Client->timeout = MQTT_TIMEOUT;
		
	if((len = MQTTSerialise_zero(Client->write_buf,Client->write_size,DISCONNECT)) == ERROR)	//creat packet
	{
		printf("serialise fail\r\n");
		return ERROR;
	}

	if(Send_packet(Client,len,start_time) == ERROR)
	{
		printf("send packet fail\r\n");
		return ERROR;
	}

	printf("close mqttlink success!!!\r\n");
	Destory_network(Client);
	Destory_client(Client);
	return 0;
}

void MQTT_yield(iot_client_parament *Client,unsigned long timeout)
{
	time_val start_time;

	//MQTTTime_now(&start_time);
	MQTTTime_init(&start_time,timeout);
	
	while(MQTTTime_left(start_time,start_time.t_end))
	{
		cycle(Client,start_time);
	}
}

void MQTT_waitForCompletion(iot_client_parament *Client,unsigned long timeout)
{
	time_val start_time;

	//MQTTTime_now(&start_time);
	MQTTTime_init(&start_time,timeout);

	while(MQTTTime_left(start_time,start_time.t_end))
	{
		MQTT_yield(Client,200);
	}

	printf("game over!!!\r\n");
}

iot_client_parament* MQTTClient_Creat()
{
	iot_client_parament *Client; 

	Client = (iot_client_parament*)malloc(sizeof(iot_client_parament));
	if(Client == NULL)
	{
		printf("creat client error\r\n");
		return NULL;
	}
	
	memset(Client,0x0,sizeof(iot_client_parament));
	memset(topic_filter,0x0,sizeof(topic_filter));
	
	Client->read_size = MaxSize;
	Client->write_size = MaxSize;
	Client->read_buf = (unsigned char*)malloc(MaxSize);
	Client->write_buf = (unsigned char*)malloc(MaxSize);
	Client->timeout = MQTT_TIMEOUT;

	if(Network_Init(&Client->PNetwork) == -1)
	{
		Destory_client(Client);
		return NULL;
	}

	printf("creat client success\r\n");
	return Client;
}

int main(int argc,char *argv[])
{
	iot_mqttconn_parament conn_parament = MQTT_CONNECT_Initializer;
	iot_client_parament *Client = NULL; 
	
	if(argc == 3)
	{
		conn_parament.user_name.cstr = argv[1];
		conn_parament.user_name.lenght = strlen(argv[1]);
		conn_parament.passworld.cstr = argv[2];
		conn_parament.passworld.lenght = strlen(argv[2]);
	}
	if(conn_parament.user_name.lenght == 0 || conn_parament.passworld.lenght == 0)
	{
		printf("username error\r\n");
		return 0;
	}
	if((Client = MQTTClient_Creat()) == NULL)
	{
		return 0;
	}	

	conn_parament.conn_flage.byte = 0;
	conn_parament.conn_flage.bits.Clean_Session = 1;
	conn_parament.conn_flage.bits.Pass_Flage = 1;
	conn_parament.conn_flage.bits.Name_Flage = 1;
	MQTT_CONNECT(Client,conn_parament);

	iot_mqtt_parament pub_mes = {0,1,0,"neoway",6,"hello!",6,0};
	iot_mqtt_parament sub_mes = {0,0,0,"neoway",6,NULL,0,0};

	MQTT_SUBSCRIBE(Client,pub_mes);
	MQTT_PUBLISH(Client,pub_mes);
	MQTT_waitForCompletion(Client,MQTT_TIMEOUT);
	MQTT_DISCONN(Client);
	return 0;
}
