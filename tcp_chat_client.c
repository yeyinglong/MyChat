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
#include <sys/ioctl.h>
#include <net/if.h>
#include <libxml/parser.h>  
#include <libxml/xmlmemory.h>
#include <libxml/tree.h>
#include <mysql/mysql.h>
#include "log.h"

int LOG_FLAG = 4;
#define PORT 12321
#define BUFFER_SIZE 512
#define COUNTOF(x) (sizeof(x)/sizeof((x)[0]))

static char sendbuf[BUFFER_SIZE];
static char recvbuf[BUFFER_SIZE];
static char stdinbuf[128];
static char sqlcmd[BUFFER_SIZE];
static MYSQL *conn;       //数据库
static int sockfd;

struct file_transmit{
	int tra_status;
	char sendname[32];
	char recvname[32];
	char filename[32];
};
#define FILE_MAXLEN 1024
struct waving_file{               //用于文件传输的结构体
	char name[32];              //文件名，用于检查传输文件的正确性
	int lenth;                     //文件内容的长度，用于校验信息的完整性
	char data[FILE_MAXLEN];
};
struct chatuser{
	char username[32];
	int u_status;
	char friendname[32];
	char groupname[32];
	struct file_transmit *ftra;
}*ChatUser;
//定义用户登录状态
#define U_ST_LOGIN 1  //已登录
#define U_ST_LOGOUT 0  //未登录
#define U_ST_LOGING 2   //正在登陆
#define U_ST_CHAT 3   //正在聊天   
#define U_ST_GCHAT 4   //正在group聊天  
//定义文件传输状态，正在传输时或正在准备时无法再进行文件传输
#define TRA_ST_RUN 1  //传输状态
#define TRA_ST_REST 0  //文件传输的休息状态
#define TRA_ST_PREP 2  //准备状态，即发送文件传输的信息直到双方建立连接的状态

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
*************join %s : 加入一个指定的组
*************leave %s ： 离开一个指定的组
*************create %s : 创建一个组
*************delete %s : 创建者可以摧毁一个组
*************show group :　显示该用户已加入的组
*************gchat : 指定聊天的对象组
*************group list : 显示组内所有成员
*************show record : 显示用户所有的聊天记录
*************clean record : 清理用户所有聊天记录，只清楚自己发送的
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

int startup_handler(void);
int mysql_query_my(MYSQL *conn, const char *str);
int face(char *str,char*oldstr,char*newstr,char*buf);//表情函数；
int geteth0ip(char *);

void *client_file_recv(void *);
void *client_file_send(void *);
void *client_alive(void *);   //线程函数，用于定时向服务器发送信息

int recv_message(xmlDocPtr, xmlNodePtr);
int login_res(xmlDocPtr doc, xmlNodePtr cur);
int send_res(xmlDocPtr doc, xmlNodePtr cur);
int logout_res(xmlDocPtr doc, xmlNodePtr cur);
int list_res(xmlDocPtr doc, xmlNodePtr cur);
int recv_groupmsg(xmlDocPtr doc, xmlNodePtr cur);
int file_send_to(xmlDocPtr doc, xmlNodePtr cur);
int file_recv_from(xmlDocPtr doc, xmlNodePtr cur);

typedef int (*pfun_xml)(xmlDocPtr,xmlNodePtr);

