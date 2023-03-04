#include "rotators/rotctld.hpp"

void rotctld::Initialize(std::string tcpHost, int tcpPort, bool gpredictBugWalkaround)
{
  this->tcpHost = tcpHost;
  this->tcpPort = tcpPort;
  this->gpredictBugWalkaround = gpredictBugWalkaround;
}

void rotctld::connStart()
{
  int status;

  sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == -1) {
    SOCKET_PRINT_ERROR("Error creating socket");
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
    int connSock, n;
    unsigned int clientAddrLen = sizeof(clientAddr);
#ifdef WIN32
    connSock = accept(self->sock, (struct sockaddr *)&clientAddr, (int *)&clientAddrLen);
#else
    connSock = accept(self->sock, (struct sockaddr *)&clientAddr, &clientAddrLen);
#endif

    if (connSock < 0) {
      fprintf(stderr, "rotctld Thread: error accepting\n");
      CLOSE_SOCKET(self->sock);
      return;
    }

    char str[INET_ADDRSTRLEN];
		printf("New connection from %s at PORT %d\n",
		       inet_ntop(AF_INET, &clientAddr.sin_addr, str, sizeof(str)),
		       ntohs(clientAddr.sin_port));

    self->clientWorkers.push_back(
      std::thread(
        [](rotctld *self, int connSock, struct sockaddr_in clientAddr) {
          rotctld::connThreadMain(self, connSock, clientAddr);
          printf("Client exited.\n");
        },
        self, connSock, clientAddr
      )
    );
  }
  
}

// TODO: elegant
int recv_until_newline(int sockfd, char *buf, size_t buflen) {
  int bytes_read = 0, ret;
  char byte;
  while ((ret = recv_fixed(sockfd, &byte, 1, 0)) > 0 && bytes_read < buflen) {
    if (ret != 1) {
      return ret;
    }

    buf[bytes_read] = byte;
    printf("Got %c\n", byte);
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

  printf("rotctld::connThreadMain initialized.\n");

  while (!self->threadClosing) {
    int ret;
    if (self->gpredictBugWalkaround) {
      // In Gpredict v2.2.1, there is incorrect handling for Windows rotator protocol that accidentally
      // got the trailing '\n' removed; See
      // https://github.com/csete/gpredict/commit/f0d6afce3fc457963de9ae620af517c76deb82a1
      //
      // However, this is not a reliable way, since there is no guarantee that a full
      // command packet can be read by one recv call, and parsing this way is potentially buggy
      ret = recv(connSock, buf, sizeof(buf), 0);
    } else {
      ret = recv_until_newline(connSock, buf, sizeof(buf));
    }
    
    if (ret <= 0) {
      fprintf(stderr, "rotctld Thread: failed to read newline.\n");
      CLOSE_SOCKET(connSock);
      return;
    }

    if (buf[0] == 'p') {
      // printf("rotctld Thread: Command received: %c\n", buf[0]);
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

      // printf("rotctld Thread: Command response: azi=%lf, ele=%lf\n", azi, ele);

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

      std::string respBuf = "RET 0";
      ret = send_fixed(connSock, respBuf.c_str(), respBuf.size(), 0);
      if (ret < 0) {
        fprintf(stderr, "rotctld Thread: failed to send response.\n");
        CLOSE_SOCKET(connSock);
        return;
      }
    } else if (buf[0] == 'S') {
      // stop
      std::string respBuf = "RET 0";
      ret = send_fixed(connSock, respBuf.c_str(), respBuf.size(), 0);
      if (ret < 0) {
        fprintf(stderr, "rotctld Thread: failed to send response.\n");
        CLOSE_SOCKET(connSock);
        return;
      }

      CLOSE_SOCKET(connSock);
      return;
    }
  }
}

void rotctld::WaitForClose() {
  worker.join();
}

void rotctld::Start() {
  worker = std::thread(rotctld::threadMain, this);
  threadExited = false;
  printf("rotctld Initialized.\n");
}

void rotctld::Terminate() {
  threadClosing = true;

  worker.join();
  threadExited = true;
}

bool rotctld::SetRequestHandler(
  std::function<RotatorResponse(RotatorRequest)> callback
) {
  requestHandler = callback;
  return true;
}