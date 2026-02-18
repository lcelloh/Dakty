#ifndef NET_UTILS_H 
#define NET_UTILS_H 

#include <sys/socket.h>
#include <sys/types.h>

ssize_t recv_all(int sockfd, void *buf, size_t len, int flags);

ssize_t send_all(int sockfd, const void *buf, size_t len, int flags);

int safe_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

#endif
