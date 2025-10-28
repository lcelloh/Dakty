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

#include "../Common/utils.h"
#include "clt_thr.h"

const int BACKLOG = 10;
const int PORTNUM = 1234;

// FUNCTIONS

int main(int argc, char** argv){
	
	int skt_fd;
	struct addrinfo hints;
	struct addrinfo* s_info;

	memset(&hints, 0, sizeof hints);   
	hints.ai_family = AF_UNSPEC; 
	hints.ai_socktype = SOCK_STREAM; 
	hints.ai_flags = AI_PASSIVE; 

	char sPort[16];
	sprintf(sPort, "%d", PORTNUM);
	
	if(getaddrinfo(NULL, sPort, &hints, &s_info) != 0)	
	{
		P_EXT("getaddrinfo error\n");
	}	
	
	if ((skt_fd = socket(s_info->ai_family, 
		s_info->ai_socktype, s_info->ai_protocol)) == -1)
	{
		P_EXT("socket creation error\n");
	}
    
	if(bind(skt_fd, s_info->ai_addr, s_info->ai_addrlen) == -1)
	{
		P_EXT("bind error\n");
	}

	if(listen(skt_fd, BACKLOG) == -1)
	{
		P_EXT("listen error\n");
	}

	printf("listening for connections\n");

	while(1){

		int clt_fd;
		struct sockaddr_storage clt_as;
		int add_size = sizeof(clt_as);

		if((clt_fd = accept(skt_fd, (struct sockaddr *)&clt_as, &add_size)) == -1)
		{
			P_EXT("accept error\n");
		}

		pthread_t tid;  // creo nuovo thread e passo il file descriptor
		long* cfd_p;
		if((cfd_p = (long*)malloc(sizeof(long))) == NULL)
		{
			P_EXT("malloc cfd_p error\n");
		}

		*cfd_p = clt_fd;
		pthread_create(&tid, NULL, clt_thr, (void*)cfd_p);
	}

	return 0;
}
