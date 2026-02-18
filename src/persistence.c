#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "../include/persistence.h"

#define DB_USER_FILE "data/utenti.txt"
#define DB_MSG_FILE "data/messaggi.txt" // Nuovo file per i messaggi
#define WRITE_QUEUE_SIZE 50

// ==========================================
//  1. STATO: UTENTI
// ==========================================
static UserRecord user_cache[MAX_USERS];
static int user_count = 0;
static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

static UserRecord user_queue[WRITE_QUEUE_SIZE];
static int uq_head = 0, uq_tail = 0, uq_count = 0;
static pthread_mutex_t uq_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t uq_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_t user_io_thread;

// ==========================================
//  2. STATO: MESSAGGI
// ==========================================
static MessageRecord message_cache[MAX_MESSAGGES];
static int message_count = 0;
static uint32_t next_message_id = 1;
static pthread_mutex_t msg_mutex = PTHREAD_MUTEX_INITIALIZER;

// Invece di salvare solo le aggiunte, per il nostro progetto OS 
// usiamo un comando "FLUSH" per i messaggi. Quando la bacheca cambia (aggiunta o rimozione),
// diciamo al thread di riscrivere il file per mantenere la sincronia.
static int msg_needs_flush = 0; 
static pthread_mutex_t mq_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mq_flush_needed = PTHREAD_COND_INITIALIZER;
static pthread_t msg_io_thread;

static int persistence_running = 1;
// ==========================================

static void* io_user_worker(void* arg) {
    while (1) {
        pthread_mutex_lock(&uq_mutex);
        while (uq_count == 0 && persistence_running) {
            pthread_cond_wait(&uq_not_empty, &uq_mutex);
        }
        
        if (!persistence_running && uq_count == 0) {
            pthread_mutex_unlock(&uq_mutex);
            break;
        }
        
        UserRecord new_user = user_queue[uq_head];
        uq_head = (uq_head + 1) % WRITE_QUEUE_SIZE;
        uq_count--;
        pthread_mutex_unlock(&uq_mutex);
        
        FILE* file = fopen(DB_USER_FILE, "a");
        if (file) {
            fprintf(file, "%s %s\n", new_user.username, new_user.password);
            fclose(file);
        }
    }
    return NULL;
}

// Thread Messaggi (Lavora in modalità "Dump/Truncate")
// Riscrive tutto l'array sul disco quando c'è una modifica (aggiunta o cancellazione)
static void* io_message_worker(void* arg) {
    while (1) {
        pthread_mutex_lock(&mq_mutex);
        while (!msg_needs_flush && persistence_running) {
            pthread_cond_wait(&mq_flush_needed, &mq_mutex);
        }
        
        if (!persistence_running && !msg_needs_flush) {
            pthread_mutex_unlock(&mq_mutex);
            break;
        }
        
        msg_needs_flush = 0; // Reset del flag
        pthread_mutex_unlock(&mq_mutex);

        // Blocca la cache il minimo indispensabile per copiare i dati in un buffer locale sicuro
        // per poi scriverli sul disco senza bloccare i lettori per molto tempo.
        pthread_mutex_lock(&msg_mutex);
        int local_count = message_count;
        // Allocazione dinamica o statica locale per fare il dump
        MessageRecord* local_dump = malloc(local_count * sizeof(MessageRecord));
        if (local_dump) {
            memcpy(local_dump, message_cache, local_count * sizeof(MessageRecord));
        }
        pthread_mutex_unlock(&msg_mutex);

        if (local_dump) {
            FILE* file = fopen(DB_MSG_FILE, "w"); // "w" svuota il file prima di scriverlo
            if (file) {
                // Scriviamo il formato: ID|Autore|Oggetto|Testo
                // Usiamo un delimitatore raro (es. |) perché oggetto e testo contengono spazi!
                for (int i = 0; i < local_count; i++) {
                    fprintf(file, "%u|%s|%s|%s\n", 
                            local_dump[i].id, 
                            local_dump[i].sender, 
                            local_dump[i].subject, 
                            local_dump[i].body);
                }
                fclose(file);
            }
            free(local_dump);
        }
    }
    return NULL;
}

// Funzione helper interna per dire al thread messaggi di salvare
static void trigger_message_flush() {
    pthread_mutex_lock(&mq_mutex);
    msg_needs_flush = 1;
    pthread_cond_signal(&mq_flush_needed);
    pthread_mutex_unlock(&mq_mutex);
}

// ==========================================
// PERSISTENCE API
// ==========================================
void persistence_init() {
    FILE* f_users = fopen(DB_USER_FILE, "r");
    if (f_users) {
        char u[MAX_USERNAME], p[MAX_PASSWORD];
        while (fscanf(f_users, "%31s %31s", u, p) == 2 && user_count < MAX_USERS) {
            strncpy(user_cache[user_count].username, u, MAX_USERNAME);
            strncpy(user_cache[user_count].password, p, MAX_PASSWORD);
            user_count++;
        }
        fclose(f_users);
    }
    
    // CARICAMENTO MESSAGGI E SINCRONIZZAZIONE ID
    FILE* f_msgs = fopen(DB_MSG_FILE, "r");
    uint32_t max_id = 0;
    if (f_msgs) {
        char line[1024]; // Buffer capiente per leggere una riga intera
        while (fgets(line, sizeof(line), f_msgs) != NULL && message_count < MAX_MESSAGGES) {
            // Rimuoviamo il newline
            line[strcspn(line, "\n")] = '\0';
            
            // Usiamo strtok per separare i campi dal delimitatore '|'
            char* id_str = strtok(line, "|");
            char* sender = strtok(NULL, "|");
            char* subject = strtok(NULL, "|");
            char* body = strtok(NULL, "|");
            
            if (id_str && sender && subject && body) {
                uint32_t id = (uint32_t)atoi(id_str);
                
                message_cache[message_count].id = id;
                strncpy(message_cache[message_count].sender, sender, MAX_USERNAME - 1);
                strncpy(message_cache[message_count].subject, subject, MAX_SUBJECT - 1);
                strncpy(message_cache[message_count].body, body, MAX_BODY - 1);
                message_count++;
                
                if (id > max_id) max_id = id;
            }
        }
        fclose(f_msgs);
    }
    
    next_message_id = max_id + 1; // Allineamento dell'Auto-Increment!
    
    printf("[Sistema] Persistenza: %d Utenti, %d Messaggi (Next ID: %u).\n", user_count, message_count, next_message_id);
    
    // Avvio dei due thread gemelli
    pthread_create(&user_io_thread, NULL, io_user_worker, NULL);
    pthread_create(&msg_io_thread, NULL, io_message_worker, NULL);
}

