#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "webserver.h"

WebServer::WebServer(int port, int threadsnum, int sqlnum, string user, string passwd, string dbname){
	//root文件及路径
	char server_path[200];
	getcwd(server_path, 200);
	char root[6] = "/root";
	root_ = (char *)malloc(strlen(server_path) + strlen(root) + 1);
	strcpy(root_, server_path);
	strcat(root_, root);
	
	port_ = port;
	threadsnum_ = threadsnum;
	sqlnum_ = sqlnum;
	pool_ = threadpool<http_conn>::getInstance(threadsnum_);

	epollfd_ = epoll_create(1);
	assert(epollfd_ >= 0);
	http_conn::epollfd_ = epollfd_;


	listenfd_ = socket(AF_INET, SOCK_STREAM, 0);
	assert(listenfd_ >= 0);	
	//设置端口重用
	int flag = 1;
	assert(!setsockopt(listenfd_, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)));

	epoll_event ev;
	ev.data.fd = listenfd_;
	ev.events = EPOLLIN;

	int ret = epoll_ctl(epollfd_, EPOLL_CTL_ADD, listenfd_, &ev);
	assert(ret != -1);

	dbUser_ = user;
	dbPasswd_ = passwd;
	dbName_ = dbname;

	users_ = new http_conn[MAX_FD];
	assert(users_);
    
	// 初始化数据库连接池
	connPool_ = connection_pool::getInstance();
	assert(connPool_ != NULL);
	connPool_->init("localhost", user, passwd, dbname, 3306, sqlnum_);

	users_->initmysql_result(connPool_);

	timerfd_ = createTimerfd();
	timerqueue_.setTimerfd(timerfd_);
	timerqueue_.users_ = users_;

	ev.data.fd = timerfd_;
	ret = epoll_ctl(epollfd_, EPOLL_CTL_ADD, timerfd_, &ev);
	assert(ret != -1);
}

void WebServer::eventLoop(){
	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port_);
	int ret = 0;
	socklen_t socklen = sizeof(addr);
	ret = bind(listenfd_, (sockaddr*)&addr, socklen);
	assert(ret >= 0);
	ret = listen(listenfd_, 5);
	assert(ret >= 0);

	bool timeout = false;
	while (1){
		int num = epoll_wait(epollfd_, events, MAX_EVENT_NUM, -1);
		assert(num >= 0);
		for (int i = 0; i < num; ++ i){
			int fd = events[i].data.fd;
			if (fd == listenfd_){
				onConnection();
			}else if (fd == timerfd_){
				timerqueue_.handleRead();
				
			}else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
				printf("出错啦...\n");
				if (events[i].events & EPOLLRDHUP) printf("ERROR: EPOLLRDHUP\n");
				else if (events[i].events & EPOLLHUP) printf("ERROR: EPOLLHUP\n");
				else if (events[i].events & EPOLLERR) printf("ERROR: EPOLLERR\n");
				users_[fd].closeConnection();
			}else if (events[i].events & EPOLLIN){     //处理客户连接上接收到的数据
				onRead(fd);
			}else if (events[i].events & EPOLLOUT){
				
				onWrite(fd);
			}
		}
		if (timeout){
			// bool ok = dealWithTimeout();
			timeout = false;
		}
	}
}

void WebServer::onConnection(){
	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	socklen_t len = sizeof(addr);
	int clientfd = accept(listenfd_, (sockaddr*)&addr, &len);
	assert(clientfd >= 0);
	// printf("Accept new connection, fd: %d, port: %d, address: %s, Now have %d connections\n", clientfd, ntohs(addr.sin_port), inet_ntoa(addr.sin_addr), http_conn::user_count_);
	
	if (http_conn::user_count_ >= MAX_FD){
		// Output somethins.
		const char *errorinfo = "Internal server busy.\n";
		send(clientfd, errorinfo, strlen(errorinfo), 0);
		close(clientfd);
		return ;
	}
	users_[clientfd].init(clientfd, addr, root_, dbUser_, dbPasswd_, dbName_, connPool_);
	timerqueue_.addTimer(clientfd);
	
}
void WebServer::onRead(int sockfd){
	// printf("Calling onRead...., fd: %d\n", sockfd);
	// Adjust timer
	if (users_[sockfd].state_ != 0) {
		users_[sockfd].init();
	}
	users_[sockfd].state_ = 0;
	pool_->addtask(users_ + sockfd);
}

void WebServer::onWrite(int sockfd){
	// printf("Calling onWrite...., state: %d\n", users_[sockfd].state_);
	//Adjust timer
	if (users_[sockfd].state_ != 1) return;
	pool_->addtask(users_ + sockfd);
	timerqueue_.updateTimer(sockfd);
}
