#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "../include/client_controller.h"

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

void dakty_disconnect(int sockfd) {
    close(sockfd);
}

int dakty_login(int sockfd, const char* username, const char* password) {
    DKTHeader header;
    AuthPayload payload;

    // 1. Prepariamo i dati
    memset(&header, 0, sizeof(DKTHeader));
    memset(&payload, 0, sizeof(AuthPayload));

    header.type = REQ_LOGIN;
    // htonl converte l'intero nel formato di rete (gestione Endianness)
    header.payload_length = htonl(sizeof(AuthPayload)); 

    strncpy(payload.username, username, MAX_USERNAME - 1);
    strncpy(payload.password, password, MAX_PASSWORD - 1);

    // 2. FASE DI INVIO (Due SYSCALL separate per Header e Payload)
    if (send(sockfd, &header, sizeof(DKTHeader), 0) < 0) {
        perror("Errore invio header login");
        return 0;
    }
    if (send(sockfd, &payload, sizeof(AuthPayload), 0) < 0) {
        perror("Errore invio payload login");
        return 0;
    }

    DKTHeader resp_header;
    if (recv(sockfd, &resp_header, sizeof(DKTHeader), 0) <= 0) {
        perror("Errore ricezione risposta dal server");
        return 0;
    }

    // Convertiamo la lunghezza dal formato di rete al formato host
    uint32_t resp_len = ntohl(resp_header.payload_length);

    if (resp_header.type == RESP_SUCCESS) {
        return 1; // Login riuscito! (Nessun payload extra da leggere per il successo base)
    } else if (resp_header.type == RESP_ERROR) {
        ErrorPayload err_payload;
        if (resp_len > 0 && recv(sockfd, &err_payload, sizeof(ErrorPayload), 0) > 0) {
            printf("Server Dakty rifiuta l'accesso: %s\n", err_payload.error_msg);
        }
        return 0;
    }

    return 0;
}

int dakty_register(int sockfd, const char* username, const char* password) {
    DKTHeader header;
    AuthPayload payload;

    // 1. Inizializziamo le strutture a zero per pulizia
    memset(&header, 0, sizeof(DKTHeader));
    memset(&payload, 0, sizeof(AuthPayload));

    // 2. Prepariamo l'Header
    header.type = REQ_REGISTER;
    header.payload_length = htonl(sizeof(AuthPayload)); // Lunghezza in Network Byte Order

    // 3. Prepariamo il Payload copiando le stringhe in modo sicuro
    strncpy(payload.username, username, MAX_USERNAME - 1);
    strncpy(payload.password, password, MAX_PASSWORD - 1);

    // 4. Invio a due fasi: prima l'Header, poi il Payload
    if (send(sockfd, &header, sizeof(DKTHeader), 0) < 0) {
        perror("Errore invio header di registrazione");
        return 0;
    }
    if (send(sockfd, &payload, sizeof(AuthPayload), 0) < 0) {
        perror("Errore invio payload di registrazione");
        return 0;
    }

    // 5. Attesa della risposta dal server (leggiamo solo l'header)
    DKTHeader resp_header;
    if (recv(sockfd, &resp_header, sizeof(DKTHeader), 0) <= 0) {
        perror("Errore ricezione risposta dal server");
        return 0;
    }

    // 6. Valutazione del risultato
    if (resp_header.type == RESP_SUCCESS) {
        return 1; // Registrazione riuscita!
    } else {
        // Se il server risponde RESP_ERROR (es. utente già esistente)
        return 0; 
    }
}

int dakty_logout(int sockfd) {
    DKTHeader header;
    DKTHeader resp_header;

    // 1. Prepariamo e inviamo la richiesta (Solo Header, niente Payload)
    header.type = REQ_LOGOUT;
    header.payload_length = htonl(0); 

    if (send(sockfd, &header, sizeof(DKTHeader), 0) < 0) {
        perror("Errore invio richiesta di logout");
        return 0;
    }

    // 2. Attendiamo la conferma dal server
    if (recv(sockfd, &resp_header, sizeof(DKTHeader), 0) <= 0) {
        perror("Errore ricezione risposta al logout");
        return 0;
    }

    // 3. Verifichiamo il successo
    if (resp_header.type == RESP_SUCCESS) {
        return 1;
    }
    return 0;
}

