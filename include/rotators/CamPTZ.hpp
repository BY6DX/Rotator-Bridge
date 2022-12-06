#pragma once

#include "RotatorCommon.hpp"

class CamPTZ : RotatorController {
private:
  // offset configurations
  double aziOffset;  // (-360, 360)
  double eleOffset;  // (-90, 90)

  int sock;
  bool sockConnected = false;
  bool threadClosing = false;

  std::unique_ptr<std::thread> worker;

  using threadJob = std::pair<RotatorRequest, std::function<void(RotatorResponse)>>;
  ThreadsafeQueue<threadJob> jobQueue;
  
  // signal mechanism
  std::mutex jobEventMutex;
  std::condition_variable jobEvent;

  void connStart();
  void connTerminate();
  static void threadMain(CamPTZ *self);

public:
  std::string tcpHost;
  int tcpPort;

  void Initialize(std::string tcpHost, int tcpPort, double aziOffset, double eleOffset);

  virtual void Start() override;
  virtual void Terminate() override;
  virtual void Request(RotatorRequest req, std::function<void(RotatorResponse)> callback) override;
};