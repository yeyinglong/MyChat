#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <libxml/parser.h>  
#include <libxml/xmlmemory.h>
#include <libxml/tree.h>


#include <mysql/mysql.h>

#include "log.h"
#include "service.h"

int LOG_FLAG = 4;
const char SEND_TEXT[]={
	"<xml>"
	"<CMD>msg</CMD>"
	"<FromUser>%s</FromUser>"
	"<Context><![CDATA[%s]]></Context>"
	"</xml>"
};

const char GROUP_TEXT[]={
	"<xml>"
	"<CMD>groupmsg</CMD>"
	"<Group>%s</Group>"
	"<FromUser>%s</FromUser>"
	"<Context><![CDATA[%s]]></Context>"
	"</xml>"
};

const char SEND_RES[]={
	"<xml>"
	"<CMD>res</CMD>"
	"<ERROR>%s</ERROR>"
	"</xml>"
};

const char LOGIN_RES[]={
	"<xml>"
	"<CMD>Login</CMD>"
	"<ERROR>%s</ERROR>"
	"</xml>"
};

const char LOGOUT_RES[]={
	"<xml>"
	"<CMD>Logout</CMD>"
	"<ERROR>%s</ERROR>"
	"</xml>"
};

const char LIST_RES[]={
	"<xml>"
	"<CMD>ReqList</CMD>"
	"<User>%s</User>"
	"</xml>"
};

const char GROUP_LIST_RES[]={
	"<xml>"
	"<CMD>GroupList</CMD>"
	"<User>%s</User>"
	"</xml>"
};

const char ALIVE_RES[]={
	"<xml>"
	"<CMD>Alive</CMD>"
	"<ERROR>%s</ERROR>"
	"</xml>"
};


#if 1
const char FILE_SEND_TO[]={    //服务器接到发送端请求后，发给接收端的包
	"<xml>"
	"<CMD>FileSend</CMD>"
	"<FromUser>%s</FromUser>"
	"</xml>"
};

const char FILE_RECV_FROM[]={   //服务器接到接收端准备就绪的信息后，发给发送端的包
	"<xml>"
	"<CMD>FileRecv</CMD>"
	"<FromUser>%s</FromUser>"
	"<ADDR>"
		"<IP>%s</IP>"
		"<PORT>%s</PORT>"
	"</ADDR>"
	"</xml>"
};

const char FILE_SEND_ERR[]={    //接收端出现异常时，服务器反馈给发送端的包
	"<xml>"
	"<CMD>FileSend</CMD>"
	"<ERROR>%s</ERROR>"
	"</xml>"
};

const char FILE_RECV_ERR[]={      //发送端出现异常时，服务器反馈给接收端的包
	"<xml>"
	"<CMD>FileRecv</CMD>"
	"<ERROR>%s</ERROR>"
	"</xml>"
};

#endif

const char KILL_RES[]={
	"<xml>"
	"<CMD>KILL</CMD>"
	"<ERROR>%s</ERROR>"
	"</xml>"
};
const char SS_RES[]={
	"<xml>"
	"<CMD>%s</CMD>"
	"<ERROR>%s</ERROR>"
	"</xml>"
};
#define COUNTOF(x) (sizeof(x)/sizeof((x)[0]))
#define SEND_BUF_SIZE 1024
#define RECV_BUF_SIZE 1024
static char sendbuf[SEND_BUF_SIZE];
static char recvbuf[SEND_BUF_SIZE];
char sqlcmd[SEND_BUF_SIZE];
MYSQL *conn;       //数据库

typedef struct {
	struct list_head entry;
	pClient pclt;
	char *userName;
}Chater, *pChater;

static pChater pcht = NULL;
static LIST_HEAD(head);        //定义链表头

/*服务器启动时调用函数*/
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

/*新客户端接入时调用函数*/
int accept_handler(pClient pclt)
{
	return 0;
}

