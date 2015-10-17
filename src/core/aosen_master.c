#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <aosen/aosen_core.h>

/*
 * 此文件为master进程核心文件，主要用于管理worker进程状况，监控各worker进程的健康状况，
 * 防止恶意攻击的逻辑都在这里，设置同意连接的访问频率。
 */

/*master控制逻辑*/
/*master检查worker进程是否正常运行*/
static void master_check_worker();
/*信号处理函数*/
static void handle_signal(int signo);
/*服务器终止函数*/
static void aosen_stop();


static void master_check_worker()
{
    printf("22222222222222222222222222\n");
}

static void aosen_stop() 
{

}

static void handle_signal(int signal)
{
	switch(signal)
	{
	case SIGCHLD:
		master_check_worker();
	default:
		aosen_stop();
	}
}


/*等待自进程报告*/
void aosen_master_run(aosen_server *aosen, aosen_core_data *core_data)
{
    int status;
    int i;
    pid_t ret;
    aosen_worker_head *worker_head = NULL;
	//信号处理
	//signal(SIGHUP, handle_signal);
	//signal(SIGINT, handle_signal);
	//signal(SIGQUIT, handle_signal);
	//signal(SIGILL, handle_signal);
	//signal(SIGABRT, handle_signal);
	//signal(SIGKILL, handle_signal);
	//signal(SIGTERM, handle_signal);
	//子进程终止信号，master需要重新建立新的worker进程
	//signal(SIGCHLD, handle_signal);
	//signal(SIGSTOP, handle_signal);
	//signal(SIGTSTP, handle_signal);


	while(1)
	{
        sleep(3);
        ret = waitpid(-1,&status,0);
        if(ret <0){
            perror("master wait error\n");
        }

        aosen_master_head *q = core_data->master_head;
        q ++;
        aosen_worker_head *p = (aosen_worker_head*)q;
        for(i=0;i<core_data->cpu_core_num;i++)
        {
            if(ret==p->pid)
                break;
            else
                p ++;
        }

        if (WIFEXITED(status))
            printf("core:%d exited normal exit status=%d\n",p->cpu_core_id, WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
        {
            printf("core:%d exited abnormal signal number=%d\n",p->cpu_core_id, WTERMSIG(status));
        }
        else if (WIFSTOPPED(status))
            printf("core:%d stoped signal number=%d\n",p->cpu_core_id , WSTOPSIG(status));
	}
}


