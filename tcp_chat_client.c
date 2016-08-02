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

const char ERQLIST[]={
    "<xml>"
    "<FromUser>%s</FromUser>"
    "<CMD>ReqList</CMD>"
    "</xml>"
};

int startup_handler(void);
int mysql_query_my(MYSQL *conn, const char *str);
int recv_message(xmlDocPtr, xmlNodePtr);
int login_res(xmlDocPtr doc, xmlNodePtr cur);

typedef int (*xml_handle_t)(xmlDocPtr, xmlNodePtr);
xml_handle_t xml_handle_table[] = {
	recv_message,
    login_res
};

int main(int argc, char *argv[])
{
	int sockfd;
    struct pollfd fds[2]={0};
	struct hostent *host;
	struct sockaddr_in serv_addr;

	if(argc<2)
	{
		fprintf(stderr,"USAGE: ./tcp_chat_client YourName\n");
		exit(1);
	}

    startup_handler();

    printf("passwd:\n");
    while(1)
    {
        fgets(stdinbuf, 128, stdin);
        sprintf(sqlcmd, "select passwd from user where UserName='%s'", argv[1]);
        mysql_query_my(conn, sqlcmd);
        MYSQL_RES *res = mysql_store_result(conn);
        if(res) break;
    }

	if((host = gethostbyname("yeyl.site"))==NULL)
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

    fds[0].fd = 0;
    fds[0].events = POLLIN;
    fds[1].fd = sockfd;
    fds[1].events = POLLRDNORM;

    sprintf(sendbuf, LOGIN_MSG, argv[1]);
    send(sockfd, sendbuf, strlen(sendbuf), 0);

    while(1)
    {
        poll(fds, 2, 4000);
        if(fds[0].revents & POLLIN)
        {
            fgets(stdinbuf,128,stdin);
            stdinbuf[strlen(stdinbuf)-1]=0;
            if(stdinbuf != strstr(stdinbuf, "exit"))
            {
                sprintf(sendbuf, SEND_MSG, argv[1], "All",stdinbuf);
                send(sockfd, sendbuf, strlen(sendbuf), 0);
            }
            else
            {
                shutdown(sockfd, SHUT_WR);
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
            for(i=0; i<COUNTOF(xml_handle_table); i++)
            {
                int ret = (xml_handle_table[i])(doc, cur);
                if(ret >= 0)
                    break;
            }
            xmlFreeDoc(doc);
        }
    }
	close(sockfd);
	exit(0);
}

/*客户端启动时调用函数*/
int startup_handler(void)
{
	conn = mysql_init(NULL);
	char value = 1;
	mysql_options(conn, MYSQL_OPT_RECONNECT, (char *)&value);
    //连接数据库
    if (!mysql_real_connect(conn, "yeyl.site", "yeyl", "123456", "MyChat", 0, NULL, 0)) 
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


int recv_message(xmlDocPtr doc, xmlNodePtr cur)
{
    xmlChar *fromuser = NULL;
    xmlChar *context = NULL;
    if((!xmlStrcmp(cur->name, (const xmlChar *)"FromUser")))
    {
        fromuser = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
    }
    if((cur = cur->next) == NULL)
		return -1;
    if((!xmlStrcmp(cur->name, (const xmlChar *)"Context")))
    {
        context = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
    }
    if(fromuser!=NULL && context!=NULL)
    {
        printf("%s:%s\n", fromuser, context);
    }
    xmlFree(fromuser);
    xmlFree(context);
    return 0;
}

int login_res(xmlDocPtr doc, xmlNodePtr cur)
{
    xmlChar *login_res = NULL;
    if((!xmlStrcmp(cur->name, (const xmlChar *)"Login")))
    {
        login_res = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
    }
    else
    {
        return -1;
    }
    if(login_res != NULL)
    {
        if(strcmp((char *)login_res, "success") == 0)
        {
            xmlFree(login_res);
            printf("login success\n");
            return 1;
        }
        else if(strcmp((char *)login_res, "loged") == 0)
        {
            printf("you has loged\n");
        }
    }
    xmlFree(login_res);
    return 0;
}