void persistence_shutdown() {
    // Segnale di stop globale
    pthread_mutex_lock(&uq_mutex);
    pthread_mutex_lock(&mq_mutex);
    persistence_running = 0;
    pthread_cond_signal(&uq_not_empty);
    pthread_cond_signal(&mq_flush_needed);
    pthread_mutex_unlock(&mq_mutex);
    pthread_mutex_unlock(&uq_mutex);
    
    // Attendiamo la fine di entrambi i thread
    pthread_join(user_io_thread, NULL);
    pthread_join(msg_io_thread, NULL);
    
    printf("[Sistema] Modulo Persistenza chiuso correttamente.\n");
}

int authenticate_user(const char* username, const char* password) {
    int success = 0;
    
    pthread_mutex_lock(&cache_mutex); // Blocca la lettura per evitare sovrapposizioni
    for (int i = 0; i < user_count; i++) {
        if (strcmp(user_cache[i].username, username) == 0 && 
            strcmp(user_cache[i].password, password) == 0) {
            success = 1;
            break; // Utente trovato
        }
    }
    pthread_mutex_unlock(&cache_mutex);
    
    return success;
}

int register_user(const char* username, const char* password) {
    pthread_mutex_lock(&cache_mutex);
    
    // Controllo 1: La cache è piena?
    if (user_count >= MAX_USERS) {
        pthread_mutex_unlock(&cache_mutex);
        return 0; 
    }
    
    // Controllo 2: L'utente esiste già?
    for (int i = 0; i < user_count; i++) {
        if (strcmp(user_cache[i].username, username) == 0) {
            pthread_mutex_unlock(&cache_mutex);
            return 0; // Nome utente non disponibile
        }
    }
    
    // Inserimento nella Cache Veloce
    strncpy(user_cache[user_count].username, username, MAX_USERNAME);
    strncpy(user_cache[user_count].password, password, MAX_PASSWORD);
    user_count++;
    pthread_mutex_unlock(&cache_mutex);
    
    // Sottomissione asincrona al Thread di I/O (Produttore)
    pthread_mutex_lock(&uq_mutex);
    if (uq_count < WRITE_QUEUE_SIZE) {
        strncpy(user_queue[uq_tail].username, username, MAX_USERNAME);
        strncpy(user_queue[uq_tail].password, password, MAX_PASSWORD);
        uq_tail = (uq_tail + 1) % WRITE_QUEUE_SIZE;
        uq_count++;
        pthread_cond_signal(&uq_not_empty); // Sveglia il Persister!
    } else {
        // Gestione limite coda (per un progetto OS possiamo semplicemente scartare o bloccarci, qui scartiamo per non bloccare il server)
        printf("[Errore] Coda di scrittura disco piena! Salvataggio perso.\n");
    }
    pthread_mutex_unlock(&uq_mutex);
    
    return 1;
}

int save_message(const char* username, const char* subject, const char* body) {
    uint32_t new_id = 0;
    pthread_mutex_lock(&msg_mutex);
    if (message_count < MAX_MESSAGGES) {
        new_id = next_message_id++;
        message_cache[message_count].id = new_id;
        strncpy(message_cache[message_count].sender, username, MAX_USERNAME - 1);
        strncpy(message_cache[message_count].subject, subject, MAX_SUBJECT - 1);
        strncpy(message_cache[message_count].body, body, MAX_BODY - 1);
        message_count++;
    }
    pthread_mutex_unlock(&msg_mutex);
    
    if (new_id > 0) {
        trigger_message_flush(); // Avvisa il thread I/O di riscrivere il file!
    }
    return new_id > 0 ? (int)new_id : -1;
}

int get_messages(MessageRecord* buffer, int num) {
    int count = 0;
    
    pthread_mutex_lock(&msg_mutex);
    
    // Copiamo fino a 'num' messaggi, ma non più di quanti ne abbiamo realmente
    count = (num < message_count) ? num : message_count;
    
    for (int i = 0; i < count; i++) {
        buffer[i] = message_cache[i]; // In C, assegnare una struct copia tutti i suoi campi!
    }
    
    pthread_mutex_unlock(&msg_mutex);
    
    return count;
}

int delete_message(uint32_t id, const char* username) {
    int found = 0;
    pthread_mutex_lock(&msg_mutex);
    for (int i = 0; i < message_count; i++) {
        if (message_cache[i].id == id && strcmp(message_cache[i].sender, username) == 0) {
            for (int j = i; j < message_count - 1; j++) {
                message_cache[j] = message_cache[j + 1];
            }
            message_count--;
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&msg_mutex);
    
    if (found) {
        trigger_message_flush(); // Avvisa il thread I/O di riscrivere il file (ora senza il messaggio eliminato)
    }
    return found;
}   
