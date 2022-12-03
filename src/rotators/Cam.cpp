#include "rotators/Cam.hpp"
#include <cstring>

void Cam::Initialize(std::string tcpHost, int tcpPort) {
    int status;

    this->tcpHost = tcpHost;
    this->tcpPort = tcpPort;

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

#ifdef WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return;
    }

    memcpy((char *)&hostAddr.sin_addr.s_addr, h->h_addr_list[0], h->h_length);
    hostAddr.sin_port = tcpPort;

    status = connect(sock, (struct sockaddr *)&hostAddr, sizeof(hostAddr));
    if (status == -1) {
        fprintf(stderr, "Error connecting to target\n");

#ifdef WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return;
    }
    printf("Cam Initialized.\n");
}
