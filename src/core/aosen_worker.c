#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#define __USE_GNU
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
#include <aosen/aosen_client.h>
#include <aosen/aosen_http.h>

/*epoll事件处理函数声明*/

/*
 * accept处理函数，用来平衡各worker进程的负载问题
 * 当新连接请求来临时，worker进程会查询共享内存中各worker的正在处理连接数
 * 如果自己是处理连接数最少的那个，就接受此次连接请求，否则就不处理，
 * 让操作系统重新分配
 * 成功返回1 表明可以处理本次accept, 将成功accept后的文件描述符存入链表
 * aosen_worker_head结构提中的accept_num加1，并新建aosen_worker_node节点
 * 失败返回0 不处理本次accept
 */

//删除连接，用在close之后
static void aosen_dec_accept(aosen_core_data *core_data);

//增加连接，用在aosen_check_accept 里面
static void aosen_inc_accept(aosen_core_data *core_data);

static int aosen_check_accept(int epollfd, aosen_server *aosen, aosen_core_data *core_data);

//设置监听套接字非阻塞
static int setnonblocking(int sockfd);

//事件处理函数
static void aosen_handle_events(int epollfd,struct epoll_event *events,int num,aosen_server *aosen, aosen_core_data *core_data);

//处理接收到的连接
static int aosen_handle_accept(int epollfd,aosen_server* aosen, aosen_core_data *core_data);
//添加事件
static void aosen_add_event(int epollfd,int fd,int state);

//修改事件
static void aosen_modify_event(int epollfd,int fd,int state);

//删除事件
static void aosen_delete_event(int epollfd,int fd,int state);

/*一些列worker node处理函数*/
//初始化worker node 并将worker node插到链表头部
static aosen_worker_node* aosen_add_worker_node(int event_user_fd, aosen_core_data *core_data, aosen_server *aosen);
//增加读写缓冲区大小
static char *aosen_add_buf(char *old_buf, int iter, size_t size);
//搜索event事件的worker node节点
static aosen_worker_node* aosen_search_event(struct epoll_event *event, aosen_core_data *core_data, aosen_server* aosen);
//删除worker node 节点
static void aosen_delete_worker_node(aosen_core_data *core_data, aosen_worker_node *worker_node);

//读用户数据
static int aosen_read_from_user(int epollfd, int fd, aosen_worker_node *worker_node, aosen_server *aosen, aosen_core_data *core_data);
//将用户数据发送给目标服务器
static int aosen_write_to_server(int epollfd, int fd, aosen_worker_node *worker_node, aosen_server *aosen, aosen_core_data *core_data);
//读取目标服务器数据
static int aosen_read_from_server(int epollfd, int fd, aosen_worker_node *worker_node, aosen_server *aosen, aosen_core_data *core_data);
//将目标服务器的数据写给用户
static int aosen_write_to_user(int epollfd, int fd, aosen_worker_node *worker_node, aosen_server *aosen, aosen_core_data *core_data);


/*处理系统给worker发来的信号*/
static void handle_signal(int signal)
{
	switch(signal)
	{
	case SIGPIPE:
        printf("user break down\n");
	}
}

//*epoll 处理函数*/
//添加事件
static void aosen_add_event(int epollfd,int fd,int state)
{
    struct epoll_event ev;
    ev.events = state;
    ev.data.fd = fd;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&ev);
}


//修改事件
static void aosen_modify_event(int epollfd,int fd,int state)
{
    struct epoll_event ev;
    ev.events = state;
    ev.data.fd = fd;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&ev);
}

//删除事件
static void aosen_delete_event(int epollfd,int fd,int state)
{
    struct epoll_event ev;
    ev.events = state;
    ev.data.fd = fd;
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,&ev);
}


//读用户数据
static int aosen_read_from_user(int epollfd, int fd, aosen_worker_node *worker_node, aosen_server *aosen, aosen_core_data *core_data)
{
    int nread;
    int client_fd;
    int i;
    int len;
    /*迭代器指针*/
    char *iter_p=NULL;
    memset(worker_node->read_from_user_buf, 0, BUFFSIZE);
    nread = recv(fd, worker_node->read_from_user_buf, BUFFSIZE-1, 0);
    if(nread > 0)
    {
        client_fd = aosen_local_client(epollfd, fd, worker_node, aosen, core_data);
        if(client_fd == -1)
            return -1;
        else
        {
            worker_node->event_server_fd = client_fd;
            aosen_add_event(epollfd, worker_node->event_server_fd, EPOLLOUT|EPOLLET|EPOLLERR);
            worker_node->read_from_user_size = nread;
            return 0;
        }
    }
    if (nread == -1 &&  errno != EAGAIN)
    {
        return -1;
    }
    else if(nread == 0 && errno != EAGAIN)
    {
        return 0;
    }
    else
    {
        /*EAGAIN 继续运行 等待read*/
        return 1;
    }
}



