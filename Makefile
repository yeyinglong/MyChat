CC = gcc
CFLAGS = -Wall -I/usr/include/libxml2 -I./mxml-2.9
CDEFINE = -DCONFIG_LOG

TARGETS = chatservice
TARGETC = mychat
#服务端源文件
SRCS += service.c 
SRCS += service_handle.c 

SRCC += tcp_chat_client.c

OBJS = $(SRCS:.c=.o)
OBJC = $(SRCC:.c=.o)

# $@ 代表目标文件
# $^ 代表所有依赖文件
# %< 代表第一个依赖文件
$(TARGETS):$(OBJS)  
	$(CC) -o $@ $^ -L /usr/lib/i386-linux-gun -lxml2 -lmysqlclient
	
$(TARGETC):$(OBJC)
	$(CC) -o $@ $^   -L /usr/lib/i386-linux-gun -lxml2 -lmysqlclient -lpthread

cservice:  
	rm -rf $(TARGETS) $(OBJS)
cclient:
	rm -rf $(TARGETC) $(OBJC)

%.o:%.c  
	$(CC) $(CFLAGS) $(CDEFINE) -o $@ -c $<

