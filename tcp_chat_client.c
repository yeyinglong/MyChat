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
#include <fcntl.h>
#include <sys/stat.h>

#include <libxml/parser.h>  
#include <libxml/xmlmemory.h>
#include <libxml/tree.h>

#include <mysql/mysql.h>

#include "log.h"

#define PORT 12321
#define BUFFER_SIZE 512
#define COUNTOF(x) (sizeof(x)/sizeof((x)[0]))

int LOG_FLAG = 4;

char sendbuf[BUFFER_SIZE];
char recvbuf[BUFFER_SIZE];
char stdinbuf[128];
char sqlcmd[BUFFER_SIZE];
MYSQL *conn;       //数据库
char username[32];
char groupname[32];

struct file_transmit{
	int status;
	char sendname[32];
	char recvname[32];
	char filename[32];
}*pFileTransmit;
//定义文件传输状态，正在传输时或正在准备时无法再进行文件传输
#define TRA_ST_RUN 1  //传输状态
#define TRA_ST_REST 0  //文件传输的休息状态
#define TRA_ST_PREP 2  //准备状态，即发送文件传输的信息直到双方建立连接的状态

static int user_status;
static int sockfd;
	
#define U_ST_LOGIN 1  //已登录
#define U_ST_LOGOUT 0  //未登录
#define U_ST_LOGING 2   //正在登陆
#define U_ST_CHAT 3   //正在聊天   
#define U_ST_GCHAT 4   //正在group聊天   


#define FILE_MAXLEN 1024
struct waving_file{               //用于文件传输的结构体
	char name[32];              //文件名，用于检查传输文件的正确性
	int lenth;                     //文件内容的长度，用于校验信息的完整性
	char data[FILE_MAXLEN];
};

