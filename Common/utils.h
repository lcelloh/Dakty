#if !defined(UTIL_H)
#define UTIL_H

#define PORT_STR "1234"
#define PORT_NUM 1234
#define BUFF_SIZE 1024

// P_EXT
#define P_EXT(msg)          \ 
   do {                     \
        perror(msg);        \
        exit(EXIT_FAILURE); \
    } while (0)

int safe_send(int fd);
int safe_recv(int fd);

#endif 



