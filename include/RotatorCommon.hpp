#pragma once

#include <string>
#include <queue>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <optional>
#include <chrono>
#include <thread>

/* NETWORK */
#ifndef WIN32
#include <arpa/inet.h>          /* htons() */
#include <netdb.h>              /* gethostbyname() */
#include <netinet/in.h>         /* struct sockaddr_in */
#include <sys/socket.h>         /* socket(), connect(), send() */
#include <unistd.h>             /* close() */
#define CLOSE_SOCKET(X) close(X)
#define SOCKET_PRINT_ERROR(X) perror(X)
#define SOCKET_INIT() /* no-op */
#define SOCKET_EXIT() /* no-op */
#else
#include <winsock2.h>
#include <Ws2ipdef.h>
#include <WS2tcpip.h>
#define CLOSE_SOCKET(X) closesocket(X)
#define SOCKET_PRINT_ERROR(X) socket_print_error(X)

// TODO: fix print; currently only useful when putting breakpoint inside
inline void socket_print_error(const char* X) {
    do {
        \
            wchar_t* s = NULL;               \
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, \
                NULL, WSAGetLastError(), \
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), \
                (LPWSTR)&s, 0, NULL); \
            fprintf(stderr, "%s: %ls\n", X, s); \
            LocalFree(s); \
    } while (0);
}

#define SOCKET_INIT() InitWinSock2()
#define SOCKET_EXIT() CloseWinSock2()

/* This code was given from MSDN */
inline void InitWinSock2(void)
{
    WORD            wVersionRequested;
    WSADATA         wsaData;
    int             err;

    wVersionRequested = MAKEWORD(2, 2);

    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0)
    {
        /* Tell the user that we could not find a usable */
        /* WinSock DLL.                                  */
        return;
    }

    /* Confirm that the WinSock DLL supports 2.2. */
    /* Note that if the DLL supports versions later    */
    /* than 2.2 in addition to 2.2, it will still return */
    /* 2.2 in wVersion since that is the version we      */
    /* requested.                                        */

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
    {
        /* Tell the user that we could not find a usable */
        /* WinSock DLL.                                  */
        WSACleanup();
        return;
    }
}

inline void CloseWinSock2(void)
{
    WSACleanup();
}
#endif

inline int send_fixed(int sockfd, const char *buf, size_t buflen, int opts) {
  int ret;
  int bytes_written = 0;
  while (bytes_written < buflen) {
    ret = send(sockfd, buf + bytes_written, buflen - bytes_written, opts);
    if (ret < 0) {
      return ret;
    }

    bytes_written += ret;
  }
  return bytes_written;
}

inline int recv_fixed(int sockfd, char *buf, size_t buflen, int opts) {
  int ret;
  int bytes_read = 0;
  while (bytes_read < buflen) {
    ret = recv(sockfd, buf + bytes_read, buflen - bytes_read, opts);
    if (ret < 0) {
      return ret;
    }

    bytes_read += ret;
  }
  return bytes_read;
}

// thread safe queue, from https://codetrips.com/2020/07/26/modern-c-writing-a-thread-safe-queue/
template<typename T>
class ThreadsafeQueue {
  std::queue<T> queue_;
  mutable std::mutex mutex_;
 
  // Moved out of public interface to prevent races between this
  // and pop().
  bool empty() const {
    return queue_.empty();
  }
 
 public:
  ThreadsafeQueue() = default;
  ThreadsafeQueue(const ThreadsafeQueue<T> &) = delete ;
  ThreadsafeQueue& operator=(const ThreadsafeQueue<T> &) = delete ;
 
  ThreadsafeQueue(ThreadsafeQueue<T>&& other) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_ = std::move(other.queue_);
  }
 
  virtual ~ThreadsafeQueue() { }
 
  unsigned long size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }
 
  std::optional<T> pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return {};
    }
    T tmp = queue_.front();
    queue_.pop();
    return tmp;
  }
 
  void push(const T &item) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(item);
  }
};

enum RotatorCmd {
  CHANGE_AZI,
  CHANGE_ELE,
  GET_AZI,
  GET_ELE,
  CAMPTZ_PRESET_CALL,
  CAMPTZ_PRESET_SET,
  CAMPTZ_PRESET_CLEAR
};

// ele = 0 means pointing the antenna to horizon
// ele = 90 means pointing the antenna to the sky
struct RotatorRequest {
  RotatorCmd cmd;
  union {
    struct {
      double aziRequested;
    } ChangeAzi;
    struct {
      double eleRequested;
    } ChangeEle;
    struct {
      char presetIdx;
    } CamPTZPreset;
  } payload;
};

struct RotatorResponse {
  bool success;
  union {
    struct {
      double azi;
    } aziResp;
    struct {
      double ele;
    } eleResp;
  } payload;
};

class RotatorController {
public:
  enum RotatorStatus {
    IDLE,
    ROTATING
  };

  virtual void Start() = 0;
  virtual void Terminate() = 0;

  virtual bool Request(RotatorRequest req, std::function<void(RotatorResponse)> callback) = 0;

  // synchronized version; timeout in milliseconds; 0 for unlimited
  inline std::optional<RotatorResponse> RequestSync(
    RotatorRequest req,
    int timeout_msec = 0
  ) {
    std::optional<RotatorResponse> respTemp;
    std::mutex cv_m;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(cv_m);  // locks the lock

    bool ret = this->Request(req, [&](RotatorResponse resp) {
      respTemp = resp;
      cv.notify_all();
    });

    // failed to submit
    if (!ret) {
      return respTemp;
    }

    // atomically unlock and wait; lock when cv is signaled
    if (timeout_msec == 0) {
      cv.wait(lk);
    } else {
      cv.wait_for(lk, std::chrono::milliseconds(timeout_msec));  
    }
    
    return respTemp;
  }
};

class PseudoRotator {
public:
  virtual void Start() = 0;
  virtual void Terminate() = 0;
  virtual void WaitForClose() = 0;

  virtual bool SetRequestHandler(std::function<RotatorResponse(RotatorRequest)> callback) = 0;
};