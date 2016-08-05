#include <stdio.h>
#include <stdlib.h>
#include<string.h>
#include <errno.h>
#include<time.h>
#include<pthread.h>
#include<libxml/parser.h>
#include<libxml/xmlmemory.h>
#include<libxml/tree.h>
#include <mysql/mysql.h>
#include"log.h"
#include"service.h"

#define PORT 12321
#define BUFFER_SIZE 1024
#define COUNTOF(x) (sizeof(x)/sizeof((x)[0]))


int LOG_FLAG = 4;
char sendbuf[BUFFER_SIZE];
char recvbuf[BUFFER_SIZE];
char stdinbuf[128];
static int sockfd;
MYSQL *conn;       //数据库
const char SEND_CMD[]={
	"<xml>"
	"<Fromss>ss</Fromss>"
	"<CMD>%s</CMD>"
	"</xml>"
};

const char SEND_KILL[]={
	"<xml>"
	"<Fromss>ss</Fromss>"
	"<CMD>KILL</CMD>"
	"<name>%s</name>"
	"</xml>"
};

const char ALIVE_MSG[]={
	"<xml>"
	"<FromUser>%s</FromUser>"
	"<CMD>Alive</CMD>"
	"</xml>"
};

int startup_handler(void)
{
	conn = mysql_init(NULL);
	char value = 1;
	mysql_options(conn, MYSQL_OPT_RECONNECT, (char *)&value);
    //连接数据库
    if (!mysql_real_connect(conn, "yeyl.site", "root", "201qyzx201", "MyChat", 0, NULL, 0)) 
    {
		LOG_ERR_MYSQL(conn);
    }
	return 0;
}
int login_res(xmlDocPtr doc, xmlNodePtr cur);
int cmd_res(xmlDocPtr doc, xmlNodePtr cur);
int send_res(xmlDocPtr doc, xmlNodePtr cur);
int logout_res(xmlDocPtr doc, xmlNodePtr cur);
int list_res(xmlDocPtr doc, xmlNodePtr cur);
int cmd_res(xmlDocPtr doc, xmlNodePtr cur);
int ss_login();
int ss_logout();

void *client_alive(void *arg);

typedef int (*pfun)(xmlDocPtr,xmlNodePtr);
typedef struct{
	char operator[32];
	pfun func;
}xml_handler_t;

xml_handler_t xml_handler_table[] = {
	{"ERR",cmd_res},
	{"res",send_res},
	{"Login",login_res},
	{"Logout",logout_res},
	{"ReqList",list_res},
	{"INFO",cmd_res},
	{"DEBUG",cmd_res},
	{"WARNING",cmd_res},
	{"KILL",cmd_res},
};

