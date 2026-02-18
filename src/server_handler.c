#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include "../include/protocol.h"
#include "../include/server_handler.h"
#include "../include/persistence.h"

// SOTTOFUNZIONI PRIVATE

static void process_login(int client_sock, uint32_t payload_len, char* username, int* is_logged) {
    AuthPayload auth_data;
    DKTHeader response;

    // Controllo di sicurezza sulla dimensione
    if (payload_len != sizeof(AuthPayload)) {
        printf("[Thread %lu] Errore: dimensione payload Auth non valida.\n", pthread_self());
        response.type = RESP_ERROR;
        response.payload_length = htonl(0);
        send(client_sock, &response, sizeof(DKTHeader), 0);
        return;
    }

    // Lettura del payload
    recv(client_sock, &auth_data, payload_len, MSG_WAITALL);
    printf("[Thread %lu] Tentativo di login da: %s\n", pthread_self(), auth_data.username);
    
    // Chimata allo stato di persistenza per l'autenticazione dell'utente
    if (authenticate_user(auth_data.username, auth_data.password)) {
        response.type = RESP_SUCCESS;
        
        // Salviamo l'identità nella sessione del thread
        *is_logged= 1;
        strncpy(username, auth_data.username, MAX_USERNAME - 1);
        username[MAX_USERNAME - 1] = '\0'; // Sicurezza stringhe C
        
        printf("[Thread %lu] Login ACCETTATO. Sessione legata a '%s'.\n", pthread_self(), username);
    } else {
        response.type = RESP_ERROR;
        *is_logged= 0;
        memset(username, 0, MAX_USERNAME); // Puliamo per sicurezza
        printf("[Thread %lu] Login RIFIUTATO (credenziali errate o utente inesistente).\n", pthread_self());

    }
    response.payload_length = htonl(0);
    send(client_sock, &response, sizeof(DKTHeader), 0);
}

static void process_register(int client_sock, uint32_t payload_len, char* username, int* is_logged) {
    AuthPayload auth_data;
    DKTHeader response;

    if (payload_len != sizeof(AuthPayload)) {
        printf("[Thread %lu] Registrazione fallita: dimensione payload errata.\n", pthread_self());
        response.type = RESP_ERROR;
        response.payload_length = htonl(0);
        send(client_sock, &response, sizeof(DKTHeader), 0);
        return;
    }

    if (recv(client_sock, &auth_data, payload_len, MSG_WAITALL) <= 0) {
        perror("[Thread %lu] Errore recv in process_register");
        return;
    }

    printf("[Thread %lu] Richiesta di registrazione per: '%s'\n", pthread_self(), auth_data.username);

    // Proviamo a registrare l'utente nel sistema
    if (register_user(auth_data.username, auth_data.password)) {
        response.type = RESP_SUCCESS;
        
        // AUTO-LOGIN: L'utente è appena stato creato, lo consideriamo già loggato!
        *is_logged= 1;
        strncpy(username, auth_data.username, MAX_USERNAME - 1);
        username[MAX_USERNAME - 1] = '\0'; // Sicurezza stringhe C
        
        printf("[Thread %lu] Registrazione COMPLETATA. Utente '%s' auto-loggato nella sessione.\n", pthread_self(), username);
    } else {
        response.type = RESP_ERROR;
        *is_logged= 0; // Per sicurezza, assicuriamoci che non sia loggato
        printf("[Thread %lu] Registrazione FALLITA (Utente già esistente o cache piena).\n", pthread_self());
    }

    response.payload_length = htonl(0);
    send(client_sock, &response, sizeof(DKTHeader), 0);
}

static void process_logout(int client_sock, char* user, int* is_logged) {
    DKTHeader response;
    response.payload_length = htonl(0);

    if (*is_logged) {
        printf("[Thread %lu] L'utente '%s' ha effettuato il logout.\n", pthread_self(), user);
        
        // Pulizia dello stato in memoria del thread
        *is_logged= 0;
        memset(user, 0, MAX_USERNAME);
        
        response.type = RESP_SUCCESS;
    } else {
        printf("[Thread %lu] Richiesta di logout da utente non loggato.\n", pthread_self());
        response.type = RESP_ERROR;
    }

    response.payload_length = htonl(0);
    send(client_sock, &response, sizeof(DKTHeader), 0);
}

