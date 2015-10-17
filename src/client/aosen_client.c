#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sched.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/sysinfo.h>
#include <fcntl.h>     /* nonblocking */
#include <netinet/in.h>      /* sockaddr_in{} and other Internet defns */
#include <arpa/inet.h>       /* inet(3) functions */
#include <errno.h>
#include <signal.h>
#include <sys/ipc.h>

#include <aosen/aosen_core.h>

/*初始化客户端socket*/
static int init_client_socket(aosen_core_data *core_data);
/*生成一个本地连接*/
static int aosen_local_connect(int epollfd, int fd, aosen_worker_node *worker_node, aosen_server* aosen, aosen_core_data *core_data);

/*初始化客户端socket*/
static int init_client_socket(aosen_core_data *core_data)
{   
    int cc;
	int reuse = 1;
    struct sockaddr_in client_addr;
    int err;
    
    cc = socket(AF_INET, SOCK_STREAM, 0);
    if (cc<0)
    {
        return -1;
    }

	//防止服务器因重启导致端口被占用 绑定端口失败 如果不设置的话，当进程退出后，端口会保留2-4分钟，开始阶段让人抓狂
	if (setsockopt(cc, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
	{
		return -1;
	}

    //将socket建立为非阻塞
    int flags = fcntl(cc, F_GETFL, 0);
    fcntl(cc, F_SETFL, flags|O_NONBLOCK);

    return cc;
}


/*生成一个本地连接*/
static int aosen_local_connect(int epollfd, int fd, aosen_worker_node *worker_node, aosen_server* aosen, aosen_core_data *core_data)
{
    int state;
    struct sockaddr_in server_addr;
    struct hostent *phost;
    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(core_data->local_port);
    server_addr.sin_addr.s_addr = INADDR_NONE;
    if(server_addr.sin_addr.s_addr == INADDR_NONE)
    {   //如果输入的是域名
        phost = (struct hostent*)gethostbyname(core_data->local_server);
        if(phost==NULL)
        {
            return -1;
        }
        server_addr.sin_addr.s_addr =((struct in_addr*)phost->h_addr)->s_addr;
    }
    state = connect(fd,(struct sockaddr*)&server_addr,sizeof(server_addr));
    if(state == -1 && errno != EINPROGRESS)
    {
        return -1;
    }
    else if(errno == EINPROGRESS)
    {
        return 0;
    }
    else if(state == 0)
    {
        return -1;
    }
}


/*生成一个本地客户端*/
int aosen_local_client(int epollfd, int fd, aosen_worker_node* worker_node, aosen_server *aosen, aosen_core_data *core_data)
{
    int client = init_client_socket(core_data);
    if(client == -1)
        return -1;
    if(-1 == aosen_local_connect(epollfd, client, worker_node, aosen, core_data))
        return -1;
    else
    {
        return client;
    }
}
