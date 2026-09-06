// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#pragma once
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace yolo::net {
#ifdef _WIN32
using Socket = SOCKET;
using Poll = WSAPOLLFD;
constexpr Socket invalid = INVALID_SOCKET;
constexpr int send_flags = 0;
inline void init() {
  static const bool ready = [] { WSADATA w{}; return WSAStartup(MAKEWORD(2,2), &w) == 0; }();
  if (!ready) throw std::runtime_error("Winsock initialization failed");
}
inline void close(Socket s) { closesocket(s); }
inline int poll(Poll *p, unsigned n, int ms) { return WSAPoll(p, n, ms); }
inline bool again() { auto e=WSAGetLastError(); return e==WSAEWOULDBLOCK || e==WSAEINPROGRESS; }
inline bool interrupted() { return WSAGetLastError()==WSAEINTR; }
inline bool nonblock(Socket s) { u_long on=1; return ioctlsocket(s,FIONBIO,&on)==0; }
#else
using Socket = int;
using Poll = pollfd;
constexpr Socket invalid = -1;
constexpr int send_flags = MSG_NOSIGNAL;
inline void init() {}
inline void close(Socket s) { ::close(s); }
inline int poll(Poll *p, unsigned n, int ms) { return ::poll(p,n,ms); }
inline bool again() { return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINPROGRESS; }
inline bool interrupted() { return errno==EINTR; }
inline bool nonblock(Socket s) {
  return fcntl(s,F_SETFD,FD_CLOEXEC)==0 && fcntl(s,F_SETFL,O_NONBLOCK)==0;
}
#endif
inline Socket socket(int family) {
  init(); auto s=::socket(family,SOCK_STREAM,IPPROTO_TCP);
  if (s!=invalid && !nonblock(s)) {close(s); return invalid;}
  return s;
}
}
