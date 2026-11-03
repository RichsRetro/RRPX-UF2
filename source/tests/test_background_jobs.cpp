#include "backgroundJobs.hpp"
#include <cassert>
#include <future>
#include <iostream>
int main(){
 const auto owner=std::this_thread::get_id();MainCompletions mailbox;
 std::promise<void> entered,release,posted;auto unblock=release.get_future().share();bool callback=false;
 BackgroundJobs io;
 assert(io.post([&]{entered.set_value();unblock.wait();mailbox.post([&]{assert(std::this_thread::get_id()==owner);callback=true;});posted.set_value();}));
 entered.get_future().wait();
 // A blocked I/O job does not prevent the owner from servicing its queue.
 for(unsigned i=0;i<10000;++i)mailbox.pump();assert(!callback);
 std::atomic<unsigned> stale{0},latest{0};std::promise<void> latestDone;
 assert(io.post([&]{++stale;},true));assert(io.post([&]{++latest;latestDone.set_value();},true));
 release.set_value();posted.get_future().wait();latestDone.get_future().wait();assert(!callback);mailbox.pump();assert(callback&&stale==0&&latest==1);
 io.requestStop();io.join();assert(io.stopped());assert(!io.post([]{}));
 BackgroundJobs draining;std::promise<void> blocked,finish;auto finishSignal=finish.get_future().share();
 assert(draining.post([&]{blocked.set_value();finishSignal.wait();}));blocked.get_future().wait();
 assert(draining.post([&]{++stale;}));draining.requestStop();assert(!draining.stopped());
 // Shutdown requests do not terminate an in-flight job or run its queued successor.
 finish.set_value();draining.join();assert(draining.stopped()&&stale==0);
 std::cout<<"PASS: blocked I/O leaves owner responsive; callbacks only run when owner pumps; stale pending jobs coalesce; active work drains before teardown; stopped queue rejects jobs\n";
}
