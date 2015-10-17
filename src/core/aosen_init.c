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
#include <sys/sysinfo.h>
#include <fcntl.h>     /* nonblocking */
#include <netinet/in.h>      /* sockaddr_in{} and other Internet defns */
#include <arpa/inet.h>       /* inet(3) functions */
#include <errno.h>
#include <signal.h>
#include <sys/ipc.h>

#include <aosen/aosen_core.h>
#include <aosen/aosen_cJSON.h>
#include <aosen/aosen_client.h>

/*
 * 本文件为服务器核心文件，包含配置文件加载，socket启动 进程启动 共享内存开辟 
 * 共享内存格式化 共享内存访问方法 worker进程启动事件监听 
 * master进程开启监控，默认接口实现函数
 *
 * 此文件包含从程序启动到master进程 worker进入就绪态过程的所有逻辑
 * */

/*用户加载配置文件*/
static char* load_file(const char *);
/*利用cJson解析jeon数据*/
static cJSON* parse_json(char *);
/*初始化配置文件*/
static void init_conf(aosen_core_data* , aosen_server*, const char *);
/*初始化socket 开始监听*/
static int init_socket(int port, int listen_num);
/*初始话进程，默认进程数与当前可用cpu核心数相等*/
static void init_process(aosen_server* aosen, aosen_core_data* core_data);

//初始化内存
static int aosen_init_share_mem(aosen_server *aosen,aosen_core_data *core_data);
static int aosen_free_share_mem(aosen_server *aosen, aosen_core_data *core_data);
//建设共享内存
static aosen_master_head *beautify_share_mem(aosen_core_data* core_data, aosen_master_head *master_head);

//一系列链表操作方法
/*创建双向循环worker head worker node循环链表*/
static void init_worker_head_list(aosen_core_data *core_data);


/*加载配置文件*/
static char* load_file(const char *filename)
{
    FILE *fp = fopen(filename, "r");
	if(fp==NULL)
	{
		printf("no config file\n");
		exit(-1);
	}
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *data = (char*)malloc(len + 1);
    fread(data, 1, len, fp);
    fclose(fp);
    return data;
}

static cJSON* parse_json(char *data)
{
    cJSON *root_json = cJSON_Parse(data);
    free(data);
    if (NULL == root_json)
    {
        printf("error:%s\n", cJSON_GetErrorPtr());
        cJSON_Delete(root_json);
        exit(-1);
    }
    else
        return root_json;
}

static void init_conf(aosen_core_data *core_data, aosen_server* aosen, const char *filename)
{
    char *data = load_file(filename);
    /*加载配置文件*/
    printf("load config file\n");
    cJSON* js = parse_json(data);
	cJSON *cp;
	if(NULL==(cp=cJSON_GetObjectItem(js, "port")))
    {
		printf("no port\n");
        exit(-1);
    }
	else
		core_data->port = cp->valueint;
	if(NULL==(cp=cJSON_GetObjectItem(js, "listen")))
    {
		printf("no listen\n");
        exit(-1);
    }
	else
		core_data->max_listen = cp->valueint;
	if(NULL==(cp=cJSON_GetObjectItem(js, "share_mem_key")))
    {
		printf("no share_mem_key\n");
        exit(-1);
    }
	else
		core_data->share_mem_key = cp->valueint;
	if(NULL==(cp=cJSON_GetObjectItem(js, "local_server")))
    {
		printf("no local_server\n");
        exit(-1);
    }
	else
    {
		core_data->local_server = cp->valuestring;
        aosen->local_server = cp->valuestring;
    }
	if(NULL==(cp=cJSON_GetObjectItem(js, "local_port")))
    {
		printf("no local port\n");
        exit(-1);
    }
	else
    {
		core_data->local_port = cp->valueint;
        aosen->local_port = cp->valueint;
    }

    printf("port:%d\nmax_listen:%d\n", core_data->port, core_data->max_listen);
}


