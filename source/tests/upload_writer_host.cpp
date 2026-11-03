
#include <mutex>
#include <condition_variable>
#include <thread>
#include <string>
#include <vector>
#include <cassert>
#include <chrono>
#include <algorithm>
#include <cstdio>
using DWORD=unsigned long; using BOOL=int; using HANDLE=void*;
#define WINAPI
#define INFINITE 0xffffffff
namespace i18n { const char* c(const char* s){return s;} }
struct Disk { std::vector<char> bytes; size_t failAt=~size_t(0); bool zero=false; };
thread_local DWORD lastError=0;
DWORD GetLastError(){return lastError;}
BOOL WriteFile(HANDLE h,const char* p,DWORD n,DWORD* actual,void*) {
 auto& d=*static_cast<Disk*>(h);
 std::this_thread::sleep_for(std::chrono::microseconds(20));
 if(d.bytes.size()>=d.failAt){*actual=0;lastError=111;return 0;}
 if(d.zero){*actual=0;return 1;}
 *actual=std::min<size_t>({n,997,d.failAt-d.bytes.size()});
 d.bytes.insert(d.bytes.end(),p,p+*actual); return 1;
}
HANDLE CreateThread(void*,int,DWORD(*fn)(void*),void* p,int,void*) {
 return new std::thread([=]{fn(p);});
}
void WaitForSingleObject(HANDLE h,DWORD){static_cast<std::thread*>(h)->join();}
void CloseHandle(HANDLE h){delete static_cast<std::thread*>(h);}
class UploadWriter {

  HANDLE file;

  HANDLE worker = nullptr;

  std::mutex mutex;

  std::condition_variable changed;

  const char* pendingBytes = nullptr;

  size_t pendingSize = 0;

  bool pending = false;

  bool finishing = false;

  bool failed = false;

  unsigned long long committed;

  std::string failure;

  static DWORD WINAPI threadMain(void* context) {

    static_cast<UploadWriter*>(context)->run(); return 0;

  }

  void run() {

    std::unique_lock<std::mutex> lock(mutex);

    for (;;) {

      changed.wait(lock, [this]{ return pending || finishing; });

      if (!pending) return;

      const char* bytes = pendingBytes;

      const size_t size = pendingSize;

      lock.unlock();

      size_t written = 0;

      DWORD diskError = 0;

      bool ok = true;

      while (written < size) {

        DWORD actual = 0;

        const BOOL saved = WriteFile(file, bytes + written,

            static_cast<DWORD>(size - written), &actual, nullptr);

        diskError = saved ? 0 : GetLastError();

        written += actual;

        if (!saved || !actual) { ok = false; break; }

      }

      lock.lock();

      committed += written;

      pending = false;

      if (!ok) {

        failed = true;

        failure = "451 Disk write failed; error " + std::to_string(diskError) +

                  i18n::c(" after ") + std::to_string(committed) + " bytes saved.\r\n";

      }

      changed.notify_all();

      if (failed) return;

    }

  }

public:

  UploadWriter(HANDLE destination, unsigned long long position)

      : file(destination), committed(position) {

    worker = CreateThread(nullptr, 0, threadMain, this, 0, nullptr);

  }

  ~UploadWriter() { finish(); }

  bool available() const { return worker != nullptr; }

  bool submit(const char* bytes, size_t size) {

    std::unique_lock<std::mutex> lock(mutex);

    changed.wait(lock, [this]{ return !pending || failed; });

    if (failed || finishing) return false;

    pendingBytes = bytes; pendingSize = size; pending = true;

    changed.notify_all(); return true;

  }

  void finish() {

    if (!worker) return;

    { std::lock_guard<std::mutex> lock(mutex); finishing = true; changed.notify_all(); }

    WaitForSingleObject(worker, INFINITE);

    CloseHandle(worker); worker = nullptr;

  }

  bool getFailure(std::string& text) {

    std::lock_guard<std::mutex> lock(mutex);

    if (failed) text = failure;

    return failed;

  }

  unsigned long long bytesSaved() {

    std::lock_guard<std::mutex> lock(mutex); return committed;

  }

};
int main(){
 for(int scenario=0;scenario<3;scenario++){
  Disk disk; if(scenario==1)disk.failAt=10000; if(scenario==2)disk.zero=true;
  std::vector<char> buffers[2]={std::vector<char>(4096),std::vector<char>(4096)};
  std::vector<char> expected; bool accepted=true;
  UploadWriter writer(&disk,0); assert(writer.available());
  // Match production ordering: fill next buffer, wait for previous submission,
  // submit it, then reuse the drained buffer on the next iteration.
  for(int i=0;i<300;i++){
   auto& b=buffers[i%2]; std::fill(b.begin(),b.end(),char(i%251));
   if(!writer.submit(b.data(),b.size())){accepted=false;break;}
   expected.insert(expected.end(),b.begin(),b.end());
  }
  writer.finish(); writer.finish(); // drain and idempotent shutdown
  std::string error; bool failed=writer.getFailure(error);
  assert(writer.bytesSaved()==disk.bytes.size());
  if(scenario==0){assert(accepted&&!failed);assert(disk.bytes==expected);}
  else {assert(failed&&!error.empty());assert(std::equal(disk.bytes.begin(),disk.bytes.end(),expected.begin()));}
 }
 // Destruction must drain a pending final write before caller releases storage.
 Disk disk; std::vector<char> b(16384,42);
 {UploadWriter writer(&disk,0); assert(writer.submit(b.data(),b.size()));}
 assert(disk.bytes==b);
 puts("PASS: production writer overlap, 300 alternating buffers, partial writes, disk failure, zero-progress, final drain and repeated finish");
}
