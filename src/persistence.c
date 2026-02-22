#include "../include/persistence.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>   // Per la gestione di EINTR

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
 * disco gestendo esplicitamente le interruzioni di sistema (EINTR).
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

    FILE *file;
    while ((file = fopen(DB_USER_FILE, "a")) == NULL) {
      if (errno == EINTR) continue;
      break;
    }

    if (file) {
      while (fprintf(file, "%s %s\n", new_user.username, new_user.password) < 0) {
        if (errno == EINTR) {
          clearerr(file); 
          continue;
        }
        perror("[Errore] Scrittura su file utenti fallita");
        break; 
      }
      
      while (fclose(file) != 0) {
        if (errno == EINTR) continue;
        perror("[Errore] Chiusura file utenti fallita");
        break;
      }
    } else {
      perror("[Errore] Impossibile aprire il file utenti per l'append");
    }
  }
  return NULL;
}

/*
 * Descrizione: Thread worker in background per la persistenza dei messaggi.
 * Utilizza la tecnica del file temporaneo e del rename atomico per garantire
 * l'integrità del database anche in caso di errori di scrittura.
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

    pthread_mutex_lock(&msg_mutex);
    int local_count = message_count;
    MessageRecord *local_dump = malloc(local_count * sizeof(MessageRecord));
    if (local_dump) {
      memcpy(local_dump, message_cache, local_count * sizeof(MessageRecord));
    }
    pthread_mutex_unlock(&msg_mutex);

    if (local_dump) {
      FILE *file;
      while ((file = fopen(DB_MSG_FILE ".tmp", "w")) == NULL) {
        if (errno == EINTR) continue;
        break;
      }

      if (file) {
          // Se avviene errore fatale
        int write_error = 0;
        
        for (int i = 0; i < local_count; i++) {
          while (fprintf(file, "%u|%s|%s|%s\n", local_dump[i].id, local_dump[i].sender,
                         local_dump[i].subject, local_dump[i].body) < 0) {
            if (errno == EINTR) {
              clearerr(file);
              continue;
            }
            write_error = 1; 
            break;
          }
          if (write_error) break; 
        }

        while (fclose(file) != 0) {
          if (errno == EINTR) continue;
          write_error = 1;
          break;
        }

        if (!write_error) {
          if (rename(DB_MSG_FILE ".tmp", DB_MSG_FILE) != 0) {
            perror("[Errore] Sostituzione del file messaggi fallita");
          }
        } else {
          perror("[Errore] Salvataggio fallito. Ripristino lo stato precedente.");
          remove(DB_MSG_FILE ".tmp"); 
        }
      } else {
        perror("[Errore] Impossibile creare il file temporaneo dei messaggi");
      }
      free(local_dump);
    }
  }
  return NULL;
}

static void trigger_message_flush() {
  pthread_mutex_lock(&mq_mutex);
  msg_needs_flush = 1;
  pthread_cond_signal(&mq_flush_needed);
  pthread_mutex_unlock(&mq_mutex);
}

// ==========================================
// PERSISTENCE API (PUBBLICHE)
// ==========================================

void persistence_init() {
  FILE *f_users;
  while ((f_users = fopen(DB_USER_FILE, "r")) == NULL) {
    if (errno == EINTR) continue;
    break;
  }
  
  if (f_users) {
    char u[MAX_USERNAME], p[MAX_PASSWORD];
    int ret;
    while (user_count < MAX_USERS) {
      /* Lettura utenti protetta da EINTR */
      ret = fscanf(f_users, "%31s %31s", u, p);
      if (ret == 2) {
        strncpy(user_cache[user_count].username, u, MAX_USERNAME);
        strncpy(user_cache[user_count].password, p, MAX_PASSWORD);
        user_count++;
      } else if (ret == EOF) {
        if (ferror(f_users) && errno == EINTR) {
          clearerr(f_users);
          continue; 
        }
        break;
      } else {
        break; 
      }
    }
    
    while (fclose(f_users) != 0) {
        if (errno == EINTR) continue;
        break;
    }
  }

  FILE *f_msgs;
  while ((f_msgs = fopen(DB_MSG_FILE, "r")) == NULL) {
    if (errno == EINTR) continue;
    break;
  }

  uint32_t max_id = 0;
  if (f_msgs) {
    char line[1024];
    while (message_count < MAX_MESSAGGES) {
      if (fgets(line, sizeof(line), f_msgs) != NULL) {
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

          if (id > max_id) max_id = id;
        }
      } else {
        if (ferror(f_msgs) && errno == EINTR) {
          clearerr(f_msgs);
          continue;
        }
        break; 
      }
    }
    
    while (fclose(f_msgs) != 0) {
        if (errno == EINTR) continue;
        break;
    }
  }

  next_message_id = max_id + 1;

  printf("[Sistema] Persistenza: %d Utenti, %d Messaggi (Next ID: %u).\n",
         user_count, message_count, next_message_id);

  pthread_create(&user_io_thread, NULL, user_file_thr, NULL);
  pthread_create(&msg_io_thread, NULL, message_file_thr, NULL);
}

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
