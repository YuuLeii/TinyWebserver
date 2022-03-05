#ifndef WEBSERVER_H
#define WEBSERBER_H

#include "threadpool.h"
#include "http_conn.h"
#include "TimerQueue.h"

#include <string>
using std::string;

const int MAX_EVENT_NUM = 10000;
const int MAX_FD = 65535;
class WebServer{
public:
	WebServer(int port, int threadsnum, int sqlnum, string user, string passwd, string dbname);
	~WebServer(){};
	// void init();
	void eventLoop();

private:
	void onConnection();
	void onRead(int sockfd);
	void onWrite(int sockfd);

	char *root_;

	int threadsnum_;
	threadpool<http_conn> *pool_;
	connection_pool *connPool_;
	int port_;

	string dbUser_;
	string dbPasswd_;
	string dbName_;
	int sqlnum_;

	int listenfd_;
	int epollfd_;         // 这里有问题
	epoll_event events[MAX_EVENT_NUM];

	int timerfd_;
	TimerQueue timerqueue_;
	http_conn *users_;
};


#endif