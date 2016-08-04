/**
  ******************************************************************************
  * @file    common.h
  * @author  yeyinglong
  * @version V1.0
  * @date    2016-03-20
  * @brief   logging helpers.
  ******************************************************************************
  */

#ifndef __LOG_H
#define __LOG_H

#include <time.h>

extern int LOG_FLAG;

#if defined(CONFIG_LOG)

#define LOG_DBG(fmt, ...) {if(LOG_FLAG >= 3){printf("log_d: %s:%d: " fmt "\n", __func__,__LINE__,  ##__VA_ARGS__);}}

#define LOG_ERR(fmt, ...) {if(LOG_FLAG >=1){time_t timer = time(NULL);\
							printf("log_e: %s" fmt ": %s\n",ctime(&timer),\
##__VA_ARGS__, strerror(errno));}}

#define LOG_WARN(fmt, ...) {if(LOG_FLAG>=2){time_t timer = time(NULL);\
							printf("log_w: %s" fmt "\n",ctime(&timer),\
							##__VA_ARGS__);}}

#define LOG_INFO(fmt, ...) {if(LOG_FLAG>=4){time_t timer = time(NULL);\
							printf("log_i: %s" fmt "\n",ctime(&timer), ##__VA_ARGS__);}}

#define LOG_ASSERT(cond) if (!(cond)) {LOG_ERR("assert: '" #cond "' failed");}

#define LOG_ERR_MYSQL(conn) {time_t timer = time(NULL);\
							printf("log_e: %sMYSQL ERROR:%d %s\n",ctime(&timer),\
							mysql_errno(conn), mysql_error(conn));}
#else
#define LOG_DBG(fmt, ...)
#define LOG_ERR(fmt, ...)
#define LOG_WARN(fmt, ...)
#define LOG_INFO(fmt, ...)
#define LOG_ASSERT(cond)
#endif /* CONFIG_BLUETOOTH_DEBUG */


#endif //_DEBUG_H

