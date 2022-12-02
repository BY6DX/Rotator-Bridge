#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

// TODO: refactor
struct RotatorCfg {
  // Act as controller if true, else act as pseudo device object
  bool asController;
  std::string tcpHost;
  int tcpPort;
};

struct RotatorCmd {
  enum CmdType {
    CHANGE_AZI,
    CHANGE_HDG,
    GET_AZI,
    GET_HDG
  } type;
  union {
    struct {
      double aziRequested;
    } ChangeAzi;
    struct {
      double hdgRequested;
    } ChangeHdg;
  } payload;
};

class RotatorController {
public:
  enum ConnectionStatus {
    DISCONNECTED,
    CONNECTED
  };

  enum RotatorStatus {
    IDLE,
    ROTATING
  };

  virtual void Start();
  virtual enum ConnectionStatus GetConnectionStatus() = 0;
  virtual void Terminate() = 0;

  virtual void GetAzimuthAsync(std::function<void(double)> callback) = 0;
  virtual void GetHeadingAsync(std::function<void(double)> callback) = 0;
  virtual void SetAzimuthAsync(double azi, std::function<void()> onfinish) = 0;
  virtual void SetHeadingAsync(double hdg, std::function<void()> onfinish) = 0;

  // synchronized version
  virtual inline void GetAzimuth(double &azi) {
    double aziTemp;
    std::mutex cv_m;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(cv_m);  // locks the lock

    this->GetAzimuthAsync([&](double aziRes) {
      aziTemp = aziRes;
      cv.notify_all();
    });

    cv.wait(lk);  // atomically unlock and wait; lock when cv is signaled
    azi = aziTemp;
  }

  virtual void GetHeading(double &hdg) {
    double hdgTemp;
    std::mutex cv_m;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(cv_m);

    this->GetHeadingAsync([&](double hdgRes) {
      hdgTemp = hdgRes;
      cv.notify_all();
    });

    cv.wait(lk);
    hdg = hdgTemp;
  }

  virtual void SetAzimuth(double azi) {
    std::mutex cv_m;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(cv_m);

    this->SetAzimuthAsync(azi, [&]() {
      cv.notify_all();
    });

    cv.wait(lk);
  }

  virtual void SetHeading(double hdg) {
    std::mutex cv_m;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lk(cv_m);

    this->SetHeadingAsync(hdg, [&]() {
      cv.notify_all();
    });

    cv.wait(lk);
  }
};

class PseudoRotator {
public:
  virtual void Start() = 0;
  virtual enum ConnectionStatus GetConnectionStatus() = 0;
  virtual void Terminate() = 0;

  virtual RotatorCmd WaitForCommand() = 0;

};