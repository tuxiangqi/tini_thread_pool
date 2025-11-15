# Thread Pool 项目 AI 编码指南

## 架构概览

这是一个 C++17 线程池实现项目，包含两个独立版本：

1. **ThreadPool** (`include/threadpool.h`, `src/threadpool.cpp`) - 功能完整版本
   - 基于继承的任务模型：用户必须继承 `Task` 类并重写 `run()` 方法
   - 自定义类型擦除：使用 `Any` 类封装任意类型的返回值
   - 双模式支持：`MODE_FIXED`（固定线程数）和 `MODE_CACHED`（动态线程数）
   - 异步结果：通过 `Result` 类和信号量实现任务结果的获取

2. **ThreadPool_slim** (`include/threadpool_slim.h`, `src/test_thread_pool_slim.cpp`) - 简化版本
   - 使用现代 C++ 标准库：`std::packaged_task`, `std::future`
   - 更简洁的 API，无需继承

## 关键设计模式

### 类型擦除（Any 类）
```cpp
// 使用动态多态实现类型擦除，类似 std::any
Any result = task.run();
uLong value = result.cast_<uLong>();  // 必须类型匹配，否则抛出异常
```

### 任务生命周期
1. 用户继承 `Task` 类实现自定义任务
2. 通过 `submitTask()` 提交 `shared_ptr<Task>`，返回 `Result` 对象
3. `Result::get()` 阻塞等待任务完成（通过信号量同步）
4. `Task::exec()` 内部调用 `run()` 并将结果传递给 `Result`

### 线程管理
- **Fixed 模式**：启动时创建固定数量线程，一直运行
- **Cached 模式**：
  - 初始创建少量线程
  - 当 `任务数 > 空闲线程数` 且未达上限时，动态创建新线程
  - 线程空闲超过 60 秒自动回收（仅回收超出初始数量的线程）

## 构建与运行

```bash
# 配置构建（使用 Ninja）
cmake -B build -G Ninja

# 编译两个可执行文件
cmake --build build

# 运行完整版测试
./build/ThreadPool

# 运行简化版测试
./build/ThreadPool_slim
```

**注意**：项目已配置 CMake 任务，但通常直接使用命令行构建。

## 代码约定

- **命名**：成员变量使用尾下划线（如 `threadSize_`）
- **并发安全**：所有共享状态访问都需要持有 `taskQueMtx_` 锁
- **原子变量**：`taskSize_`, `curThreadSize_`, `idleThreadSize_` 使用 `std::atomic` 确保线程安全
- **注释语言**：主要使用中文
- **智能指针**：
  - `Task` 使用 `shared_ptr`（跨线程共享）
  - `Thread` 使用 `unique_ptr`（线程池独占所有权）

## 关键实现细节

### 线程函数（threadFunc）
- 持续循环从 `taskQueue_` 获取任务
- 使用条件变量 `notEmpty_` 等待任务
- Cached 模式下空闲线程会超时等待并自行回收
- 退出时需要从 `threads_` map 中删除自身（通过 `threadId`）

### 信号量实现
自定义 `Semaphore` 类（基于互斥锁和条件变量）：
- `wait()` 递减计数并阻塞直到 > 0
- `post()` 递增计数并通知等待线程
- 用于 `Result` 类实现任务完成通知

### 析构过程
1. 设置 `isPoolRunning_ = false`
2. 唤醒所有等待的线程 (`notEmpty_.notify_all()`)
3. 等待 `curThreadSize_` 降为 0 (`exitCond_.wait()`)
4. 线程自行清理并从 `threads_` map 中移除

## 扩展开发

添加新任务时：
```cpp
class MyTask : public Task {
    Any run() override {
        // 实现任务逻辑
        return result_value;
    }
};

// 使用
Result res = pool.submitTask(std::make_shared<MyTask>());
auto value = res.get().cast_<ExpectedType>();
```

## 已知特性

- `threadpool_slim.h` 当前为空文件（简化版实现待完善）
- 主函数使用 `getchar()` 阻塞防止过早退出
- 任务队列满时 `submitTask()` 最多等待 1 秒，超时返回无效 `Result`
