#ifndef __FTPSERVER_H
#define __FTPSERVER_H

#include "uploadPathLocks.hpp"
#include <map>
#include <memory>
#include <vector>
#include <atomic>
#include <string>
#include <thread>
#include "config.hpp"
#include "ftpConnection.hpp"
#ifdef NXDK
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#else
#include <netdb.h>
#endif

class ftpServer {
  fd_set master;
  fd_set readFds;
  int fdmax;

  struct ClientWorker {
    std::thread thread;
    std::atomic<bool> finished{ false };
  };
  std::vector<std::unique_ptr<ClientWorker>> workers;

  int listener{ -1 };
  int newfd;
  int i;

  struct sockaddr_storage raddr;
  socklen_t addrlen;
  struct addrinfo hints, *ai, *p;

  std::thread serverThread;
  std::atomic<bool> stopRequested{ false };
  std::atomic<unsigned> activeTransfers{ 0 };
  UploadPathLocks uploadPaths;

  void* getInAddr(struct sockaddr* sa);
  static int thread_runner(ftpServer* server);

public:
  ftpServer(ftpConfig const* conf);
  ~ftpServer();
  int init();
  int run();
  void runAsync();
  void stop();
  bool isStopping() const { return stopRequested.load(); }
  std::shared_ptr<std::mutex> uploadPathMutex(const std::string& path) { return uploadPaths.get(path); }
  void beginTransfer() { activeTransfers.fetch_add(1, std::memory_order_relaxed); }
  void endTransfer() { activeTransfers.fetch_sub(1, std::memory_order_relaxed); }
  bool hasActiveTransfers() const { return activeTransfers.load(std::memory_order_relaxed) != 0; }

  int openConnection(std::string const& addr, std::string const& port);

  const ftpConfig* conf;
};

template<std::size_t Size> struct PicoFtpAbiTag {};
void verifyPicoFtpAbi(PicoFtpAbiTag<sizeof(ftpServer)>*);
#endif
