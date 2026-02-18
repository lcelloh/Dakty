#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>      // Per la gestione dei segnali
#include <errno.h>       // Per la gestione di EINTR
#include <arpa/inet.h>
#include <sys/socket.h>
#include "../include/threadpool.h"
#include "../include/server_handler.h"
#include "../include/persistence.h"

#define PORT 8080

int server_fd;
ThreadPool pool;
volatile sig_atomic_t keep_running = 1;

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        // Avvisa il main loop di terminare
        keep_running = 0; 
        close(server_fd);
    }
}


void* worker_thread(void* arg) {
    while(1) {
        int client_sock = pool_fetch(&pool); 
        // Se pool_fetch restituisce -1, significa che il server si sta spegnendo
        if (client_sock == -1) {
            break;
        }

        printf("Thread %lu gestisce il socket %d\n", pthread_self(), client_sock);
        handle_client(client_sock);
        close(client_sock); 
    }
    printf("Thread %lu in chiusura.\n", pthread_self());
    return NULL;
}

int main() {
    int client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(client_addr);

    // Registrazione del gestore di segnali tramite sigaction
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // Nessun flag speciale, non vogliamo riavviare le system call interrotte (SA_RESTART)
    
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Errore: fallita l'impostazione di SIGINT");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("Errore: fallita l'impostazione di SIGTERM");
        exit(EXIT_FAILURE);
    }
    
    // Iniziallizzo thread_pool e stato di persistenza
    printf("Init strutture necessarie.");
    persistence_init();
    pool_init(&pool);

    pthread_t threads[THREAD_POOL_SIZE];
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }

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

    while (keep_running) {
        client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &addr_size);
        
        if (client_sock < 0) {
            // Se accept fallisce a causa del nostro segnale (Interrupted system call)
            if (errno == EINTR) {
                printf("\nSegnale ricevuto. Uscita dal ciclo di accept...\n");
                break; 
            }
            perror("Errore: fallita la accept");
            continue;
        }
        pool_submit(&pool, client_sock);
    }

    // FASE DI CLEANUP (Graceful Shutdown)
    printf("Avvio procedura di spegnimento di Dakty...\n");
    close(server_fd); // Chiudiamo subito la porta principale

    pool_shutdown(&pool); // Svegliamo e terminiamo i thread (da implementare)
    persistence_shutdown();

    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_join(threads[i], NULL); // Attendiamo che ogni thread finisca
    }

    printf("Server chiuso correttamente. Arrivederci!\n");
    return 0;
}
