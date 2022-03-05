#include "TimerQueue.h"

int createTimerfd() {
	int timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	if (timerfd < 0)
		// LOG_SYSFATAL << "Failed in timerfd_create";
	;
	return timerfd;
}
struct timespec howMuchTimeFromNow(Timestamp when) {
	int64_t microseconds = when.microSecondsSinceEpoch() - Timestamp::now().microSecondsSinceEpoch();
	if (microseconds < 100) {
		microseconds = 100;
	}
	struct timespec ts;
	ts.tv_sec = static_cast<time_t>(microseconds / Timestamp::kMicroSecondsPerSecond);
	ts.tv_nsec = static_cast<long>(microseconds % Timestamp::kMicroSecondsPerSecond) * 1000;
	return ts;
}
void readTimerfd(int timerfd, Timestamp now) {
	uint64_t howmany;
	ssize_t n = read(timerfd, &howmany, sizeof howmany);
	// LOG_TRACE << "TimerQueeu::handleRead() " << howmany << " at " << now.toString();
	if (n != sizeof howmany)
		std::cout << "What happened." << std::endl;
		// LOG_ERROR << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
}

void TimerQueue::addTimer(int connfd) {
	// printf("%s, %d\n", __func__, __LINE__);
	bool earliestChanged = false;
	Timestamp when = addTime(Timestamp::now(), TIMESLOT);
	Timer *timer = new Timer(connfd, when);
	TimerList::iterator it = timers_.begin();
	if (it == timers_.end() || when < it->first) 
		earliestChanged = true;

	{
		std::pair<TimerList::iterator, bool> result = timers_.insert(Entry(when, timer));
		assert(result.second);
	}
	if (earliestChanged)
		resetTimerfd(when);
	
}

void TimerQueue::resetTimerfd(Timestamp expiration) {
	// printf("%s, %d\n", __func__, __LINE__);
	struct itimerspec newValue;
	struct itimerspec oldValue;
	memset(&newValue, 0, sizeof newValue);
	memset(&oldValue, 0, sizeof oldValue);
	newValue.it_value = howMuchTimeFromNow(expiration);
	int ret = timerfd_settime(timerfd_, 0, &newValue, NULL);
	if (ret) {
		// LOG_SYSERR << "timerfd_settime()";
	}
}
vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now) {

	// printf("%s, %d\n", __func__, __LINE__);
	std::vector<Entry> expired;

	// for (const Entry& it: timers_) {
	// 	if (it.first < now) {
	// 		expired.push_back(it);
	// 		// timers_.erase(it);
	// 	}
	// }
	// for (auto &e : expired) {
	// 	timers_.erase(timers_.find(e));
	// }
	
	// 用更高效的库函数代替

	Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
	TimerList::iterator end = timers_.lower_bound(sentry);
	assert(end == timers_.end() || now < end->first);
	std::copy(timers_.begin(), end, back_inserter(expired));
	timers_.erase(timers_.begin(), end);
	return expired;
}

void TimerQueue::handleRead() {
	// printf("%s, %d\n", __func__, __LINE__);
	Timestamp now(Timestamp::now());
	readTimerfd(timerfd_, now);

	vector<Entry> expired = getExpired(now);
	
	assert(users_ != NULL);
	for (const Entry& it: expired) {
		users_[it.second->fd()].closeConnection();
		// std::cout << "closed fd: " << it.second->fd() << std::endl;
		// close(it.second->fd());
	}

	for (const Entry& it : expired)
		delete it.second;
	
	Timestamp nextExpire;
	if (!timers_.empty())
		nextExpire = timers_.begin()->second->expiration();

	if (nextExpire.valid())
		resetTimerfd(nextExpire);
	
}
void TimerQueue::updateTimer(int connfd) {
	// printf("%s, %d\n", __func__, __LINE__);
	Timer *timer = NULL;
	for (auto it = timers_.begin(); it != timers_.end(); ++ it) {
		if (it->second->fd() == connfd) {
			timer = it->second;
			timers_.erase(it);
			break;
		}
	}
	Timestamp nextExpiration = addTime(Timestamp::now(), TIMESLOT);

	timer->restart(nextExpiration);
	auto result = timers_.insert(std::pair<Timestamp, Timer*>(nextExpiration, timer));
	assert(result.second);
}