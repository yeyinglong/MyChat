#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>

#include <libxml/parser.h>  
#include <libxml/xmlmemory.h>
#include <libxml/tree.h>

#include <mysql/mysql.h>

#include "log.h"

#define PORT 12321
#define BUFFER_SIZE 512
#define COUNTOF(x) (sizeof(x)/sizeof((x)[0]))
char sendbuf[BUFFER_SIZE];
char recvbuf[BUFFER_SIZE];
char stdinbuf[128];
char sqlcmd[BUFFER_SIZE];
MYSQL *conn;       //数据库
char username[32];
static int user_status;
static int sockfd;
#define U_ST_LOGIN 1  //已登录
#define U_ST_LOGOUT 0  //未登录
#define U_ST_LOGING 2   //正在登陆
#define U_ST_CHAT 3   //正在聊天   


/************一套输入指令:
*************logout : 登出当前账号
*************login ：登陆账号
*************show list : 显示当前在线用户
*************chat %s,friendname : 选择聊天对象
*************msg %s:输入msg 加聊天的内容，将内容和聊天对象发送给服务器


*************/


const char SEND_MSG[]={
	"<xml>"
	"<FromUser>%s</FromUser>"
    "<CMD>msg</CMD>"
    "<ToUser>%s</ToUser>"
	"<Context><![CDATA[%s]]></Context>"
	"</xml>"
};

const char LOGIN_MSG[]={
    "<xml>"
    "<FromUser>%s</FromUser>"
    "<CMD>Login</CMD>"
    "</xml>"
};

const char LOGOUT_MSG[]={
    "<xml>"
    "<FromUser>%s</FromUser>"
    "<CMD>Logout</CMD>"
    "</xml>"
};

const char REQLIST[]={
    "<xml>"
    "<FromUser>%s</FromUser>"
    "<CMD>ReqList</CMD>"
    "</xml>"
};

const char ALIVE_MSG[]={
	"xml"
	"<FromUser>%s</FromUser>"
	"<CMD>Alive</CMD>"
};

int startup_handler(void);
int mysql_query_my(MYSQL *conn, const char *str);
int recv_message(xmlDocPtr, xmlNodePtr);
int login_res(xmlDocPtr doc, xmlNodePtr cur);
int send_res(xmlDocPtr doc, xmlNodePtr cur);
int logout_res(xmlDocPtr doc, xmlNodePtr cur);
int list_res(xmlDocPtr doc, xmlNodePtr cur);
int cleanup(void);


void *client_alive(void *);   //线程函数，用于定时向服务器发送信息
int load_user();     //用于登陆账号

// typedef int (*xml_handle_t)(xmlDocPtr, xmlNodePtr);
// xml_handle_t xml_handle_table[] = {
	// recv_message,
    // login_res
// };

typedef int (*pfun)(xmlDocPtr,xmlNodePtr);

typedef struct{
	char operator[32];
	pfun func;
}xml_handler_t;

xml_handler_t xml_handler_table[5] = {
	{"msg",recv_message},{"res",send_res},\
	{"Login",login_res},{"Logout",logout_res},\
	{"ReqList",list_res}
};

