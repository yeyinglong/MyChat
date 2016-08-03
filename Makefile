CC = gcc
CFLAGS = -Wall -I/usr/include/libxml2 -g
CDEFINE = -DCONFIG_LOG

TARGETS = chatservice
TARGETC = mychat
TARGETSS = ss
#服务端源文件
SRCS += service.c 
SRCS += service_handle.c 

SRCC += tcp_chat_client.c

SRCSS += ss.c

OBJS = $(SRCS:.c=.o)
OBJC = $(SRCC:.c=.o)
OBJSS = $(SRCSS:.c=.o)

# $@ 代表目标文件
# $^ 代表所有依赖文件
# %< 代表第一个依赖文件
all:$(TARGETS) $(TARGETC) $(TARGETSS)

$(TARGETS):$(OBJS)  #编译服务器
	$(CC) -o $@ $^ -L /usr/lib/i386-linux-gun -lxml2 -lmysqlclient
	
$(TARGETC):$(OBJC)  #编译聊天客户端
	$(CC) -o $@ $^   -L /usr/lib/i386-linux-gun -lxml2 -lmysqlclient -lpthread

$(TARGETSS):$(OBJSS) #编译ss
	$(CC) -o $@ $^   -L /usr/lib/i386-linux-gun -lxml2 -lmysqlclient -lpthread

clean:  
	rm -rf $(TARGETS) $(OBJS)
	rm -rf $(TARGETC) $(OBJC)
	rm -rf $(TARGETSS) $(OBJSS)

%.o:%.c  
	$(CC) $(CFLAGS) $(CDEFINE) -o $@ -c $<