//将用户数据转发给目标服务器
static int aosen_write_to_server(int epollfd, int fd, aosen_worker_node *worker_node, aosen_server *aosen, aosen_core_data *core_data)
{
    int i;
    int nwrite;
    int n = worker_node->read_from_user_size;
    PRINTF("core:%d start write to server\n", CPU);
    while (n > 0) {
        nwrite = send(fd, worker_node->read_from_user_buf + worker_node->read_from_user_size - n, n, 0);
        if (nwrite < n) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                return -1;
            }
        }
        n -= nwrite;
    }
    aosen_modify_event(epollfd,worker_node->event_server_fd,EPOLLIN|EPOLLERR|EPOLLET);
    return 0;
}



//读取目标服务器数据
static int aosen_read_from_server(int epollfd, int fd, aosen_worker_node *worker_node, aosen_server *aosen, aosen_core_data *core_data)
{
    int nread;
    int i;
    memset(worker_node->read_from_server_buf, 0, BUFFSIZE);
    nread = recv(fd, worker_node->read_from_server_buf, BUFFSIZE-1, 0);
    if(nread > 0)
    {
        worker_node->read_from_server_size = nread;
        aosen_modify_event(epollfd, worker_node->event_user_fd, EPOLLOUT|EPOLLERR|EPOLLET);
        return 0;
    }
    else if (nread == -1 &&  errno != EAGAIN)
    {
        return -1;
    }
    else
    {
        /*EAGAIN 继续运行 等待read*/
        return 1;
    }
}



//将目标服务器的数据写给用户
static int aosen_write_to_user(int epollfd, int fd, aosen_worker_node *worker_node, aosen_server *aosen, aosen_core_data *core_data)
{
    int i;
    int nwrite;
    int n = worker_node->read_from_server_size;
    while (n > 0) {
        nwrite = send(fd, worker_node->read_from_server_buf + worker_node->read_from_server_size - n, n, 0);
        if (nwrite < n) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                return -1;
            }
        }
        n -= nwrite;
    }
    aosen_modify_event(epollfd, worker_node->event_server_fd, EPOLLIN|EPOLLERR|EPOLLET);
    return 0;
}

//设置监听套接字非阻塞
static int setnonblocking(int sockfd)
{
	if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFD, 0)|O_NONBLOCK) == -1) {
		return -1;
	}
	return 0;
}

//处理接收到的连接
static int aosen_handle_accept(int epollfd,aosen_server *aosen, aosen_core_data *core_data)
{
    int clifd;
    struct sockaddr_in cliaddr;
    socklen_t  cliaddrlen;
    aosen_worker_node *worker_node;
    bzero(&cliaddr, sizeof(struct sockaddr_in));
	cliaddrlen = sizeof(struct sockaddr_in);
    while ((clifd = accept(core_data->fd_s, (struct sockaddr *)&cliaddr,&cliaddrlen)) > 0)
    {
		if (setnonblocking(clifd) < 0)
		{
			perror("setnonblocking error");
		}
        //添加一个客户描述符和事件
        worker_node = aosen_add_worker_node(clifd, core_data, aosen);
        if(worker_node == NULL)
            return -1;
        else
        {
            worker_node->event_user_fd = clifd;
            aosen_add_event(epollfd,clifd,EPOLLIN|EPOLLERR|EPOLLET);
            core_data->master_head->accept_cpu_core_id = (core_data->master_head->accept_cpu_core_id + 1) % core_data->cpu_core_num;
            return 0;
        }
    }
    if (clifd == -1)
    {
        if (errno != EAGAIN && errno != ECONNABORTED && errno != EPROTO && errno != EINTR)
            return -1;
        else
        {
            return 1;
        }
    }
}


/*一些列worker node 操作函数*/
//初始化worker node 并将worker node插到双向循环链表头部
static aosen_worker_node* aosen_add_worker_node(int event_user_fd, aosen_core_data *core_data, aosen_server* aosen)
{
    aosen_worker_node *worker_node=NULL;
    worker_node = (aosen_worker_node*)malloc(sizeof(aosen_worker_node));
    if(worker_node == NULL)
        return NULL;
    memset(worker_node, 0, sizeof(aosen_worker_node));

    /*构造双向循环链表*/
    //头插法
    worker_node->next_worker_node = core_data->worker_head->next_worker_node->next_worker_node;
    core_data->worker_head->next_worker_node->next_worker_node->prev_worker_node = worker_node;
    core_data->worker_head->next_worker_node->next_worker_node = worker_node;
    worker_node->prev_worker_node = core_data->worker_head->next_worker_node;

    core_data->worker_head->accept_num ++;
    return worker_node;
}

