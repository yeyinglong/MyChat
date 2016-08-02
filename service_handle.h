#ifndef _SERVICE_HANDLE_H
#define _SERVICE_HANDLE_H

#define COUNTOF(x) (sizeof(x)/sizeof((x)[0]))
#define SEND_BUF_SIZE 512

extern unsigned char sendbuf[SEND_BUF_SIZE];

extern MYSQL *conn;       //数据库
extern char sqlcmd[512];  //数据库命令

int mysql_query_my(MYSQL *conn, const char *str);

#endif