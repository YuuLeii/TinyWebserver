#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>

#include <assert.h>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include <functional>
#include <vector>
#include <deque>
#include "locker.h"

/*
    同一个连接上的不同请求可能会由线程池中的不同线程处理
*/

template<class T>
class threadpool{
public:
    threadpool(const threadpool&) = delete;
    const threadpool& operator=(const threadpool&) = delete;

    static threadpool *getInstance(int size);
    ~threadpool();

    // using task_t = std::function<void()>;   
    // void addtask(const task_t&);

    void addtask(T *);
    void stop();
private:
    threadpool(int size);
    // void start();
    static void *loop(void *);

    // std::vector<pthread_t*> threads_;      //segament fault
    pthread_t *threads_;
    std::deque<T *> tasks_;
    int threadsnum_;
    bool isStarted_;
    locker lock_;
    sem sem_;

    static threadpool* threadpool_;
};


template<class T>
threadpool<T>::threadpool(int size): threadsnum_(size){
    threads_ = new pthread_t[threadsnum_];
    isStarted_ = true;
    for (int i = 0; i < threadsnum_; ++ i){
        if (pthread_create(threads_ + i, NULL, loop, this)){
            throw std::exception();
        }
        if (pthread_detach(threads_[i])){
            throw std::exception();
        }
    }
}

template <class T>
threadpool<T>::~threadpool(){
    stop();
    delete[] threads_;
}

template <class T>
threadpool<T> *threadpool<T>::getInstance(int size){
    printf("%s, %d\n", __func__, __LINE__);
    static threadpool tpool(size);
    return &tpool;
}
template <class T>
void threadpool<T>::addtask(T *request){
    lock_.lock();
    tasks_.push_back(request);
    lock_.unlock();
    sem_.post();
}

template <class T>
void *threadpool<T>::loop(void *arg){
    threadpool *pool = (threadpool *)arg;
    // printf("%s, %d, PID: %ld\n", __func__, __LINE__, pthread_self());
    while (pool->isStarted_){
        pool->sem_.wait();
        pool->lock_.lock();
        if (pool->tasks_.empty()){
            pool->lock_.unlock();
            continue;
        }
        // threadpool::task_t task = pool->tasks_.front();
        T *task = pool->tasks_.front();
        pool->tasks_.pop_front();
        pool->lock_.unlock();

        // task();
        assert(task != nullptr);
        task->process();
    }
    return pool;
}

template <class T>
void threadpool<T>::stop(){
    isStarted_ = false;
}
#endif