//增加读写缓冲区大小
static char *aosen_add_buf(char *old_buf, int iter, size_t size)
{
    char *tmp = NULL;
    tmp = (char*)malloc((iter+1)*size);
    if(tmp == NULL)
    {
        return NULL;
    }
    else
    {
        memset(tmp, 0, (iter+1)*size);
        memcpy(tmp, old_buf, iter*size);
    }
    return tmp;
}

//搜索event事件的worker node节点
//搜索算法需要优化
static aosen_worker_node* aosen_search_event(struct epoll_event *event, aosen_core_data *core_data, aosen_server *aosen)
{
    int i;
    aosen_worker_node *p=NULL;
    p = core_data->worker_head->next_worker_node->prev_worker_node->prev_worker_node;
    for(i=0; i < (core_data->worker_head->accept_num); i++)
    {
        if(p->event_user_fd == event->data.fd || p->event_server_fd == event->data.fd)
        {
            return p;
        }
        else
            p = p->prev_worker_node;
    }
    return NULL;
}

//删除worker node 节点
static void aosen_delete_worker_node(aosen_core_data *core_data, aosen_worker_node *worker_node)
{
    core_data->worker_head->accept_num --;

    worker_node->prev_worker_node->next_worker_node = worker_node->next_worker_node;
    worker_node->next_worker_node->prev_worker_node = worker_node->prev_worker_node;
    worker_node->prev_worker_node = NULL;
    worker_node->next_worker_node = NULL;

    if(worker_node->event_server_fd>0)
    {
        close(worker_node->event_server_fd);
    }
    if(worker_node->event_user_fd>0)
    {
        close(worker_node->event_user_fd);
    }

    if(worker_node!=NULL)
    {
        free(worker_node);
        worker_node=NULL;
    }
    printf("core:%d free done accept_num:%d\n", core_data->cpu_core_id, core_data->worker_head->accept_num);
}

