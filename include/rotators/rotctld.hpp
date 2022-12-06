#pragma once

#include "RotatorCommon.hpp"

class rotctld : PseudoRotator {
private:
  std::string tcpHost;
  int tcpPort;

  int sock;
  bool sockActive = false;
  bool threadClosing = false;
  bool threadExited = true;
  bool gpredictBugWalkaround;

  std::thread worker;
  std::vector<std::thread> clientWorkers;
  std::function<RotatorResponse(RotatorRequest)> requestHandler;

  void connStart();
  void connTerminate();
  static void connThreadMain(rotctld *self, int connSock, struct sockaddr_in clientAddr);
  static void threadMain(rotctld *self);

public:
  void Initialize(std::string tcpHost, int tcpPort, bool gpredictBugWalkaround);

  virtual void Start() override;
  virtual void WaitForClose() override;
  virtual void Terminate() override;
  virtual bool SetRequestHandler(std::function<RotatorResponse(RotatorRequest)> callback) override;
};