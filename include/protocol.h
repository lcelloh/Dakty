#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h> // Necessario per uint32_t

#define MAX_USERNAME 32
#define MAX_PASSWORD 32

#define MAX_SUBJECT 64
#define MAX_BODY 512

// Tipi di operazioni supportate dal protocollo Dakty
typedef enum {
    REQ_REGISTER,
    REQ_LOGIN,
    REQ_LOGOUT,
    REQ_POST_MSG,
    REQ_READ_MSG,
    REQ_DELETE_MSG,
    RESP_SUCCESS,
    RESP_ERROR
} OperationType;

// Ogni pacchetto sarà costituito da header + payload
typedef struct __attribute__((packed)) {
    uint8_t type;            // 1 Byte fisso
    uint32_t payload_length; // 4 Byte fissi (da usare con htonl/ntohl)
} DKTHeader;

// Payload per le fasi di Registrazione e Login
typedef struct __attribute__((packed)){
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
} AuthPayload;

// Payload per l'invio o la ricezione di messaggi in bacheca
typedef struct __attribute__((packed)){
    uint32_t id;
    char sender[MAX_USERNAME];
    char subject[MAX_SUBJECT];
    char body[MAX_BODY];
} MessagePayload;

typedef struct __attribute__((packed)) {
    uint32_t message_id;
} DeletePayload;

// 3. Cosa invia il Server al Client quando legge la bacheca
typedef struct __attribute__((packed)){
    uint32_t message_id; // Fondamentale da mostrare all'utente per farglielo eliminare poi
    char sender[MAX_USERNAME];
    char subject[MAX_SUBJECT];
    char body[MAX_BODY];
} ResponsePayload;

// Payload generico per inviare messaggi di errore al client
typedef struct __attribute__((packed)){
    char error_msg[128];
} ErrorPayload;

#endif
