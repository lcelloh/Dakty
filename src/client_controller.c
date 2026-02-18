#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "../include/client_controller.h"
#include "../include/net_utils.h" // <-- Il nostro nuovo scudo TCP!

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

    // La connect rimane normale come abbiamo discusso
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

    uint32_t msg_count = ntohl(resp_header.payload_length);

    if (msg_count == 0) {
        return 0; 
    }

    *messages_out = (ResponsePayload*)malloc(msg_count * sizeof(ResponsePayload));
    if (*messages_out == NULL) {
        return -1; 
    }

    for (uint32_t i = 0; i < msg_count; i++) {
        // Rimosso MSG_WAITALL, ci pensa il nostro recv_all!
        if (recv_all(sockfd, &((*messages_out)[i]), sizeof(ResponsePayload), 0) <= 0) {
            free(*messages_out);
            *messages_out = NULL;
            return -1;
        }

        (*messages_out)[i].message_id = ntohl((*messages_out)[i].message_id);
    }

    return (int)msg_count; 
}

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
