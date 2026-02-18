#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include "protocol.h" // Per le dimensioni MAX_USERNAME e MAX_PASSWORD
#include <stdint.h>

#define MAX_USERS 100 // Limite massimo di utenti in RAM
#define MAX_MESSAGGES 500 // Limite massimo messaggi in RAM

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

void persistence_init();

void persistence_shutdown();

int authenticate_user(const char* username, const char* password);

int register_user(const char* username, const char* password);

int save_message(const char* username, const char* subject, const char* body);

int get_messages(MessageRecord* buffer, int num);

int delete_message(uint32_t id, const char* username);

#endif
