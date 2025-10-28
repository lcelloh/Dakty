#include "utils.h"

void P_EXT(char* msg){
	perror(msg);
	exit(EXIT_FAILURE);
}