//事件处理函数
static void aosen_handle_events(int epollfd,struct epoll_event *events,int num,aosen_server *aosen, aosen_core_data *core_data)
{
    int i;
    int fd;
    int res;
    int j;
    aosen_worker_node *worker_node=NULL;
    //进行选择遍历
    for (i = 0;i < num;i++)
    {
        fd = events[i].data.fd;
        //根据描述符的类型和事件类型进行处理
		//数据结构设计的有问题，需要把缓存放在worker_node中去，然后每有新连接到来，就加一个节点
        //出错或读就释放节点
		//涉及到链表的查找
		//查找后对worker_node中的buf进行复制
        if (fd==0 || fd==1 || fd==2)
        {
            /*防止出错*/
            continue;
        }
        else if ((fd == core_data->fd_s) &&(events[i].events & EPOLLIN))
        {
			//各worker依次接受请求
            if(core_data->master_head->accept_cpu_core_id == core_data->cpu_core_id)
            {
                res = aosen_handle_accept(epollfd, aosen, core_data);
                if(res == -1)
                {
                    printf("core:%d aosen_handle_accept error:%s errno:%d\n", core_data->cpu_core_id, strerror(errno), errno);
                    continue;
                }
                else if(res == 0)
                {
                    PRINTF("core:%d aosen_handle_accept success\n", core_data->cpu_core_id);
                    PRINTF("core:%d accept_cpu_core_id:%d\n", CPU, core_data->master_head->accept_cpu_core_id);
                    continue;
                }
                //accept 被阻塞,此事就绪队列里的链接都被处理
                else
                    continue;
            }
            else
            {
                continue;
            }
        }
        else if ((fd == core_data->fd_s) &&(events[i].events & EPOLLERR))
        {
            printf("accept error\n");
            continue;
        }

        worker_node = aosen_search_event(&events[i], core_data, aosen);
        if(worker_node == NULL)
        {
            continue;
        }
        else if(fd == worker_node->event_server_fd)
        {
            if(events[i].events & EPOLLIN)
            {
                PRINTF("core:%d start read from server\n", CPU);
                res = aosen_read_from_server(epollfd, fd, worker_node, aosen, core_data);
                if(res == 0)
                {
                    PRINTF("core:%d read from server buf:\n", CPU);
                }
                else if(res == -1)
                {
                    printf("core:%d read_from_server_buf error:%s\n", core_data->cpu_core_id, strerror(errno));
                    aosen_delete_event(epollfd, worker_node->event_user_fd, EPOLLIN|EPOLLOUT|EPOLLERR);
                    aosen_delete_event(epollfd, worker_node->event_server_fd, EPOLLIN|EPOLLOUT|EPOLLERR);
                    aosen_delete_worker_node(core_data, worker_node);
                }
                else if(res == 1)
                {
                    PRINTF("core:%d go on wait server\n", core_data->cpu_core_id);
                }
            }
            else if(events[i].events & EPOLLOUT)
            {
                PRINTF("core:%d start write to server\n", CPU);
                res = aosen_write_to_server(epollfd, fd, worker_node, aosen, core_data);
                if(res == 0)
                {
                    PRINTF("core:%d write to server buf:\n", CPU);
                    for(j=0;j<BUFFSIZE;j++)
                        PRINTF("%c", worker_node->read_from_user_buf[j]);
                    PRINTF("%c", '\n');
                }
                else if(res == -1)
                {
                    printf("core:%d write_to_server_buf error:%s\n", core_data->cpu_core_id, strerror(errno));
                    aosen_delete_event(epollfd, worker_node->event_user_fd, EPOLLIN|EPOLLOUT|EPOLLERR);
                    aosen_delete_event(epollfd, worker_node->event_server_fd, EPOLLIN|EPOLLOUT|EPOLLERR);
                    aosen_delete_worker_node(core_data, worker_node);
                }
            }
            else if(events[i].events & EPOLLERR)
            {
                printf("core:%d errorno:%d err:%s\n", core_data->cpu_core_id, errno, strerror(errno));
            }
        }
        else if(fd == worker_node->event_user_fd)
        {
            if(events[i].events & EPOLLIN)
            {
                PRINTF("core:%d start read from user\n", CPU);
                res = aosen_read_from_user(epollfd, fd, worker_node, aosen, core_data);
                if(res == 0)
                {
                    PRINTF("core:%d read from user buf:\n", CPU);
                }
                else if(res == -1)
                {
                    printf("core:%d read_from_user_buf error:%s\n", core_data->cpu_core_id, strerror(errno));
                    aosen_delete_event(epollfd, worker_node->event_user_fd, EPOLLIN|EPOLLOUT|EPOLLERR);
                    aosen_delete_event(epollfd, worker_node->event_server_fd, EPOLLIN|EPOLLOUT|EPOLLERR);
                    aosen_delete_worker_node(core_data, worker_node);
                }
                else if(res == 1)
                {
                    PRINTF("core:%d go on wait user\n", core_data->cpu_core_id);
                    continue;
                }
            }
            else if(events[i].events & EPOLLOUT)
            {
                PRINTF("core:%d start write to user\n", CPU);
                res = aosen_write_to_user(epollfd, fd, worker_node, aosen, core_data);
                if(res == 0)
                {
                    PRINTF("core:%d write_to_user_buf:\n", CPU);
                    for(j=0;j<BUFFSIZE;j++)
                        PRINTF("%c", worker_node->read_from_server_buf[j]);
                    PRINTF("%c", '\n');
                }
                else if(res == -1)
                {
                    printf("core:%d write_to_user_buf error:%s\n", core_data->cpu_core_id, strerror(errno));
                    aosen_delete_event(epollfd, worker_node->event_user_fd, EPOLLIN|EPOLLOUT|EPOLLERR);
                    aosen_delete_event(epollfd, worker_node->event_server_fd, EPOLLIN|EPOLLOUT|EPOLLERR);
                    aosen_delete_worker_node(core_data, worker_node);
                }
            }
            else if(events[i].events & EPOLLERR)
            {
                printf("core:%d errorno:%d err:%s\n", core_data->cpu_core_id, errno, strerror(errno));
            }
        }
        else
            printf("666666666666666666666\n");
    }
}

//处理事件
void aosen_do_epoll(aosen_server *aosen, aosen_core_data *core_data)
{
    int epollfd;
    struct epoll_event events[EPOLLEVENTS];
    int ret;
    /*注册信号处理*/
	signal(SIGPIPE, handle_signal);
    //创建一个描述符
    epollfd = epoll_create(FDSIZE);
    //添加监听描述符事件,边缘触发
    aosen_add_event(epollfd,core_data->fd_s,EPOLLIN | EPOLLET);
    printf("core:%d start run\n", core_data->cpu_core_id);
    for ( ; ; )
    {
        //获取已经准备好的描述符事件
        if(ret = epoll_wait(epollfd,events,EPOLLEVENTS,-1)>0)
            aosen_handle_events(epollfd,events,ret,aosen,core_data);
        else
            continue;
    }
    close(epollfd);
}