int dakty_post_message(int sockfd, const char* subject, const char* body) {
    DKTHeader header;
    MessagePayload payload;

    memset(&header, 0, sizeof(DKTHeader));
    memset(&payload, 0, sizeof(MessagePayload));

    header.type = REQ_POST_MSG;
    header.payload_length = htonl(sizeof(MessagePayload));

    // Copiamo solo oggetto e testo. ID e Sender li gestisce il server in totale sicurezza!
    strncpy(payload.subject, subject, MAX_SUBJECT - 1);
    strncpy(payload.body, body, MAX_BODY - 1);

    if (send(sockfd, &header, sizeof(DKTHeader), 0) < 0) {
        perror("Errore invio header post message");
        return 0;
    }
    if (send(sockfd, &payload, sizeof(MessagePayload), 0) < 0) {
        perror("Errore invio payload post message");
        return 0;
    }

    DKTHeader resp_header;
    if (recv(sockfd, &resp_header, sizeof(DKTHeader), 0) <= 0) {
        perror("Errore ricezione risposta dal server");
        return 0;
    }

    return (resp_header.type == RESP_SUCCESS) ? 1 : 0;
}


int dakty_read_messages(int sockfd, ResponsePayload** messages_out) {
    DKTHeader header;
    DKTHeader resp_header;

    memset(&header, 0, sizeof(DKTHeader));
    header.type = REQ_READ_MSG;
    header.payload_length = htonl(0);

    // Inizializziamo il puntatore di output a NULL per sicurezza
    *messages_out = NULL;

    // 1. Invia la richiesta
    if (send(sockfd, &header, sizeof(DKTHeader), 0) < 0) {
        return -1; // Errore di rete
    }

    // 2. Ricevi l'header di risposta
    if (recv(sockfd, &resp_header, sizeof(DKTHeader), 0) <= 0) {
        return -1;
    }

    if (resp_header.type == RESP_ERROR) {
        return -1; // Errore logico (es. non loggato)
    }

    // 3. Estrai il numero di messaggi
    uint32_t msg_count = ntohl(resp_header.payload_length);

    if (msg_count == 0) {
        return 0; // Zero messaggi, ma non è un errore
    }

    // 4. Alloca l'array dinamicamente in base al numero di messaggi!
    *messages_out = (ResponsePayload*)malloc(msg_count * sizeof(ResponsePayload));
    if (*messages_out == NULL) {
        return -1; // Errore di allocazione memoria
    }

    // 5. Popola l'array leggendo dal socket
    for (uint32_t i = 0; i < msg_count; i++) {
        if (recv(sockfd, &((*messages_out)[i]), sizeof(ResponsePayload), MSG_WAITALL) <= 0) {
            // Se la rete cade a metà, puliamo la memoria ed usciamo
            free(*messages_out);
            *messages_out = NULL;
            return -1;
        }

        (*messages_out)[i].message_id = ntohl((*messages_out)[i].message_id);
    }

    return (int)msg_count; // Ritorna il numero esatto di messaggi estratti
}

int dakty_delete_message(int sockfd, int message_id) {
    DKTHeader header;
    DeletePayload payload;

    memset(&header, 0, sizeof(DKTHeader));
    memset(&payload, 0, sizeof(DeletePayload));

    header.type = REQ_DELETE_MSG;
    header.payload_length = htonl(sizeof(DeletePayload));

    // 1. Cast da int a uint32_t
    // 2. Conversione in Network Byte Order (htonl)
    payload.message_id = htonl((uint32_t)message_id);

    if (send(sockfd, &header, sizeof(DKTHeader), 0) < 0) {
        perror("Errore invio header delete message");
        return 0;
    }
    if (send(sockfd, &payload, sizeof(DeletePayload), 0) < 0) {
        perror("Errore invio payload delete message");
        return 0;
    }

    DKTHeader resp_header;
    if (recv(sockfd, &resp_header, sizeof(DKTHeader), 0) <= 0) {
        perror("Errore ricezione risposta dal server");
        return 0;
    }

    return (resp_header.type == RESP_SUCCESS) ? 1 : 0;
}
