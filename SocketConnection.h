#ifndef __SOCKET_CONNECTION_H_
#define __SOCKET_CONNECTION_H_

/*
 * This is a very simple example client socket connection.
 * Plain functions are used in this example instead of a well formed C++ class.
 * This should not be used as "the correct" way of doing TCP connection but it is sufficient for this example
 * and the demonstration of the RSCP protocol which is not limited to TCP or Ethernet at all.
 */

int SocketConnect(const char *cpIpAddress, int iPort);
void SocketClose(int iSocket);
int SocketSendData(int iSocket, const unsigned char * ucBuffer, int iLength);
int SocketRecvData(int iSocket, unsigned char * ucBuffer, int iLength);


 #endif // __SOCKET_CONNECTION_H_