/*客户端断开连接调用函数*/
int close_handler(pClient pclt)
{
	struct list_head *pos, *tmpos;
	list_for_each_safe(pos, tmpos, &head)    //查询设备是否已经连接
	{
		pcht = list_entry(pos, Chater, entry);
		if(pcht->pclt == pclt)
		{
			sprintf(sendbuf, LOGOUT_RES, "success");
			if(send(pclt->fd, sendbuf, strlen(sendbuf), 0))
			{
				LOG_ERR("%s:%d send",__func__,__LINE__);
			}
			list_del(pos);
			free(pos);
			pos = NULL;
			break;
		}
	}
	return 0;
}

//关闭服务器
int close_service(pClient pclt, xmlDocPtr doc, xmlNodePtr cur)
{
	if (xmlStrcmp(cur->name, (const xmlChar *)"close") == 0)
    {
		//本地客户端才能关闭服务器
		if(pclt->client_addr.sin_addr.s_addr == inet_addr("127.0.0.1"))
			return 1;
		else
			return 0;
    }
    else
	{
		return -1;
	}
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


//从当前节点获取内容，获取成功返回内容指针
xmlChar* xmlGetNodeText(xmlDocPtr doc, xmlNodePtr cur, const char *name)
{
	xmlChar* text = NULL;
	if(cur == NULL)
	{
		LOG_WARN("xml end of list");
	}
	else if(xmlStrcmp(cur->name, (const xmlChar *)name) == 0)
	{
		text = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
		if(text != NULL)
		{
			return text;
		}
		else
		{
			LOG_WARN("xml node %s no context", name);
		}
	}
	else
	{
		LOG_WARN("xml no this node %s but %s", name, (char*)cur->name);
	}
	return NULL;
}

int transmit_msg(pClient pclt, xmlDocPtr doc, xmlNodePtr cur, xmlChar *fromUser)
{
	char res[16] = "success";
	cur = cur->next;
	xmlChar *toUser = xmlGetNodeText(doc, cur, "ToUser");
	if(toUser == NULL)
		return 0;
		
	cur = cur->next;
	xmlChar *context = xmlGetNodeText(doc, cur, "Context");
	if(context == NULL)
	{
		xmlFree(toUser);
		return 0;
	}

	struct list_head *pos;
	list_for_each(pos, &head)    //查询设备是否已经连接
	{
		pcht = list_entry(pos, Chater, entry);
		if(strcmp(pcht->userName, (const char*)toUser) == 0)
		{
			sprintf(sendbuf,SEND_TEXT,fromUser,context);
			if((send(pcht->pclt->fd, sendbuf, strlen(sendbuf), 0)) == -1)
			{
				LOG_ERR("%s:%d send",__func__,__LINE__);
				strcpy(res, "SendError");
				break;
			}
			if((recv(pcht->pclt->fd, recvbuf, RECV_BUF_SIZE, 0)) < 0)
			{
				LOG_ERR("%s:%d recv",__func__,__LINE__);
				strcpy(res, "RespondError");
			}
			break;
		}
	}
	if(pos ==  &head)
	{
		strcpy(res, "friendLogout");
	}
	LOG_DBG("message status %s", res);
	sprintf(sendbuf, SEND_RES, res);
	if(send(pclt->fd, sendbuf, strlen(sendbuf), 0) < 0)
		LOG_ERR("%s:%d send",__func__,__LINE__);
	xmlFree(toUser); 
	xmlFree(context); 
	return 0;
}

int user_login(pClient pclt, xmlDocPtr doc, xmlNodePtr cur, xmlChar *fromUser)
{
	LOG_INFO("%s try login", fromUser);
	struct list_head *pos;
	list_for_each(pos, &head)    //查询设备是否已经连接
	{
		pcht = list_entry(pos, Chater, entry);
		if(strcmp(pcht->userName, (const char*)fromUser) == 0)
		{
			LOG_INFO("%s has loged", fromUser);
			sprintf(sendbuf,LOGIN_RES,"loged");
			if((send(pclt->fd, sendbuf, strlen(sendbuf), 0)) == -1)
			{
				LOG_ERR("%s:%d send",__func__,__LINE__);
			}
			break;
		}
	}
	if(pos == &head)
	{
		pcht = (pChater)malloc(sizeof(Chater));
		pcht->pclt = pclt;
		pcht->userName = (char*)malloc(strlen((char*)fromUser)+1);
		strcpy(pcht->userName, (char *)fromUser);
		list_add_head(&(pcht->entry),&head);
		sprintf(sendbuf,LOGIN_RES,"success");
		if((send(pclt->fd, sendbuf, strlen(sendbuf), 0)) == -1)
		{
			LOG_ERR("%s:%d send",__func__,__LINE__);
		}
		else
			LOG_INFO("%s login success", fromUser);
	}
	return 0;
}

int user_logout(pClient pclt, xmlDocPtr doc, xmlNodePtr cur, xmlChar *fromUser)
{
	char res[16] = "success";
	struct list_head *pos, *tmpos;
	list_for_each_safe(pos, tmpos, &head)    //查询设备是否已经连接
	{
		pcht = list_entry(pos, Chater, entry);
		if(strcmp(pcht->userName, (const char*)fromUser) == 0)
		{
			list_del(pos);
			free(pcht);
			pcht = NULL;
			break;
		}
	}
	if(pos == &head)
	{
		strcpy(res, "NotLogin");
	}
	sprintf(sendbuf, LOGOUT_RES, res);
	if(send(pclt->fd, sendbuf, strlen(sendbuf), 0))
	{
		LOG_ERR("%s:%d send",__func__,__LINE__);
	}
	return 0;
}

/*通过ss程序让用户下线*/
int ss_QUITUSER(pClient pclt,xmlDocPtr doc,xmlNodePtr cur,xmlChar*fromss)
{
	char res[16] = "success";
	cur = cur->next;
	xmlChar *toUser = xmlGetNodeText(doc, cur, "ToUser");
	if(toUser == NULL)
		return 0;
	LOG_INFO("ss try to kill %s", toUser);
	struct list_head*pos,*tmpos;
	list_for_each_safe(pos,tmpos,&head)
	{
		pcht=list_entry(pos,Chater,entry);
		if(strcmp(pcht->userName,(const char*)toUser)==0)
		{
			list_del(pos);
			free(pcht);
			LOG_INFO("ss:%s Logout", toUser);
			pcht=NULL;
			break;
		}	
	}	
	if(pos==&head)
	{
		LOG_DBG("ss:%s NotLogin", toUser);
		strcpy(res,"NotLogin");
	}
	sprintf(sendbuf,LOGOUT_RES,res);
	if(send(pclt->fd,sendbuf,strlen(sendbuf),0))
	{
		LOG_ERR("%s:%d send",__func__,__LINE__);
	}
	xmlFree(toUser);
	return 0;
}


int user_ReqList(pClient pclt, xmlDocPtr doc, xmlNodePtr cur, xmlChar *fromUser)
{
	struct list_head *pos, *tmpos;
	LOG_INFO("FromUser:%s replist", fromUser);
	list_for_each_safe(pos, tmpos, &head)    //查询设备是否已经连接
	{
		pcht = list_entry(pos, Chater, entry);
		if(strcmp(pcht->userName, (const char*)fromUser) == 0)
			continue;
		sprintf(sendbuf, LIST_RES, pcht->userName);
		if(send(pclt->fd, sendbuf, strlen(sendbuf), 0) < 0)
		{
			LOG_ERR("%s:%d send",__func__,__LINE__);
		}
		if(recv(pclt->fd, recvbuf, RECV_BUF_SIZE, 0) < 0)
		{
			LOG_ERR("%s:%d recv",__func__,__LINE__);
		}
	}
	return 0;
}

int user_Alive(pClient pclt, xmlDocPtr doc, xmlNodePtr cur, xmlChar *fromUser)
{
	// LOG_INFO("%s Alive", fromUser);
	// sprintf(sendbuf, ALIVE_RES, "success");
	// if(send(pclt->fd, sendbuf, strlen(sendbuf), 0) < 0)
	// {
	// 	LOG_ERR("%s:%d send",__func__,__LINE__); 
	// }
	return 0;
}

int user_FileSend(pClient pclt, xmlDocPtr doc, xmlNodePtr cur, xmlChar *fromUser)
{
	char res[16] = "success";
	cur = cur->next;
	xmlChar *toUser = xmlGetNodeText(doc, cur, "ToUser");
	if(toUser == NULL)
		return 0;
	struct list_head *pos;
	list_for_each(pos, &head)    //查询设备是否已经连接
	{
		pcht = list_entry(pos, Chater, entry);
		if(strcmp(pcht->userName, (const char*)toUser) == 0)
		{
			sprintf(sendbuf, FILE_SEND_TO, fromUser);
			if(send(pcht->pclt->fd, sendbuf, strlen(sendbuf), 0) < 0)
			{
				LOG_ERR("%s:%d send",__func__,__LINE__);
				strcpy(res, "sendError");
			}
			break;
		}
	}
	if(pos == &head)
	{
		strcpy(res, "friendLogout");
	}
	if(strcmp(res, "success")) //操作失败返回错误信息
	{
		LOG_DBG("File send estbilsh error %s", res);
		sprintf(sendbuf, FILE_SEND_ERR, res);
		if(send(pclt->fd, sendbuf, strlen("success"), 0) < 0)
		{
			LOG_ERR("%s:%d send",__func__,__LINE__);
		}
	}
	xmlFree(toUser);
	return 0;
}

int user_FileRecv(pClient pclt, xmlDocPtr doc, xmlNodePtr cur, xmlChar *fromUser)
{
	xmlChar *toUser = NULL;
	xmlChar *ip = NULL;
	xmlChar *port = NULL;
	cur = cur->next;
	toUser = xmlGetNodeText(doc, cur, "ToUser");
	if(toUser == NULL)
		return 0;
	cur = cur->next;
	if(xmlStrcmp(cur->name,(const xmlChar*)"ADDR") ==0 )
	{
		cur = cur->xmlChildrenNode;
		ip = xmlGetNodeText(doc, cur, "IP");
		if(ip == NULL) goto user_FileRecv_free;

		cur = cur->next;
		port = xmlGetNodeText(doc, cur, "PORT");
		if(port == NULL) goto user_FileRecv_free;
	}
	LOG_INFO("File recv addr ip:%s port:%s", ip, port);
	struct list_head *pos;
	list_for_each(pos, &head) 
	{
		pcht = list_entry(pos, Chater, entry);
		if(strcmp(pcht->userName, (const char*)toUser) == 0)
		{
			sprintf(sendbuf, FILE_RECV_FROM, fromUser, ip, port);
			if(send(pcht->pclt->fd, sendbuf, strlen(sendbuf), 0) < 0)
			{
				LOG_ERR("%s:%d send",__func__,__LINE__);
			}
			break;
		}
	}
user_FileRecv_free:
	xmlFree(toUser);
	xmlFree(ip);
	xmlFree(port);
	return 0;
}

int user_FileSendErro(pClient pclt, xmlDocPtr doc, xmlNodePtr cur, xmlChar *fromUser)
{
	xmlChar *toUser = xmlGetNodeText(doc, cur, "ToUser");
	if(toUser == NULL)
		return 0;
	xmlChar *error = xmlGetNodeText(doc, cur, "ERROR");
	if(error == NULL) 
	{
		xmlFree(toUser);
		return 0;
	}
	LOG_INFO("File Send: %s >> %s Error %s", toUser, fromUser, error);
	struct list_head *pos;
	list_for_each(pos, &head) 
	{
		pcht = list_entry(pos, Chater, entry);
		if(strcmp(pcht->userName, (const char*)fromUser) == 0)
		{
			sprintf(sendbuf, FILE_SEND_ERR, error);
			if(send(pclt->fd, sendbuf, strlen(sendbuf), 0) < 0)
			{
				LOG_ERR("%s:%d send",__func__,__LINE__);
			}
			break;
		}
	}
	return 0;
}

int user_FileRecvErro(pClient pclt, xmlDocPtr doc, xmlNodePtr cur, xmlChar *fromUser)
{
	cur = cur->next;
	xmlChar *toUser = xmlGetNodeText(doc, cur, "ToUser");
	if(toUser == NULL)
		return 0;
		
	cur = cur->next;
	xmlChar *error = xmlGetNodeText(doc, cur, "ERROR");
	if(error == NULL) 
	{
		xmlFree(toUser);
		return 0;
	}
	LOG_INFO("File Send: %s >> %s Error %s", fromUser, toUser, error);
	struct list_head *pos;
	list_for_each(pos, &head) 
	{
		pcht = list_entry(pos, Chater, entry);
		if(strcmp(pcht->userName, (const char*)fromUser) == 0)
		{
			sprintf(sendbuf, FILE_RECV_ERR, error);
			if(send(pclt->fd, sendbuf, strlen(sendbuf), 0) < 0)
			{
				LOG_ERR("%s:%d send",__func__,__LINE__);
			}
			break;
		}
	}
	return 0;
}

int user_groupmsg(pClient pclt, xmlDocPtr doc, xmlNodePtr cur, xmlChar *fromUser)
{
	cur = cur->next;
	xmlChar *group = xmlGetNodeText(doc, cur, "GROUP");
	if(group == NULL)
		return 0;
	cur = cur->next;
	xmlChar *context = xmlGetNodeText(doc, cur, "Context");
	if(context == NULL)
	{
		xmlFree(group);
		return 0;
	}
	sprintf(sqlcmd, "select * from %s", group);
	mysql_query_my(conn, sqlcmd);
    MYSQL_RES *res = mysql_store_result(conn);
	if(res == NULL) goto user_groupmsg_free;
	MYSQL_ROW row;
	while((row = mysql_fetch_row(res)) != NULL)
	{
		struct list_head *pos;
		if(strcmp(row[0], (char*)fromUser) == 0)
			continue;
		list_for_each(pos, &head) 
		{
			pcht = list_entry(pos, Chater, entry);
			if(strcmp(pcht->userName, row[0]) == 0)
			{
				sprintf(sendbuf, GROUP_TEXT, group, fromUser, context);
				if(send(pcht->pclt->fd, sendbuf, strlen(sendbuf), 0) < 0)
				{
					LOG_ERR("%s:%d send",__func__,__LINE__);
				}
				else
				{
					LOG_INFO("%s/%s:%s",group, fromUser, context);
				}
			}
		}
	}
user_groupmsg_free:
	xmlFree(group);
	xmlFree(context);
	return 0;
}

int user_grouplist(pClient pclt, xmlDocPtr doc, xmlNodePtr cur, xmlChar *fromUser)
{
	cur = cur->next;
	xmlChar *group = xmlGetNodeText(doc, cur, "GROUP");
	if(group == NULL)
		return 0;
		sprintf(sqlcmd, "select * from %s", group);
	mysql_query_my(conn, sqlcmd);
    MYSQL_RES *res = mysql_store_result(conn);
	if(res == NULL) 
	{
		xmlFree(group);
		return 0;
	}
	MYSQL_ROW row;
	while((row = mysql_fetch_row(res)) != NULL)
	{
		struct list_head *pos;
		if(strcmp(row[0], (char*)fromUser) == 0)
			continue;
		list_for_each(pos, &head) 
		{
			pcht = list_entry(pos, Chater, entry);
			if(strcmp(pcht->userName, row[0]) == 0)
			{
				sprintf(sendbuf, GROUP_LIST_RES, pcht->userName);
				if(send(pclt->fd, sendbuf, strlen(sendbuf), 0) < 0)
				{
					LOG_ERR("%s:%d send",__func__,__LINE__);
				}
				if(recv(pclt->fd, recvbuf, RECV_BUF_SIZE, 0) < 0)
				{
					LOG_ERR("%s:%d recv",__func__,__LINE__);
				}
			}
		}
	}
	xmlFree(group);
	return 0;
}

int ss_err(pClient pclt, xmlDocPtr doc, xmlNodePtr cur, xmlChar *fromUser)
{
	LOG_FLAG = 1;
	bzero(sendbuf,sizeof(sendbuf));
	if(LOG_FLAG == 1)
	{
		sprintf(sendbuf,SS_RES,"ERR","success");
		send(pclt->fd,sendbuf,sizeof(sendbuf),0);
	}
	else
	{
		sprintf(sendbuf,SS_RES,"ERR","fail");
		send(pclt->fd,sendbuf,sizeof(sendbuf),0);
	}
	
	return 0;
}
int ss_info(pClient pclt, xmlDocPtr doc, xmlNodePtr cur, xmlChar *fromUser)
{
	LOG_FLAG = 4;
	LOG_INFO("info");
	bzero(sendbuf,sizeof(sendbuf));
	if(LOG_FLAG == 4)
	{
		sprintf(sendbuf,SS_RES,"INFO","success");
		send(pclt->fd,sendbuf,sizeof(sendbuf),0);
	}
	else
	{
		sprintf(sendbuf,SS_RES,"INFO","fail");
		send(pclt->fd,sendbuf,sizeof(sendbuf),0);
	}
	return 0;
}
int ss_debug(pClient pclt, xmlDocPtr doc, xmlNodePtr cur, xmlChar *fromUser)
{
	LOG_FLAG = 3;
	LOG_INFO("debug");
	bzero(sendbuf,sizeof(sendbuf));
	if(LOG_FLAG == 3)
	{
		sprintf(sendbuf,SS_RES,"DEBUG","success");
		send(pclt->fd,sendbuf,sizeof(sendbuf),0);
	}
	else
	{
		sprintf(sendbuf,SS_RES,"DEBUG","fail");
		send(pclt->fd,sendbuf,sizeof(sendbuf),0);
	}
	return 0;
}
int ss_warning(pClient pclt, xmlDocPtr doc, xmlNodePtr cur, xmlChar *fromUser)
{
	LOG_FLAG = 2;
	LOG_INFO("warning");
	bzero(sendbuf,sizeof(sendbuf));
	if(LOG_FLAG == 2)
	{
		sprintf(sendbuf,SS_RES,"WARNING","success");
		send(pclt->fd,sendbuf,sizeof(sendbuf),0);
	}
	else
	{
		sprintf(sendbuf,SS_RES,"WARNING","fail");
		send(pclt->fd,sendbuf,sizeof(sendbuf),0);
	}
	return 0;
}
int ss_kill(pClient pclt, xmlDocPtr doc, xmlNodePtr cur, xmlChar *fromUser)
{

	char res[16] = "kill success";
	cur = cur->next;
	xmlChar *toUser = xmlGetNodeText(doc, cur, "name");
	if(toUser == NULL)
		return 0;
	struct list_head *pos;
	list_for_each(pos, &head)    //查询设备是否已经连接
	{
		pcht = list_entry(pos, Chater, entry);
		if(strcmp(pcht->userName, (const char*)toUser) == 0)
		{
			sprintf(sendbuf, LOGOUT_RES, "success");
			if(send(pcht->pclt->fd, sendbuf, strlen(sendbuf), 0))
			{
				LOG_ERR("%s:%d send",__func__,__LINE__);
			}
			list_del(pos);			//从链表删除结点
			free(pcht);	
			LOG_INFO("client %s bekilled", inet_ntoa(pclt->client_addr.sin_addr));
			break;
		}
	}
	if(pos ==  &head)
	{
		strcpy(res, "no such user");
	}
	sprintf(sendbuf, SS_RES,"KILL", res);
	if(send(pclt->fd, sendbuf, strlen(sendbuf), 0) < 0)
		LOG_ERR()
	xmlFree(toUser); 
	return 0;
}

typedef struct
{
	const char *cmd;
	int (*handler)(pClient, xmlDocPtr, xmlNodePtr, xmlChar*);
}chat_handle_t;

chat_handle_t chat_handle_table[] = {
	{"msg", transmit_msg},
	{"Login", user_login},
	{"Logout", user_logout},
	{"ReqList", user_ReqList},
	{"Alive", user_Alive},
	{"FileSend", user_FileSend},
	{"FileRecv", user_FileRecv},
	{"FileSendError", user_FileSendErro},
	{"FileRecvError", user_FileRecvErro},
	{"groupmsg", user_groupmsg},
	{"GroupList", user_grouplist},
};
chat_handle_t ss_handle_table[] = {
	{"Login", user_login},
	{"Logout", user_logout},
	{"ReqList", user_ReqList},
	{"Alive", user_Alive},
	{"ERR",ss_err},
	{"DEBUG",ss_debug},
	{"WARNING",ss_warning},
	{"INFO",ss_info},
	{"KILL",ss_kill},
};

/*socket 接收数据事件， 返回-1关闭服务器*/
int recv_handler(pClient pclt, char *recvbuf,int recvlen)
{
	xmlDocPtr doc;   //定义解析文档指针
    xmlNodePtr cur;  //定义结点指针(你需要它为了在各个结点间移动)
    recvbuf[recvlen] = 0;
    doc = xmlParseMemory((const char *)recvbuf, strlen((char *)recvbuf)+1);  
    if (doc == NULL )
    {
        LOG_WARN("Document not parsed successfully. \n");
        return 0;
    }
    cur = xmlDocGetRootElement(doc);  //确定文档根元素
    /*检查确认当前文档中包含内容*/
    if (cur == NULL)
    {
        LOG_WARN("empty document\n");
		goto recv_handler_release;
    }
    if (xmlStrcmp(cur->name, (const xmlChar *) "xml"))
    {
        LOG_WARN("document of the wrong type, root node != xml");
		goto recv_handler_release;
    }
    if((cur = cur->xmlChildrenNode) == NULL)
	{
        LOG_WARN("xml no child node");
		goto recv_handler_release;
	}
	if(xmlStrcmp(cur->name, (const xmlChar *)"FromUser") == 0)
	{
		xmlChar *fromUser = NULL;
		xmlChar *cmd = NULL; 
		fromUser = xmlGetNodeText(doc, cur, "FromUser");
		if(fromUser == NULL)
			goto recv_handler_release;
		cur = cur->next;
		cmd = xmlGetNodeText(doc, cur, "CMD");
		if(cmd == NULL)
		{
			xmlFree(fromUser);
			goto recv_handler_release;
		}
		LOG_INFO("FromUser:%s CMD:%s", fromUser, cmd);
		int i;
		for(i=0; i<COUNTOF(chat_handle_table); i++)
		{
			if(strcmp((char*)cmd, chat_handle_table[i].cmd) == 0)
			{
				chat_handle_table[i].handler(pclt, doc, cur, fromUser);
				break;
			}
		}
		xmlFree(fromUser);
		xmlFree(cmd);
	}
	if(xmlStrcmp(cur->name, (const xmlChar *)"Fromss") == 0)
	{
		xmlChar *fromss = NULL;
		xmlChar *cmd = NULL; 
		fromss = xmlGetNodeText(doc, cur, "Fromss");
		if(fromss == NULL)
			goto recv_handler_release;
		cur = cur->next;
		cmd = xmlGetNodeText(doc, cur, "CMD");
		if(cmd == NULL)
		{
			xmlFree(fromss);
			goto recv_handler_release;
		}
		LOG_INFO("Fromss:%s CMD:%s", fromss, cmd);
		int i;
		for(i=0; i<COUNTOF(ss_handle_table); i++)
		{
			if(strcmp((char*)cmd, ss_handle_table[i].cmd) == 0)
			{
				ss_handle_table[i].handler(pclt, doc, cur,fromss);
				break;
			}
		}
		xmlFree(fromss);
		xmlFree(cmd);
	}
recv_handler_release:
	xmlFreeDoc(doc);
	return 0;
}

