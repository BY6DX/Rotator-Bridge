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

  // smartSink: suppress subsequent (but same) pos change for a moving motor
  // - Condition 1: Requesting for same target movement
  // - Condition 2: Previous effective request is in 7 seconds
  // - Condition 3: Motor is moving with >5deg/sec past 1 second
  //   - Suppress subsequent sampling if already got one
  bool smartSink;
  const double changeEffectiveMargin = 7.0;  // (s)
  const int smartSinkSamplingInterval = 1000; // (ms)
  const double smartSinkAngularVelocityMargin = 5; // deg per sec
  std::chrono::steady_clock::time_point lastPosChange;
  double lastAziTargetted = 0.0, lastEleTargetted = 0.0;
  std::atomic<bool> smartSinkSampling;
  double smartSinkLastAzi, smartSinkLastEle;
  double smartSinkThisAzi, smartSinkThisEle;
  std::chrono::steady_clock::time_point smartSinkLastQuery;
  std::atomic<bool> smartSinkTargetChanged; // 

  bool rotatorKeepAlive;
  const int keepAliveInterval = 5000; // (ms)
  std::thread keepAliveThread;

  void connStart();
  void connTerminate();
  static void threadMain(CamPTZ *self);

  bool RequestImpl(RotatorRequest req, std::function<void(RotatorResponse)> callback, bool noSmartSink);

public:
  void Initialize(std::string tcpHost, int tcpPort, double aziOffset, double eleOffset, bool smartSink, bool keepAlive);

  virtual void Start() override;
  virtual void Terminate() override;
  virtual bool Request(RotatorRequest req, std::function<void(RotatorResponse)> callback) override;
};