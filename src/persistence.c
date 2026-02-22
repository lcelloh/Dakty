#include "../include/persistence.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DB_USER_FILE "data/utenti.txt"
#define DB_MSG_FILE "data/messaggi.txt"
#define WRITE_QUEUE_SIZE 50

//  STATO: UTENTI
static UserRecord user_cache[MAX_USERS];
static int user_count = 0;
static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

static UserRecord user_queue[WRITE_QUEUE_SIZE];
static int uq_head = 0, uq_tail = 0, uq_count = 0;
static pthread_mutex_t uq_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t uq_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_t user_io_thread;

//  STATO: MESSAGGI
static MessageRecord message_cache[MAX_MESSAGGES]; 
static int message_count = 0;
static uint32_t next_message_id = 1;
static pthread_mutex_t msg_mutex = PTHREAD_MUTEX_INITIALIZER;

static int msg_needs_flush = 0;
static pthread_mutex_t mq_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mq_flush_needed = PTHREAD_COND_INITIALIZER;
static pthread_t msg_io_thread;

static int persistence_running = 1;

//  WORKER THREADS

/*
 * Descrizione: Thread worker in background dedicato al salvataggio incrementale
 * degli utenti. Estrae i record dalla coda circolare e li appende al file su
 * disco.
 *
 * Parametri:
 * arg - Puntatore generico (non utilizzato, richiesto dalla firma di pthreads).
 *
 * Ritorno:
 * void* - Restituisce sempre NULL alla terminazione del thread.
 */
static void *user_file_thr(void *arg) {
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

    FILE *file = fopen(DB_USER_FILE, "a");
    if (file) {
      fprintf(file, "%s %s\n", new_user.username, new_user.password);
      fclose(file);
    }
  }
  return NULL;
}

/*
 * Descrizione: Thread worker in background per la persistenza dei messaggi.
 * Utilizza un approccio "Dump/Truncate": ad ogni segnalazione di modifica,
 * esegue una copia locale della cache e sovrascrive interamente il file su
 * disco per mantenere la sincronia in caso di eliminazioni.
 *
 * Parametri:
 * arg - Puntatore generico (non utilizzato).
 *
 * Ritorno:
 * void* - Restituisce sempre NULL alla terminazione del thread.
 */
static void *message_file_thr(void *arg) {
  while (1) {
    pthread_mutex_lock(&mq_mutex);
    while (!msg_needs_flush && persistence_running) {
      pthread_cond_wait(&mq_flush_needed, &mq_mutex);
    }

    if (!persistence_running && !msg_needs_flush) {
      pthread_mutex_unlock(&mq_mutex);
      break;
    }

    msg_needs_flush = 0;
    pthread_mutex_unlock(&mq_mutex);

    /* Copia della cache in un buffer locale per minimizzare il tempo di lock */
    pthread_mutex_lock(&msg_mutex);
    int local_count = message_count;
    MessageRecord *local_dump = malloc(local_count * sizeof(MessageRecord));
    if (local_dump) {
      memcpy(local_dump, message_cache, local_count * sizeof(MessageRecord));
    }
    pthread_mutex_unlock(&msg_mutex);

    if (local_dump) {
      FILE *file = fopen(DB_MSG_FILE, "w");
      if (file) {
        for (int i = 0; i < local_count; i++) {
          fprintf(file, "%u|%s|%s|%s\n", local_dump[i].id, local_dump[i].sender,
                  local_dump[i].subject, local_dump[i].body);
        }
        fclose(file);
      }
      free(local_dump);
    }
  }
  return NULL;
}

/*
 * Descrizione: Funzione helper che segnala al thread di persistenza dei
 * messaggi la necessità di effettuare un nuovo dump su disco.
 *
 * Parametri: Nessuno.
 *
 * Ritorno: Nessuno.
 */
static void trigger_message_flush() {
  pthread_mutex_lock(&mq_mutex);
  msg_needs_flush = 1;
  pthread_cond_signal(&mq_flush_needed);
  pthread_mutex_unlock(&mq_mutex);
}

