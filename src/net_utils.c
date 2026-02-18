#include "../include/net_utils.h"
#include <errno.h>

ssize_t recv_all(int sockfd, void *buf, size_t len, int flags) {
    size_t total_read = 0;
    ssize_t bytes_left = len;
    ssize_t n;
    char *ptr = (char *)buf;

    while (total_read < len) {
        n = recv(sockfd, ptr + total_read, bytes_left, flags);
        
        if (n < 0) {
            if (errno == EINTR) {
                continue; // Interrotto da un segnale, riproviamo!
            }
            return -1; // Vero errore di rete
        } else if (n == 0) {
            break; // Il client ha chiuso la connessione (EOF)
        }
        
        total_read += n;
        bytes_left -= n;
    }
    return total_read;
}

ssize_t send_all(int sockfd, const void *buf, size_t len, int flags) {
    size_t total_sent = 0;
    ssize_t bytes_left = len;
    ssize_t n;
    const char *ptr = (const char *)buf;

    while (total_sent < len) {
        n = send(sockfd, ptr + total_sent, bytes_left, flags);
        
        if (n < 0) {
            if (errno == EINTR) {
                continue; // Interrotto da un segnale, riproviamo!
            }
            return -1; // Vero errore di rete
        }
        
        total_sent += n;
        bytes_left -= n;
    }
    return total_sent;
}

int safe_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    int fd;
    do {
        fd = accept(sockfd, addr, addrlen);
    } while (fd < 0 && errno == EINTR);
    return fd;
}