int main(void)
{
    struct pollfd fds[2];
	struct hostent *host;
	struct sockaddr_in serv_addr;
	char friendname[32] = {0};
	
	user_status = U_ST_LOGOUT;
	
    startup_handler();
	
	if((host = gethostbyname("localhost"))==NULL)
	{
		perror("gethostbyname");
		exit(1);
	}
	
	/*创建socket*/
	if((sockfd=socket(AF_INET,SOCK_STREAM,0))==-1)
	{
		perror("socket");
		exit(1);
	}

	serv_addr.sin_family=AF_INET;
	serv_addr.sin_port=htons(PORT);
	serv_addr.sin_addr=*((struct in_addr*)host->h_addr);
	bzero(&(serv_addr.sin_zero),8);

	/*调用connect函数主动发起对服务器的连接*/

	if(connect(sockfd,(struct sockaddr*)&serv_addr,sizeof(struct sockaddr)) == -1)
	{
		perror("connect");
		exit(1);
	}	
	//建立保活线程
	pthread_t tid_alive;
	pthread_create(&tid_alive,NULL,client_alive,NULL);
	pthread_detach(tid_alive);
	
	
	//在服务器上登陆账号
	if(load_user() !=0)
	{
		printf("load user error!\n");
		close(sockfd);
		exit(1);
	}
	sprintf(sendbuf, LOGIN_MSG, username);
    send(sockfd, sendbuf, strlen(sendbuf), 0);
	user_status = U_ST_LOGIN;
   
    fds[0].fd = 0;
    fds[0].events = POLLIN;
    fds[1].fd = sockfd;
    fds[1].events = POLLRDNORM;

when_getmessage:
	while(user_status != U_ST_LOGOUT)
    {
		bzero(sendbuf,sizeof(sendbuf));
        poll(fds, 2, 4000);
        if(fds[0].revents & POLLIN)
        {
			fgets(stdinbuf,sizeof(stdinbuf),stdin);
			stdinbuf[strlen(stdinbuf)-1] = '\0';
			if(strncmp(stdinbuf,"logout",strlen("logout")) ==0)
			{
				sprintf(sendbuf,LOGOUT_MSG,username);
				send(sockfd,sendbuf,strlen(sendbuf),0);
				user_status = U_ST_LOGOUT;	
			}
			else if(strncmp(stdinbuf,"show list",strlen("show list")) == 0)
			{
				sprintf(sendbuf,REQLIST,username);
				send(sockfd,sendbuf,strlen(sendbuf),0);
			}
			else if(strncmp(stdinbuf,"chat",strlen("chat")) == 0)
			{
				strtok(stdinbuf," ");
				char *str;
				if((str = strtok(NULL," ")) == NULL)
				{
					printf("please input the name you want to chat with!\n");
				}
				else
				{
					strcpy(friendname,str);
					user_status = U_ST_CHAT;
				}
			}
			else if(user_status == U_ST_CHAT || strncmp(stdinbuf,"msg",strlen("msg")) == 0)
			{
				strtok(stdinbuf," ");
				char *str;
				if((str = strtok(NULL," ")) == NULL)
				{
					printf("please input the message you want to send\n");
				}
				else
				{
					sprintf(sendbuf,SEND_MSG,username,friendname,str);
					send(sockfd,sendbuf,strlen(sendbuf),0);
				}
			}
			else
			{
				printf("pleace input using currect instructions!\n");
			}
        }
        if(fds[1].revents & POLLRDNORM)
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
	while(1)
	{
		if(user_status != U_ST_LOGOUT && user_status != U_ST_LOGING)
			goto when_getmessage; 
		char *this_status[4] = {"logout","login","loging","chat"};
		printf("user_status:%s",this_status[user_status]);
		printf("input login to login again or quit to exit\n");
		fgets(stdinbuf,sizeof(stdinbuf),stdin);
		stdinbuf[strlen(stdinbuf)-1] = '\0';
		if(strncmp(stdinbuf,"login",strlen("login")) ==0)
			load_user();
		else if(strncmp(stdinbuf,"quit",strlen("quit")) == 0)
			break;
	}
	close(sockfd);
	exit(0);
}

int load_user()
{
	printf("username:\n");
	fgets(username,sizeof(username),stdin);
	username[strlen(username)-1] = '\0';
	
    while(1)
    {
		printf("passwd:");
        fgets(stdinbuf, 128, stdin);
        sprintf(sqlcmd, "select passwd from user where UserName='%s'", username);
        mysql_query_my(conn, sqlcmd);
        MYSQL_RES *res = mysql_store_result(conn);
        if(res==NULL)
		{
			printf("this username hasn't regist!\n");
			bzero(username,sizeof(username));
			load_user();
		}
		else
		{
			MYSQL_ROW row = mysql_fetch_row(res);
			if(row!= NULL)
			{
				if(strncmp((char *)row[0],stdinbuf,strlen(stdinbuf)-1) != 0) 
				{
					printf("passwd error!please input agian\n");
					continue;
				}
				else break;
			}				
		}
		
    }	
	return 0;
}


/*客户端启动时调用函数*/
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

int mysql_query_my(MYSQL *conn, const char *str)
{
	mysql_ping(conn);
	int ret = mysql_query(conn, str);
	if(ret)
	{
		LOG_ERR_MYSQL(conn);
	}
	return ret;
}


void *client_alive(void *arg)
{
	char alive_buf[BUFFER_SIZE] = {0};
	while(1)
	{
		sprintf(alive_buf,ALIVE_MSG,username);
		send(sockfd,alive_buf,strlen(alive_buf),0);
		bzero(alive_buf,sizeof(alive_buf));
		sleep(300);
	}
	return (void *)0;
}

int recv_message(xmlDocPtr doc, xmlNodePtr cur)
{
	xmlChar *fromuser;
	xmlChar *contex;
	if((cur = cur->next) == NULL)
		return -1;
	if(xmlStrcmp(cur->name,(const xmlChar *)"FromUser"))
		return -1;
		
	fromuser = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
	if(fromuser == NULL)
		return -1;
	
	if((cur = cur->next) == NULL)
	{
		free(fromuser);
		return -1;
	}
	if(xmlStrcmp(cur->name,(const xmlChar *)"Contex"))
	{
		free(fromuser);
		return -1;
	}
	contex = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
	if(contex == NULL)
	{
		free(fromuser);
		return -1;
	}
	printf("%s : %s\n",fromuser,contex);               //获取收到的信息
	free(contex);
	free(fromuser);
	return 0;
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
	if(strncpy((char *)error,"login error",strlen("login error"))==0)
		user_status = U_ST_LOGOUT;
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