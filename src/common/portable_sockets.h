#ifndef PORTABLE_SOCKETS_H
#define PORTABLE_SOCKETS_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <afunix.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define IS_INVALID_SOCKET(fd) ((fd) == INVALID_SOCKET)
#define CLOSE_SOCKET(fd) closesocket(fd)
#define SOCKET_ERROR_CODE WSAGetLastError()
#define SOCKET_EWOULDBLOCK WSAEWOULDBLOCK
#define SOCKET_EAGAIN WSAEWOULDBLOCK
#define SOCKET_EINTR WSAEINTR

#define poll WSAPoll

/* Map POSIX read/write to recv/send for sockets on Windows */
#define socket_read(fd, buf, len) recv((fd), (char *)(buf), (int)(len), 0)
#define socket_write(fd, buf, len)                                             \
  send((fd), (const char *)(buf), (int)(len), 0)

static inline int portable_socket_init(void) {
  WSADATA wsaData;
  return WSAStartup(MAKEWORD(2, 2), &wsaData);
}
static inline void portable_socket_cleanup(void) { WSACleanup(); }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define IS_INVALID_SOCKET(fd) ((fd) < 0)
#define CLOSE_SOCKET(fd) close(fd)
#define SOCKET_ERROR_CODE errno
#define SOCKET_EWOULDBLOCK EWOULDBLOCK
#define SOCKET_EAGAIN EAGAIN
#define SOCKET_EINTR EINTR

#define socket_read(fd, buf, len) read((fd), (buf), (len))
#define socket_write(fd, buf, len) write((fd), (buf), (len))

static inline int portable_socket_init(void) { return 0; }
static inline void portable_socket_cleanup(void) {}
#endif

#endif /* PORTABLE_SOCKETS_H */
