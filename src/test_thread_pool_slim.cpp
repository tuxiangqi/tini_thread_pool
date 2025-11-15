#include "threadpool_slim.h"
#include <iostream>
#include <future>
#include <functional>
#include <thread>

int sum(int a, int b)
{
    return a + b;
}

int main()
{
    ThreadPool pool;
    pool.setMode(PoolMode::MODE_CACHED);
    pool.start(2); // 启动线程池，指定初始线程数量为4
    auto future = pool.submitTask(sum, 1, 2);
    int result = future.get();
    std::cout << "Result: " << result << std::endl;
    return 0;
}
