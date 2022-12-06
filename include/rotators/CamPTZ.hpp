#pragma once

#include "RotatorCommon.hpp"

class CamPTZ : public RotatorController {
private:
  // offset configurations
  double aziOffset;  // (-360, 360), wrap
  double eleOffset;  // (-90, 90), no-wrap

  std::string tcpHost;
  int tcpPort;

  int sock;
  bool sockConnected = false;
  bool threadClosing = false;
  bool threadExited = true;

  std::thread worker;

  using threadJob = std::pair<RotatorRequest, std::function<void(RotatorResponse)>>;
  ThreadsafeQueue<threadJob> jobQueue;
  
  // signal mechanism
  std::mutex jobEventMutex;
  std::condition_variable jobEvent;

  void connStart();
  void connTerminate();
  void connSettingPreset();
  static void threadMain(CamPTZ *self);

public:
  void Initialize(std::string tcpHost, int tcpPort, double aziOffset, double eleOffset);

  virtual void Start() override;
  virtual void Terminate() override;
  virtual bool Request(RotatorRequest req, std::function<void(RotatorResponse)> callback) override;
};