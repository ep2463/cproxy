#ifndef __AOSEN_HTTP_H__
#define __AOSEN_HTTP_H__
#include <stdio.h>
#include <stdlib.h>

#include <aosen/aosen_core.h>


/*获得Content_Length字段, 成功返回长度 失败返回0*/
int aosen_http_get_content_length(const char *read_buf);
/*检测服务器数据是否发送完毕，发送完毕返回1，没有返回0*/
int aosen_http_check_server_send_done(const char *read_buf);
#endif
