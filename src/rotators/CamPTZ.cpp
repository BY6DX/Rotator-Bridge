#include "rotators/CamPTZ.hpp"
#include <cstring>
#include <cassert>

void CamPTZ::Initialize(
  std::string tcpHost, int tcpPort,
  double aziOffset, double eleOffset, bool smartSink, bool keepAlive)
{
  this->tcpHost = tcpHost;
  this->tcpPort = tcpPort;
  this->aziOffset = aziOffset;
  this->eleOffset = eleOffset;
  this->smartSink = smartSink;
  this->rotatorKeepAlive = keepAlive;
}

void CamPTZ::connStart()
{
  int status;

  sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == -1) {
    SOCKET_PRINT_ERROR("Error creating socket");
    return;
  }

  struct sockaddr_in hostAddr;
  hostAddr.sin_family = AF_INET;
  struct hostent *h = gethostbyname(tcpHost.c_str());
  if (h == nullptr) {
    fprintf(stderr, "Error during gethostbyname()\n");
    CLOSE_SOCKET(sock);
    return;
  }

  memcpy((char *)&hostAddr.sin_addr.s_addr, h->h_addr_list[0], h->h_length);
  hostAddr.sin_port = htons(tcpPort);

  status = connect(sock, (struct sockaddr *)&hostAddr, sizeof(hostAddr));
  if (status == -1) {
    SOCKET_PRINT_ERROR("Error connecting to target");
    CLOSE_SOCKET(sock);
    return;
  }

  sockConnected = true;

  // keepalive
  if (rotatorKeepAlive) {
    printf("CamPTZ Thread: Keep-alive started.\n");
    keepAliveThread = std::thread([this]() {
      bool error = false;
      while (!error) {
        std::this_thread::sleep_for(std::chrono::milliseconds(this->keepAliveInterval));

        printf("CamPTZ Thread: Requesting keep-alive.\n");
        RotatorRequest req;
        req.cmd = CHANGE_AZI;
        req.payload.ChangeAzi.aziRequested = this->lastAziTargetted;
        auto ret = this->RequestSync(req);
        if (!ret.has_value()) {
          error = true;
        }

        req.cmd = CHANGE_ELE;
        req.payload.ChangeEle.eleRequested = this->lastEleTargetted;
        ret = this->RequestSync(req);
        if (!ret.has_value()) {
          error = true;
        }
      }

      printf("CamPTZ Thread: Keep-alive exited due to error.\n");
    });

  }
}

void CamPTZ::connTerminate()
{
  CLOSE_SOCKET(sock);
}