// ==========================================
// PERSISTENCE API (PUBBLICHE)
// ==========================================

/*
 * Descrizione: Inizializza il modulo di persistenza. Carica in RAM gli utenti
 * e i messaggi dai rispettivi file, allinea il contatore degli ID
 * autoincrementanti e avvia i thread worker asincroni per le operazioni di I/O.
 *
 * Parametri: Nessuno.
 *
 * Ritorno: Nessuno.
 */
void persistence_init() {
  FILE *f_users = fopen(DB_USER_FILE, "r");
  if (f_users) {
    char u[MAX_USERNAME], p[MAX_PASSWORD];
    while (fscanf(f_users, "%31s %31s", u, p) == 2 && user_count < MAX_USERS) {
      strncpy(user_cache[user_count].username, u, MAX_USERNAME);
      strncpy(user_cache[user_count].password, p, MAX_PASSWORD);
      user_count++;
    }
    fclose(f_users);
  }

  FILE *f_msgs = fopen(DB_MSG_FILE, "r");
  uint32_t max_id = 0;
  if (f_msgs) {
    char line[1024];
    while (fgets(line, sizeof(line), f_msgs) != NULL &&
           message_count < MAX_MESSAGGES) {
      line[strcspn(line, "\n")] = '\0';

      char *id_str = strtok(line, "|");
      char *sender = strtok(NULL, "|");
      char *subject = strtok(NULL, "|");
      char *body = strtok(NULL, "|");

      if (id_str && sender && subject && body) {
        uint32_t id = (uint32_t)atoi(id_str);

        message_cache[message_count].id = id;
        strncpy(message_cache[message_count].sender, sender, MAX_USERNAME - 1);
        strncpy(message_cache[message_count].subject, subject, MAX_SUBJECT - 1);
        strncpy(message_cache[message_count].body, body, MAX_BODY - 1);
        message_count++;

        if (id > max_id)
          max_id = id;
      }
    }
    fclose(f_msgs);
  }

  next_message_id = max_id + 1;

  printf("[Sistema] Persistenza: %d Utenti, %d Messaggi (Next ID: %u).\n",
         user_count, message_count, next_message_id);

  pthread_create(&user_io_thread, NULL, user_file_thr, NULL);
  pthread_create(&msg_io_thread, NULL, message_file_thr, NULL);
}

/*
 * Descrizione: Segnala ai thread worker di I/O l'avvio della procedura di
 * spegnimento del server e attende in modo bloccante la loro terminazione
 * tramite pthread_join per garantire il salvataggio dei dati pendenti.
 *
 * Parametri: Nessuno.
 *
 * Ritorno: Nessuno.
 */
void persistence_shutdown() {
  pthread_mutex_lock(&uq_mutex);
  pthread_mutex_lock(&mq_mutex);
  persistence_running = 0;
  pthread_cond_signal(&uq_not_empty);
  pthread_cond_signal(&mq_flush_needed);
  pthread_mutex_unlock(&mq_mutex);
  pthread_mutex_unlock(&uq_mutex);

  pthread_join(user_io_thread, NULL);
  pthread_join(msg_io_thread, NULL);

  printf("[Sistema] Modulo Persistenza chiuso correttamente.\n");
}

/*
 * Descrizione: Verifica se le credenziali fornite corrispondono a un
 * record presente nella cache degli utenti.
 *
 * Parametri:
 * username - Stringa contenente il nome utente.
 * password - Stringa contenente la password.
 *
 * Ritorno:
 * int - 1 se l'autenticazione ha successo, 0 in caso contrario.
 */
int authenticate_user(const char *username, const char *password) {
  int success = 0;

  pthread_mutex_lock(&cache_mutex);
  for (int i = 0; i < user_count; i++) {
    if (strcmp(user_cache[i].username, username) == 0 &&
        strcmp(user_cache[i].password, password) == 0) {
      success = 1;
      break;
    }
  }
  pthread_mutex_unlock(&cache_mutex);

  return success;
}

