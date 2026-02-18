#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "../include/client_controller.h"
#include "../include/net_utils.h"

/*
 * Descrizione: Inizializza un socket TCP e tenta la connessione al server
 * all'indirizzo IP e porta specificati.
 *
 * Parametri:
 * server_ip - Stringa contenente l'indirizzo IPv4 del server.
 * port - Porta di ascolto del server.
 *
 * Ritorno:
 * int - Il file descriptor del socket connesso in caso di successo, -1 in caso di errore.
 */
int dakty_connect(const char* server_ip, int port) {
    int sockfd;
    struct sockaddr_in server_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Errore: impossibile creare il socket");
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Errore: indirizzo IP non valido");
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Errore: connessione al server Dakty fallita");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

/*
 * Descrizione: Chiude la connessione TCP attiva con il server.
 *
 * Parametri:
 * sockfd - File descriptor del socket da chiudere.
 *
 * Ritorno: Nessuno.
 */
void dakty_disconnect(int sockfd) {
    close(sockfd);
}

/*
 * Descrizione: Invia una richiesta di login al server e attende la risposta.
 * Serializza le credenziali nel payload e formatta l'header in Network Byte Order.
 *
 * Parametri:
 * sockfd - File descriptor del socket connesso.
 * username - Nome utente fornito dal client.
 * password - Password fornita dal client.
 *
 * Ritorno:
 * int - 1 se l'autenticazione ha successo, 0 in caso di credenziali errate o errore di rete.
 */
int dakty_login(int sockfd, const char* username, const char* password) {
    DKTHeader header;
    AuthPayload payload;

    memset(&header, 0, sizeof(DKTHeader));
    memset(&payload, 0, sizeof(AuthPayload));

    header.type = REQ_LOGIN;
    header.payload_length = htonl(sizeof(AuthPayload)); 

    strncpy(payload.username, username, MAX_USERNAME - 1);
    strncpy(payload.password, password, MAX_PASSWORD - 1);

    if (send_all(sockfd, &header, sizeof(DKTHeader), 0) < 0) {
        perror("Errore invio header login");
        return 0;
    }
    if (send_all(sockfd, &payload, sizeof(AuthPayload), 0) < 0) {
        perror("Errore invio payload login");
        return 0;
    }

    DKTHeader resp_header;
    if (recv_all(sockfd, &resp_header, sizeof(DKTHeader), 0) <= 0) {
        perror("Errore ricezione risposta dal server");
        return 0;
    }

    uint32_t resp_len = ntohl(resp_header.payload_length);

    if (resp_header.type == RESP_SUCCESS) {
        return 1; 
    } else if (resp_header.type == RESP_ERROR) {
        ErrorPayload err_payload;
        if (resp_len > 0 && recv_all(sockfd, &err_payload, sizeof(ErrorPayload), 0) > 0) {
            printf("Server Dakty rifiuta l'accesso: %s\n", err_payload.error_msg);
        }
        return 0;
    }

    return 0;
}

/*
 * Descrizione: Invia una richiesta di registrazione al server.
 *
 * Parametri:
 * sockfd - File descriptor del socket connesso.
 * username - Nome utente scelto per la registrazione.
 * password - Password associata al nuovo account.
 *
 * Ritorno:
 * int - 1 se la registrazione ha successo, 0 in caso di fallimento o errore di rete.
 */
int dakty_register(int sockfd, const char* username, const char* password) {
    DKTHeader header;
    AuthPayload payload;

    memset(&header, 0, sizeof(DKTHeader));
    memset(&payload, 0, sizeof(AuthPayload));

    header.type = REQ_REGISTER;
    header.payload_length = htonl(sizeof(AuthPayload));

    strncpy(payload.username, username, MAX_USERNAME - 1);
    strncpy(payload.password, password, MAX_PASSWORD - 1);

    if (send_all(sockfd, &header, sizeof(DKTHeader), 0) < 0) {
        perror("Errore invio header di registrazione");
        return 0;
    }
    if (send_all(sockfd, &payload, sizeof(AuthPayload), 0) < 0) {
        perror("Errore invio payload di registrazione");
        return 0;
    }

    DKTHeader resp_header;
    if (recv_all(sockfd, &resp_header, sizeof(DKTHeader), 0) <= 0) {
        perror("Errore ricezione risposta dal server");
        return 0;
    }

    if (resp_header.type == RESP_SUCCESS) {
        return 1;
    } else {
        return 0; 
    }
}

/*
 * Descrizione: Invia una richiesta formale di terminazione sessione (Logout).
 * L'operazione non prevede alcun payload aggiuntivo, ma solo l'invio dell'header.
 *
 * Parametri:
 * sockfd - File descriptor del socket connesso.
 *
 * Ritorno:
 * int - 1 in caso di successo, 0 in caso di errore di comunicazione.
 */
int dakty_logout(int sockfd) {
    DKTHeader header;
    DKTHeader resp_header;

    header.type = REQ_LOGOUT;
    header.payload_length = htonl(0); 

    if (send_all(sockfd, &header, sizeof(DKTHeader), 0) < 0) {
        perror("Errore invio richiesta di logout");
        return 0;
    }

    if (recv_all(sockfd, &resp_header, sizeof(DKTHeader), 0) <= 0) {
        perror("Errore ricezione risposta al logout");
        return 0;
    }

    if (resp_header.type == RESP_SUCCESS) {
        return 1;
    }
    return 0;
}

/*
 * Descrizione: Invia un nuovo messaggio testuale alla bacheca del server.
 *
 * Parametri:
 * sockfd - File descriptor del socket connesso.
 * subject - Stringa contenente l'oggetto del messaggio.
 * body - Stringa contenente il corpo del messaggio.
 *
 * Ritorno:
 * int - 1 se la pubblicazione è avvenuta con successo, 0 altrimenti.
 */
int dakty_post_message(int sockfd, const char* subject, const char* body) {
    DKTHeader header;
    MessagePayload payload;

    memset(&header, 0, sizeof(DKTHeader));
    memset(&payload, 0, sizeof(MessagePayload));

    header.type = REQ_POST_MSG;
    header.payload_length = htonl(sizeof(MessagePayload));

    strncpy(payload.subject, subject, MAX_SUBJECT - 1);
    strncpy(payload.body, body, MAX_BODY - 1);

    if (send_all(sockfd, &header, sizeof(DKTHeader), 0) < 0) {
        perror("Errore invio header post message");
        return 0;
    }
    if (send_all(sockfd, &payload, sizeof(MessagePayload), 0) < 0) {
        perror("Errore invio payload post message");
        return 0;
    }

    DKTHeader resp_header;
    if (recv_all(sockfd, &resp_header, sizeof(DKTHeader), 0) <= 0) {
        perror("Errore ricezione risposta dal server");
        return 0;
    }

    return (resp_header.type == RESP_SUCCESS) ? 1 : 0;
}

/*
 * Descrizione: Richiede e preleva tutti i messaggi presenti nella bacheca del server.
 * Alloca dinamicamente la memoria necessaria per contenere i messaggi ricevuti.
 * Convertendo i campi numerici dal formato di rete a quello dell'host locale.
 *
 * Parametri:
 * sockfd - File descriptor del socket connesso.
 * messages_out - Doppio puntatore usato per restituire al chiamante l'array allocato.
 * La responsabilità della liberazione della memoria (free) ricade sul chiamante.
 *
 * Ritorno:
 * int - Il numero totale di messaggi ricevuti (può essere 0). 
 * Restituisce -1 in caso di errore di rete o di allocazione di memoria.
 */
int dakty_read_messages(int sockfd, ResponsePayload** messages_out) {
    DKTHeader header;
    DKTHeader resp_header;

    memset(&header, 0, sizeof(DKTHeader));
    header.type = REQ_READ_MSG;
    header.payload_length = htonl(0);

    *messages_out = NULL;

    if (send_all(sockfd, &header, sizeof(DKTHeader), 0) < 0) {
        return -1; 
    }

    if (recv_all(sockfd, &resp_header, sizeof(DKTHeader), 0) <= 0) {
        return -1;
    }

    if (resp_header.type == RESP_ERROR) {
        return -1; 
    }

    /* Il payload_length in questo specifico caso contiene il conteggio dei messaggi, non i byte */
    uint32_t msg_count = ntohl(resp_header.payload_length);

    if (msg_count == 0) {
        return 0; 
    }

    *messages_out = (ResponsePayload*)malloc(msg_count * sizeof(ResponsePayload));
    if (*messages_out == NULL) {
        return -1; 
    }

    for (uint32_t i = 0; i < msg_count; i++) {
        if (recv_all(sockfd, &((*messages_out)[i]), sizeof(ResponsePayload), 0) <= 0) {
            free(*messages_out);
            *messages_out = NULL;
            return -1;
        }

        /* Ripristino dell'Endianness per l'identificativo del messaggio */
        (*messages_out)[i].message_id = ntohl((*messages_out)[i].message_id);
    }

    return (int)msg_count; 
}

/*
 * Descrizione: Invia una richiesta per eliminare un messaggio specifico dalla bacheca,
 * garantendo la corretta conversione in Network Byte Order dell'ID.
 *
 * Parametri:
 * sockfd - File descriptor del socket connesso.
 * message_id - Identificativo intero del messaggio da rimuovere.
 *
 * Ritorno:
 * int - 1 se l'eliminazione ha successo (il chiamante ne è l'autore), 0 in caso contrario.
 */
int dakty_delete_message(int sockfd, int message_id) {
    DKTHeader header;
    DeletePayload payload;

    memset(&header, 0, sizeof(DKTHeader));
    memset(&payload, 0, sizeof(DeletePayload));

    header.type = REQ_DELETE_MSG;
    header.payload_length = htonl(sizeof(DeletePayload));

    payload.message_id = htonl((uint32_t)message_id);

    if (send_all(sockfd, &header, sizeof(DKTHeader), 0) < 0) {
        perror("Errore invio header delete message");
        return 0;
    }
    if (send_all(sockfd, &payload, sizeof(DeletePayload), 0) < 0) {
        perror("Errore invio payload delete message");
        return 0;
    }

    DKTHeader resp_header;
    if (recv_all(sockfd, &resp_header, sizeof(DKTHeader), 0) <= 0) {
        perror("Errore ricezione risposta dal server");
        return 0;
    }

    return (resp_header.type == RESP_SUCCESS) ? 1 : 0;
}
