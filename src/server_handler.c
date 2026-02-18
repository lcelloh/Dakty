#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include "../include/protocol.h"
#include "../include/server_handler.h"
#include "../include/persistence.h"
#include "../include/net_utils.h" 

// ==========================================
// SOTTOFUNZIONI PRIVATE (ROUTING RICHIESTE)
// ==========================================

/*
 * Descrizione: Gestisce la richiesta di REQ_LOGIN. Estrae le credenziali dal
 * socket, interroga il modulo di persistenza per l'autenticazione e, in caso
 * di successo, aggiorna lo stato della sessione del thread corrente.
 *
 * Parametri:
 * client_sock - File descriptor del socket del client connesso.
 * payload_len - Dimensione attesa del payload associato alla richiesta.
 * username - Buffer del thread in cui salvare il nome utente autenticato.
 * is_logged - Puntatore alla variabile di stato della sessione (1 = loggato, 0 = no).
 *
 * Ritorno: Nessuno (risponde direttamente al client tramite socket).
 */
static void process_login(int client_sock, uint32_t payload_len, char* username, int* is_logged) {
    AuthPayload auth_data;
    DKTHeader response;

    /* Validazione di sicurezza preventiva sulla dimensione del payload in arrivo */
    if (payload_len != sizeof(AuthPayload)) {
        printf("[Thread %lu] Errore: dimensione payload Auth non valida.\n", pthread_self());
        response.type = RESP_ERROR;
        response.payload_length = htonl(0);
        send_all(client_sock, &response, sizeof(DKTHeader), 0);
        return;
    }

    if (recv_all(client_sock, &auth_data, payload_len, 0) <= 0) {
        perror("[Thread] Errore di rete durante la lettura del payload di login");
        return;
    }

    printf("[Thread %lu] Tentativo di login da: %s\n", pthread_self(), auth_data.username);
    
    if (authenticate_user(auth_data.username, auth_data.password)) {
        response.type = RESP_SUCCESS;
        
        *is_logged = 1;
        strncpy(username, auth_data.username, MAX_USERNAME - 1);
        username[MAX_USERNAME - 1] = '\0'; 
        
        printf("[Thread %lu] Login ACCETTATO. Sessione legata a '%s'.\n", pthread_self(), username);
    } else {
        response.type = RESP_ERROR;
        *is_logged = 0;
        memset(username, 0, MAX_USERNAME); 
        printf("[Thread %lu] Login RIFIUTATO (credenziali errate o utente inesistente).\n", pthread_self());
    }

    response.payload_length = htonl(0);
    send_all(client_sock, &response, sizeof(DKTHeader), 0);
}

/*
 * Descrizione: Gestisce la richiesta di REQ_REGISTER. Invia i dati al
 * modulo di persistenza per l'inserimento. In caso di successo, esegue 
 * l'auto-login automatico aggiornando lo stato della sessione.
 *
 * Parametri:
 * client_sock - File descriptor del socket del client connesso.
 * payload_len - Dimensione attesa del payload di registrazione.
 * username - Buffer in cui memorizzare il nome utente (in caso di auto-login).
 * is_logged - Puntatore alla variabile di stato della sessione.
 *
 * Ritorno: Nessuno.
 */
static void process_register(int client_sock, uint32_t payload_len, char* username, int* is_logged) {
    AuthPayload auth_data;
    DKTHeader response;

    if (payload_len != sizeof(AuthPayload)) {
        printf("[Thread %lu] Registrazione fallita: dimensione payload errata.\n", pthread_self());
        response.type = RESP_ERROR;
        response.payload_length = htonl(0);
        send_all(client_sock, &response, sizeof(DKTHeader), 0);
        return;
    }

    if (recv_all(client_sock, &auth_data, payload_len, 0) <= 0) {
        perror("[Thread %lu] Errore recv_all in process_register");
        return;
    }

    printf("[Thread %lu] Richiesta di registrazione per: '%s'\n", pthread_self(), auth_data.username);

    if (register_user(auth_data.username, auth_data.password)) {
        response.type = RESP_SUCCESS;
        
        *is_logged = 1;
        strncpy(username, auth_data.username, MAX_USERNAME - 1);
        username[MAX_USERNAME - 1] = '\0';
        
        printf("[Thread %lu] Registrazione COMPLETATA. Utente '%s' auto-loggato.\n", pthread_self(), username);
    } else {
        response.type = RESP_ERROR;
        *is_logged = 0;
        printf("[Thread %lu] Registrazione FALLITA (Utente già esistente o cache piena).\n", pthread_self());
    }

    response.payload_length = htonl(0);
    send_all(client_sock, &response, sizeof(DKTHeader), 0);
}

