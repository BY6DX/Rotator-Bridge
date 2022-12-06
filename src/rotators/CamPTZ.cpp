#include "rotators/CamPTZ.hpp"
#include <cstring>
#include <cassert>

void CamPTZ::Initialize(std::string tcpHost, int tcpPort, double aziOffset, double eleOffset)
{
  this->tcpHost = tcpHost;
  this->tcpPort = tcpPort;
  this->aziOffset = aziOffset;
  this->eleOffset = eleOffset;
}

void CamPTZ::connStart()
{
  int status;

  sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == -1) {
    fprintf(stderr, "Error creating socket\n");
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
  hostAddr.sin_port = tcpPort;

  status = connect(sock, (struct sockaddr *)&hostAddr, sizeof(hostAddr));
  if (status == -1) {
    fprintf(stderr, "Error connecting to target\n");
    CLOSE_SOCKET(sock);
    return;
  }

  sockConnected = true;
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

      char aziResp[6];
      int ret = recv_fixed(self->sock, aziResp, sizeof(aziResp), 0);
      if (ret == -1) {
        fprintf(stderr, "CamPTZ recv error\n");
        error = true;
      }

      double aziGot = 0;
      aziGot += aziResp[4] * 256.0 + aziResp[5];
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

      char eleResp[6];
      int ret = recv_fixed(self->sock, eleResp, sizeof(eleResp), 0);
      if (ret == -1) {
        fprintf(stderr, "CamPTZ recv error\n");
        error = true;
      }

      double eleGot = 0;
      eleGot += eleResp[4] * 256.0 + eleResp[5];
      eleGot -= self->eleOffset;

      RotatorResponse resp;
      resp.success = !error;
      resp.payload.eleResp.ele = eleGot;
      job->second(resp);

      break;
    }
    
    default:
      fprintf(stderr, "Unknown command in CamPTZ packet. Ignore.\n");
    }

    // TODO: retry or cleanup
    if (error) {
      fprintf(stderr, "CamPTZ Thread: Sock error encountered, thread exiting\n");
      return;
    }
  }

  // TODO: cleanup, if needed
  self->connTerminate();
  return;
}

void CamPTZ::Start()
{
  worker = std::thread(CamPTZ::threadMain, this);
  threadExited = false;
  printf("Cam Initialized.\n");
}

bool CamPTZ::Request(RotatorRequest req, std::function<void(RotatorResponse)> callback)
{
  if (threadExited || worker.joinable()) {
    // error
    fprintf(stderr, "Worker closed, unable to request\n");

    return false;
  }
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