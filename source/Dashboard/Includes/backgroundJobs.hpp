/* SPDX-License-Identifier: MIT. Worker jobs never run owner/UI callbacks. */
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
class BackgroundJobs {
 std::mutex mutex;std::condition_variable wake;std::deque<std::function<void()>> jobs;
 bool stopping=false;std::atomic<bool> finished{false};std::thread worker;
 void run(){for(;;){std::function<void()> job;{std::unique_lock<std::mutex>g(mutex);wake.wait(g,[this]{return stopping||!jobs.empty();});if(stopping)break;job=std::move(jobs.front());jobs.pop_front();}job();}finished=true;}
public:
 BackgroundJobs():worker([this]{run();}){}
 ~BackgroundJobs(){requestStop();join();}
 bool post(std::function<void()> job,bool replacePending=false){std::lock_guard<std::mutex>g(mutex);if(stopping)return false;if(replacePending)jobs.clear();jobs.push_back(std::move(job));wake.notify_one();return true;}
 void requestStop(){std::lock_guard<std::mutex>g(mutex);stopping=true;jobs.clear();wake.notify_one();}
 bool stopped()const{return finished.load();}
 void join(){if(worker.joinable())worker.join();}
};
class MainCompletions {
 std::mutex mutex;std::deque<std::function<void()>> ready;
public:
 void post(std::function<void()> item){std::lock_guard<std::mutex>g(mutex);ready.push_back(std::move(item));}
 void pump(){std::deque<std::function<void()>> items;{std::lock_guard<std::mutex>g(mutex);items.swap(ready);}for(auto&item:items)item();}
 void clear(){std::lock_guard<std::mutex>g(mutex);ready.clear();}
};
