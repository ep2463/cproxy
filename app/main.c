#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>

#include <aosen/aosen_core.h>
#include <aosen/aosen_cJSON.h>


int main(int argc, const char *argv[])
{
	/*初始化aosen server*/
	aosen_server aosen;
    /*主循环 启动aosen_run*/
    aosen_run(&aosen);
	return 0;
}
