cmake_minimum_required(VERSION 3.15)
project(ThreadPool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # 使用标准 C++ 而非编译器扩展（如 -std=c++17 而非 -std=gnu++17）
set(SOURCES
    src/main.cpp
    src/threadpool.cpp
    include/threadpool.h
)
set(SOURCES2
    src/test_thread_pool_slim.cpp
    include/threadpool_slim.h
)

find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME} ${SOURCES})
    
add_executable(${PROJECT_NAME}_slim ${SOURCES2})
target_include_directories(${PROJECT_NAME}_slim PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)
target_link_libraries(${PROJECT_NAME}_slim PRIVATE Threads::Threads)