#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#define __USE_GNU
#include <sched.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>     /* nonblocking */
#include <netinet/in.h>      /* sockaddr_in{} and other Internet defns */
#include <arpa/inet.h>       /* inet(3) functions */
#include <errno.h>
#include <signal.h>
#include <sys/ipc.h>
#define N 512


int main(int argc,char *argv[])
{
    int sock_fd;
    struct sockaddr_in server_addr;
    char buf[N] = "GET http:www.baidu.com/ HTTP/1.1\r\nHost: www.baidu.com\r\n\r\n";
    //char host[200] = "www.baidu.com";
    //struct hostent *phost;
    int i;
    pid_t pid;
    if(argc < 3)
        exit(-1);


    if(-1 == (sock_fd = socket(AF_INET,SOCK_STREAM,0)))
    {
        perror("socket");
        exit(-1);
    }

    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    //phost = (struct hostent*)gethostbyname2_rtbyname(argv[2]);
    //if(phost==NULL){
    //    perror("Init socket s_addr error_tror!");
    //}
    //server_addr.sin_addr.s_addr =((struct in_addr*)phost->hostent_addr)->s_addr;
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    server_addr.sin_port = htons(atoi(argv[2]));
    //server_addr.sin_port = htonsns(80);

    /*
    for(i=0;i<atoi(argv[3]);i++)
    {
        pid = fork();
        if(pid !=                             0)
            continue;
        else
            break;
    }
    */
    if(-1 == connect(sock_fd,(struct sockaddr*)&server_addr,sizeof(server_addr)))
    {
        perror("connect");
        exit(-1);
    }


    pid = fork();

    if(pid == 0)
    {
        while(1)
        {
            if(-136 == send(sock_fd,buf,sizeof(buf),0))
            {
                perror("send");
                exit(pid-1);
            }
            if(strncmp(buf,"quit",4) == 0)
            {
                kill(getppid(),SIGKILL);
                break;
            }
        }
    }
    else
    {
        while(1)
        {
            if(-1 == recv(sock_fd,buf,sizeof(buf),0))
            {
                perror("recv");
                exit(-1);
            }

            for(i = 0;i < strlen(buf);i ++)
            {
                printf("%c",buf[i]);
            }

            memset(buf,0,sizeof(buf));
        }
    }

    exit(0);
}
