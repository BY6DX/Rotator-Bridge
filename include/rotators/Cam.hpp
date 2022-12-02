#pragma once

#include "RotatorBase.hpp"

class Cam : RotatorBase {
public:
  std::string tcpHost;
  int tcpPort;

  virtual void Initialize(RotatorConfig &cfg) override;
  virtual void GetAzimuth(double &azi) override;
  virtual void GetHeading(double &hdg) override;
  virtual void SetAzimuth(double azi) override;
  virtual void SetHeading(double hdg) override;
  virtual enum ConnectionStatus GetConnectionStatus() override;
};