//字符
char* newstr[]={">_<","⊙﹏⊙","^_^","-_-","..@_@..","(0_0;)","o_0","O__O","o_O???","一 一+","(*>﹏<*)","('')Y","(~_~)/"};
char* oldstr[]={"#01","#02","#03","#04","#05","#06","#07","#08","#09","#10","#11","#66","88"};
/************一套输入指令:
*************logout : 登出当前账号
*************login ：在LOGOUT状态下登陆账号
*************show list : 显示当前在线用户
*************chat %s,friendname : 选择聊天对象
*************%s:在CHAT状态下输入聊天的内容，将内容和聊天对象发送给服务器
*************regist : 在LOGOUT状态下注册账号
*************quit : 在LOGOUT状态退出程序
*************send file : 在LOGIN或者CHAT状态下，进行发送文件的操作
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


const char GROUP_MSG[]={    //group发生信息-
	"<xml>"
	"<FromUser>%s</FromUser>"
    "<CMD>groupmsg</CMD>"
    "<GROUP>%s</GROUP>"
	"<Context><![CDATA[%s]]></Context>"
	"</xml>"
};

const char GROUP_LIST[]={    //group在线人员
	"<xml>"
	"<FromUser>%s</FromUser>"
    "<CMD>GroupList</CMD>"
    "<GROUP>%s</GROUP>"
	"</xml>"
};

const char ALIVE_MSG[]={
	"<xml>"
	"<FromUser>%s</FromUser>"
	"<CMD>Alive</CMD>"
	"</xml>"
};

const char FILE_SEND[]={              //发送端发给服务器的包
	"<xml>"
	"<FromUser>%s</FromUser>"
	"<CMD>FileSend</CMD>"
	"<ToUser>%s</ToUser>"
	"</xml>"
};

const char FILE_RECV[]={               //接收端发给服务器的包
	"<xml>"
	"<FromUser>%s</FromUser>"
	"<CMD>FileRecv</CMD>"
	"<ToUser>%s</ToUser>"
	"<ADDR>"
		"<IP>%s</IP>"
		"<PORT>%s</PORT>"
	"</ADDR>"
	"</xml>"
};

const char C_FILE_SEND_ERR[]={    //发送端出现异常时，发送给服务器的包
	"<xml>"
	"<FromUser>%s</FromUser>"
	"<CMD>FileSendError</CMD>"
	"<ToUser>%s</ToUser>"
	"<ERROR>%s</ERROR>"
	"</xml>"
};

const char C_FILE_RECV_ERR[]={      //接收端出现异常时，发送给服务器的包
	"<xml>"
	"<FromUser>%s</FromUser>"
	"<CMD>FileRecvError</CMD>"
	"<ToUser>%s</ToUser>"
	"<ERROR>%s</ERROR>"
	"</xml>"
};


int showgroup();
int joingroup();
int leavegroup();
int creategroup();
int deletegroup();
int startup_handler(void);
int mysql_query_my(MYSQL *conn, const char *str);
int recv_message(xmlDocPtr, xmlNodePtr);
int login_res(xmlDocPtr doc, xmlNodePtr cur);
int send_res(xmlDocPtr doc, xmlNodePtr cur);
int logout_res(xmlDocPtr doc, xmlNodePtr cur);
int list_res(xmlDocPtr doc, xmlNodePtr cur);
int recv_groupmsg(xmlDocPtr doc, xmlNodePtr cur);
int file_send_to(xmlDocPtr doc, xmlNodePtr cur);
int file_recv_from(xmlDocPtr doc, xmlNodePtr cur);
int face(char *str,char*oldstr,char*newstr,char*buf);//表情函数；
void *client_file_recv(void *);
void *client_file_send(void *);
void *client_alive(void *);   //线程函数，用于定时向服务器发送信息
int load_user();     //用于登陆账号
int regist_user();   //注册账号


typedef int (*pfun)(xmlDocPtr,xmlNodePtr);

typedef struct{
	char operator[32];
	pfun func;
}xml_handler_t;


xml_handler_t xml_handler_table[] = {
	{"msg",recv_message},
	{"res",send_res},
	{"Login",login_res},
	{"Logout",logout_res},
	{"ReqList",list_res},
	{"FileSend",file_send_to},
	{"FileRecv",file_recv_from},
	{"groupmsg", recv_groupmsg},
	{"GroupList", list_res}
};

int main(int argc, char *argv[])
{
	pFileTransmit = (struct file_transmit *)malloc(sizeof(struct file_transmit));
    struct pollfd fds[2];
	struct hostent *host;
	struct sockaddr_in serv_addr;
	char friendname[32] = {0};
//	char filename[32] = {0};
	
	user_status = U_ST_LOGOUT;
	pFileTransmit->status = TRA_ST_REST;
    
	startup_handler();
	
	char hostname[32] = "localhost";
	if(argc > 1)
		strcpy(hostname, argv[1]);
	if((host = gethostbyname(hostname))==NULL)
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
	
    fds[0].fd = 0;
    fds[0].events = POLLIN;
    fds[1].fd = sockfd;
    fds[1].events = POLLRDNORM;
	printf("input login to login again ,regist to register a new account or quit to exit\n");
	while(1)
    {
		bzero(sendbuf,sizeof(sendbuf));
        poll(fds, 2, 4000);
        if(fds[0].revents & POLLIN) //数据可读
        {
			if(user_status != U_ST_LOGOUT && user_status != U_ST_LOGING)
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
				else if(strncmp(stdinbuf,"show group",strlen("show group")) == 0)// 发送show group 命令；
				{
					showgroup();
				}
				else if(strncmp(stdinbuf,"join",strlen("join")) == 0) //join group
				{
					strtok(stdinbuf," ");
					char *str;
					if((str = strtok(NULL," ")) == NULL)
					{
						printf("please input the group name you want to join!\n");
					}
					else
					{
						strcpy(groupname,str);
						joingroup();
					}
				}
				else if(strncmp(stdinbuf,"leave",strlen("leave")) == 0) //join group
				{
					strtok(stdinbuf," ");
					char *str;
					if((str = strtok(NULL," ")) == NULL)
					{
						printf("please input the group name you want to leave!\n");
					}
					else
					{
						strcpy(groupname,str);
						leavegroup();
						
					}
				}
				else if(strncmp(stdinbuf,"create",strlen("create")) == 0) //join group
				{
					strtok(stdinbuf," ");
					char *str;
					if((str = strtok(NULL," ")) == NULL)
					{
						printf("please input the group name you want to leave!\n");
					}
					else
					{
						if(strcmp(str,"group"))
						{
							strcpy(groupname,str);
							creategroup();
						}
						else
							printf("cannot create group!!\n");	
					}
				}
				else if(strncmp(stdinbuf,"delete",strlen("delete")) == 0) //join group
				{
					strtok(stdinbuf," ");
					char *str;
					if((str = strtok(NULL," ")) == NULL)
					{
						printf("please input the group name you want to delete!\n");
					}
					else
					{
						strcpy(groupname,str);
						deletegroup();
						
					}
				}
				else if(strncmp(stdinbuf,"chatgroup",strlen("chatgroup")) == 0) //chatgroup name
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
						user_status = U_ST_GCHAT;
					}
				}
				else if(strncmp(stdinbuf,"chat",strlen("chat")) == 0) //chat name
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
				else if(strncmp(stdinbuf,"send file",strlen("send file")) == 0)
				{

					printf("friend name:");
					fgets(pFileTransmit->recvname,sizeof(pFileTransmit->recvname),stdin);
					pFileTransmit->recvname[strlen(pFileTransmit->recvname)-1] = '\0';
					
					pFileTransmit->status = TRA_ST_PREP;

					strcpy(pFileTransmit->sendname,username);
					// printf("file name:");
					// fgets(filename,sizeof(filename),stdin);
					// filename[strlen(filename)-1] = '\0';
					sprintf(sendbuf,FILE_SEND,pFileTransmit->sendname,pFileTransmit->recvname);
					send(sockfd,sendbuf,strlen(sendbuf),0);
				}
				else if(strncmp(stdinbuf,"group list",strlen("group list"))==0 && user_status == U_ST_GCHAT)
				{
					sprintf(sendbuf,GROUP_LIST,username,friendname);
					send(sockfd,sendbuf,strlen(sendbuf),0);
				}
				else if(user_status == U_ST_CHAT)//chat发送-
				{
					sprintf(sendbuf,SEND_MSG,username,friendname,stdinbuf);
					send(sockfd,sendbuf,strlen(sendbuf),0);
				}
				else if(user_status == U_ST_GCHAT)//chatgroup发送-
				{
					sprintf(sendbuf,GROUP_MSG,username,friendname,stdinbuf);
					send(sockfd,sendbuf,strlen(sendbuf),0);
				}
				else
				{
					printf("input chat somebody to chat ，chat somegroup to chat, input show list to show friend who online\n");
				}
			}
			else
			{
				char *this_status[4] = {"logout","login","loging","chat"};
				fgets(stdinbuf,sizeof(stdinbuf),stdin);
				stdinbuf[strlen(stdinbuf)-1] = '\0';
				if(strncmp(stdinbuf,"login",strlen("login")) ==0)
				{
					if(load_user() == 0)
					{
						sprintf(sendbuf, LOGIN_MSG, username);
						if(send(sockfd, sendbuf, strlen(sendbuf), 0)<0)
							printf("send %d:%s", errno, strerror(errno));
						else
						{
							user_status = U_ST_LOGIN;
							continue;
						}
					}
				}
				else if(strncmp(stdinbuf,"regist",strlen("regist")) == 0)
					regist_user();
				else if(strncmp(stdinbuf,"quit",strlen("quit")) == 0)
					break;
				
				printf("user_status:%s\n",this_status[user_status]);
				printf("input login to login again ,regist to register a new account or quit to exit\n");
			}
        }
        if(fds[1].revents & POLLRDNORM)
        {
            xmlDocPtr doc;   //定义解析文档指针
            xmlNodePtr cur;  //定义结点指针(你需要它为了在各个结点间移动)
            int recvlen = recv(sockfd, recvbuf, BUFFER_SIZE-1, 0);
            if(recvlen <= 0)
			{
				printf("connect is broken!\n");
				user_status = U_ST_LOGOUT;
				exit(0);
			}
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
	pthread_cancel(tid_alive);
	free(pFileTransmit);
	close(sockfd);
	exit(0);
}

int showgroup()
{
	sprintf(sqlcmd,"select * from chatgroup");//打印chatgroup的表
	mysql_query_my(conn,sqlcmd);
	MYSQL_RES *res = mysql_store_result(conn);
	 if(res==NULL)
	{
		printf("select * from error!\n");
		exit(1);
	}
	MYSQL_ROW row;
	while((row = mysql_fetch_row(res))!= NULL)//一行行读取表的内容
	{
		sprintf(sqlcmd, "select user from %s where user='%s'", (char*) row[0],username);
		mysql_query_my(conn, sqlcmd);
		MYSQL_RES * groupres = mysql_store_result(conn);
		//MYSQL_RES *result = mysql_store_result(conn);
		MYSQL_ROW rowgroup= mysql_fetch_row(groupres);
		if(rowgroup == NULL)
			continue;
		printf("%s\n",(char* )row[0]);
	}
	return 0;
	
}

int joingroup()
{
	sprintf(sqlcmd,"select groupname from chatgroup where groupname='%s'",groupname);
	mysql_query_my(conn,sqlcmd);
	MYSQL_RES *res = mysql_store_result(conn);
	if(res==NULL)
	{
		printf("select  error!\n");
		exit(1);
	}
	MYSQL_ROW row = mysql_fetch_row(res);
	if(row == NULL)
	{
		printf("no such group \n");
		return 0;
	}
	else
	{
		sprintf(sqlcmd, "select user from %s where user='%s'", (char*) row[0],username);
		mysql_query_my(conn, sqlcmd);
		MYSQL_RES *grpres = mysql_store_result(conn);
		MYSQL_ROW grprow = mysql_fetch_row(grpres);
		if(grprow ==NULL )//自己不在组里-
		{
			sprintf(sqlcmd,"insert into %s (user) values ('%s')",(char *)row[0],username);
			mysql_query_my(conn,sqlcmd);
			mysql_store_result(conn);
			printf("success\n");
			return 0;
		}
		else
		{
			printf("you have already in this group\n");
		}
	}
	return 0;
}
int leavegroup()
{
	sprintf(sqlcmd,"select groupname from chatgroup where groupname='%s'",groupname);
	mysql_query_my(conn,sqlcmd);
	MYSQL_RES *res = mysql_store_result(conn);
	if(res==NULL)
	{
		printf("select * from error!\n");
		exit(1);
	}
	MYSQL_ROW row;
	row = mysql_fetch_row(res);
	if(row == NULL)	
	{
		printf("no such group\n");	
	}
	else
	{
		sprintf(sqlcmd, "select user from %s where user='%s'", (char*) row[0],username);
		mysql_query_my(conn, sqlcmd);
		MYSQL_RES * grpres = mysql_store_result(conn);
		MYSQL_ROW grprow = mysql_fetch_row(grpres);
		if(grprow == NULL)
		{
			printf("you not in this group");
		}
		else
		{
			sprintf(sqlcmd,"delete from %s where user='%s'",(char *)row[0],username);
			mysql_query_my(conn,sqlcmd);
			printf("delete success\n");
			return 0;
		}
	}	
	return 0;
}
int creategroup()
{
	sprintf(sqlcmd,"select groupname from chatgroup where groupname='%s'",groupname);
	mysql_query_my(conn,sqlcmd);
	MYSQL_RES *res = mysql_store_result(conn);
	if(res==NULL)
	{
		printf("select error!\n");
		exit(1);
	}
	MYSQL_ROW row;
	row = mysql_fetch_row(res);
	if(row == NULL)
	{
		sprintf(sqlcmd, "insert into chatgroup (groupname) value ('%s') ", groupname);
		mysql_query_my(conn, sqlcmd);
		sprintf(sqlcmd, "create table %s(user varchar(20))", groupname);//创建表
		mysql_query_my(conn, sqlcmd);
		printf("create success\n");
		joingroup();
	}
	else
	{
		printf("this group alread exist\n");
	}
	return 0;
}
int deletegroup()
{
	sprintf(sqlcmd,"select groupname from chatgroup where groupname='%s'",groupname);
	mysql_query_my(conn,sqlcmd);
	MYSQL_RES *res = mysql_store_result(conn);
	if(res==NULL)
	{
		printf("select error!\n");
		exit(1);
	}
	MYSQL_ROW row;
	row = mysql_fetch_row(res);
	if(row == NULL)
	{
		printf("no such table\n");
	}
	else
	{
		sprintf(sqlcmd,"select * from %s",groupname);
		mysql_query_my(conn,sqlcmd);
		MYSQL_RES *grpres = mysql_store_result(conn);
		MYSQL_ROW grprow = mysql_fetch_row(grpres);
		if(strcmp((char *)grprow[0],username))
		{
			printf("you over your right\n");
			return 0;
		}
		sprintf(sqlcmd, "delete from chatgroup where groupname=('%s') ", groupname);
		mysql_query_my(conn, sqlcmd);
		sprintf(sqlcmd, "drop table %s", groupname);//创建表
		mysql_query_my(conn, sqlcmd);
		printf("deldete success\n");
	}
	return 0;
}
int load_user()
{	
	char passwd[32];
    while(1)
    {
		printf("username:");
		fgets(username,sizeof(username),stdin);
		username[strlen(username)-1] = '\0';

		bzero(stdinbuf,sizeof(stdinbuf));
		printf("passwd:");
        fgets(passwd, 32, stdin);
		passwd[strlen(passwd)-1] = '\0';

        sprintf(sqlcmd, "select passwd from user where UserName='%s'", username);
        mysql_query_my(conn, sqlcmd);
        MYSQL_RES *res = mysql_store_result(conn);
        if(res==NULL)
		{
			printf("load user error!\n");
			exit(1);
		}
		else
		{
			MYSQL_ROW row = mysql_fetch_row(res);
			if(row!= NULL)
			{
				if(strcmp((char *)row[0],passwd) != 0) 
				{
					printf("passwd error!please input agian\n");
					continue;
				}
				else break;
			}
			else
			{
				printf("this username hasn't regist!\n");
				bzero(username,sizeof(username));
				continue;
			}
		}		
    }	
	return 0;
}

int regist_user()
{
	char regist_username[32] = {0}; 
	char regist_password[32] = {0};
	MYSQL_RES *res;
	mysql_ping(conn);
	printf("regist username :");
	fgets(stdinbuf,sizeof(stdinbuf),stdin);
	stdinbuf[strlen(stdinbuf)-1] = '\0';
	stdinbuf[31] = '\0';
	char *str1 = strtok(stdinbuf," ");
	strcpy(regist_username,str1);
	bzero(stdinbuf,sizeof(stdinbuf));
	
	bzero(sqlcmd,sizeof(sqlcmd));
	sprintf(sqlcmd, "select * from user where UserName='%s'", regist_username);
	mysql_query_my(conn,sqlcmd);
	res = mysql_store_result(conn);
	if(res != NULL)
	{
		MYSQL_ROW row = mysql_fetch_row(res);
		if(row != NULL)
		{
			if(strncmp(row[0],regist_username,strlen(regist_username)) ==0)
			{
				printf("regist error! this username has registed!\n");
				return -1;
			}
		}
	}
	else
	{
		printf("regist error!\n");
		return -1;
	}
	printf("regist password :");
	fgets(stdinbuf,sizeof(stdinbuf),stdin);
	stdinbuf[strlen(stdinbuf)-1] = '\0';
	stdinbuf[31] = '\0';
	char *str2 = strtok(stdinbuf," ");
	
	bzero(stdinbuf,sizeof(stdinbuf));
	printf("confirm password : ");
	fgets(stdinbuf,sizeof(stdinbuf),stdin);
	stdinbuf[strlen(stdinbuf)-1] = '\0';
	stdinbuf[31] = '\0';
	char *str3 = strtok(stdinbuf," ");
	if(strcmp(str2,str3) == 0)
		strcpy(regist_password,str2);
	else
	{
		printf("password is different!\n");
		return -1;
	}
	bzero(sqlcmd,sizeof(sqlcmd));
	sprintf(sqlcmd,"insert into user values('%s','%s')",regist_username,regist_password);
	mysql_query_my(conn,sqlcmd);
	//res = mysql_store_result(conn);
	// if(res == NULL)
	// {
		// printf("unkonwn error! regist error!\n");
		// return -1;
	// }
	printf("regist success!\n");
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
		sleep(100);
		if(user_status == U_ST_LOGIN || user_status == U_ST_CHAT)
		{
			sprintf(alive_buf,ALIVE_MSG,username);
			send(sockfd,alive_buf,strlen(alive_buf),0);
		}
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
	if(xmlStrcmp(cur->name,(const xmlChar *)"Context"))
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
	char buf[1024]={0};//字符
	char str[1024]={0};
	strcpy(str,(char*)contex);
	
	int count;
	for(count = 0;count<COUNTOF(oldstr);count++)
	{
		face(str,oldstr[count],newstr[count],buf);
		strcpy(str,buf);
	}
	printf("%s : %s\n",fromuser,str);               //获取收到的信息
	send(sockfd, "received", strlen("received"), 0);
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
	if(strncmp((char *)error,"loged",strlen("loged"))==0)
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
	if(strcmp((char *)error,"success"))
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
	user_status = U_ST_LOGOUT;
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

int recv_groupmsg(xmlDocPtr doc, xmlNodePtr cur)
{
	xmlChar *group = NULL;
	if((cur = cur->next) == NULL)
		return -1;
	if(xmlStrcmp(cur->name,(const xmlChar *)"Group"))
		return -1;
	group = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
	if(group == NULL)
		return 0;

	xmlChar *fromUser = NULL;
	if((cur = cur->next) == NULL)
		return -1;
	if(xmlStrcmp(cur->name,(const xmlChar *)"FromUser"))
		return -1;
	fromUser = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
	if(fromUser == NULL)
		return 0;
	
	xmlChar *context = NULL;
	if((cur = cur->next) == NULL)
		return -1;
	if(xmlStrcmp(cur->name,(const xmlChar *)"Context"))
		return -1;
	context = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
	if(context == NULL)
		return 0;

	printf("%s/%s:%s\n",group,fromUser,context);
	free(fromUser);
	free(context);
	free(group);
	return 1;
}

int file_send_to(xmlDocPtr doc, xmlNodePtr cur)
{
	//printf("recv filesend\n");
	bzero(sendbuf,sizeof(sendbuf));
	xmlChar *userfrom;
	xmlChar *error;
	if((cur = cur->next) == NULL)
		return -1;
	if(xmlStrcmp(cur->name,(const xmlChar *)"FromUser"))
	{
		if(xmlStrcmp(cur->name,(const xmlChar *)"ERROR"))
			return -1;
		error = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
		printf("receive error : %s\n",error);
		bzero(pFileTransmit,sizeof(struct file_transmit));
		pFileTransmit->status = TRA_ST_REST;
		free(error);
		return 1;
	}
	userfrom = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
	if(userfrom == NULL)
		return -1;
	if(pFileTransmit->status != TRA_ST_REST)
	{
		sprintf(sendbuf,C_FILE_RECV_ERR,username,userfrom,"cannot receive");
		send(sockfd,sendbuf,sizeof(sendbuf),0);
		return 0;
	}
	
	printf("do your want to accept file from %s,press N|n to refuse or other to accept\n",userfrom);
	char c = getchar();
	if(c == 'N' || c == 'n')
	{
		sprintf(sendbuf,C_FILE_RECV_ERR,username,userfrom,"refuse");
		send(sockfd,sendbuf,sizeof(sendbuf),0);
		free(userfrom);
		return 0;
	}
	
	pFileTransmit->status = TRA_ST_PREP;
	strcpy(pFileTransmit->sendname,(char *)userfrom);
	strcpy(pFileTransmit->recvname,username);
	
	pthread_t tid_FileTra;
	pthread_create(&tid_FileTra,NULL,client_file_recv,NULL);
	pthread_detach(tid_FileTra);
	free(userfrom);
	return 0;
}
int file_recv_from(xmlDocPtr doc, xmlNodePtr cur)
{
	bzero(sendbuf,sizeof(sendbuf));
	struct sockaddr_in *file_recv_addr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
	bzero(file_recv_addr,sizeof(struct sockaddr_in));
	file_recv_addr->sin_family = AF_INET;
	
	xmlChar *userto;
	xmlChar *error;
	xmlChar *recv_ip;
	xmlChar *recv_port;
	if((cur = cur->next) == NULL)
		return -1;
	if(xmlStrcmp(cur->name,(const xmlChar *)"FromUser"))
	{
		if(xmlStrcmp(cur->name,(const xmlChar *)"ERROR"))
			return -1;
		error = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
		printf("send error : %s\n",error);
		bzero(pFileTransmit,sizeof(struct file_transmit));
		pFileTransmit->status = TRA_ST_REST;
		free(error);
		return 1;
	}
	userto = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
	if(strcmp((char *)userto,pFileTransmit->recvname)!=0)
	{
		sprintf(sendbuf,C_FILE_SEND_ERR,username,userto,"mismatching");
		send(sockfd,sendbuf,strlen(sendbuf),0);
		bzero(pFileTransmit,sizeof(struct file_transmit));
		pFileTransmit->status = TRA_ST_REST;
		return -1;
	}
	
	if((cur = cur->next) == NULL)
	{
		sprintf(sendbuf,C_FILE_SEND_ERR,username,userto,"misaddr");
		send(sockfd,sendbuf,strlen(sendbuf),0);
		bzero(pFileTransmit,sizeof(struct file_transmit));
		pFileTransmit->status = TRA_ST_REST;
		return -1;
	}
	if((cur = cur->xmlChildrenNode) == NULL)
	{
		sprintf(sendbuf,C_FILE_SEND_ERR,username,userto,"misaddrip");
		send(sockfd,sendbuf,strlen(sendbuf),0);
		bzero(pFileTransmit,sizeof(struct file_transmit));
		pFileTransmit->status = TRA_ST_REST;
		return -1;
	}
	recv_ip = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
	if(recv_ip == NULL)
	{		
		sprintf(sendbuf,C_FILE_SEND_ERR,username,userto,"addrip is NULL");
		send(sockfd,sendbuf,strlen(sendbuf),0);
		bzero(pFileTransmit,sizeof(struct file_transmit));
		pFileTransmit->status = TRA_ST_REST;
		return -1;		
	}
	if((cur = cur->next) == NULL)
	{
		sprintf(sendbuf,C_FILE_SEND_ERR,username,userto,"misaddrport");
		send(sockfd,sendbuf,strlen(sendbuf),0);
		bzero(pFileTransmit,sizeof(struct file_transmit));
		pFileTransmit->status = TRA_ST_REST;
		return -1;
	}
	recv_port = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
	if(recv_port == NULL)
	{		
		sprintf(sendbuf,C_FILE_SEND_ERR,username,userto,"addrport is NULL");
		send(sockfd,sendbuf,strlen(sendbuf),0);
		bzero(pFileTransmit,sizeof(struct file_transmit));
		pFileTransmit->status = TRA_ST_REST;
		return -1;
	}
	
	file_recv_addr->sin_port = htons(atoi((char *)recv_port));
	inet_pton(AF_INET, (char *)recv_ip, &(file_recv_addr->sin_addr));
	
	pthread_t tid_FileTra;
	pthread_create(&tid_FileTra,NULL,client_file_send,(void *)file_recv_addr);
	pthread_detach(tid_FileTra);
	free(userto);
	return 0;
}

void *client_file_recv(void *arg)
{
	struct waving_file file_recv;
	bzero(&file_recv,sizeof(file_recv));
	int sockid_recvfile = socket(AF_INET,SOCK_STREAM,0);
	if(sockid_recvfile < 0)
	{
		sprintf(sendbuf,C_FILE_RECV_ERR,username,pFileTransmit->sendname,"cannot receive");
		send(sockfd,sendbuf,sizeof(sendbuf),0);
		perror("socket");
		bzero(pFileTransmit,sizeof(struct file_transmit));
		pFileTransmit->status = TRA_ST_REST;
		pthread_exit((void *)1);
	}
	int on = 1;
	setsockopt(sockid_recvfile, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	
	struct sockaddr_in file_recv_addr;
	bzero(&file_recv_addr,sizeof(file_recv_addr));
	file_recv_addr.sin_family = AF_INET;
	file_recv_addr.sin_port = htons(11111);
	inet_pton(AF_INET,"127.0.0.1",&file_recv_addr.sin_addr);
	
	if(bind(sockid_recvfile,(struct sockaddr *)&file_recv_addr,sizeof(file_recv_addr)) != 0)
	{
		sprintf(sendbuf,C_FILE_RECV_ERR,username,pFileTransmit->sendname,"cannot receive");
		send(sockfd,sendbuf,sizeof(sendbuf),0);
		perror("connect");
		bzero(pFileTransmit,sizeof(struct file_transmit));
		pFileTransmit->status = TRA_ST_REST;
		pthread_exit((void *)1);
	}
	if(listen(sockid_recvfile,5) < 0)
	{
		sprintf(sendbuf,C_FILE_RECV_ERR,username,pFileTransmit->sendname,"cannot receive");
		send(sockfd,sendbuf,sizeof(sendbuf),0);
		perror("listen");
		bzero(pFileTransmit,sizeof(struct file_transmit));
		pFileTransmit->status = TRA_ST_REST;
		pthread_exit((void *)1);
	}
	pFileTransmit->status = TRA_ST_RUN;
	


	sprintf(sendbuf,FILE_RECV,username,pFileTransmit->sendname,"127.0.0.1","11111");
	
	send(sockfd,sendbuf,strlen(sendbuf),0);
	
	struct sockaddr_in file_send_addr;
	socklen_t addr_lenth = sizeof(file_send_addr);
	int connectid = accept(sockid_recvfile,(struct sockaddr *)&file_send_addr,&addr_lenth);
	
	//printf("client host %d\n",ntohs(file_send_addr.sin_port));    //调试信息
	struct timeval tv = {60,0};
	fd_set readset;
	FD_ZERO(&readset);
	FD_SET(connectid,&readset);
	select(connectid+1,&readset,NULL,NULL,&tv);
	 
	bzero(&pFileTransmit->filename,sizeof(pFileTransmit->filename));
	recv(connectid,pFileTransmit->filename,sizeof(pFileTransmit->filename ),0);
	printf("%s\n",pFileTransmit->filename); //调试信息	
	int filefd = open(pFileTransmit->filename,O_RDWR|O_CREAT,0777);
	if(filefd < 0)
	{
		printf("file create error!\n");
		bzero(pFileTransmit,sizeof(struct file_transmit));
		pFileTransmit->status = TRA_ST_REST;
		close(filefd);
		close(connectid);
		close(sockid_recvfile);
		pthread_exit((void *)0);
	}
	//int lenth;
	FD_ZERO(&readset);
	FD_SET(connectid,&readset);
	select(connectid+1,&readset,NULL,NULL,&tv);
	while(recv(connectid,(char *)&file_recv,sizeof(file_recv),0) > 0)
	{
		//printf("%s:%d:%s\n",file_recv.name,file_recv.lenth,file_recv.data);       //调试信息
		if(strcmp(file_recv.name,pFileTransmit->filename)!=0 )
		{
			printf("filename mismatch\n");
			continue;
		}
		
		write(filefd,file_recv.data,file_recv.lenth);
		bzero(&file_recv,sizeof(file_recv));
		FD_ZERO(&readset);
		FD_SET(connectid,&readset);
		select(connectid+1,&readset,NULL,NULL,&tv);
	}
	printf("file send over\n");
	bzero(pFileTransmit,sizeof(struct file_transmit));
	pFileTransmit->status = TRA_ST_REST;
	close(filefd);
	close(connectid);
	close(sockid_recvfile);
	pthread_exit((void *)0);
}

void *client_file_send(void *arg)
{
	struct sockaddr_in file_recv_addr = *((struct sockaddr_in *)arg);
	struct waving_file sendfile;
	bzero(&sendfile,sizeof(sendfile));
	int filefd;
	int sockid_sendfile = socket(AF_INET,SOCK_STREAM,0);
	if(sockid_sendfile < 0)
	{
		bzero(pFileTransmit,sizeof(struct file_transmit));
		pFileTransmit->status = TRA_ST_REST;
		free(arg);
		close(sockid_sendfile);
		pthread_exit((void *)1);
	}
	if(connect(sockid_sendfile,(struct sockaddr *)&file_recv_addr,sizeof(file_recv_addr)) < 0)
	{
		bzero(pFileTransmit,sizeof(struct file_transmit));
		pFileTransmit->status = TRA_ST_REST;
		free(arg);
		close(sockid_sendfile);
		pthread_exit((void *)1);
	}

	pFileTransmit->status = TRA_ST_RUN;
	while(1)
	{
		printf("file name : ");
		fgets(stdinbuf,sizeof(stdinbuf),stdin);
		stdinbuf[strlen(stdinbuf)-1] = '\0';
		strcpy(pFileTransmit->filename,stdinbuf);
		filefd = open(pFileTransmit->filename,O_RDONLY);
		if(filefd < 0)
		{
			printf("cannot open this file!\n");
			continue;
		}
		else break;
	}
	send(sockid_sendfile,pFileTransmit->filename,strlen(pFileTransmit->filename),0);
	printf("sendfile %s\n",pFileTransmit->filename);
	strcpy(sendfile.name,pFileTransmit->filename);
	
	usleep(1000*1000);
	int lenth;
	while((lenth = read(filefd,sendfile.data,FILE_MAXLEN)) > 0)
	{
		// printf("read lenth:%d\n",lenth);
		// printf("data lenth:%d\n",strlen(sendfile.data));
		strcpy(sendfile.name,pFileTransmit->filename);
		sendfile.lenth = lenth;
		send(sockid_sendfile,(char *)&sendfile,sizeof(sendfile),0);
		bzero(&sendfile,sizeof(sendfile));
	}
	
	
	printf("file send over\n");
	//send(sockid_sendfile,"file send over",strlen("file send over"),0);

	bzero(pFileTransmit,sizeof(struct file_transmit));
	pFileTransmit->status = TRA_ST_REST;
	free(arg);
	close(filefd);
	close(sockid_sendfile);
	pthread_exit((void *)0);
}
//字符
int face(char *str,char*oldstr,char*newstr,char*buf)
{
	char* p;
	if((str==NULL)||(oldstr==NULL)||(newstr==NULL)||(buf==NULL))
		return -1;
	while(1)
	{
		p = strstr(str,oldstr);
		if(p == NULL)
		{
			strcpy(buf,str);
			break;
		}
		bzero(buf,strlen(buf));
		strncpy(buf,str,p-str);
		strcat(buf,newstr);
		p += strlen(oldstr);
		strcat(buf,p);
		strcpy(str,buf);	
	}
	return 0;
}
