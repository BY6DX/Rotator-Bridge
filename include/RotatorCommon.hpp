#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <optional>
#include <chrono>

/* NETWORK */
#ifndef WIN32
#include <arpa/inet.h>          /* htons() */
#include <netdb.h>              /* gethostbyname() */
#include <netinet/in.h>         /* struct sockaddr_in */
#include <sys/socket.h>         /* socket(), connect(), send() */
#else
#include <winsock2.h>
#endif

enum RotatorCmd {
  CHANGE_AZI,
  CHANGE_ELE,
  GET_AZI,
  GET_ELE
};

struct RotatorRequest {
  RotatorCmd cmd;
  union {
    struct {
      double aziRequested;
    } ChangeAzi;
    struct {
      double hdgRequested;
    } ChangeEle;
  } payload;
};

struct RotatorResponse {
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

  virtual void Request(RotatorRequest req, std::function<void(RotatorResponse)> callback) = 0;

  // synchronized version; timeout in milliseconds; 0 for unlimited
  virtual inline std::optional<RotatorResponse> Request(
    RotatorRequest req,
    int timeout_msec = 0
  ) {
    std::optional<RotatorResponse> respTemp;
    std::mutex cv_m;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(cv_m);  // locks the lock

    this->Request(req, [&](RotatorResponse resp) {
      respTemp = resp;
      cv.notify_all();
    });

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

  virtual void OnRequest(std::function<RotatorResponse(RotatorRequest)> callback) = 0;
};