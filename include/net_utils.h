#ifndef NET_UTILS_H 
#define NET_UTILS_H 

#include <sys/socket.h>
#include <sys/types.h>

// Legge ESATTAMENTE 'len' byte, gestendo EINTR e letture parziali.
// Ritorna 'len' in caso di successo, 0 se la connessione è chiusa, -1 per errore.
ssize_t recv_all(int sockfd, void *buf, size_t len, int flags);

// Scrive ESATTAMENTE 'len' byte, gestendo EINTR e scritture parziali.
// Ritorna 'len' in caso di successo, -1 per errore fatale.
ssize_t send_all(int sockfd, const void *buf, size_t len, int flags);

// Esegue l'accept gestendo internamente EINTR.
int safe_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

#endif
