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

class Any
{
public:
    Any() = default;
    ~Any() = default;
    Any(const Any &) = delete;
    Any &operator=(const Any &) = delete;
    Any(Any &&) = default;
    Any &operator=(Any &&) = default;

    template <typename T>
    Any(T data) : base_(std::make_unique<Derive<T>>(data)){};

    template <typename T>
    T cast_()
    {
        Derive<T> *pd = dynamic_cast<Derive<T> *>(base_.get());
        if (pd == nullptr)
        {
            throw std::runtime_error("type mismatch");
            std::cerr << "type is unmatch" << std::endl;
        }
        return pd->data_;
    }

private:
    class Base
    {
    public:
        virtual ~Base() = default;
    };
    template <typename T>
    class Derive : public Base
    {
    public:
        Derive(T data) : data_(data) {};
        T data_;
    };

private:
    // 基类指针
    std::unique_ptr<Base> base_;
};

class Semaphore
{
public:
    Semaphore(int limit = 0) : resLimit_(limit), isExit_(false) {};
    ~Semaphore() 
    {
        isExit_ = true;
    }
    void wait()
    {
        if (isExit_)
            return;
        std::unique_lock<std::mutex> lock(mtx_);
        cond_.wait(lock, [&]() -> bool
                   { return resLimit_ > 0; });
        resLimit_--;
    }
    void post()
    {
        if (isExit_)
            return;
        std::unique_lock<std::mutex> lock(mtx_);
        resLimit_++;
        cond_.notify_all();
    }

private:
    std::atomic_bool isExit_;
    int resLimit_;
    std::mutex mtx_;
    std::condition_variable cond_;
};

// 前置声明
class Task;
class Result
{
public:
    Result(std::shared_ptr<Task> task, bool isValid = true);
    ~Result() = default;
    // setVal 获取任务返回值
    void setVal(Any any);
    // get 方法，获取 Task的返回值
    Any get();

private:
    Any any_;                    // 存储任务返回值
    Semaphore sem_;              // 线程通信信号量
    std::shared_ptr<Task> task_; // 对应获取返回值的 Task 对象
    std::atomic_bool isValid_;   // 返回值是否有效
};

// 任务抽象基类
// 所有具体任务都必须继承该类并重写run方法，实现自定义任务处理
class Task
{
public:
    Task();
    virtual ~Task() = default;
    void exec();
    void setResult(Result *res);
    virtual Any run() = 0; // 纯虚函数
private:
    Result *result_; // 与任务对应的 Result 对象
};

enum class PoolMode
{
    MODE_FIXED, // 固定数量线程
    MODE_CACHED // 线程数量可以动态增长
};

class Thread
{
public:
    using ThreadFunc = std::function<void(int)>;
    Thread(ThreadFunc func);
    ~Thread();
    // 启动线程
    void start();
    // 获取线程 id
    int getId() const;

private:
    ThreadFunc func_;
    static int generateId_;
    int threadId_; // 保存线程 id
};
/*
example:
ThreadPool pool;
pool.start(4); //启动线程池，指定初始线程数量为4
class MyTask:public Task
{
    void run() override
    {
        //任务处理逻辑
    }
};
std::shared_ptr<Task> task=std::make_shared<MyTask>();
pool.submitTask(task); //提交任务到线程池
*/
class ThreadPool
{
public:
    ThreadPool();
    ~ThreadPool();

    // 设置线程池模式
    void setMode(PoolMode mode);
    // 设置初始线程数量
    void setInitThreadSize(int num);
    // 设置Task任务队列上限阈值
    void setTaskQueMaxThreshHold(int size);
    // 设置线程池 cached 模式线程上限阈值
    void setThreadSizeMaxThreshHold(int size);
    // 提交任务
    Result submitTask(std::shared_ptr<Task> sp);
    // 启动线程池
    void start(int initThreadSize = std::thread::hardware_concurrency());

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

private:
    // 定义线程函数
    void threadFunc(int threadId);

    // 检查 pool 的运行状态
    bool checkRunningState() const;

private:
    // std::vector<std::unique_ptr<Thread>> threads_; //任务队列
    std::unordered_map<int, std::unique_ptr<Thread>> threads_; // 任务队列
    size_t initThreadSize_;                                    // 初始线程数量
    std::atomic_int curThreadSize_;                            // 线程池当前线程总数量
    int threadSizeThreshHold_;                                 // 线程数量上限值
    std::atomic_int idleThreadSize_;                           // 空闲线程的数量
    std::queue<std::shared_ptr<Task>> taskQueue_;              // 任务队列
    std::atomic_int taskSize_;                                 // 任务数量
    int taskQueMaxThreshHold_;                                 // 任务队列最大容量

    std::mutex taskQueMtx_;            // 任务队列互斥锁
    std::condition_variable notFull_;  // 任务队列不为空条件变量
    std::condition_variable notEmpty_; // 任务队列不为满条件变量
    std::condition_variable exitCond_; // 线程池退出条件变量

    PoolMode poolMode_;              // 线程池模式
    std::atomic_bool isPoolRunning_; // 线程池是否开始运行
};

#endif