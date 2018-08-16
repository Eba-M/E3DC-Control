#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <resolv.h>

/*
 * This is a very simple example client socket connection.
 * This implementation is limited to Linux (POSIX) sockets only.
 * A Microsoft Windows implementation is not supplied in this example.
 */

int SocketConnect(const char *cpIpAddress, int iPort) {

    unsigned char ucBuffer[sizeof(struct in6_addr)];

    if(inet_pton(AF_INET, cpIpAddress, ucBuffer) <= 0) {
        printf("IP address %s cannot be converted.\n", cpIpAddress);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(iPort);
    server_addr.sin_addr = *((struct in_addr *) ucBuffer);

    int iSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(iSocket < 0) {
        printf("Cannot create socket. Error %i errno %i.\n", iSocket, errno);
        return iSocket;
    }

    // 3 secs receive timeout setup
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(iSocket, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *) &tv, sizeof(struct timeval));
    // send time out should never occur on normal OS configurations but just in case set the timeout to 5 seconds
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(iSocket, SOL_SOCKET, SO_SNDTIMEO, (struct timeval *) &tv, sizeof(struct timeval));

    int enable = 1;
    setsockopt(iSocket, IPPROTO_TCP, TCP_NODELAY, (char *) &enable, sizeof(enable));


    // wait 3 seconds for connection to get ready
    int iRetries = 3;
    if(connect(iSocket, (struct sockaddr *) &server_addr, sizeof(struct sockaddr)) < 0) {
        printf("Cannot connect to server. errno %i.\n", errno);
        close(iSocket);
        return -1;
    }

    return iSocket;
}

void SocketClose(int iSocket)
{
    // sanity check
    if(iSocket >= 0) {
        shutdown(iSocket, SHUT_RD);
        close(iSocket);
    }
}

int SocketSendData(int iSocket, const unsigned char * ucBuffer, int iLength)
{
    // sanity check
    if(iSocket < 0) {
        return iSocket;
    }

    int iSentBytes = 0;
    while(iLength)
    {
        int result = send(iSocket, ucBuffer, iLength, 0);
        if(result <= 0) {
            return -1;
        }
        iSentBytes += result;
        ucBuffer += result;
        iLength -= result;
    }
    return iSentBytes;
}

int SocketRecvData(int iSocket, unsigned char * ucBuffer, int iLength)
{
    // sanity check
    if(iSocket < 0) {
        return iSocket;
    }

    return recv(iSocket, ucBuffer, iLength, 0);
}
