#pragma once
#include <thread>
#include <condition_variable>
#include <queue>
#include <memory>
#include <functional>
#include <future>

class ThreadPool{
    size_t mPoolSize;
    bool stop;
    std::vector<std::thread> mThreadPool;
    std::queue<std::function<void()>>  mQueue;
    std::mutex mMutex;
    std::condition_variable mConditon;

    void clearPool(){
        std::unique_lock lock(this->mMutex);
        this->stop = true;
        this->mConditon.notify_all();
    }

public:
    explicit ThreadPool(size_t num=0){
        stop = false;
        for(mPoolSize=0; mPoolSize<num; mPoolSize++){
            std::thread thd([this](){
                // 线程内运行函数
                while(true){
                    std::unique_lock lock(this->mMutex);
                    while(this->mQueue.empty() && !this->stop)
                        this->mConditon.wait(lock);
                    if(this->mQueue.empty())
                        return;
                    // 到这里确定队列非空
                    auto task = this->mQueue.front();
                    this->mQueue.pop();
                    lock.unlock();
                    // 运行任务
                    task();
                }
            });
            mThreadPool.push_back(std::move(thd));
        }
    }

    template<typename F, typename... Args>
    auto submitTask(F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type>{
        // 组装任务包
        using return_type = typename std::result_of<F(Args...)>::type;
        // packaged_task本身只是一个可调用的对象，无论是本线程还是其他线程，都是可以调用的
        auto task = std::make_shared<std::packaged_task<return_type()>>
                    (std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<return_type> res = task->get_future();

        std::unique_lock lock(mMutex);
        mQueue.push([task]() {(*task)();});
        mConditon.notify_one();
        return res;
    }

    ~ThreadPool(){
        clearPool();
        for(auto& t:mThreadPool)
            t.join();
    }
};
