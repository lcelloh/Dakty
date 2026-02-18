#ifndef CLIENT_CONTROLLER_H
#define CLIENT_CONTROLLER_H

#include "protocol.h"

int dakty_connect(const char* server_ip, int port);

void dakty_disconnect(int sockfd);

int dakty_login(int sockfd, const char* username, const char* password);

int dakty_register(int sockfd, const char* username, const char* password);

int dakty_logout(int sockfd);

int dakty_post_message(int sockfd, const char* subject, const char* body);

int dakty_read_messages(int sockfd, ResponsePayload** messages_out);

int dakty_delete_message(int sockfd, int message_id);

#endif
