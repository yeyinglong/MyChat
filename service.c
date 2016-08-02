#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "service.h"
#include "log.h"

#define MYPORT 12321    			// the port users will be connecting to
#define BUF_SIZE 1024
#define MAX_IDLECONNCTIME 300 	//300秒无数据断开

int startup_handler(void);		//程序启动时调用函数
int accept_handler(pClient);	//新客户端连接时调用函数
int close_handler(pClient);		//客户端关闭时调用函数
int recv_handler(pClient,unsigned char *,int);	//接收到客户端数据时调用函数

unsigned char recvbuf[BUF_SIZE];	//定义接收缓冲区

int main()
{
	int Listen_socket, ret;
	fd_set fdsr; 					//文件描述符集的定义
	struct timeval tv = {4,0};		//select函数超时时间
	struct list_head *pos, *tmpos;	//用于遍历链表的指针
	LIST_HEAD(head);				//定义链表头保存所有客户端信息
	
	setvbuf(stdout, NULL, _IONBF, 0); //设置无缓冲输出
	LOG_INFO("start...");
	startup_handler();
	//建立socket套接字
	if ((Listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
	{
		LOG_ERR("socket");
		exit(1);
	}

	//bind API 函数将允许地址的立即重用
	int on = 1;
	setsockopt(Listen_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	//设置本机服务类型
	struct sockaddr_in local_addr;
	local_addr.sin_family = AF_INET;
	local_addr.sin_port = htons(MYPORT);
	local_addr.sin_addr.s_addr = INADDR_ANY;
	//绑定本机IP和端口号
	if (bind(Listen_socket, (struct sockaddr*)&local_addr, sizeof(struct sockaddr)) == -1)
	{
		LOG_ERR("bind");
        exit(1);
	}

	//监听客户端连接
	if (listen(Listen_socket, 8) == -1)
	{
		LOG_ERR("listen");
        exit(1);
	}
	int maxsock = Listen_socket;
	LOG_INFO("lisenting......");
	/***************************************
	以下为并发连接处理，系统关键部分
	***************************************/
	while (1)
	{
		FD_ZERO(&fdsr); //每次进入循环都重建描述符集
		FD_SET(Listen_socket, &fdsr);
		list_for_each_safe(pos, tmpos, &head)         //遍历链表
		{
			pClient pclt = list_entry(pos, Client, entry);
			if (pclt->last_time + MAX_IDLECONNCTIME <= time(NULL))
			{
				close_handler(pclt); //断开连接调用函数
				close(pclt->fd);
				LOG_INFO("client %s timeout break", inet_ntoa(pclt->client_addr.sin_addr));
				list_del(pos);
				free(pclt);
				continue;
			}
			FD_SET(pclt->fd, &fdsr);
		}
		ret = select(maxsock+1, &fdsr, NULL, NULL, &tv); //关键的select()函数，用来探测各套接字的异常
		//如果在文件描述符集中有连接请求或发送请求，会作相应处理，
		//从而成功的解决了单线程情况下阻塞进程的情况，实现多用户连接与通信
		if (ret < 0) //<0表示探测失败
		{
			LOG_ERR("select");
			break;
		}
		else if (ret == 0) //=0表示超时，下一轮循环
		{
			continue;
		}
		// 如果select发现有异常，循环判断各活跃连接是否有数据到来
		list_for_each_safe(pos, tmpos, &head)	//遍历链表
		{
			pClient pclt = list_entry(pos, Client, entry);
			if(FD_ISSET(pclt->fd, &fdsr))
			{
				ret = recv(pclt->fd, recvbuf, BUF_SIZE, 0);
				if(ret > 0)
				{
					pclt->last_time = time(NULL);	//保存接收数据时间
					if(-1 == recv_handler(pclt, recvbuf, ret))
						goto end_free_res;
				}
				else if(ret == 0)
				{
					close_handler(pclt);	//关闭客户调用函数
					close(pclt->fd);		//关闭客户端
					LOG_INFO("client %s close", inet_ntoa(pclt->client_addr.sin_addr));
					FD_CLR(pclt->fd, &fdsr);
					list_del(pos);			//从链表删除结点
					free(pclt);				//释放内存
				}
				else
				{
					LOG_ERR("recv error");
					LOG_DBG();
				}
			}
		}

		// 以下说明异常有来自客户端的连接请求
		if (FD_ISSET(Listen_socket, &fdsr))
		{
			struct sockaddr_in client_addr;
			socklen_t addr_size = sizeof(struct sockaddr_in);
			int new_fd = accept(Listen_socket, (struct sockaddr *)&client_addr, &addr_size);
			if (new_fd < 0)
			{
				LOG_ERR("accept");
				continue;
			}
			struct timeval srtv = {2,0}; //设置发送接收超时为2秒
			setsockopt(new_fd, SOL_SOCKET, SO_SNDTIMEO, &srtv, sizeof(srtv));
			setsockopt(new_fd, SOL_SOCKET, SO_RCVTIMEO, &srtv, sizeof(srtv));
			//添加新连接到文件描述符集中
			pClient pclt = (pClient)malloc(sizeof(Client));
			if(pclt != NULL)
			{
				pclt->fd = new_fd;
				pclt->last_time = time(NULL);
				pclt->connect_time = time(NULL);
				pclt->client_addr = client_addr;
				list_add_tail(&(pclt->entry), &head);    //将节点插入链表结尾
				LOG_INFO("new connection client %s:%d",
					inet_ntoa(pclt->client_addr.sin_addr), 
					ntohs(pclt->client_addr.sin_port));
				accept_handler(pclt);		//建立连接调用函数
				if (new_fd > maxsock)
					maxsock = new_fd;
			}
			else
			{
				LOG_INFO("alloc memery error, exit");
				close(new_fd);
				continue;
			}
		}
	}
end_free_res:
	LOG_INFO("server exit...\n");
	close(Listen_socket);
	 // close other connections
	list_for_each_safe(pos, tmpos, &head)  //遍历链表
	{
		pClient pclt = list_entry(pos, Client, entry);
		if (pclt->fd != 0) {
            close(pclt->fd);
        }		
		list_del(pos);     
		free(pclt);
	}
	return 0;
}


