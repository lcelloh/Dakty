#ifndef CLIENT_CONTROLLER_H
#define CLIENT_CONTROLLER_H

#include "protocol.h"

// Inizializza la connessione TCP. 
// Restituisce il file descriptor del socket in caso di successo, o -1 in caso di errore.
int dakty_connect(const char* server_ip, int port);

// Chiude la connessione in modo pulito.
void dakty_disconnect(int sockfd);

// Esegue il processo a due fasi per il login.
// Restituisce 1 per successo, 0 per fallimento.
int dakty_login(int sockfd, const char* username, const char* password);

int dakty_register(int sockfd, const char* username, const char* password);

int dakty_logout(int sockfd);

int dakty_post_message(int sockfd, const char* subject, const char* body);

// Richiede e stampa a video tutti i messaggi della bacheca
// Restituisce il numero di messaggi ricevuti, oppure -1 in caso di errore.
// NOTA: Il chiamante è responsabile di chiamare free() su *messages_out!
int dakty_read_messages(int sockfd, ResponsePayload** messages_out);

// Richiede l'eliminazione di un proprio messaggio tramite ID
int dakty_delete_message(int sockfd, int message_id);

#endif
