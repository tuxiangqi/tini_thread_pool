#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <vector>
#include <queue>
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <future>

const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60;

enum class PoolMode
{
    MODE_FIXED, // 固定数量线程
    MODE_CACHED // 线程数量可以动态增长
};

class Thread
{
public:
    using ThreadFunc = std::function<void(int)>;
    Thread(ThreadFunc func) : func_(func), threadId_(generateId_++)
    {
    }

    ~Thread() = default;
    // 启动线程
    void start()
    {
        // 创建一个线程来执行一个函数
        std::thread t(func_, threadId_);
        t.detach();
    }
    // 获取线程 id
    int getId() const
    {
        return threadId_;
    }

private:
    ThreadFunc func_;
    static int generateId_;
    int threadId_; // 保存线程 id
};

int Thread::generateId_ = 0;

class ThreadPool
{
public:
    ThreadPool()
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
    ~ThreadPool()
    {
        isPoolRunning_ = false;
        // 等待所有线程退出
        std::unique_lock<std::mutex> lock(taskQueMtx_);
        notEmpty_.notify_all(); // 唤醒所有线程，让其退出
        exitCond_.wait(lock, [&]() -> bool
                       { return curThreadSize_ == 0; });
    }

    // 设置线程池模式
    void setMode(PoolMode mode)
    {
        if (checkRunningState())
            return;
        poolMode_ = mode;
    }
    // 设置初始线程数量
    void setInitThreadSize(int num)
    {
        if (checkRunningState())
            return;
        initThreadSize_ = num;
    }
    // 设置Task任务队列上限阈值
    void setTaskQueMaxThreshHold(int size)
    {
        if (checkRunningState())
            return;
        taskQueMaxThreshHold_ = size;
    }
    // 设置线程池 cached 模式线程上限阈值
    void setThreadSizeMaxThreshHold(int size)
    {
        if (checkRunningState())
            return;
        if (poolMode_ == PoolMode::MODE_CACHED)
        {
            threadSizeThreshHold_ = size;
        }
    }
    // 提交任务,使用可变参模板编程，可以接受任意任务函数和任意数量的参数
    // Result submitTask(std::shared_ptr<Task> sp);
    template <typename Func, typename... Args>
    auto submitTask(Func &&func, Args &&...args) -> std::future<decltype(func(args...))>
    {
        using RType = decltype(func(args...));
        auto task = std::make_shared<std::packaged_task<RType()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
        std::future<RType> result = task->get_future();
        // 获取锁
        std::unique_lock<std::mutex> lock(taskQueMtx_);
        // 线程通信，等待任务队列有空余
        if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]() -> bool
                               { return taskQueue_.size() < (size_t)taskQueMaxThreshHold_; }))
        {
            // 等待 1 秒还是没满足
            std::cerr << "taskQue is full, submit task fail" << std::endl;
            auto task0 = std::make_shared<std::packaged_task<RType()>>(
                []() -> RType
                { return RType(); });
            return task0->get_future();
        }
        // 如果有空，把任务放入任务队列
        // using Task = std::function<void()>;
        // std::queue<Task> taskQueue_;
        taskQueue_.emplace([task](){(*task)();});

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
        return result;
    }
    // 启动线程池
    void start(int initThreadSize = std::thread::hardware_concurrency())
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

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

private:
    // 定义线程函数
    void threadFunc(int threadId)
    {
        auto lastTime = std::chrono::high_resolution_clock::now();
        while (isPoolRunning_)
        {
            Task task;
            {
                std::unique_lock<std::mutex> lock(taskQueMtx_);

                std::cout << "tid=" << std::this_thread::get_id() << " try to get task" << std::endl;

                // cache 模式下，线程空闲超过 60 秒，自动结束多余的线程(超过 initThreadSize_的线程)

                while (isPoolRunning_ && taskQueue_.size() == 0)
                {
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
                // 线程池被关闭，线程退出
                if (!isPoolRunning_)
                {
                    break;
                }

                idleThreadSize_--;

                std::cout << "tid=" << std::this_thread::get_id() << " get task" << std::endl;

                task = taskQueue_.front();
                taskQueue_.pop();
                taskSize_--;
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
                // task->run();//把任务返回值 setVal 传给 Result
                task();
            }
            idleThreadSize_++;
            lastTime = std::chrono::high_resolution_clock::now();
        }
        threads_.erase(threadId);
        curThreadSize_--;
        idleThreadSize_--;
        exitCond_.notify_all();
        std::cout << "threadid=" << std::this_thread::get_id() << " exit" << std::endl;
    }

    // 检查 pool 的运行状态
    bool checkRunningState() const
    {
        return isPoolRunning_;
    }

private:
    // std::vector<std::unique_ptr<Thread>> threads_; //任务队列
    std::unordered_map<int, std::unique_ptr<Thread>> threads_; // 任务队列
    size_t initThreadSize_;                                    // 初始线程数量
    std::atomic_int curThreadSize_;                            // 线程池当前线程总数量
    int threadSizeThreshHold_;                                 // 线程数量上限值
    std::atomic_int idleThreadSize_;                           // 空闲线程的数量

    using Task = std::function<void()>;
    std::queue<Task> taskQueue_; // 任务队列
    std::atomic_int taskSize_;   // 任务数量
    int taskQueMaxThreshHold_;   // 任务队列最大容量

    std::mutex taskQueMtx_;            // 任务队列互斥锁
    std::condition_variable notFull_;  // 任务队列不为空条件变量
    std::condition_variable notEmpty_; // 任务队列不为满条件变量
    std::condition_variable exitCond_; // 线程池退出条件变量

    PoolMode poolMode_;              // 线程池模式
    std::atomic_bool isPoolRunning_; // 线程池是否开始运行
};

#endif