int main(int argc, char *argv[])
{
	//建立连接；
	struct hostent *host;
	if(argc < 2 )
	{
		if((host = gethostbyname("localhost"))==NULL)
		{
			perror("gethostbyname");
			exit(1);
		}
	}
	else
	{
		if((host = gethostbyname(argv[1]))==NULL)
		{
			perror(argv[1]);
			exit(1);
		}
	}
	startup_handler();
	if((sockfd=socket(AF_INET,SOCK_STREAM,0))==-1)
	{
		perror("socket");
		exit(1);
	}
	struct sockaddr_in ss_addr;
	ss_addr.sin_family = AF_INET;
	ss_addr.sin_port =htons(PORT);
	ss_addr.sin_addr =*((struct in_addr*)host->h_addr);
	bzero(&(ss_addr.sin_zero),8);
	if(connect(sockfd,(struct sockaddr*)&ss_addr,sizeof(ss_addr))== -1)
	{
		perror("connect");
		exit(1);
	}
	//ss保活信息；
	pthread_t tid_alive;
	pthread_create(&tid_alive,NULL,client_alive,NULL);
	//ss_login(); //发送登陆信息
	
	fd_set fdsr;
	struct timeval tv = {4,0};
	while(1) //循环检测是否有数据变化；
	{	
		FD_ZERO(&fdsr);
		FD_SET(0,&fdsr);
		FD_SET(sockfd,&fdsr);
		select(sockfd+1,&fdsr,NULL,NULL,&tv);
		if(FD_ISSET(0,&fdsr))
		{
			fgets(stdinbuf,sizeof(stdinbuf),stdin);
			stdinbuf[strlen(stdinbuf)-1]=0;
			if(strncmp(stdinbuf,"INFO",strlen("INFO"))&&strncmp(stdinbuf,"DEBUG",strlen("DEBUG"))&&strncmp(stdinbuf,"WARNING",strlen("WARNING"))&&strncmp(stdinbuf,"ERR",strlen("ERR"))&&strncmp(stdinbuf,"ReqList",strlen("ReqList")))
			{
				if(strncmp(stdinbuf,"KILL",strlen("KILL"))== 0)  //查找kill name；
				{
					strtok(stdinbuf," ");
					char  * str;
					if((str = strtok(NULL," ")) == NULL)
					{
						printf("please input the name you want to kill!\n");
					}
					else
					{
						bzero(sendbuf,sizeof(sendbuf));
						sprintf(sendbuf,SEND_KILL,str);
						send(sockfd,sendbuf,sizeof(sendbuf),0);
					}
				}
				else if(strncmp(stdinbuf,"exit",strlen("exit"))==0)
					break;
				else
					printf("no such cmd\n");
				continue;
			}
			sprintf(sendbuf,SEND_CMD,stdinbuf);
			send(sockfd,sendbuf,sizeof(sendbuf),0);
		}
		if(FD_ISSET(sockfd,&fdsr))
		{
			int ret;
			ret =recv(sockfd,recvbuf,sizeof(recvbuf),0);
			if(ret == 0)
				continue;
			xmlDocPtr doc;
			xmlNodePtr cur;
			recvbuf[ret] = 0;
			doc = xmlParseMemory((const char*)recvbuf,strlen((char *)recvbuf)+1);
			if(doc == NULL)
			{
				LOG_WARN("DOCUMENT not parsed successfully\n");
				continue;
			}
			cur = xmlDocGetRootElement(doc);
			if (cur == NULL)
            {
                LOG_WARN("empty document\n");
                xmlFreeDoc(doc);
                continue;
            }
            if (xmlStrcmp(cur->name, (const xmlChar *) "xml"))
            {
                LOG_WARN("document of the wrong type, root node != xml");
                xmlFreeDoc(doc);
                continue;
            }
            if((cur = cur->xmlChildrenNode) == NULL)
                continue;
			int i;
            for(i=0; i<COUNTOF(xml_handler_table); i++)
            {
				xmlChar *cmd = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
				if(cmd == NULL)
					continue;
				if(strncmp(xml_handler_table[i].operator,(const char *)cmd,strlen(xml_handler_table[i].operator))==0)
					xml_handler_table[i].func(doc,cur);
				free(cmd);
            }
            xmlFreeDoc(doc);
		
		}
		
	}
	pthread_detach(tid_alive);
	ss_logout(); //发送登出信息；
	return 0;
}

int ss_login()
{
	bzero(sendbuf,sizeof(sendbuf));
	sprintf(sendbuf,SEND_CMD,"Login");
	send(sockfd,sendbuf,sizeof(sendbuf),0);
	return 0;
}

int ss_logout()
{
	bzero(sendbuf,sizeof(sendbuf));
	sprintf(sendbuf,SEND_CMD,"Logout");
	send(sockfd,sendbuf,sizeof(sendbuf),0);
	return 0;
}

void *client_alive(void *arg)
{
	char alive_buf[BUFFER_SIZE] = {0};
	while(1)
	{
		sleep(300);
		sprintf(alive_buf,ALIVE_MSG,"ss");
		send(sockfd,alive_buf,strlen(alive_buf),0);
		bzero(alive_buf,sizeof(alive_buf));
	}
	return (void *)0;
}




int login_res(xmlDocPtr doc, xmlNodePtr cur)
{
	xmlChar *error;
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
int cmd_res(xmlDocPtr doc, xmlNodePtr cur)
{
	xmlChar *error;
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
int send_res(xmlDocPtr doc, xmlNodePtr cur)
{
	xmlChar *error;
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
int logout_res(xmlDocPtr doc, xmlNodePtr cur)
{
	xmlChar *error;
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
	send(sockfd,"OK",strlen("OK"),0);
	free(user_list);
	return 1;
}