/*
 * Descrizione: Registra un nuovo utente nel sistema. Verifica l'assenza
 * di duplicati, aggiorna la cache in RAM e inserisce il record nella coda
 * di scrittura asincrona su file.
 *
 * Parametri:
 * username - Stringa contenente il nome utente scelto.
 * password - Stringa contenente la password associata.
 *
 * Ritorno:
 * int - 1 se la registrazione ha successo, 0 se l'utente esiste già o la cache
 * è piena.
 */
int register_user(const char *username, const char *password) {
  pthread_mutex_lock(&cache_mutex);

  if (user_count >= MAX_USERS) {
    pthread_mutex_unlock(&cache_mutex);
    return 0;
  }

  for (int i = 0; i < user_count; i++) {
    if (strcmp(user_cache[i].username, username) == 0) {
      pthread_mutex_unlock(&cache_mutex);
      return 0;
    }
  }

  strncpy(user_cache[user_count].username, username, MAX_USERNAME);
  strncpy(user_cache[user_count].password, password, MAX_PASSWORD);
  user_count++;
  pthread_mutex_unlock(&cache_mutex);

  pthread_mutex_lock(&uq_mutex);
  if (uq_count < WRITE_QUEUE_SIZE) {
    strncpy(user_queue[uq_tail].username, username, MAX_USERNAME);
    strncpy(user_queue[uq_tail].password, password, MAX_PASSWORD);
    uq_tail = (uq_tail + 1) % WRITE_QUEUE_SIZE;
    uq_count++;
    pthread_cond_signal(&uq_not_empty);
  } else {
    printf("[Errore] Coda di scrittura disco piena! Salvataggio perso.\n");
  }
  pthread_mutex_unlock(&uq_mutex);

  return 1;
}

/*
 * Descrizione: Salva un nuovo messaggio in bacheca. Genera un ID univoco,
 * aggiorna la cache e segnala al thread worker dei messaggi di eseguire un
 * flush.
 *
 * Parametri:
 * username - Nome utente dell'autore (derivato dalla sessione del socket).
 * subject - Oggetto del messaggio.
 * body - Corpo principale del messaggio.
 *
 * Ritorno:
 * int - L'ID univoco generato per il messaggio in caso di successo, -1 in caso
 * di errore (cache piena).
 */
int save_message(const char *username, const char *subject, const char *body) {
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
    trigger_message_flush();
  }
  return new_id > 0 ? (int)new_id : -1;
}

/*
 * Descrizione: Recupera i messaggi presenti in bacheca, copiandoli dalla cache
 * a un buffer fornito dal chiamante, in modo thread-safe.
 *
 * Parametri:
 * buffer - Puntatore all'array di MessageRecord pre-allocato dal chiamante.
 * num - Capacità massima del buffer.
 *
 * Ritorno:
 * int - Il numero effettivo di messaggi copiati nel buffer.
 */
int get_messages(MessageRecord *buffer, int num) {
  int count = 0;

  pthread_mutex_lock(&msg_mutex);
  count = (num < message_count) ? num : message_count;

  for (int i = 0; i < count; i++) {
    buffer[i] = message_cache[i];
  }

  pthread_mutex_unlock(&msg_mutex);

  return count;
}

/*
 * Descrizione: Rimuove un messaggio dalla bacheca, traslando l'array in
 * memoria. Verifica preventivamente l'autorizzazione confrontando il nome
 * utente del chiamante con quello del mittente del messaggio. Attiva il flush
 * su disco in caso di successo.
 *
 * Parametri:
 * id - ID del messaggio da eliminare.
 * username - Nome utente di chi ha richiesto l'eliminazione.
 *
 * Ritorno:
 * int - 1 se il messaggio è stato eliminato, 0 se non è stato trovato o
 * l'utente non è autorizzato.
 */
int delete_message(uint32_t id, const char *username) {
  int found = 0;
  pthread_mutex_lock(&msg_mutex);
  for (int i = 0; i < message_count; i++) {
    if (message_cache[i].id == id &&
        strcmp(message_cache[i].sender, username) == 0) {
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
    trigger_message_flush();
  }
  return found;
}
