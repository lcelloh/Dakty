// INCLUDES
#include <netinet/in.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <netdb.h>

#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 

#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#include "Common/util.h"

void* clt_thr(void* arg);