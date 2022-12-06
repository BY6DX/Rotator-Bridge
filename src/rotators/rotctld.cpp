#include "rotators/rotctld.hpp"

void rotctld::connStart()
{
  int status;

  sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == -1) {
    fprintf(stderr, "Error creating socket\n");
    return;
  }

  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serverAddr.sin_port = htons(tcpPort);

  if (bind(sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
    fprintf(stderr, "rotctld Thread: error binding to port %d\n", tcpPort);
    CLOSE_SOCKET(sock);
    sockActive = false;
  }

  sockActive = true;
}

void rotctld::connTerminate()
{
  CLOSE_SOCKET(sock);
}

void rotctld::threadMain(rotctld *self) {
  self->connStart();

  if (!self->sockActive) {
    fprintf(stderr, "rotctld Thread: Error binding to target\n");
    return;
  }

  if (listen(self->sock, 1) < 0) {
    fprintf(stderr, "rotctld Thread: error listening to port %d\n", self->tcpPort);
    CLOSE_SOCKET(self->sock);
    return;
  }

  while (true) {
    struct sockaddr_in clientAddr;
    int connSock, clientAddrLen = sizeof(clientAddr), n;
    connSock = accept(self->sock, (struct sockaddr *)&clientAddr, &clientAddrLen);

    if (connSock < 0) {
      fprintf(stderr, "rotctld Thread: error accepting\n");
      CLOSE_SOCKET(self->sock);
      return;
    }

    char str[INET_ADDRSTRLEN];
		printf("New connection from %s at PORT %d\n",
		       inet_ntop(AF_INET, &clientAddr.sin_addr, str, sizeof(str)),
		       ntohs(clientAddr.sin_port));

    self->clientWorkers.push_back(std::thread(rotctld::connThreadMain, self, connSock, clientAddr));
  }
  
}

// TODO: elegant
int recv_until_newline(int sockfd, char *buf, size_t buflen) {
  int bytes_read = 0, ret;
  char byte;
  while (recv_fixed(sockfd, &byte, 1, 0) > 0 && bytes_read < buflen) {
    if (ret != 1) {
      return ret;
    }

    buf[bytes_read] = byte;
    bytes_read++;

    if (byte == '\n') {
      return bytes_read;
    }
  }

  // insufficient buf, but its your fault!
  return -1;
}

void rotctld::connThreadMain(rotctld *self, int connSock, struct sockaddr_in clientAddr) {
  char buf[80];

  while (!self->threadClosing) {
    int ret = recv_until_newline(connSock, buf, sizeof(buf));
    if (ret <= 0) {
      fprintf(stderr, "rotctld Thread: failed to read newline.\n");
      CLOSE_SOCKET(connSock);
      return;
    }

    if (buf[0] == 'p') {
      // Request: Print az and el
      double azi, ele;

      // Get azi
      {
        RotatorRequest req;
        req.cmd = GET_AZI;

        RotatorResponse resp = self->requestHandler(req);
        azi = resp.payload.aziResp.azi;
      }

      // Get ele
      {
        RotatorRequest req;
        req.cmd = GET_ELE;

        RotatorResponse resp = self->requestHandler(req);
        ele = resp.payload.eleResp.ele;
      }

      char respBuf[80];
      snprintf(respBuf, sizeof(respBuf), "%lf\n%lf\n", azi, ele);

      ret = send_fixed(connSock, respBuf, sizeof(respBuf), 0);
      if (ret < 0) {
        fprintf(stderr, "rotctld Thread: failed to send response.\n");
        CLOSE_SOCKET(connSock);
        return;
      }

    } else if (buf[0] == 'P') {
      // Request: set az and el
      double azi, ele;
      sscanf(buf + 1, "%lf %lf", &azi, &ele);

      // Set azi
      {
        RotatorRequest req;
        req.cmd = CHANGE_AZI;
        req.payload.ChangeAzi.aziRequested = azi;

        RotatorResponse resp = self->requestHandler(req);
        // TODO: check return value
      }

      // Set ele
      {
        RotatorRequest req;
        req.cmd = CHANGE_ELE;
        req.payload.ChangeEle.eleRequested = ele;

        RotatorResponse resp = self->requestHandler(req);
        // TODO: check return value
      }

      
    } 
  }
}