void CamPTZ::threadMain(CamPTZ *self)
{
  self->connStart();

  if (!self->sockConnected) {
    fprintf(stderr, "CamPTZ Thread: Error connecting to target\n");
    self->threadExited = true;
    return;
  }

  while (!self->threadClosing) {
    bool error = false;
    std::optional<threadJob> job;

    // Wait on job
    {
      std::unique_lock<std::mutex> lk(self->jobEventMutex);
      self->jobEvent.wait(lk, [self]
                          { return (self->jobQueue.size() > 0) || (self->threadClosing); });

      job = self->jobQueue.pop();
    }

    assert(job.has_value());

    switch (job->first.cmd)
    {
    case CHANGE_AZI: {
      double aziDesired = job->first.payload.ChangeAzi.aziRequested;
      aziDesired += self->aziOffset;
      if (aziDesired < 0) {
        aziDesired += 360;
      } else if (aziDesired >= 360) {
        aziDesired -= 360;
      }

      int aziInt = std::round(aziDesired * 100);
      char aziCmd[] = {0xFF, 0x00, 0x00, 0x4B, 0x00, 0x00, 0x00};
      aziCmd[4] = (char)(aziInt / 256);
      aziCmd[5] = (char)(aziInt % 256);
      aziCmd[6] = (char)(aziCmd[3] + aziCmd[4] + aziCmd[5]);

      int ret = send_fixed(self->sock, aziCmd, sizeof(aziCmd), 0);
      if (ret == -1) {
        fprintf(stderr, "CamPTZ send error\n");
        error = true;
      }

      RotatorResponse resp;
      resp.success = !error;
      job->second(resp);

      break;
    }

    case CHANGE_ELE: {
      double eleDesired = job->first.payload.ChangeEle.eleRequested;
      eleDesired += self->eleOffset;
      eleDesired = 90 - eleDesired;

      int eleInt = std::round(eleDesired * 100);
      char eleCmd[] = {0xFF, 0x00, 0x00, 0x4D, 0x00, 0x00, 0x00};
      eleCmd[4] = (char)(eleInt / 256);
      eleCmd[5] = (char)(eleInt % 256);
      eleCmd[6] = (char)(eleCmd[3] + eleCmd[4] + eleCmd[5]);

      int ret = send_fixed(self->sock, eleCmd, sizeof(eleCmd), 0);
      if (ret == -1) {
        fprintf(stderr, "CamPTZ send error\n");
        error = true;
      }

      // an dummy one as callback
      RotatorResponse resp;
      resp.success = !error;
      job->second(resp);

      break;
    }

    case GET_AZI: {
      char aziCmd[] = {0xFF, 0x00, 0x00, 0x51, 0x00, 0x00, 0x51};
      int ret = send_fixed(self->sock, aziCmd, sizeof(aziCmd), 0);
      if (ret == -1) {
        fprintf(stderr, "CamPTZ send error\n");
        error = true;
      }

      char aziResp[7];
      ret = recv_fixed(self->sock, aziResp, sizeof(aziResp), 0);
      if (ret == -1) {
        fprintf(stderr, "CamPTZ recv error\n");
        error = true;
      }

      double aziGot = 0;
      aziGot += aziResp[4] * 256.0 + aziResp[5];
      aziGot /= 100;
      aziGot -= self->aziOffset;

      RotatorResponse resp;
      resp.success = !error;
      resp.payload.aziResp.azi = aziGot;
      job->second(resp);

      break;
    }
    
    case GET_ELE: {
      char eleCmd[] = {0xFF, 0x00, 0x00, 0x53, 0x00, 0x00, 0x53};
      int ret = send_fixed(self->sock, eleCmd, sizeof(eleCmd), 0);
      if (ret == -1) {
        fprintf(stderr, "CamPTZ send error\n");
        error = true;
      }

      char eleResp[7];
      ret = recv_fixed(self->sock, eleResp, sizeof(eleResp), 0);
      if (ret == -1) {
        fprintf(stderr, "CamPTZ recv error\n");
        error = true;
      }

      double eleGot = 0;
      eleGot += eleResp[4] * 256.0 + eleResp[5];
      eleGot /= 100;
      eleGot -= self->eleOffset;
      eleGot = 90 - eleGot;

      RotatorResponse resp;
      resp.success = !error;
      resp.payload.eleResp.ele = eleGot;
      // printf("CamPTZ Thread: GET_ELE Response: ele=%lf\n", eleGot);
      job->second(resp);

      break;
    }
    
    case CAMPTZ_PRESET_CALL:
    case CAMPTZ_PRESET_SET:
    case CAMPTZ_PRESET_CLEAR: {
      auto setPreset = [&](int presetIdx) {
        char cmd[] = {0xFF, 0x00, 0x00, 0x03, 0x00, presetIdx, 0x03 + presetIdx};
        int ret = send_fixed(self->sock, cmd, sizeof(cmd), 0);
        if (ret == -1) {
          fprintf(stderr, "CamPTZ send error\n");
          error = true;
        }
      };

      auto clearPreset = [&](int presetIdx) {
        char cmd[] = {0xFF, 0x00, 0x00, 0x05, 0x00, presetIdx, 0x05 + presetIdx};
        int ret = send_fixed(self->sock, cmd, sizeof(cmd), 0);
        if (ret == -1) {
          fprintf(stderr, "CamPTZ send error\n");
          error = true;
        }
      };

      auto callPreset = [&](int presetIdx) {
        char cmd[] = {0xFF, 0x00, 0x00, 0x07, 0x00, presetIdx, 0x07 + presetIdx};
        int ret = send_fixed(self->sock, cmd, sizeof(cmd), 0);
        if (ret == -1) {
          fprintf(stderr, "CamPTZ send error\n");
          error = true;
        }
      };

      if (job->first.cmd == CAMPTZ_PRESET_CALL) {
        callPreset(job->first.payload.CamPTZPreset.presetIdx);
      } else if (job->first.cmd == CAMPTZ_PRESET_SET) {
        setPreset(job->first.payload.CamPTZPreset.presetIdx);
      } else if (job->first.cmd == CAMPTZ_PRESET_CLEAR) {
        clearPreset(job->first.payload.CamPTZPreset.presetIdx);
      }

      RotatorResponse resp;
      resp.success = !error;
      job->second(resp);

      break;
    }

    default:
      fprintf(stderr, "Unknown command in CamPTZ packet. Ignore.\n");
    }

    // TODO: retry or cleanup
    if (error) {
      fprintf(stderr, "CamPTZ Thread: Sock error encountered, thread exiting\n");
      self->threadExited = true;
      return;
    }
  }

  // TODO: cleanup, if needed
  self->connTerminate();
  self->threadExited = true;
  return;
}

void CamPTZ::Start()
{
  worker = std::thread(CamPTZ::threadMain, this);
  threadExited = false;
  printf("CamPTZ Initialized.\n");
}

bool CamPTZ::Request(RotatorRequest req, std::function<void(RotatorResponse)> callback) {
  return RequestImpl(req, callback, false);
}

