#ifndef __AOSEN_CORE__
#define __AOSEN_CORE__

//#define DEBUG
#ifdef DEBUG
#define PRINTF printf
#else
#define PRINTF //
#endif

#define TEST1 printf("core:%d 11111111111111111111111111\n", CPU);
#define TEST2 printf("core:%d 22222222222222222222222222\n", CPU);
#define TEST3 printf("core:%d 33333333333333333333333333\n", CPU);

#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/epoll.h>
#include <sched.h>

#include <aosen/aosen_cJSON.h>

/*最大处理文件描述符个数*/
#define FDSIZE 1000
#define EPOLLEVENTS 500

/*读写缓冲区大小*/
#define BUFFSIZE 4096


#define CPU core_data->cpu_core_id


//worker链表数据结构
typedef struct _aosen_worker_node_
{
    /*用户与代理服务器事件*/
    int event_user_fd;
    /*本地服务器与代理服务器事件*/
    int event_server_fd;
    /*用户与代理服务器建立连接的时间*/
    int state;
    /*记录从用户端读取次数，第一次需要获得http header*/
	char read_from_user_buf[BUFFSIZE];
    char read_from_server_buf[BUFFSIZE];
    int read_from_user_size;
    int read_from_server_size;
    int read_from_server_num;
    int content_length;
    int now_length;
	/*客户端读取目标服务器的缓冲区*/
	struct _aosen_worker_node_ *next_worker_node;
    struct _aosen_worker_node_ *prev_worker_node;
}aosen_worker_node;

//worker链表头
typedef struct _aosen_worker_head_
{
    //调用内存格式化函数beautify_share_mem
	//当前该worker进程处理的连接数
	int accept_num;
	/*绑定的核id*/
	int cpu_core_id;
	/*worker进程id*/
	pid_t pid;
	//指向下一节点的指针
	struct _aosen_worker_node_ *next_worker_node;
}aosen_worker_head;

//master链表节点
typedef struct _aosen_master_node_
{
	//指向下一节点的指针
	struct _aosen_master_node *next_master_node;
}aosen_master_node;

//master链表头
typedef struct _aosen_master_head_
{
	//master链表长度
	int len;
    /*谁有权利accept*/
    int accept_cpu_core_id;
	/*cpu核心数*/
	int cpu_core_num;
    //服务端文件描述符
    int fd_s;
	/*master进程id*/
	pid_t master_pid;
	//指向下一个master节点的指针
	struct _aosen_master_node_* next_master_node;
}aosen_master_head;


//服务器内核结构体，开发者无法访问到
typedef struct _aosen_core_date_
{
    //端口号
    int port;
    //监听最大值
    int max_listen;
	//cpu的核心数
	int cpu_core_num;
	//绑定的核id
	int cpu_core_id;
    //master进程pid
    pid_t master_pid;
    //当前进程pid
    pid_t pid;
	//共享内存key
	key_t share_mem_key;
	//共享内存大小　
	size_t share_mem_size;
	//共享内存id
	int shmid;
	//共享内存指针
	aosen_master_head *master_head;
	//共享内存中对应该进程的worker_head头指针
	aosen_worker_head *worker_head;
    //服务端文件描述符
    int fd_s;
	//客户端文件描述符
	int fd_c;
    //本地服务器地址
    char *local_server;
    //本地服务器端口号
    int local_port;
}aosen_core_data;


//aosen_server结构体 贯穿进程始终,该结构体对开发人员可见
//包括开发者使用的各种接口
//由于采用epoll 事件驱动，请不要在实现函数中调用阻塞函数
typedef struct _aosen_server_
{
    /*本地请求地址*/
    char* local_server;
    /*本地请求端口号*/
    int local_port;
}aosen_server;


/*run aosen 程序正式运行*/
extern void aosen_run(aosen_server*);

//master进程开启 主要功能是管理worker进程，初始化共享内存
extern void aosen_master_run(aosen_server* aosen, aosen_core_data *core_data);
//worker 进程 处理事件主函数
extern void aosen_do_epoll(aosen_server *aosen, aosen_core_data *core_data);

#endif
