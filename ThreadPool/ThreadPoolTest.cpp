#include "ThreadPool.hpp"
#include <set>
#include <iostream>
#include <chrono>

int main(){
    ThreadPool thdPool(6);

    std::vector<std::future<void>> vec;
    for(int i=0; i<10; i++){
        auto res = thdPool.submitTask([](){std::cout<<"线程"<<std::this_thread::get_id()<<'\n';});
        vec.push_back(std::move(res));
    }

    std::vector<std::future<std::thread::id>> ids;
    std::set<std::thread::id> idSet;
    for(int i=0; i<10000; i++){
        auto res = thdPool.submitTask([](){return std::this_thread::get_id();});
        ids.push_back(std::move(res));
    }
    for(auto& res: vec)
        res.get();
    for(auto& res: ids){
        idSet.insert(res.get());
    }

    for(auto& id:idSet)
        std::cout<<id<<'\n';
}