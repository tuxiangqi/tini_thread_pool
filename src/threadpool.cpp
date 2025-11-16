#include "../include/threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

//////////线程池方法实现
const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60;

ThreadPool::ThreadPool()
    : initThreadSize_(0),
      taskSize_(0),
      taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD),
      poolMode_(PoolMode::MODE_FIXED),
      isPoolRunning_(false),
      idleThreadSize_(0),
      curThreadSize_(0),
      threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
{
}

ThreadPool::~ThreadPool()
{
    isPoolRunning_ = false;
    // 等待所有线程退出
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    notEmpty_.notify_all(); // 唤醒所有线程，让其退出
    exitCond_.wait(lock, [&]() -> bool
                   { return curThreadSize_ == 0; });
}

void ThreadPool::setMode(PoolMode mode)
{
    if (checkRunningState())
        return;
    poolMode_ = mode;
}
void ThreadPool::setInitThreadSize(int num)
{
    if (checkRunningState())
        return;
    initThreadSize_ = num;
}
void ThreadPool::setTaskQueMaxThreshHold(int size)
{
    if (checkRunningState())
        return;
    taskQueMaxThreshHold_ = size;
}

void ThreadPool::setThreadSizeMaxThreshHold(int size)
{
    if (checkRunningState())
        return;
    if (poolMode_ == PoolMode::MODE_CACHED)
    {
        threadSizeThreshHold_ = size;
    }
}

Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
    // 获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    // 线程通信，等待任务队列有空余
    if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]() -> bool
                           { return taskQueue_.size() < (size_t)taskQueMaxThreshHold_; }))
    {
        // 等待 1 秒还是没满足
        std::cerr << "taskQue is full, submit task fail" << std::endl;
        return Result(sp, false);
    }
    // 如果有空，把任务放入任务队列
    taskQueue_.emplace(sp);
    taskSize_++;
    // 放入队列，队列不空，在 notEmpty_通知
    notEmpty_.notify_all();

    // cache 模式，任务比较紧急。场景：小而快的任务，根据任务数量和空闲线程数量，动态增加线程
    if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThreshHold_)
    {
        std::cout << "create new thread" << std::endl;
        // 创建一个线程对象，并启动
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
        threads_[threadId]->start();
        curThreadSize_++;
        idleThreadSize_++;
    }
    return Result(sp);
}
void ThreadPool::start(int initThreadSize)
{
    // 设置线程池运行状态
    isPoolRunning_ = true;
    // 记录初始线程个数
    initThreadSize_ = initThreadSize;
    curThreadSize_ = initThreadSize;

    // 创建线程对象
    for (int i = 0; i < initThreadSize_; i++)
    {
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
    }

    // 启动所有线程
    for (int i = 0; i < initThreadSize_; i++)
    {
        threads_[i]->start();
        idleThreadSize_++;
    }
}

// 自定义线程函数，从任务队列取任务，体现了一个线程的生命周期
void ThreadPool::threadFunc(int threadId)
{
    // 记录这个线程的起始时间（并在每次循环之后更新），用于判断空闲时间（如果是 cached 模式）
    auto lastTime = std::chrono::high_resolution_clock::now();
    for (;;)
    {
        std::shared_ptr<Task> task;
        {
            std::unique_lock<std::mutex> lock(taskQueMtx_);

            std::cout << "tid=" << std::this_thread::get_id() << " try to get task" << std::endl;

            // cache 模式下，线程空闲超过 60 秒，自动结束多余的线程(超过 initThreadSize_的线程)

            // 当任务队列为空的时候，线程的逻辑
            while (taskQueue_.size() == 0)
            {
                // 线程池被关闭的逻辑
                if (!isPoolRunning_)
                {
                    threads_.erase(threadId);
                    curThreadSize_--;
                    idleThreadSize_--;
                    exitCond_.notify_all();
                    std::cout << "threadid=" << std::this_thread::get_id() << " exit" << std::endl;
                    return; // 线程函数结束，线程退出
                }

                if (poolMode_ == PoolMode::MODE_CACHED)
                {
                    // 每一秒返回一次
                    if (std::cv_status::timeout ==
                        notEmpty_.wait_for(lock, std::chrono::seconds(1)))
                    {
                        auto nowTime = std::chrono::high_resolution_clock::now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(nowTime - lastTime);
                        if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
                        {
                            // 开始回收当前线程
                            // 记录线程数量的相关变量的值修改
                            // 把线程对象从线程列表容器里面删除
                            threads_.erase(threadId);
                            curThreadSize_--;
                            idleThreadSize_--;
                            std::cout << "threadid=" << std::this_thread::get_id() << " exit" << std::endl;
                            return;
                        }
                    }
                }
                else
                {
                    notEmpty_.wait(lock);
                }
            }
            idleThreadSize_--;

            task = taskQueue_.front();
            taskQueue_.pop();
            taskSize_--;
            std::cout << "tid=" << std::this_thread::get_id() << " get task" << std::endl;
            // 有剩余任务，通知其他线程
            if (taskQueue_.size() > 0)
            {
                notEmpty_.notify_all();
            }

            // 取出任务，进行通知
            notFull_.notify_all();
        } // 释放锁
        // 当前线程负责执行这个任务
        if (task != nullptr)
        {
            task->exec();
        }
        idleThreadSize_++;

        // 更新线程的空闲时间点
        lastTime = std::chrono::high_resolution_clock::now();
    }
}

bool ThreadPool::checkRunningState() const
{
    return isPoolRunning_;
}

//////////线程类方法实现
int Thread::generateId_ = 0;

int Thread::getId() const
{
    return threadId_;
}

void Thread::start()
{
    // 创建一个线程来执行一个函数
    std::thread t(func_, threadId_);
    t.detach();
}

Thread::Thread(ThreadFunc func)
    : func_(func), threadId_(generateId_++)
{
}

Thread::~Thread() {}

///////////////////////Task 实现

Task::Task()
    : result_(nullptr)
{
}

void Task::exec()
{
    if (result_ == nullptr)
        return;
    result_->setVal(run());
}

void Task::setResult(Result *res)
{
    result_ = res;
}

///////////////////////Result 方法实现
Result::Result(std::shared_ptr<Task> task, bool isValid) : task_(task), isValid_(isValid)
{
    task_->setResult(this);
}

Any Result::get()
{
    if (!isValid_)
        return "";
    sem_->wait(); // task 任务如果没有执行完毕
    return std::move(any_);
}

void Result::setVal(Any any)
{
    this->any_ = std::move(any);
    sem_->post();
}