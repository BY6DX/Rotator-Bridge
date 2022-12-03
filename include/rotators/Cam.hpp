#pragma once

#include "RotatorCommon.hpp"

class Cam : RotatorController {
private:
  int sock;

  double pseudoAzi;
  double pseudoEle;

public:
  std::string tcpHost;
  int tcpPort;

  void Initialize(std::string tcpHost, int tcpPort);

  virtual void Start() override;
  virtual void Terminate() override;
};