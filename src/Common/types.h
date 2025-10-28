#if !defined(TYPES_H)
#define TYPES_H

typedef struct Message
{
    int usr_id;
    char* text;
    // time
}Message;

enum State{
    LOGDOUT,
    LOGDIN
};

enum Action{
    ACCES,
    REGISTER,
    DISCONNECT,
    READ,
    WRITE,
    REMOVE,
    HELP,
    LOGOUT
};

#endif // TYPES_H