/*初始化服务端socket*/
static int init_socket(int port, int listen_num)
{   
    int ss;
	int reuse = 1;
    struct sockaddr_in server_addr;
    int err;
    
    ss = socket(AF_INET, SOCK_STREAM, 0);
    if (ss<0)
    {
        printf("socket error\n");
        exit(-1);
    }

	//防止服务器因重启导致端口被占用 绑定端口失败 如果不设置的话，当进程退出后，端口会保留2-4分钟，开始阶段让人抓狂
	if (setsockopt(ss, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
	{
		perror("setsockopet error\n");
		return -1;
	}
    
    /*设置服务器地址*/
    bzero(&server_addr, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    /*绑定地址结构到套接字描述符*/
    err = bind(ss, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if(err<0)
    {
        perror("bind error");
        exit(-1);
    }

    /*设置监听*/
    err = listen(ss, listen_num);
    if(err)
    {
        printf("listen error\n");
        exit(-1);
    }

    //将socket建立为非阻塞
    int flags = fcntl(ss, F_GETFL, 0);
    fcntl(ss, F_SETFL, flags|O_NONBLOCK);
    printf("start listen\n");
    return ss;
}


/*初始化进程*/
static void init_process(aosen_server* aosen, aosen_core_data* core_data)
{
    int i;
    int pid;
	int size;

	core_data->master_pid = getpid();
	core_data->pid = core_data->master_pid;
	cpu_set_t mask;
	CPU_ZERO(&mask);
    printf("cpu core num: %d\n", core_data->cpu_core_num);
    printf("master pid: %d\n", core_data->master_pid);
    for(i=0;i<core_data->cpu_core_num;i++)
    {
        if(getppid() != core_data->master_pid)
        {
            pid = fork();
            if(pid==0)
            {
                //将进程绑定到cpu核上
                pid_t my_pid = getpid();
	            CPU_SET(i, &mask);
                if(sched_setaffinity(0, sizeof(mask), &mask) < 0)
                {
                    printf("pid=%d bind cpu core fail\n", my_pid);
                    exit(-1);
                }
                else
                {
					core_data->cpu_core_id = i;
                    printf("pid:%d bind cpu_core_id:%d\n", my_pid, i);
					core_data->pid = getpid();
                }
            }
            else
            {
                continue;
            }
        }
        else
            break;
    }
}


/*创建双向worker head worker node循环链表*/
static void init_worker_head_list(aosen_core_data *core_data)
{
    aosen_worker_node *worker_node_a = (aosen_worker_node*)malloc(sizeof(aosen_worker_node));
    aosen_worker_node *worker_node_b = (aosen_worker_node*)malloc(sizeof(aosen_worker_node));
    memset(worker_node_a, 0, sizeof(aosen_worker_node));
    memset(worker_node_b, 0, sizeof(aosen_worker_node));

    core_data->worker_head->next_worker_node = worker_node_a;

    worker_node_a->next_worker_node = worker_node_b;
    worker_node_a->prev_worker_node = worker_node_b;

    worker_node_b->next_worker_node = worker_node_a;
    worker_node_b->prev_worker_node = worker_node_a;
}

static aosen_master_head* beautify_share_mem(aosen_core_data* core_data, aosen_master_head *master_head)
{
	int i = 0;
	/*将指向共享内存初始位置的指针移动到master head的尾部，并转化为worker head*/
	aosen_master_head* p = master_head;
	aosen_worker_head *worker_head;
	p ++;
	worker_head = (aosen_worker_head*)p;
	master_head->cpu_core_num = core_data->cpu_core_num;
	/*构建与cpu核心数相同的worker head个数*/
	for(i=0;i<core_data->cpu_core_num;i++)
	{
		worker_head ++;
	}
	return master_head;
}

/*初始化共享内存*/
static int aosen_init_share_mem(aosen_server *aosen, aosen_core_data* core_data)
{
    /*初始化共享内存大小*/
    core_data->share_mem_size = sizeof(struct _aosen_master_head_) + sizeof(struct _aosen_worker_node_) * core_data->cpu_core_num;

	aosen_master_head *master_head = NULL;
	int shmid;
	void *shm = NULL;

	//创建共享内存,内存大小在配置文件中
	if(-1 == (shmid = shmget(core_data->share_mem_key, core_data->share_mem_size , IPC_CREAT| IPC_EXCL | 0666)))
	{
		if(errno == EEXIST)
		{
			shmid = shmget(core_data->share_mem_key, core_data->share_mem_size,0666);
		}
		else
		{
			perror("shmget");
			exit(-1);
		}
	}

	core_data->shmid = shmid;

	//第一次创建完共享内存时，它还不能被任何进程访问，shmat函数的作用就是用来启动对该共享内存的访问，并把共享内存连接到当前进程的地址空间。
	shm = shmat(shmid, 0, 0);
	if(shm == (void*)-1)
	{
        printf("shmat error:%s errorno:%d\n", strerror(errno), errno);
		exit(-1);
	}
	//设置共享内存
	master_head = (aosen_master_head*)shm;
	core_data->master_head = master_head;
    //clean share_mem
    memset(master_head, 0, core_data->share_mem_size);

	//格式化共享内存
	//构造master_head头节点
	beautify_share_mem(core_data, master_head);

	if(shmdt((void*)master_head) == -1)
	{
		perror("shmdt fail:");
	}
	return core_data->share_mem_size;
}

static int aosen_free_share_mem(aosen_server *aosen, aosen_core_data *core_data)
{
	//删除共享内存
	if(shmctl(core_data->shmid, IPC_RMID, 0) == -1)
	{
		perror("shmctl fail:");
	}
	printf("delete share mem success\n");
}

/*服务器启动函数*/
void aosen_run(aosen_server *aosen)
{
	/*初始化服务器核心数据*/
	aosen_core_data core_data;

    /*初始化配置文件, 并给core_data aosen初始化*/
    init_conf(&core_data, aosen, "./conf.json");

    //int ss, cc;
    int ss;
    /*初始化服务端socket 开始监听*/
    ss = init_socket(core_data.port, core_data.max_listen);
    /*初始化客户端socket*/
    //cc = init_client_socket();
	/*主进程PID*/
    int master_pid = getpid();

	core_data.master_pid = master_pid;
	core_data.fd_s = ss;
    //core_data.fd_c = cc;
	/*获得cpu核心数*/
    core_data.cpu_core_num = get_nprocs(); 
	/*初始化共享内存*/
	int size = aosen_init_share_mem(aosen, &core_data);
	PRINTF("init: %d byte share memary\n", size);

	/*初始化进程*/
    init_process(aosen, &core_data);

    if(core_data.pid == core_data.master_pid)
	{
        /*共享内存映射master进程*/
		aosen_master_head* mh = shmat(core_data.shmid,NULL,0);
		if(mh == (void *)-1)
		{
            printf("shmat error:%s errorno:%d\n", strerror(errno), errno);
			exit(-1);
	    }
		core_data.master_head = mh;
        sleep(1);
        printf("master start run\n");
		aosen_master_run(aosen, &core_data);
	}
	else
	{
		/*此时共享内存已经美化完毕，需要将cpu_core_id 和 pid进行关联*/
		//内存映射到子进程
		aosen_master_head* mh = shmat(core_data.shmid,NULL,0);
		if(mh == (void *)-1)
		{
            printf("shmat error:%s errorno:%d\n", strerror(errno), errno);
			exit(-1);
	    }
		core_data.master_head = mh;
		//将指针移动到第一个worker head处，并初始化
        aosen_master_head *q = core_data.master_head;
        q ++;
		aosen_worker_head *p = (aosen_worker_head*)q;
		//移动到进程相应的worker head
		p = p + core_data.cpu_core_id;
		core_data.worker_head = p;
        core_data.worker_head->cpu_core_id = core_data.cpu_core_id;
        core_data.worker_head->pid = core_data.pid;
        core_data.worker_head->accept_num = 0;

        core_data.master_head->len = 0;
        core_data.master_head->cpu_core_num = core_data.cpu_core_num;
        core_data.master_head->fd_s = core_data.fd_s;
        core_data.master_head->master_pid = core_data.master_pid;
        core_data.master_head->accept_cpu_core_id = 0;

        /*初始化双向循环链表 在worker_head  下生成第一worker node*/
        init_worker_head_list(&core_data);

		/*worker进程开始启动*/
        sleep(2);
		aosen_do_epoll(aosen, &core_data);
	}
}
    
