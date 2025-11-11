#include "../include/threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

//////////线程池方法实现
const int TASK_MAX_THRESHHOLD = 1024;

ThreadPool::ThreadPool()
    : initThreadSize_(0),
      taskSize_(0),
      taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD),
      poolMode_(PoolMode::MODE_FIXED)
{}

ThreadPool::~ThreadPool()
{}

void ThreadPool::setMode(PoolMode mode)
{
    poolMode_=mode;
}
void ThreadPool::setInitThreadSize(int num)
{
    initThreadSize_=num;
}
void ThreadPool::setTaskQueMaxThreshHold(int size)
{
    taskQueMaxThreshHold_=size;
}
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
    //获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    //线程通信，等待任务队列有空余
    if(!notFull_.wait_for(lock,std::chrono::seconds(1),[&]()->bool{return taskQueue_.size()<(size_t)taskQueMaxThreshHold_;}))
    {
        //等待 1 秒还是没满足
        std::cerr<<"taskQue is full, submit task fail"<<std::endl;
        return Result(sp,false);
    }
    //如果有空，把任务放入任务队列
    taskQueue_.emplace(sp);
    taskSize_++;
    //放入队列，队列不空，在 notEmpty_通知
    notEmpty_.notify_all();

    return Result(sp);
}
void ThreadPool::start(int initThreadSize)
{
    //记录初始线程个数
    initThreadSize_=initThreadSize;

    //创建线程对象
    for(int i=0;i<initThreadSize_;i++)
    {
        auto ptr=std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,this));
        threads_.emplace_back(std::move(ptr));
    }

    //启动所有线程
    for(int i=0;i<initThreadSize_;i++)
    {
        threads_[i]->start();
    }
}

//自定义线程函数，从任务队列取任务
void ThreadPool::threadFunc()
{
    // std::cout<<"begin func"<<std::endl;
    // std::cout<<std::this_thread::get_id()<<std::endl;
    // std::cout<<"end func"<<std::endl;
    for(;;)
    {
        std::shared_ptr<Task> task;
        {
            std::unique_lock<std::mutex>lock(taskQueMtx_);

            std::cout<<"tid="<<std::this_thread::get_id()<<" try to get task"<<std::endl;

            notEmpty_.wait(lock,[&]()->bool{return taskQueue_.size()>0;});

            std::cout<<"tid="<<std::this_thread::get_id()<<" get task"<<std::endl;

            task=taskQueue_.front();
            taskQueue_.pop();
            taskSize_--;
            //有剩余任务，通知其他线程
            if(taskQueue_.size()>0)
            {
                notEmpty_.notify_all();
            }

            //取出任务，进行通知
            notFull_.notify_all();
        } //释放锁
        //当前线程负责执行这个任务
        if(task!=nullptr){
            //task->run();//把任务返回值 setVal 传给 Result
            task->exec();
        }
    }
}

//////////线程类方法实现

void Thread::start()
{
    //创建一个线程来执行一个函数
    std::thread t(func_);
    t.detach();
}

Thread::Thread(ThreadFunc func)
    :func_(func){}

Thread::~Thread(){}

///////////////////////Task 实现

Task::Task()
    : result_(nullptr)
{}

void Task::exec()
{
    if(result_==nullptr) return;
    result_->setVal(run());
}

void Task::setResult(Result* res)
{
    result_=res;
}

///////////////////////Result 方法实现
Result::Result(std::shared_ptr<Task> task,bool isValid):
        task_(task),isValid_(isValid)
{
    task_->setResult(this);
}

Any Result::get()
{
    if(!isValid_) return "";
    sem_.wait();//task 任务如果没有执行完毕
    return std::move(any_);
}

void Result::setVal(Any any)
{
    this->any_=std::move(any);
    sem_.post();
}