typedef struct{
	char operator[32];
	pfun_xml func;
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


int load_user();     //用于登陆账号
int login_user();
int regist_user();   //注册账号
int logout_user();
int quit_chat();
int showlist();
int show_record(void);
int clean_record(void );
//int showgrouprecord();
int showgroup();
int joingroup();
int leavegroup();
int creategroup();
int deletegroup();
int grouplist();
int chatgroup();
int sendfile();
int singlechat();
int sendchatmsg();

typedef int (*pfun_cmd)(void);

typedef struct{
	char cmd[32];
	pfun_cmd func;
}cmd_handler_t;

cmd_handler_t cmdin_handler_table[] ={	
	{"logout",logout_user},
	{"show list",showlist},
	{"show group",showgroup},
	{"group list",grouplist},
	{"show record",show_record},
	{"clean record",clean_record},
	{"join",joingroup},
	{"leave",leavegroup},
	{"create",creategroup},
	{"delete",deletegroup},
	{"gchat",chatgroup},
	{"chat",singlechat},
	{"send file",sendfile},
	{"",sendchatmsg}
};

cmd_handler_t cmdout_handler_table[] ={
	{"login",login_user},
	{"regist",regist_user},
	{"quit",quit_chat}
};



int main(int argc, char *argv[])   //指定服务器ip地址
{
	ChatUser = (struct chatuser *)malloc(sizeof(struct chatuser));
	ChatUser->ftra = (struct file_transmit *)malloc(sizeof(struct file_transmit));
    struct pollfd fds[2];
	struct hostent *host;
	struct sockaddr_in serv_addr;
	
	ChatUser->u_status = U_ST_LOGOUT;
	ChatUser->ftra->tra_status = TRA_ST_REST;
    
	startup_handler();
	
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
	
	printf("user_status:logout\n");
	printf("input login to login ,regist to register a new account or quit to exit\n");
	while(1)
    {
		bzero(sendbuf,sizeof(sendbuf));
        poll(fds, 2, 4000);
        if(fds[0].revents & POLLIN) //数据可读
        {
			if(ChatUser->u_status != U_ST_LOGOUT && ChatUser->u_status != U_ST_LOGING)
			{
				fgets(stdinbuf,sizeof(stdinbuf),stdin);
				stdinbuf[strlen(stdinbuf)-1] = '\0';
				int i;	 
				for(i=0; i<COUNTOF(cmdin_handler_table); i++)
				{
					if(strncmp(cmdin_handler_table[i].cmd,stdinbuf,strlen(cmdin_handler_table[i].cmd))==0)
					{
						cmdin_handler_table[i].func();	
						break;
					}	
				}
			}
			else
			{
				char *this_status[4] = {"logout","login","loging","chat"};
				
				fgets(stdinbuf,sizeof(stdinbuf),stdin);
				stdinbuf[strlen(stdinbuf)-1] = '\0';
				if(strncmp(stdinbuf,"quit",strlen("quit")) == 0)
					break;			
				int i;	 
				for(i=0; i<COUNTOF(cmdout_handler_table); i++)
				{
					if(strncmp(cmdout_handler_table[i].cmd,stdinbuf,strlen(cmdout_handler_table[i].cmd))==0)
					{
						cmdout_handler_table[i].func();	
						break;
					}
				}
				printf("user_status:%s\n",this_status[ChatUser->u_status]);
				if(ChatUser->u_status == U_ST_LOGOUT)
					printf("input login to login ,regist to register a new account or quit to exit\n");				
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
				ChatUser->u_status = U_ST_LOGOUT;
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
	free(ChatUser->ftra);
	free(ChatUser);
	close(sockfd);
	exit(0);
}

int singlechat()
{
	strtok(stdinbuf," ");
	char *str;
	if((str = strtok(NULL," ")) == NULL)
	{
		printf("please input the name you want to chat with!\n");
		return -1;
	}
	strcpy(ChatUser->friendname,str);
	ChatUser->u_status = U_ST_CHAT;
	return 0;
}
int showgroup()
{
	sprintf(sqlcmd,"select * from chatgroup");//打印chatgroup的表
	mysql_query_my(conn,sqlcmd);
	MYSQL_RES *res = mysql_store_result(conn);
	 if(res==NULL)
	{
		printf("no group!\n");
		return -1;
	}
	MYSQL_ROW row;
	while((row = mysql_fetch_row(res))!= NULL)//一行行读取表的内容
	{
		sprintf(sqlcmd, "select user from %s where user='%s'", (char*) row[0],ChatUser->username);
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
int chatgroup()
{
	strtok(stdinbuf," ");
	char *str;
	if((str = strtok(NULL," ")) == NULL)
	{
		printf("please input the group you want to chat with!\n");
		return -1;
	}
	strcpy(ChatUser->groupname,str);
	sprintf(sqlcmd,"select groupname from chatgroup where groupname='%s'",ChatUser->groupname);
	mysql_query_my(conn,sqlcmd);
	MYSQL_RES *res = mysql_store_result(conn);
	if(res==NULL)
	{
		printf("select * from error!\n");
		return -1;
	}
	MYSQL_ROW row;
	row = mysql_fetch_row(res);
	if(row == NULL)	
	{
		printf("no such group\n");	
		return -1;
	}
	else
	{
		sprintf(sqlcmd, "select user from %s where user='%s'", (char*) row[0],ChatUser->username);
		mysql_query_my(conn, sqlcmd);
		MYSQL_RES * grpres = mysql_store_result(conn);
		MYSQL_ROW grprow = mysql_fetch_row(grpres);
		if(grprow == NULL)
		{
			printf("you not in this group");
			return -1;
		}	
		ChatUser->u_status = U_ST_GCHAT;	
	}	
	return 0;
}
int joingroup()
{
	strtok(stdinbuf," ");
	char *str;
	if((str = strtok(NULL," ")) == NULL)
	{
		printf("please input the group name you want to join!\n");
		return -1;
	}
	sprintf(sqlcmd,"select groupname from chatgroup where groupname='%s'",str);
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
		sprintf(sqlcmd, "select user from %s where user='%s'", (char*) row[0],ChatUser->username);
		mysql_query_my(conn, sqlcmd);
		MYSQL_RES *grpres = mysql_store_result(conn);
		MYSQL_ROW grprow = mysql_fetch_row(grpres);
		if(grprow ==NULL )//自己不在组里-
		{
			sprintf(sqlcmd,"insert into %s (user) values ('%s')",(char *)row[0],ChatUser->username);
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
	strtok(stdinbuf," ");
	char *str;
	if((str = strtok(NULL," ")) == NULL)
	{
		printf("please input the group name you want to leave!\n");
		return -1;
	}	
	sprintf(sqlcmd,"select groupname from chatgroup where groupname='%s'",str);
	mysql_query_my(conn,sqlcmd);
	MYSQL_RES *res = mysql_store_result(conn);
	if(res==NULL)
	{
		printf("select * from error!\n");
		return -1;
	}
	MYSQL_ROW row;
	row = mysql_fetch_row(res);
	if(row == NULL)	
	{
		printf("no such group\n");	
	}
	else
	{
		sprintf(sqlcmd, "select user from %s where user='%s'", (char*) row[0],ChatUser->username);
		mysql_query_my(conn, sqlcmd);
		MYSQL_RES * grpres = mysql_store_result(conn);
		MYSQL_ROW grprow = mysql_fetch_row(grpres);
		if(grprow == NULL)
		{
			printf("you not in this group");
		}
		else
		{
			sprintf(sqlcmd,"delete from %s where user='%s'",(char *)row[0],ChatUser->username);
			mysql_query_my(conn,sqlcmd);
			printf("delete success\n");
			if(ChatUser->u_status == U_ST_GCHAT)
				ChatUser->u_status = U_ST_LOGIN;
		}
	}	
	return 0;
}
int creategroup()
{
	strtok(stdinbuf," ");
	char *str;
	if((str = strtok(NULL," ")) == NULL)
	{
		printf("please input the group name you want to leave!\n");
		return -1;
	}

	if(strcmp(str,"group") == 0)
	{
		printf("unable name!\n");
		return -1;
	}
	sprintf(sqlcmd,"select groupname from chatgroup where groupname='%s'",str);
	mysql_query_my(conn,sqlcmd);
	MYSQL_RES *res = mysql_store_result(conn);
	if(res==NULL)
	{
		printf("select error!\n");
		return -1;
	}
	MYSQL_ROW row;
	row = mysql_fetch_row(res);
	if(row == NULL)
	{
		sprintf(sqlcmd, "insert into chatgroup (groupname) value ('%s') ", str);
		mysql_query_my(conn, sqlcmd);
		sprintf(sqlcmd, "create table %s(user varchar(20))", str);//创建表
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
	strtok(stdinbuf," ");
	char *str;
	if((str = strtok(NULL," ")) == NULL)
	{
		printf("please input the group name you want to delete!\n");
		return -1;
	}

	sprintf(sqlcmd,"select groupname from chatgroup where groupname='%s'",str);
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
		sprintf(sqlcmd,"select * from %s",str);
		mysql_query_my(conn,sqlcmd);
		MYSQL_RES *grpres = mysql_store_result(conn);
		MYSQL_ROW grprow = mysql_fetch_row(grpres);
		if(strcmp((char *)grprow[0],ChatUser->username))
		{
			printf("over your right\n");
			return 0;
		}
		sprintf(sqlcmd, "delete from chatgroup where groupname=('%s') ", str);
		mysql_query_my(conn, sqlcmd);
		sprintf(sqlcmd, "drop table %s", str);
		mysql_query_my(conn, sqlcmd);
		printf("deldete success\n");
		if(ChatUser->u_status == U_ST_GCHAT)
			ChatUser->u_status = U_ST_LOGIN;
	}
	return 0;
}
int load_user()
{	
	char passwd[32];
    while(1)
    {
		printf("username:");
		fgets(ChatUser->username,sizeof(ChatUser->username),stdin);
		ChatUser->username[strlen(ChatUser->username)-1] = '\0';

		bzero(stdinbuf,sizeof(stdinbuf));
		printf("passwd:");
        fgets(passwd, 32, stdin);
		passwd[strlen(passwd)-1] = '\0';

        sprintf(sqlcmd, "select passwd from user where UserName='%s'", ChatUser->username);
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
				bzero(ChatUser->username,sizeof(ChatUser->username));
				continue;
			}
		}		
    }	
	return 0;
}
int login_user()
{
	if(load_user() == 0)
	{
		sprintf(sendbuf, LOGIN_MSG, ChatUser->username);
		if(send(sockfd, sendbuf, strlen(sendbuf), 0)<0)
			printf("send %d:%s", errno, strerror(errno));
		else
			ChatUser->u_status = U_ST_LOGIN;
	}
	return 0;
}
int sendfile()
{
	printf("friend name:");
	fgets(ChatUser->ftra->recvname,sizeof(ChatUser->ftra->recvname),stdin);
	ChatUser->ftra->recvname[strlen(ChatUser->ftra->recvname)-1] = '\0';
	
	ChatUser->ftra->tra_status = TRA_ST_PREP;

	strcpy(ChatUser->ftra->sendname,ChatUser->username);

	sprintf(sendbuf,FILE_SEND,ChatUser->ftra->sendname,ChatUser->ftra->recvname);
	send(sockfd,sendbuf,strlen(sendbuf),0);
	return 0;
}
int sendchatmsg()
{
	if(ChatUser->u_status == U_ST_CHAT)//chat发送-
	{
		bzero(sqlcmd,sizeof(sqlcmd));
		sprintf(sqlcmd,"insert into chatrecord values('%s','%s','%s')",\
			ChatUser->username,ChatUser->friendname,stdinbuf);
		if(mysql_query_my(conn,sqlcmd) <0)
		{
			printf("record error!\n");
		}
		sprintf(sendbuf,SEND_MSG,ChatUser->username,ChatUser->friendname,stdinbuf);
		send(sockfd,sendbuf,strlen(sendbuf),0);
	}
	else if(ChatUser->u_status == U_ST_GCHAT)//chatgroup发送-
	{
		bzero(sqlcmd,sizeof(sqlcmd));
		sprintf(sqlcmd,"insert into chatrecord values('%s','%s','%s')",\
			ChatUser->username,ChatUser->groupname,stdinbuf);
		if(mysql_query_my(conn,sqlcmd) <0)
		{
			printf("record error!\n");
		}
		sprintf(sendbuf,GROUP_MSG,ChatUser->username,ChatUser->groupname,stdinbuf);
		send(sockfd,sendbuf,strlen(sendbuf),0);
	}
	else
	{
		printf("input chat somebody to chat ，chat somegroup to chat, input show list to show friend who online\n");
	}
	return 0;
}
int grouplist()
{
	sprintf(sendbuf,GROUP_LIST,ChatUser->username,ChatUser->groupname);
	send(sockfd,sendbuf,strlen(sendbuf),0);
	return 0;
}
int logout_user()
{
	sprintf(sendbuf,LOGOUT_MSG,ChatUser->username);
	send(sockfd,sendbuf,strlen(sendbuf),0);
	ChatUser->u_status = U_ST_LOGOUT;	
	return 0;
}
int show_record(void)
{
	sprintf(sqlcmd,"select * from chatrecord where fname='%s' or tname='%s'",\
		ChatUser->username,ChatUser->username);//打印chatrecord的表
	mysql_query_my(conn,sqlcmd);
	MYSQL_RES *res = mysql_store_result(conn);
	 if(res==NULL)
	{
		printf("no record!\n");
		return -1;
	}
	MYSQL_ROW row;
	while((row = mysql_fetch_row(res))!= NULL)//一行行读取表的内容
	{
		// sprintf(sqlcmd, "select user from %s where user='%s'", (char*) row[0],username);
		// mysql_query_my(conn, sqlcmd);
		// MYSQL_RES * groupres = mysql_store_result(conn);
		// MYSQL_ROW rowgroup= mysql_fetch_row(groupres);
		// if(rowgroup == NULL)
			// continue;
		printf("%s >> %s : %s\n",(char* )row[0],(char *)row[1],(char *)row[2]);
	}
	return 0;
	
}
int showlist()
{
	sprintf(sendbuf,REQLIST,ChatUser->username);
	send(sockfd,sendbuf,strlen(sendbuf),0);
	return 0;
}

int clean_record(void)
{
	sprintf(sqlcmd,"delete from chatrecord where fname='%s'",ChatUser->username);//打印chatrecord的表
	mysql_query_my(conn,sqlcmd);
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
		if(ChatUser->u_status == U_ST_LOGIN || ChatUser->u_status == U_ST_CHAT || ChatUser->u_status == U_ST_GCHAT)
		{
			sprintf(alive_buf,ALIVE_MSG,ChatUser->username);
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
	//bzero(sqlcmd,sizeof(sqlcmd));
	// sprintf(sqlcmd,"insert into chatrecord values('%s','%s','%s')",username,(char *)fromuser,stdinbuf);
	// if(mysql_query_my(conn,sqlcmd) <0)
	// {
		// printf("record error!\n");
	// }
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
		ChatUser->u_status = U_ST_LOGOUT;
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
	ChatUser->u_status = U_ST_LOGOUT;
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
		bzero(ChatUser->ftra,sizeof(struct file_transmit));
		ChatUser->ftra->tra_status = TRA_ST_REST;
		free(error);
		return 1;
	}
	userfrom = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
	if(userfrom == NULL)
		return -1;
	if(ChatUser->ftra->tra_status != TRA_ST_REST)
	{
		sprintf(sendbuf,C_FILE_RECV_ERR,ChatUser->username,userfrom,"cannot receive");
		send(sockfd,sendbuf,sizeof(sendbuf),0);
		return 0;
	}
	
	printf("do your want to accept file from %s,press N|n to refuse or other to accept\n",userfrom);
	char c = getchar();
	if(c == 'N' || c == 'n')
	{
		sprintf(sendbuf,C_FILE_RECV_ERR,ChatUser->username,userfrom,"refuse");
		send(sockfd,sendbuf,sizeof(sendbuf),0);
		free(userfrom);
		return 0;
	}
	
	ChatUser->ftra->tra_status = TRA_ST_PREP;
	strcpy(ChatUser->ftra->sendname,(char *)userfrom);
	strcpy(ChatUser->ftra->recvname,ChatUser->username);
	
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
		bzero(ChatUser->ftra,sizeof(struct file_transmit));
		ChatUser->ftra->tra_status = TRA_ST_REST;
		free(error);
		return 1;
	}
	userto = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
	if(strcmp((char *)userto,ChatUser->ftra->recvname)!=0)
	{
		sprintf(sendbuf,C_FILE_SEND_ERR,ChatUser->username,userto,"mismatching");
		send(sockfd,sendbuf,strlen(sendbuf),0);
		bzero(ChatUser->ftra,sizeof(struct file_transmit));
		ChatUser->ftra->tra_status = TRA_ST_REST;
		return -1;
	}
	
	if((cur = cur->next) == NULL)
	{
		sprintf(sendbuf,C_FILE_SEND_ERR,ChatUser->username,userto,"misaddr");
		send(sockfd,sendbuf,strlen(sendbuf),0);
		bzero(ChatUser->ftra,sizeof(struct file_transmit));
		ChatUser->ftra->tra_status = TRA_ST_REST;
		return -1;
	}
	if((cur = cur->xmlChildrenNode) == NULL)
	{
		sprintf(sendbuf,C_FILE_SEND_ERR,ChatUser->username,userto,"misaddrip");
		send(sockfd,sendbuf,strlen(sendbuf),0);
		bzero(ChatUser->ftra,sizeof(struct file_transmit));
		ChatUser->ftra->tra_status = TRA_ST_REST;
		return -1;
	}
	recv_ip = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
	if(recv_ip == NULL)
	{		
		sprintf(sendbuf,C_FILE_SEND_ERR,ChatUser->username,userto,"addrip is NULL");
		send(sockfd,sendbuf,strlen(sendbuf),0);
		bzero(ChatUser->ftra,sizeof(struct file_transmit));
		ChatUser->ftra->tra_status = TRA_ST_REST;
		return -1;		
	}
	if((cur = cur->next) == NULL)
	{
		sprintf(sendbuf,C_FILE_SEND_ERR,ChatUser->username,userto,"misaddrport");
		send(sockfd,sendbuf,strlen(sendbuf),0);
		bzero(ChatUser->ftra,sizeof(struct file_transmit));
		ChatUser->ftra->tra_status = TRA_ST_REST;
		return -1;
	}
	recv_port = xmlNodeListGetString(doc,cur->xmlChildrenNode,1);
	if(recv_port == NULL)
	{		
		sprintf(sendbuf,C_FILE_SEND_ERR,ChatUser->username,userto,"addrport is NULL");
		send(sockfd,sendbuf,strlen(sendbuf),0);
		bzero(ChatUser->ftra,sizeof(struct file_transmit));
		ChatUser->ftra->tra_status = TRA_ST_REST;
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
		sprintf(sendbuf,C_FILE_RECV_ERR,ChatUser->username,ChatUser->ftra->sendname,"cannot receive");
		send(sockfd,sendbuf,sizeof(sendbuf),0);
		perror("socket");
		bzero(ChatUser->ftra,sizeof(struct file_transmit));
		ChatUser->ftra->tra_status = TRA_ST_REST;
		pthread_exit((void *)1);
	}
	int on = 1;
	setsockopt(sockid_recvfile, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	
	char eth0_ip[INET_ADDRSTRLEN];
	geteth0ip(eth0_ip);
	struct sockaddr_in file_recv_addr;
	bzero(&file_recv_addr,sizeof(file_recv_addr));
	file_recv_addr.sin_family = AF_INET;
	file_recv_addr.sin_port = htons(11111);
	inet_pton(AF_INET,eth0_ip,&file_recv_addr.sin_addr);
	
	if(bind(sockid_recvfile,(struct sockaddr *)&file_recv_addr,sizeof(file_recv_addr)) != 0)
	{
		sprintf(sendbuf,C_FILE_RECV_ERR,ChatUser->username,ChatUser->ftra->sendname,"cannot receive");
		send(sockfd,sendbuf,sizeof(sendbuf),0);
		perror("connect");
		bzero(ChatUser->ftra,sizeof(struct file_transmit));
		ChatUser->ftra->tra_status = TRA_ST_REST;
		pthread_exit((void *)1);
	}
	if(listen(sockid_recvfile,5) < 0)
	{
		sprintf(sendbuf,C_FILE_RECV_ERR,ChatUser->username,ChatUser->ftra->sendname,"cannot receive");
		send(sockfd,sendbuf,sizeof(sendbuf),0);
		perror("listen");
		bzero(ChatUser->ftra,sizeof(struct file_transmit));
		ChatUser->ftra->tra_status = TRA_ST_REST;
		pthread_exit((void *)1);
	}
	ChatUser->ftra->tra_status = TRA_ST_RUN;
	


	sprintf(sendbuf,FILE_RECV,ChatUser->username,ChatUser->ftra->sendname,eth0_ip,"11111");
	
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
	 
	bzero(&ChatUser->ftra->filename,sizeof(ChatUser->ftra->filename));
	recv(connectid,ChatUser->ftra->filename,sizeof(ChatUser->ftra->filename ),0);
	printf("%s\n",ChatUser->ftra->filename); //调试信息	
	int filefd = open(ChatUser->ftra->filename,O_RDWR|O_CREAT|O_TRUNC,0777);
	if(filefd < 0)
	{
		printf("file create error!\n");
		bzero(ChatUser->ftra,sizeof(struct file_transmit));
		ChatUser->ftra->tra_status = TRA_ST_REST;
		close(filefd);
		close(connectid);
		close(sockid_recvfile);
		pthread_exit((void *)0);
	}
	int filelenth = 0;
	int lenth;
	FD_ZERO(&readset);
	FD_SET(connectid,&readset);
	select(connectid+1,&readset,NULL,NULL,&tv);
	while((lenth = recv(connectid,(char *)&(file_recv.data),sizeof(file_recv.data),0)) > 0)
	{
		//printf("%d\n",lenth);
//		printf("%s:%d:\n",file_recv.name,file_recv.lenth);       //调试信息
//		if(strcmp(file_recv.name,pFileTransmit->filename)!=0 )
//		{
//			printf("filename mismatch\n");
//			continue;
//		}
		filelenth+=lenth;
		write(filefd,file_recv.data,lenth);
		bzero(&file_recv,sizeof(file_recv));
		FD_ZERO(&readset);
		FD_SET(connectid,&readset);
		select(connectid+1,&readset,NULL,NULL,&tv);
	}
	printf("file receive over,all %d received\n",filelenth);
	bzero(ChatUser->ftra,sizeof(struct file_transmit));
	ChatUser->ftra->tra_status = TRA_ST_REST;
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
		bzero(ChatUser->ftra,sizeof(struct file_transmit));
		ChatUser->ftra->tra_status = TRA_ST_REST;
		free(arg);
		close(sockid_sendfile);
		pthread_exit((void *)1);
	}
	if(connect(sockid_sendfile,(struct sockaddr *)&file_recv_addr,sizeof(file_recv_addr)) < 0)
	{
		bzero(ChatUser->ftra,sizeof(struct file_transmit));
		ChatUser->ftra->tra_status = TRA_ST_REST;
		free(arg);
		close(sockid_sendfile);
		pthread_exit((void *)1);
	}

	ChatUser->ftra->tra_status = TRA_ST_RUN;
	while(1)
	{
		printf("file name : ");
		fgets(stdinbuf,sizeof(stdinbuf),stdin);
		stdinbuf[strlen(stdinbuf)-1] = '\0';
		strcpy(ChatUser->ftra->filename,stdinbuf);
		filefd = open(ChatUser->ftra->filename,O_RDONLY);
		if(filefd < 0)
		{
			printf("cannot open this file!\n");
			continue;
		}
		else break;
	}
	send(sockid_sendfile,ChatUser->ftra->filename,strlen(ChatUser->ftra->filename),0);
	printf("sendfile %s\n",ChatUser->ftra->filename);
	strcpy(sendfile.name,ChatUser->ftra->filename);
	
	usleep(1000*1000);
	int filelenth = 0;
	int lenth;
	while((lenth = read(filefd,sendfile.data,FILE_MAXLEN)) > 0)
	{
		filelenth += lenth;
		//printf("read lenth:%d\n",lenth);
		// printf("data lenth:%d\n",strlen(sendfile.data));
		strcpy(sendfile.name,ChatUser->ftra->filename);
		sendfile.lenth = lenth;
		send(sockid_sendfile,(char *)&(sendfile.data),lenth,0);
		bzero(&sendfile,sizeof(sendfile));
	}
	printf("file send over , all %d sent\n",filelenth);

	bzero(ChatUser->ftra,sizeof(struct file_transmit));
	ChatUser->ftra->tra_status = TRA_ST_REST;
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

int geteth0ip(char *eth0_ip)               //获取本地eth0
{
	int ret;
	int sockfd_geteth0;
	struct sockaddr_in *sin;
	struct ifreq ifr_ip;

	sockfd_geteth0 = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd_geteth0 < 0)
	{	
		printf("socket error\n");
		sprintf(sendbuf,C_FILE_RECV_ERR,ChatUser->username,ChatUser->ftra->sendname,"cannot receive");
		send(sockfd,sendbuf,sizeof(sendbuf),0);
		bzero(ChatUser->ftra,sizeof(struct file_transmit));
		ChatUser->ftra->tra_status = TRA_ST_REST;
		pthread_exit((void *)1);
	}

	memset(&ifr_ip, 0, sizeof(ifr_ip));
	strncpy(ifr_ip.ifr_name, "eth0", sizeof(ifr_ip.ifr_name) - 1);

	ret = ioctl(sockfd_geteth0, SIOCGIFADDR, &ifr_ip);
	if(ret < 0)
	{	
		printf("ioctl error\n");
		sprintf(sendbuf,C_FILE_RECV_ERR,ChatUser->username,ChatUser->ftra->sendname,"cannot receive");
		send(sockfd,sendbuf,sizeof(sendbuf),0);
		bzero(ChatUser->ftra,sizeof(struct file_transmit));
		ChatUser->ftra->tra_status = TRA_ST_REST;
		pthread_exit((void *)1);
	}

	sin = (struct sockaddr_in *)&ifr_ip.ifr_addr;
	strcpy(eth0_ip, inet_ntoa(sin->sin_addr));
	//printf("local ip:%s\n", ipaddr);
	close(sockfd_geteth0);
	return 0;
}   

int quit_chat()
{
	return 0;
}