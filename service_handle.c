#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <libxml/parser.h>  
#include <libxml/xmlmemory.h>
#include <libxml/tree.h>

#include "log.h"
#include "service.h"

const char SEND_TEXT[]={
	"<xml>"
	"<CMD>msg</CMD>"
	"<FromUser>%s</FromUser>"
	"<Context><![CDATA[%s]]></Context>"
	"</xml>"
};

const char SEND_RES[]={
	"<xml>"
	"<CMD>msg</CMD>"
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

#define COUNTOF(x) (sizeof(x)/sizeof((x)[0]))
#define SEND_BUF_SIZE 1024
#define RECV_BUF_SIZE 1024
static char sendbuf[SEND_BUF_SIZE];
static char recvbuf[SEND_BUF_SIZE];

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
	sprintf(sendbuf, SEND_RES, res);
	if(send(pclt->fd, sendbuf, strlen(sendbuf), 0) < 0)
		LOG_ERR()
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
			if((send(pcht->pclt->fd, sendbuf, strlen(sendbuf), 0)) == -1)
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
		if((send(pcht->pclt->fd, sendbuf, strlen(sendbuf), 0)) == -1)
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
			free(pos);
			pos = NULL;
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
			return 0;
		cur = cur->next;
		cmd = xmlGetNodeText(doc, cur, "CMD");
		if(cmd == NULL)
		{
			xmlFree(fromUser);
			return 0;
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
recv_handler_release:
	xmlFreeDoc(doc);
	return 0;
}