static void process_post_msg(int client_sock, uint32_t payload_len, const char* username, int is_logged) {
    MessagePayload req_data;
    DKTHeader response;

    // Controllo Sicurezza 1: L'utente è loggato?
    if (!is_logged) {
        printf("[Thread %lu] Errore Sicurezza: Utente anonimo tenta di scrivere in bacheca.\n", pthread_self());
        response.type = RESP_ERROR;
        response.payload_length = htonl(0);
        send(client_sock, &response, sizeof(DKTHeader), 0);
        // Svuotiamo il socket dal payload inatteso per non corrompere le letture future
        recv(client_sock, &req_data, payload_len, MSG_WAITALL); 
        return;
    }

    // Controllo Sicurezza 2: Dimensione corretta?
    if (payload_len != sizeof(MessagePayload)) {
        printf("[Thread %lu] Errore: dimensione payload POST non valida.\n", pthread_self());
        response.type = RESP_ERROR;
        response.payload_length = htonl(0);
        send(client_sock, &response, sizeof(DKTHeader), 0);
        return;
    }

    // Ricezione del testo e dell'oggetto inviati dal client
    recv(client_sock, &req_data, payload_len, MSG_WAITALL);

    // Salviamo in RAM (il mittente lo aggiunge il Server in base alla sessione!)
    int new_id = save_message(username, req_data.subject, req_data.body);

    if (new_id > 0) {
        printf("[Thread %lu] Messaggio salvato da '%s' con ID %d.\n", pthread_self(), username, new_id);
        response.type = RESP_SUCCESS;
    } else {
        printf("[Thread %lu] Errore: Bacheca piena. Impossibile salvare il messaggio di '%s'.\n", pthread_self(), username);
        response.type = RESP_ERROR;
    }

    response.payload_length = htonl(0);
    send(client_sock, &response, sizeof(DKTHeader), 0);
}

static void process_read_msg(int client_sock, uint32_t payload_len, int is_logged) {
    DKTHeader response;

    if (payload_len != 0) {
        printf("[Thread %lu] Errore: REQ_READ_MSG non ammette payload.\n", pthread_self());
        
        // Per robustezza, svuotiamo il socket dai byte extra (Trash flush)
        char trash[1024];
        uint32_t to_read = payload_len > 1024 ? 1024 : payload_len;
        recv(client_sock, trash, to_read, MSG_WAITALL);
        
        response.type = RESP_ERROR;
        response.payload_length = htonl(0);
        send(client_sock, &response, sizeof(DKTHeader), 0);
        return;
    }
    // Controllo Sicurezza: L'utente è loggato?
    if (!is_logged) {
        response.type = RESP_ERROR;
        response.payload_length = htonl(0);
        send(client_sock, &response, sizeof(DKTHeader), 0);
        return;
    }

    // Preleviamo tutti i messaggi dalla RAM
    MessageRecord buffer[MAX_MESSAGGES];
    int count = get_messages(buffer, MAX_MESSAGGES);

    // Diciamo al client quanti messaggi stanno per arrivare
    // Il payload_length qui non indica i Byte, ma il NUMERO DI MESSAGGI!
    // È un trucco elegante usato in molti protocolli custom.
    response.type = RESP_SUCCESS;
    response.payload_length = htonl((uint32_t)count);
    send(client_sock, &response, sizeof(DKTHeader), 0);

    printf("[Thread %lu] Invio di %d messaggi al client.\n", pthread_self(), count);

    // Inviamo i messaggi uno ad uno (Convertendo da MessageRecord a ResponsePayload)
    ResponsePayload net_msg;
    for (int i = 0; i < count; i++) {
        net_msg.message_id = htonl(buffer[i].id); // Proteggiamo l'ID dall'Endianness
        strncpy(net_msg.sender, buffer[i].sender, MAX_USERNAME);
        strncpy(net_msg.subject, buffer[i].subject, MAX_SUBJECT);
        strncpy(net_msg.body, buffer[i].body, MAX_BODY);
        
        send(client_sock, &net_msg, sizeof(ResponsePayload), 0);
    }
}