/*
 * Descrizione: Invalida la sessione del client corrente e resetta i parametri
 * del thread worker. Restituisce il risultato tramite REQ_LOGOUT.
 *
 * Parametri:
 * client_sock - File descriptor del socket.
 * user - Buffer contenente il nome utente attualmente loggato.
 * is_logged - Puntatore alla variabile di stato della sessione da azzerare.
 *
 * Ritorno: Nessuno.
 */
static void process_logout(int client_sock, char* user, int* is_logged) {
    DKTHeader response;
    response.payload_length = htonl(0);

    if (*is_logged) {
        printf("[Thread %lu] L'utente '%s' ha effettuato il logout.\n", pthread_self(), user);
        
        *is_logged = 0;
        memset(user, 0, MAX_USERNAME);
        
        response.type = RESP_SUCCESS;
    } else {
        printf("[Thread %lu] Richiesta di logout da utente non loggato.\n", pthread_self());
        response.type = RESP_ERROR;
    }

    send_all(client_sock, &response, sizeof(DKTHeader), 0);
}

/*
 * Descrizione: Riceve un nuovo messaggio dal client e lo inoltra al modulo 
 * di persistenza. Richiede che la sessione sia attiva (is_logged == 1).
 * Il mittente non viene letto dal payload per sicurezza, ma iniettato dalla sessione.
 *
 * Parametri:
 * client_sock - File descriptor del socket.
 * payload_len - Dimensione attesa del MessagePayload.
 * username - Nome utente (sicuro, derivato dalla sessione) da usare come mittente.
 * is_logged - Stato attuale della sessione (1 o 0).
 *
 * Ritorno: Nessuno.
 */
static void process_post_msg(int client_sock, uint32_t payload_len, const char* username, int is_logged) {
    MessagePayload req_data;
    DKTHeader response;

    /* Verifica permessi: respinge richieste non autorizzate e ripulisce il socket dai dati pendenti */
    if (!is_logged) {
        printf("[Thread %lu] Errore Sicurezza: Utente anonimo tenta di scrivere in bacheca.\n", pthread_self());
        response.type = RESP_ERROR;
        response.payload_length = htonl(0);
        send_all(client_sock, &response, sizeof(DKTHeader), 0);
        recv_all(client_sock, &req_data, payload_len, 0); 
        return;
    }

    if (payload_len != sizeof(MessagePayload)) {
        printf("[Thread %lu] Errore: dimensione payload POST non valida.\n", pthread_self());
        response.type = RESP_ERROR;
        response.payload_length = htonl(0);
        send_all(client_sock, &response, sizeof(DKTHeader), 0);
        return;
    }

    if (recv_all(client_sock, &req_data, payload_len, 0) <= 0) {
        return;
    }

    int new_id = save_message(username, req_data.subject, req_data.body);

    if (new_id > 0) {
        printf("[Thread %lu] Messaggio salvato da '%s' con ID %d.\n", pthread_self(), username, new_id);
        response.type = RESP_SUCCESS;
    } else {
        printf("[Thread %lu] Errore: Bacheca piena. Impossibile salvare.\n", pthread_self());
        response.type = RESP_ERROR;
    }

    response.payload_length = htonl(0);
    send_all(client_sock, &response, sizeof(DKTHeader), 0);
}

/*
 * Descrizione: Gestisce la richiesta di lettura bacheca (REQ_READ_MSG).
 * Verifica i permessi di sessione, estrae i messaggi dal modulo di persistenza
 * e li trasmette al client formattati in Network Byte Order.
 *
 * Parametri:
 * client_sock - File descriptor del socket.
 * payload_len - Deve essere 0 (questa richiesta non ammette body).
 * is_logged - Stato attuale della sessione.
 *
 * Ritorno: Nessuno.
 */
