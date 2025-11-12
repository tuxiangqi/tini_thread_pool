#include <iostream>
#include <chrono>
#include <thread>
#include "../include/threadpool.h"

using uLong = unsigned long long;
/*
有些场景希望获得线程执行任务的返回值
*/
class MyTask : public Task
{
public:
    MyTask() = default;
    MyTask(int begin, int end) : begin_(begin), end_(end) {}
    Any run() override
    {
        std::cout << "tid=" << std::this_thread::get_id() << " begin" << std::endl;
        std::cout << "Task is running in thread " << std::this_thread::get_id() << std::endl;
        ulong sum = 0;
        for (ulong i = begin_; i <= end_; i++)
        {
            sum += i;
        }
        // std::cout<<"Sum from "<<begin_<<" to "<<end_<<" is "<<sum<<std::endl;
        std::cout << "tid=" << std::this_thread::get_id() << " end" << std::endl;
        return sum;
    }

private:
    int begin_;
    int end_;
};

int main()
{

    {
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_CACHED);
        pool.start(4);
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

        ulong sum1 = res1.get().cast_<ulong>();
        ulong sum2 = res2.get().cast_<ulong>();
        ulong sum3 = res3.get().cast_<ulong>();

        std::cout << "Total sum is " << sum1 + sum2 + sum3 << std::endl;
    }
    getchar(); // 阻塞，防止主线程退出
    return 0;
}