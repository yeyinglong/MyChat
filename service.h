#ifndef __SERVICE_H
#define __SERVICE_H

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>

#include "list.h"

typedef struct client_s
{
	struct list_head entry;
	struct sockaddr_in client_addr;
	int fd; //客户端文件描述符
	time_t connect_time; //客户端链接时间
	time_t last_time; //上次接收数据时间
	//int con_time;
}Client,*pClient;

#endif //__SERVICE_H