static void process_delete_msg(int client_sock, uint32_t payload_len, const char* username, int is_logged) {
    DeletePayload req_data;
    DKTHeader response;

    // Controllo Sicurezza
    if (!is_logged) {
        response.type = RESP_ERROR;
        response.payload_length = htonl(0);
        send(client_sock, &response, sizeof(DKTHeader), 0);
        recv(client_sock, &req_data, payload_len, MSG_WAITALL); // Flush socket
        return;
    }

    if (payload_len != sizeof(DeletePayload)) {
        response.type = RESP_ERROR;
        response.payload_length = htonl(0);
        send(client_sock, &response, sizeof(DKTHeader), 0);
        return;
    }

    recv(client_sock, &req_data, payload_len, MSG_WAITALL);
    
    // Ripristiniamo l'ID dal Network Byte Order
    uint32_t target_id = ntohl(req_data.message_id);

    printf("[Thread %lu] L'utente '%s' richiede eliminazione msg ID %u.\n", pthread_self(), username, target_id);

    // Proviamo l'eliminazione (il Server controllerà se l'utente è davvero l'autore)
    if (delete_message(target_id, username)) {
        printf("[Thread %lu] Messaggio %u eliminato con successo.\n", pthread_self(), target_id);
        response.type = RESP_SUCCESS;
    } else {
        printf("[Thread %lu] Eliminazione negata per msg %u (Non trovato o non autorizzato).\n", pthread_self(), target_id);
        response.type = RESP_ERROR;
    }

    response.payload_length = htonl(0);
    send(client_sock, &response, sizeof(DKTHeader), 0);
}

// ============================================
//  INTERFACCIA PUBBLICA
// ===========================================
void handle_client(int client_sock) {
    DKTHeader header;
    ssize_t bytes_read;

    int is_logged = 0;
    char user[MAX_USERNAME] = "";

    printf("[Thread %lu] Inizio sessione con il socket %d\n", pthread_self(), client_sock);

    while (1) {
        bytes_read = recv(client_sock, &header, sizeof(DKTHeader), 0);
        
        if (bytes_read == 0) {
            printf("[Thread %lu] Il client ha chiuso la connessione (Socket %d).\n", pthread_self(), client_sock);
            break; 
        } else if (bytes_read < 0) {
            perror("Errore durante la ricezione dell'header");
            break; 
        }

        uint32_t payload_len = ntohl(header.payload_length);

        switch (header.type) {
            case REQ_LOGIN:
                process_login(client_sock, payload_len, user, &is_logged);
                break;
            case REQ_REGISTER:
                process_register(client_sock, payload_len, user, &is_logged);
                break;
            case REQ_LOGOUT:
                process_logout(client_sock, user, &is_logged);
                break; // <-- Ricordati il break qui, mancava nel tuo codice!
                
            case REQ_POST_MSG:
                process_post_msg(client_sock, payload_len, user, is_logged);
                break;
            case REQ_READ_MSG:
                process_read_msg(client_sock, payload_len, is_logged);
                break;
            case REQ_DELETE_MSG:
                process_delete_msg(client_sock, payload_len, user, is_logged);
                break;
            default:
                printf("[Thread %lu] Operazione non supportata (%d).\n", pthread_self(), header.type);
                // Svuotiamo il buffer del socket se arriva un payload inatteso per non corrompere la sessione
                if (payload_len > 0) {
                    char trash[1024];
                    int to_read = payload_len > 1024 ? 1024 : payload_len;
                    recv(client_sock, trash, to_read, MSG_WAITALL);
                }
                break;
        }
    }

    close(client_sock);
    printf("[Thread %lu] Socket %d chiuso, sessione terminata.\n", pthread_self(), client_sock);
}
