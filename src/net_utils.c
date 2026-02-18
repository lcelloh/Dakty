#include "../include/net_utils.h"
#include <errno.h>

/*
 * Descrizione: Legge esattamente la quantità richiesta di byte da un socket,
 * gestendo in modo trasparente le letture parziali (frammentazione TCP) 
 * e le interruzioni di sistema causate da segnali (EINTR).
 *
 * Parametri:
 * sockfd - File descriptor del socket da cui leggere.
 * buf - Puntatore al buffer di destinazione in memoria.
 * len - Numero esatto di byte che si desidera leggere.
 * flags - Flag opzionali da inoltrare alla system call nativa recv.
 *
 * Ritorno:
 * ssize_t - Il numero totale di byte letti. Se è minore di 'len' ma maggiore di 0, 
 * indica che la connessione è caduta a metà. Restituisce 0 se il peer ha chiuso 
 * la connessione in modo pulito (EOF), o -1 per un errore di rete fatale.
 */
ssize_t recv_all(int sockfd, void *buf, size_t len, int flags) {
    size_t total_read = 0;
    ssize_t bytes_left = len;
    ssize_t n;
    char *ptr = (char *)buf;

    while (total_read < len) {
        n = recv(sockfd, ptr + total_read, bytes_left, flags);
        
        if (n < 0) {
            /* Se la chiamata è stata interrotta da un segnale, ritenta l'operazione */
            if (errno == EINTR) {
                continue; 
            }
            return -1; 
        } else if (n == 0) {
            /* Connessione chiusa dal peer (End Of File) */
            break; 
        }
        
        total_read += n;
        bytes_left -= n;
    }
    return total_read;
}

/*
 * Descrizione: Invia un blocco esatto di dati su uno stream socket.
 * Previene la corruzione dei dati causata dalle scritture parziali dei buffer 
 * del kernel e ignora le interruzioni di segnale (EINTR).
 *
 * Parametri:
 * sockfd - File descriptor del socket su cui scrivere.
 * buf - Puntatore al buffer costante contenente i dati da inviare.
 * len - Numero esatto di byte da trasmettere al kernel.
 * flags - Flag opzionali da inoltrare alla system call nativa send.
 *
 * Ritorno:
 * ssize_t - Il numero totale di byte scritti sul socket in caso di successo, 
 * oppure -1 in caso di errore fatale di rete.
 */
ssize_t send_all(int sockfd, const void *buf, size_t len, int flags) {
    size_t total_sent = 0;
    ssize_t bytes_left = len;
    ssize_t n;
    const char *ptr = (const char *)buf;

    while (total_sent < len) {
        n = send(sockfd, ptr + total_sent, bytes_left, flags);
        
        if (n < 0) {
            /* Ripresa automatica in caso di interruzione da segnale */
            if (errno == EINTR) {
                continue; 
            }
            return -1; 
        }
        
        total_sent += n;
        bytes_left -= n;
    }
    return total_sent;
}

/*
 * Descrizione: Wrapper sicuro per la system call accept(). Estrae la prima
 * richiesta di connessione dalla coda dei socket in ascolto, garantendo 
 * la non-interruzione in caso di arrivo di segnali asincroni.
 *
 * Parametri:
 * sockfd - File descriptor del socket in stato di LISTEN.
 * addr - Puntatore a una struttura sockaddr popolata con l'indirizzo del client.
 * addrlen - Puntatore alla variabile contenente la dimensione di addr.
 *
 * Ritorno:
 * int - Il file descriptor del nuovo socket dedicato alla connessione, 
 * oppure -1 in caso di errore non derivato da un segnale.
 */
int safe_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    int fd;
    do {
        fd = accept(sockfd, addr, addrlen);
    } while (fd < 0 && errno == EINTR);
    return fd;
}
