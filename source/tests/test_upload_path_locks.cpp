#include "uploadPathLocks.hpp"
#include <cassert>
#include <future>
#include <chrono>
#include <iostream>
int main(){
 UploadPathLocks paths;
 auto first=paths.get("E:\\Games\\Title\\default.xbe");
 assert(first==paths.get("e:/games/title/DEFAULT.XBE"));
 assert(first==paths.get("E:/Games/Other/../Title/./default.xbe"));
 assert(first!=paths.get("E:/Games/Title/dialog.rcf"));
 std::unique_lock<std::mutex> held(*first);
 std::promise<void> started;auto ready=started.get_future();
 auto same=std::async(std::launch::async,[&]{auto lock=paths.get("e:/games/title/default.xbe");started.set_value();std::lock_guard<std::mutex> g(*lock);return true;});
 ready.wait();assert(same.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout);
 auto other=std::async(std::launch::async,[&]{auto lock=paths.get("E:/Games/Title/dialog.rcf");std::lock_guard<std::mutex> g(*lock);return true;});
 assert(other.wait_for(std::chrono::seconds(1))==std::future_status::ready);assert(other.get());
 held.unlock();assert(same.get());
 std::cout<<"PASS same FATX path serialized, case/slash/dot aliases agree, different files remain parallel, waiting writer proceeds after release\n";
}
