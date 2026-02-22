#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>

/* Costanti di configurazione per il dimensionamento del server */
#define THREAD_POOL_SIZE 10
#define QUEUE_SIZE 100

/*
 * Descrizione: Struttura dati che rappresenta il pool di thread e la coda
 * dei task (descrittori di socket). Implementa un buffer circolare 
 * thread-safe basato sul paradigma Produttore-Consumatore.
 *
 * Campi:
 * client_sockets - Array di interi utilizzato come coda circolare per i socket.
 * head - Indice di lettura per i worker thread per indicare qual è il prossimo client socket da servire.
 * tail - Indice di scrittura per il thread principale per indicare dove inserire prossimo socket.
 * count - Numero attuale di descrittori di socket presenti in coda.
 * lock - Mutex per garantire l'accesso esclusivo alla struttura.
 * not_empty - Variabile di condizione per bloccare i worker se la coda è vuota.
 * not_full - Variabile di condizione per bloccare il main se la coda è piena.
 * shutdown - Flag di stato (1 = spegnimento in corso, 0 = operativo).
 */
typedef struct {
    int client_sockets[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    int shutdown;
} ThreadPool;

void pool_init(ThreadPool *pool);

void pool_submit(ThreadPool *pool, int client_socket);

int pool_fetch(ThreadPool *pool);

void pool_shutdown(ThreadPool *pool);

#endif