static void process_read_msg(int client_sock, uint32_t payload_len, int is_logged) {
    DKTHeader response;

    /* Se arriva un payload non previsto, esegue un flush del socket per non corrompere le letture future */
    if (payload_len != 0) {
        printf("[Thread %lu] Errore: REQ_READ_MSG non ammette payload.\n", pthread_self());
        
        char trash[1024];
        uint32_t to_read = payload_len > 1024 ? 1024 : payload_len;
        recv_all(client_sock, trash, to_read, 0); 
        
        response.type = RESP_ERROR;
        response.payload_length = htonl(0);
        send_all(client_sock, &response, sizeof(DKTHeader), 0);
        return;
    }

    if (!is_logged) {
        response.type = RESP_ERROR;
        response.payload_length = htonl(0);
        send_all(client_sock, &response, sizeof(DKTHeader), 0);
        return;
    }

    MessageRecord buffer[MAX_MESSAGGES]; 
    int count = get_messages(buffer, MAX_MESSAGGES);

    /* Invia per primo l'header indicando il numero totale di pacchetti successivi */
    response.type = RESP_SUCCESS;
    response.payload_length = htonl((uint32_t)count);
    
    if (send_all(client_sock, &response, sizeof(DKTHeader), 0) < 0) return;

    printf("[Thread %lu] Invio di %d messaggi al client.\n", pthread_self(), count);

    ResponsePayload net_msg;
    for (int i = 0; i < count; i++) {
        net_msg.message_id = htonl(buffer[i].id);
        strncpy(net_msg.sender, buffer[i].sender, MAX_USERNAME);
        strncpy(net_msg.subject, buffer[i].subject, MAX_SUBJECT);
        strncpy(net_msg.body, buffer[i].body, MAX_BODY);
        
        if (send_all(client_sock, &net_msg, sizeof(ResponsePayload), 0) < 0) break;
    }
}

/*
 * Descrizione: Gestisce l'eliminazione di un messaggio (REQ_DELETE_MSG).
 * Riceve l'ID in formato di rete, lo converte e delega l'eliminazione al modulo
 * di persistenza che provvederà al controllo sull'autorizzazione del mittente.
 *
 * Parametri:
 * client_sock - File descriptor del socket.
 * payload_len - Dimensione attesa del DeletePayload.
 * username - Nome dell'utente connesso, per verificarne la proprietà.
 * is_logged - Stato della sessione.
 *
 * Ritorno: Nessuno.
 */
static void process_delete_msg(int client_sock, uint32_t payload_len, const char* username, int is_logged) {
    DeletePayload req_data;
    DKTHeader response;

    if (!is_logged) {
        response.type = RESP_ERROR;
        response.payload_length = htonl(0);
        send_all(client_sock, &response, sizeof(DKTHeader), 0);
        recv_all(client_sock, &req_data, payload_len, 0);
        return;
    }

    if (payload_len != sizeof(DeletePayload)) {
        response.type = RESP_ERROR;
        response.payload_length = htonl(0);
        send_all(client_sock, &response, sizeof(DKTHeader), 0);
        return;
    }

    if (recv_all(client_sock, &req_data, payload_len, 0) <= 0) return;
    
    uint32_t target_id = ntohl(req_data.message_id);

    printf("[Thread %lu] L'utente '%s' richiede eliminazione msg ID %u.\n", pthread_self(), username, target_id);

    if (delete_message(target_id, username)) {
        printf("[Thread %lu] Messaggio %u eliminato con successo.\n", pthread_self(), target_id);
        response.type = RESP_SUCCESS;
    } else {
        printf("[Thread %lu] Eliminazione negata per msg %u (Non trovato o non autorizzato).\n", pthread_self(), target_id);
        response.type = RESP_ERROR;
    }

    response.payload_length = htonl(0);
    send_all(client_sock, &response, sizeof(DKTHeader), 0);
}

// ============================================
//  INTERFACCIA PUBBLICA
// ===========================================

/*
 * Descrizione: Loop principale per la gestione del ciclo di vita di un client
 * all'interno di un worker thread. Si occupa del routing delle richieste verso
 * le funzioni specifiche analizzando il type dell'header DKTHeader in arrivo.
 *
 * Parametri:
 * client_sock - File descriptor del socket TCP appena accettato dalla pool.
 *
 * Ritorno:
 * void - Termina l'esecuzione quando il client chiude la connessione o in
 * caso di errore fatale del socket, chiudendo il file descriptor.
 */
void handle_client(int client_sock) {
    DKTHeader header;
    ssize_t bytes_read;

    /* Variabili di stato (Sessione volatile associata al thread) */
    int is_logged = 0;
    char user[MAX_USERNAME] = "";

    printf("[Thread %lu] Inizio sessione con il socket %d\n", pthread_self(), client_sock);

    while (1) {
        bytes_read = recv_all(client_sock, &header, sizeof(DKTHeader), 0);
        
        if (bytes_read == 0) {
            printf("[Thread %lu] Il client ha chiuso la connessione (Socket %d).\n", pthread_self(), client_sock);
            break; 
        } else if (bytes_read < 0) {
            perror("[Thread] Errore fatale o timeout durante la ricezione dell'header");
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
                break; 
                
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
                break;
        }
    }

    close(client_sock);
    printf("[Thread %lu] Socket %d chiuso, sessione terminata.\n", pthread_self(), client_sock);
}
