#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* Costanti di dimensionamento dei buffer per le stringhe del protocollo */
#define MAX_USERNAME 32
#define MAX_PASSWORD 32
#define MAX_SUBJECT 64
#define MAX_BODY 512

/*
 * Descrizione: Enumerazione dei codici operativi del protocollo Dakty.
 * Definisce il tipo di richiesta inviata dal client o il tipo di
 * risposta restituita dal server.
 */
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

/*
 * Descrizione: Struttura base dell'header di protocollo.
 * Precede ogni comunicazione di rete per indicare il tipo di operazione
 * e la dimensione dei dati successivi. Viene serializzata senza padding.
 *
 * Campi:
 * type - Codice operativo (appartenente a OperationType).
 * payload_length - Dimensione in byte del payload associato, in formato Network Byte Order.
 */
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint32_t payload_length;
} DKTHeader;

/*
 * Descrizione: Payload utilizzato per le operazioni di autenticazione.
 * Trasporta i dati per REQ_REGISTER e REQ_LOGIN.
 *
 * Campi:
 * username - Stringa contenente il nome utente.
 * password - Stringa contenente la password.
 */
typedef struct __attribute__((packed)) {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
} AuthPayload;

/*
 * Descrizione: Payload utilizzato dal client per l'invio di un nuovo
 * messaggio in bacheca (REQ_POST_MSG).
 *
 * Campi:
 * id - Identificativo del messaggio (compilato dal server).
 * sender - Mittente del messaggio (compilato dal server tramite sessione).
 * subject - Oggetto del messaggio digitato dall'utente.
 * body - Corpo testuale del messaggio digitato dall'utente.
 */
typedef struct __attribute__((packed)) {
    uint32_t id;
    char sender[MAX_USERNAME];
    char subject[MAX_SUBJECT];
    char body[MAX_BODY];
} MessagePayload;

/*
 * Descrizione: Payload utilizzato per richiedere l'eliminazione di
 * un messaggio esistente (REQ_DELETE_MSG).
 *
 * Campi:
 * message_id - ID del messaggio da eliminare, in formato Network Byte Order.
 */
typedef struct __attribute__((packed)) {
    uint32_t message_id;
} DeletePayload;

/*
 * Descrizione: Payload utilizzato dal server per inviare i singoli messaggi
 * al client durante la lettura della bacheca (risposta a REQ_READ_MSG).
 *
 * Campi:
 * message_id - ID univoco del messaggio, in formato Network Byte Order.
 * sender - Autore del messaggio.
 * subject - Oggetto del messaggio.
 * body - Corpo del messaggio.
 */
typedef struct __attribute__((packed)) {
    uint32_t message_id;
    char sender[MAX_USERNAME];
    char subject[MAX_SUBJECT];
    char body[MAX_BODY];
} ResponsePayload;

/*
 * Descrizione: Payload opzionale utilizzato dal server per fornire
 * dettagli testuali al client in caso di fallimento di un'operazione (RESP_ERROR).
 *
 * Campi:
 * error_msg - Stringa descrittiva dell'errore verificatosi lato server.
 */
typedef struct __attribute__((packed)) {
    char error_msg[128];
} ErrorPayload;

#endif
