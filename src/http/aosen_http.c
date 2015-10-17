#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <ctype.h>

#include <aosen/aosen_core.h>
#include <aosen/aosen_http.h>

/*获得Content_Length字段, 成功返回长度 失败返回0*/
int aosen_http_get_content_length(const char *read_buf)
{
    char cl[15] = "Content-Length:";
    int content_len;
    char *content_len_str=NULL;
    char *p = (char*)read_buf;
    char *q = NULL;
    p = strstr(read_buf, cl);
    if(!p)
        return 0;
    p = p+15;
    q = strchr(p, '\r');
    if(!q)
        return 0;
    content_len_str = (char*)malloc(q-p+1);
    memset(content_len_str, 0, q-p+1);
    memcpy(content_len_str, p, q-p);
    content_len = atoi(content_len_str);
    return content_len;
}



/*检测服务器数据是否发送完毕，发送完毕返回1，没有返回0*/
int aosen_http_check_server_send_done(const char *read_buf)
{
    char *p = NULL;
    p = strstr(read_buf, "\r\n0\r\n\r\n");
    if(!p)
        return 0;
    else
        return 1;
}

