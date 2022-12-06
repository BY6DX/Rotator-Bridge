#include <iostream>
#include "rotators/CamPTZ.hpp"
#include "rotators/rotctld.hpp"

/* This code was given from MSDN */
static void InitWinSock2(void)
{
    WORD            wVersionRequested;
    WSADATA         wsaData;
    int             err;

    wVersionRequested = MAKEWORD(2, 2);

    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0)
    {
        /* Tell the user that we could not find a usable */
        /* WinSock DLL.                                  */
        return;
    }

    /* Confirm that the WinSock DLL supports 2.2. */
    /* Note that if the DLL supports versions later    */
    /* than 2.2 in addition to 2.2, it will still return */
    /* 2.2 in wVersion since that is the version we      */
    /* requested.                                        */

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
    {
        /* Tell the user that we could not find a usable */
        /* WinSock DLL.                                  */
        WSACleanup();
        return;
    }
}

static void CloseWinSock2(void)
{
    WSACleanup();
}

int main(int argc, char *argv[]) {
    InitWinSock2();

  auto source = rotctld();
  source.Initialize("127.0.0.1", 4533, true);

  auto sink = CamPTZ();
  sink.Initialize("192.168.3.136", 4196, 0, 0);

  source.SetRequestHandler([&](RotatorRequest req) -> RotatorResponse {
    auto ret = sink.RequestSync(req, 1000);
    if (!ret.has_value()) {
      fprintf(stderr, "main: Error while processing request\n");
      RotatorResponse resp;
      resp.success = false;

      return resp;
    }

    return ret.value();
  });
  
  sink.Start();
  source.Start();

  source.WaitForClose();


  CloseWinSock2();

  return 0;
}