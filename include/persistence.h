#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include "protocol.h" // Per le dimensioni MAX_USERNAME e MAX_PASSWORD
#include <stdint.h>

#define MAX_USERS 100 // Limite massimo di utenti in RAM
#define MAX_MESSAGGES 500 // Limite massimo messaggi in RAM

// La struttura che rappresenta l'utente nel database/cache
typedef struct {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
} UserRecord;

typedef struct {
    uint32_t id;
    char sender[MAX_USERNAME];
    char subject[MAX_SUBJECT];
    char body[MAX_BODY];
} MessageRecord;

// Inizializza la persistenza: carica gli utenti dal file alla RAM e avvia il thread di I/O
void persistence_init();

// Spegne il thread di persistenza in modo pulito
void persistence_shutdown();

// Verifica le credenziali nella Cache in RAM (Thread-safe)
// Ritorna 1 se corrette, 0 se errate o utente inesistente
int authenticate_user(const char* username, const char* password);

// Registra un nuovo utente: lo salva nella Cache e segnala al thread di scriverlo su disco
// Ritorna 1 se successo, 0 se l'utente esiste già o la cache è piena
int register_user(const char* username, const char* password);

// Restituisce l'ID generato, oppure -1 in caso di errore (bacheca piena)
int save_message(const char* username, const char* subject, const char* body);

// Copia fino a 'num' messaggi nel buffer. Restituisce il numero effettivo di messaggi copiati.
int get_messages(MessageRecord* buffer, int num);

// Restituisce 1 se eliminato con successo, 0 se ID non trovato o non autorizzato
int delete_message(uint32_t id, const char* username);

#endif
