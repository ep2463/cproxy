本版本为转发服务器，将用户发过来的数据转发给tornado

解决由于没有判断epool_wait的返回值，而引起fd=0的bug

采用epool
master - worker
worker数等于cpu数 并与cpu进行绑定
master进程：服务区初始化，加载配置文件，管理worker进程 当有worker进程异常退出后，重启新的worker
worker进程：接收用户请求，处理用户请求信息，将处理后的亲求信息发送给目标服务器，就收目标服务器的回传信息，处理回传信息，将处理后的目标服务器回传信息发送给用户
            接收master进程的指令

采用共享内存进行通信

gcc -shared src/core/init.c src/event/event.c src/json/cJSON.c -o libaosen.so -fPIC
gcc main.c -L. -laosen -lm -o aosen
