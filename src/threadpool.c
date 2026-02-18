#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "../include/threadpool.h"

/*
 * Descrizione: Inizializza la struttura del Thread Pool, azzerando gli indici
 * della coda circolare e configurando il mutex e le variabili di condizione
 * necessarie per la sincronizzazione dei thread.
 *
 * Parametri:
 * pool - Puntatore alla struttura ThreadPool da inizializzare.
 *
 * Ritorno:
 * void - Nessun valore restituito. In caso di fallimento delle syscall di init,
 * il processo termina con EXIT_FAILURE.
 */
void pool_init(ThreadPool *pool) {
    pool->head = 0;
    pool->tail = 0;
    pool->count = 0;
    pool->shutdown = 0;

    if (pthread_mutex_init(&pool->lock, NULL) != 0) {
        perror("Errore: inizializzazione mutex fallita");
        exit(EXIT_FAILURE);
    }
    if (pthread_cond_init(&pool->not_empty, NULL) != 0) {
        perror("Errore: inizializzazione cond_var 'not_empty' fallita");
        exit(EXIT_FAILURE);
    }
    if (pthread_cond_init(&pool->not_full, NULL) != 0) {
        perror("Errore: inizializzazione cond_var 'not_full' fallita");
        exit(EXIT_FAILURE);
    }
}

/*
 * Descrizione: Sottomette un nuovo descrittore di socket alla coda dei task
 * (ruolo Produttore). Gestisce l'attesa se la coda ha raggiunto la sua capacità massima.
 *
 * Parametri:
 * pool - Puntatore alla struttura ThreadPool condivisa.
 * client_socket - File descriptor del socket del nuovo client da accodare.
 *
 * Ritorno:
 * void - Nessun valore restituito.
 */
void pool_submit(ThreadPool *pool, int client_socket) {
    pthread_mutex_lock(&pool->lock);

    /* Ciclo while per difendersi dalla vulnerabilità dei risvegli spuri (spurious wakeups) */
    while (pool->count == QUEUE_SIZE) {
        pthread_cond_wait(&pool->not_full, &pool->lock);
    }

    pool->client_sockets[pool->tail] = client_socket;
    pool->tail = (pool->tail + 1) % QUEUE_SIZE;
    pool->count++;

    /* Segnala a un consumatore in attesa che la coda contiene un nuovo task */
    pthread_cond_signal(&pool->not_empty);

    pthread_mutex_unlock(&pool->lock);
}

/*
 * Descrizione: Preleva un socket in attesa dalla coda dei task (ruolo Consumatore).
 * I worker thread utilizzano questa funzione bloccante per ottenere il prossimo client.
 *
 * Parametri:
 * pool - Puntatore alla struttura ThreadPool condivisa.
 *
 * Ritorno:
 * int - Il file descriptor del socket estratto, oppure -1 per indicare
 * che il server è in fase di spegnimento e il thread chiamante deve terminare.
 */
int pool_fetch(ThreadPool *pool) {
    int client_socket;
    
    pthread_mutex_lock(&pool->lock);

    while (pool->count == 0 && !pool->shutdown) {
        pthread_cond_wait(&pool->not_empty, &pool->lock);
    }

    /* Valutazione della condizione di Graceful Shutdown: coda vuota e segnale di arresto */
    if (pool->shutdown && pool->count == 0) {
        pthread_mutex_unlock(&pool->lock);
        return -1; 
    }

    client_socket = pool->client_sockets[pool->head];
    pool->head = (pool->head + 1) % QUEUE_SIZE;
    pool->count--;

    /* Segnala al produttore che si è liberato uno slot nella coda */
    pthread_cond_signal(&pool->not_full);
    
    pthread_mutex_unlock(&pool->lock);

    return client_socket;
}

/*
 * Descrizione: Avvia la procedura di chiusura sicura (Graceful Shutdown) del Thread Pool.
 * Imposta il flag di spegnimento e risveglia forzatamente tutti i worker dormienti.
 *
 * Parametri:
 * pool - Puntatore alla struttura ThreadPool da arrestare.
 *
 * Ritorno:
 * void - Nessun valore restituito.
 */
void pool_shutdown(ThreadPool *pool) {
    pthread_mutex_lock(&pool->lock);
    pool->shutdown = 1;
    
    /* Utilizzo di broadcast per risvegliare simultaneamente tutti i thread bloccati su fetch */
    pthread_cond_broadcast(&pool->not_empty); 
    
    pthread_mutex_unlock(&pool->lock);
}
