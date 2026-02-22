#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "../include/threadpool.h"
#include "../include/server_handler.h"
#include "../include/persistence.h"

#define PORT 8080

/* Stato globale del server */
int server_fd;
ThreadPool pool;
volatile sig_atomic_t keep_running = 1;

/* Strutture per tracciare i socket attivi e forzarne la chiusura (Graceful Shutdown) */
int active_sockets[THREAD_POOL_SIZE];
pthread_mutex_t active_sockets_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Handler asincrono per l'intercettazione dei segnali di terminazione (SIGINT, SIGTERM).
 * Si limita a impostare il flag atomico 'keep_running' a 0, demandando al main
 * la responsabilità di gestire la vera procedura di pulizia.
 */
void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        keep_running = 0; 
    }
}

/*
 * Funzione di esecuzione (entry-point) per ogni Worker Thread generato dal pool.
 * Cicla all'infinito estraendo i socket dalla coda tramite pool_fetch.
 * Registra il socket attivo per permettere lo spegnimento forzato, delega
 * l'interazione al livello applicativo (handle_client) e poi ripulisce lo stato.
 */
void* worker_thread(void* arg) {
    int worker_id = *(int*)arg;
    free(arg);

    while(1) {
        /* Chiamata bloccante: il thread dorme finché non c'è un client o un ordine di spegnimento */
        int client_sock = pool_fetch(&pool); 
        
        if (client_sock == -1) {
            break; /* Il pool è in shutdown, il thread può terminare */
        }

        pthread_mutex_lock(&active_sockets_mutex);
        active_sockets[worker_id] = client_sock;
        pthread_mutex_unlock(&active_sockets_mutex);

        printf("Thread %lu gestisce il socket %d\n", pthread_self(), client_sock);
        
        /* Delega al modulo server_handler.c per la logica del protocollo */
        handle_client(client_sock);

        pthread_mutex_lock(&active_sockets_mutex);
        active_sockets[worker_id] = -1;
        pthread_mutex_unlock(&active_sockets_mutex);
    }

    printf("Thread %lu in chiusura.\n", pthread_self());
    return NULL;
}

int main() {
    int client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(client_addr);

    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        active_sockets[i] = -1;
    }

    /* SETUP SEGNALI ) */
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* SA_RESTART a 0 assicura che l'accept venga interrotta dall'EINTR */
    
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Errore: fallita l'impostazione di SIGINT");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("Errore: fallita l'impostazione di SIGTERM");
        exit(EXIT_FAILURE);
    }
    
    struct sigaction sa_pipe;
    sa_pipe.sa_handler = SIG_IGN; 
    sigemptyset(&sa_pipe.sa_mask);
    sa_pipe.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa_pipe, NULL) == -1) {
        perror("Errore: fallita l'impostazione di SIGPIPE");
        exit(EXIT_FAILURE);
    }   
    /* INIZIALIZZAZIONE MODULI (Persistenza e Concorrenza) */
    printf("Init strutture necessarie.\n");
    persistence_init(); /* Carica file da disco e avvia i thread I/O */
    pool_init(&pool);   /* Inizializza mutex e condition variables per la coda */

    /* Avvio dei Worker Thread */
    pthread_t threads[THREAD_POOL_SIZE];
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        int* id = malloc(sizeof(int));
        *id = i;
        pthread_create(&threads[i], NULL, worker_thread, id);
    }

    /*SETUP RETE (Socket, Bind, Listen) */
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Errore: fallita la creazione del socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Errore: fallita la bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, QUEUE_SIZE) < 0) {
        perror("Errore: fallita la listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server Dakty in ascolto sulla porta %d...\n", PORT);
    printf("Premi Ctrl+C per spegnere il server in modo pulito.\n");

    /* MAIN LOOP (Accettazione client e sottomissione al pool) */
    while (keep_running) {
        client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &addr_size);
        
        if (client_sock < 0) {
            /* Se il segnale SIGINT interrompe l'attesa di accept, usciamo pulitamente */
            if (errno == EINTR) {
                printf("\nSegnale ricevuto. Uscita dal ciclo di accept...\n");
                break; 
            }
            perror("Errore: fallita la accept");
            continue;
        }
        
        /* Consegna il socket al pool_submit, che risveglierà un worker thread in ascolto */
        pool_submit(&pool, client_sock);
    }

    /* 5. GRACEFUL SHUTDOWN*/
    printf("Avvio procedura di spegnimento di Dakty...\n");
    close(server_fd); 
    
    /* Forza lo sblocco di tutti i worker thread bloccati su un'operazione di rete */
    pthread_mutex_lock(&active_sockets_mutex);
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        if (active_sockets[i] != -1) {
            shutdown(active_sockets[i], SHUT_RDWR); 
        }
    }
    pthread_mutex_unlock(&active_sockets_mutex);

    pool_shutdown(&pool); 
    
    persistence_shutdown();

    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_join(threads[i], NULL); 
    }

    printf("Server chiuso correttamente. Arrivederci!\n");
    return 0;
}