bool CamPTZ::RequestImpl(RotatorRequest req, std::function<void(RotatorResponse)> callback, bool noSmartSink)
{
  if (threadExited) {
    // error
    fprintf(stderr, "Worker closed, unable to request\n");

    return false;
  }

  // smartSink related
  bool suppressPushing = false;
  if (!noSmartSink) {
    bool cond1 = (req.cmd == CHANGE_AZI && req.payload.ChangeAzi.aziRequested == lastAziTargetted) 
              || (req.cmd == CHANGE_ELE && req.payload.ChangeEle.eleRequested == lastEleTargetted);
    std::chrono::duration<double> elapsed_seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - lastPosChange
    );

    bool cond2 = elapsed_seconds.count() < changeEffectiveMargin;

    if (cond1 && cond2 && !smartSinkSampling.load()) {
      // emit sampling
      smartSinkSampling.store(true);
      smartSinkTargetChanged.store(false);
      
      RotatorRequest reqQueryAzi;
      reqQueryAzi.cmd = GET_AZI;
      RequestImpl(reqQueryAzi, [&, req, callback](RotatorResponse resp) {
        this->smartSinkLastAzi = resp.payload.aziResp.azi;
        RotatorRequest reqQueryEle;
        reqQueryEle.cmd = GET_ELE;
        RequestImpl(reqQueryEle, [&, req, callback](RotatorResponse resp) {
          this->smartSinkLastQuery = std::chrono::steady_clock::now();
          this->smartSinkLastEle = resp.payload.eleResp.ele;

          // wait for smartSinkSamplingInterval; a silly version since I don't have event queue
          // and I don't want to block this working thread
          auto nextTh = std::thread([&, req, callback]() {
            std::this_thread::sleep_until(
              this->smartSinkLastQuery + std::chrono::milliseconds(this->smartSinkSamplingInterval)
            );

            // sample again
            RotatorRequest reqQueryAzi;
            reqQueryAzi.cmd = GET_AZI;
            RequestImpl(reqQueryAzi, [&, req, callback](RotatorResponse resp) {
              this->smartSinkThisAzi = resp.payload.aziResp.azi;
              RotatorRequest reqQueryEle;
              reqQueryEle.cmd = GET_ELE;
              RequestImpl(reqQueryEle, [&, req, callback](RotatorResponse resp) {
                auto timeNow = std::chrono::steady_clock::now();
                this->smartSinkThisEle = resp.payload.eleResp.ele;

                // test if the delta is sufficient; or we need to replay the request
                double deltaAzi = min(
                  360 - std::abs(smartSinkThisAzi - smartSinkLastAzi),
                  std::abs(smartSinkThisAzi - smartSinkLastAzi)
                );

                double deltaEle = min(
                  90 - std::abs(smartSinkThisEle - smartSinkLastEle),
                  std::abs(smartSinkThisEle - smartSinkLastEle)
                );

                bool needReplay = deltaEle + deltaAzi < this->smartSinkAngularVelocityMargin;
                printf(
                  "CamPTZ Thread: SmartSink got deltaEle=%lf, deltaAzi=%lf, needReplay=%s\n",
                  deltaEle, deltaAzi, needReplay ? "true" : "false"
                );

                if (needReplay && !smartSinkTargetChanged.load()) {
                  // a dummy callback, since callback have been called
                  RequestImpl(req, [](RotatorResponse) {}, false);
                }

                this->smartSinkSampling.store(false);
              }, false);
            }, false);
          });
          nextTh.detach();
        }, false);
      }, false);

      suppressPushing = true;
      // make callback by smartSink
      RotatorResponse respFake;
      respFake.success = true;
      callback(respFake);
    } else if (cond1 && cond2 && smartSinkSampling.load()) {
      suppressPushing = true;
      // make callback by smartSink
      RotatorResponse respFake;
      respFake.success = true;
      callback(respFake);
    }

    // recording
    if (req.cmd == CHANGE_AZI) {
      if (req.payload.ChangeAzi.aziRequested != lastAziTargetted) {
        this->smartSinkTargetChanged.store(true);
        lastPosChange = std::chrono::steady_clock::now();
      }
      lastAziTargetted = req.payload.ChangeAzi.aziRequested;
    } else if (req.cmd == CHANGE_ELE) {
      if (req.payload.ChangeEle.eleRequested != lastEleTargetted) {
        this->smartSinkTargetChanged.store(true);
        lastPosChange = std::chrono::steady_clock::now();
      }
      lastEleTargetted = req.payload.ChangeEle.eleRequested;
    }
  }

  // actual job push code
  if (!suppressPushing) {
    jobQueue.push(std::make_pair(
      req, callback
    ));

    std::unique_lock<std::mutex> lk(jobEventMutex);
    jobEvent.notify_all();
  }

  return true;
}

void CamPTZ::Terminate()
{
  threadClosing = true;
  std::unique_lock<std::mutex> lk(jobEventMutex);
  // in rare case this could fail because of TSO?
  // todo check this
  jobEvent.notify_all();

  worker.join();
  threadExited = true;
}