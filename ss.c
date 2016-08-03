/*************************************************************************
    > File Name: ss.c
    > Author: bsj*/


//#include"common.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<errno.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<netdb.h>
#include<arpa/inet.h>
#include<sys/types.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<time.h>
#include<libxml/parser.h>
#include<libxml/xmlmemory.h>
#include<libxml/tree.h>
#include<mysql/mysql.h>
#include "log.h"
#include<poll.h>
#include<pthread.h>

#define PORT 12321
#define BUFFER_SIZE 512
#define COUNTOF(x) (sizeof(x)/sizeof((x)[0]))

char sendbuf[BUFFER_SIZE];
char recvbuf[BUFFER_SIZE];
char stdinbuf[128];
static int sockfd;
int logout_res(xmlDocPtr,xmlNodePtr cur);
int list_res(xmlDocPtr doc,xmlNodePtr cur);

typedef int (*pfun)(xmlDocPtr,xmlNodePtr);
typedef struct{
	char operator[32];
	pfun func;
}xml_handler_t;

xml_handler_t xml_handler_table[2]={
	{"ReqList",list_res},
	{"Logout",logout_res}
};

const char REQLIST[]={
	  "<xml>"
	   "<Fromss>ss</Fromss>"
	   "<CMD>Reqlist</CMD>"
	   "</xml>"
  };

  const char CHGLOG[]={
	  "<xml>"
	  "<Fromss>ss</Fromss>"
	  "<CMD>CHGLOG</CMD>"
	  "</xml>"
  };

  const char QUITUSER[]={
	  "<xml>"
	  "<Fromss>ss</Fromss>"
	  "<CMD>QUITUSER</CMD>"
	  "<ToUser>%s</ToUser>"
	  "</xml>"
  };


int main()
{
	struct pollfd fds[2];

	struct hostent*host;
	if((host=gethostbyname("localhost"))==NULL)
	{
		perror("gethostbyname");
		exit(1);
	}
		
	sockfd=socket(AF_INET,SOCK_STREAM,0);

	struct sockaddr_in ss_addr;
	ss_addr.sin_family = AF_INET;
	ss_addr.sin_port = htons(PORT);
	ss_addr.sin_addr = *((struct in_addr*)host->h_addr);
    if(connect(sockfd,(struct sockaddr*)&ss_addr,sizeof(struct sockaddr))==-1)
	{
		perror("connect");
		exit(1);
	}

	fds[0].fd=0;
	fds[0].events = POLLIN;
	fds[1].fd = sockfd;
	fds[1].events = POLLRDNORM;

    char quit_user[]={0};
	printf("connect success\n");
	while(1)
	{
		poll(fds,2,4000);
		if(fds[0].revents & POLLIN)
		{
			printf("TO DO:");

			fgets(stdinbuf,128,stdin);
			stdinbuf[strlen(stdinbuf)-1]=0;
			if(strcmp(stdinbuf,"REQLIST")==0)
			{
				sprintf(sendbuf,REQLIST);
				write(sockfd,sendbuf,strlen(sendbuf));
			}
			else if(strcmp(stdinbuf,"CHGLOG")==0)
			{
				sprintf(sendbuf,CHGLOG);
				write(sockfd,sendbuf,strlen(sendbuf));
			}
			else if(strncmp(stdinbuf, "QUITUSER", strlen("QUITUSER"))==0)			
			{
				strtok(stdinbuf," ");
				char*str;
				if((str=strtok(NULL," "))==NULL)
				{
					printf("please input the name who you want to quit\n");
				}
				else
				{
					strcpy(quit_user,str);
				}
				sprintf(sendbuf,QUITUSER,quit_user);
				write(sockfd ,sendbuf,strlen(sendbuf));
			}
		}

		if(fds[1].revents&POLLRDNORM)
		{
			xmlDocPtr doc;   //定义解析文档指针
			xmlNodePtr cur;  //定义结点指针(你需要它为了在各个结点间移动)
			int recvlen = recv(sockfd, recvbuf, BUFFER_SIZE-1, 0);
			if(recvlen <= 0)
				break;
			recvbuf[recvlen] = 0;
			doc = xmlParseMemory((const char *)recvbuf, strlen((char *)recvbuf)+1);  
			if (doc == NULL )
			{
				LOG_WARN("Document not parsed successfully. \n");
				continue;
			}
			cur = xmlDocGetRootElement(doc);  //确定文档根元素
			/*检查确认当前文档中包含内容*/
			if (cur == NULL)
			{

			}
			if (xmlStrcmp(cur->name, (const xmlChar *) "xml"))
			{

			}
			if((cur = cur->xmlChildrenNode) == NULL)
				continue;
			xmlChar *cmd = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
			int i;
			for(i=0; i<COUNTOF(xml_handler_table); i++)
			{
				if(cmd == NULL)
					continue;
				if(strncmp(xml_handler_table[i].operator,(const char *)cmd,strlen(xml_handler_table[i].operator))==0)
					xml_handler_table[i].func(doc,cur);
			}
			free(cmd);
			xmlFreeDoc(doc);
	   }
	}
   close(sockfd);
   return 0;
}

int logout_res(xmlDocPtr doc,xmlNodePtr cur)
{
   	xmlChar*error;
  	if((cur = cur->next) == NULL)
		return -1;
	if(xmlStrcmp(cur->name,(const xmlChar *)"ERROR"))
		return -1;
	
	error = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
	if(error == NULL)
		return -1;
	printf("%s\n",error);
	free(error);
	return 0;
}

int list_res(xmlDocPtr doc, xmlNodePtr cur)
{
	xmlChar *user_list;
	if((cur = cur->next) == NULL)
		return -1;
	if(xmlStrcmp(cur->name,(const xmlChar *)"User"))
		return -1;
	user_list = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
	if(user_list == NULL)
		return 0;
	printf("%s\n",user_list);
	if(send(sockfd,"OK",strlen("OK"),0)<0)
		LOG_ERR("send");
	free(user_list);
	return 1;
}


