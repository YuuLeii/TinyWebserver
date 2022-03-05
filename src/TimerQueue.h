#ifndef TIMERQUEUE_H
#define TIMERQUEUE_H

#include <iostream>
#include <set>
#include <vector>
#include <assert.h>
#include <memory.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <inttypes.h>
#include "Timestamp.h"
#include "http_conn.h"

using std::vector;
// 多久踢掉连接
const int TIMESLOT = 30;


int createTimerfd();
class Timer {
private:
	int fd_;
	Timestamp expiration_;
public:
	Timer(int fd, Timestamp expiration) 
	: fd_(fd), expiration_(expiration) 
	{

	}
	int fd() {	return fd_; }
	Timestamp expiration() const { return expiration_; }
	void restart(Timestamp now) {
		expiration_ = now;
	}
};

class TimerQueue {
private:
	typedef std::pair<Timestamp, Timer*> Entry;
	typedef std::set<Entry> TimerList;

	int timerfd_;
	TimerList timers_;
	
public:
	http_conn *users_;

	TimerQueue() : timerfd_(-1) {};
	void setTimerfd(int timerfd) { timerfd_ = timerfd; }

	// call when timerfd alarms
	void handleRead();
	void addTimer(int connfd);
	void updateTimer(int connfd);
private:
	void resetTimerfd(Timestamp expieration);
	std::vector<Entry> getExpired(Timestamp now);
};

#endif  // TIMERQUEUE_H