#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "../include/threadpool.h"

// 1. Inizializzazione della struttura del Thread Pool
void pool_init(ThreadPool *pool) {
    pool->head = 0;
    pool->tail = 0;
    pool->count = 0;
    pool->shutdown = 0;

    // Inizializzazione del mutex e delle variabili di condizione.
    // Utilizziamo NULL per gli attributi di default.
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

// 2. Sottomissione di un nuovo client socket (Produttore)
void pool_submit(ThreadPool *pool, int client_socket) {
    // Acquisizione del lock per modificare i dati condivisi
    pthread_mutex_lock(&pool->lock);

    // Se la coda è piena, il thread principale (produttore) deve aspettare.
    // Usiamo un ciclo 'while' per difenderci dai "spurious wakeups" (risvegli fasulli).
    while (pool->count == QUEUE_SIZE) {
        pthread_cond_wait(&pool->not_full, &pool->lock);
    }

    // Inserimento del socket e avanzamento del tail con logica circolare
    pool->client_sockets[pool->tail] = client_socket;
    pool->tail = (pool->tail + 1) % QUEUE_SIZE;
    pool->count++;

    // Segnaliamo a un worker thread in attesa che la coda non è più vuota
    pthread_cond_signal(&pool->not_empty);

    // Rilascio del lock
    pthread_mutex_unlock(&pool->lock);
}

int pool_fetch(ThreadPool *pool) {
    int client_socket;
    pthread_mutex_lock(&pool->lock);

    // Se la coda è vuota e NON siamo in shutdown, aspetta.
    while (pool->count == 0 && !pool->shutdown) {
        pthread_cond_wait(&pool->not_empty, &pool->lock);
    }

    // Se ci siamo svegliati e il server è in spegnimento (e la coda è vuota)
    if (pool->shutdown && pool->count == 0) {
        pthread_mutex_unlock(&pool->lock);
        return -1; // Segnale speciale per far terminare il worker thread
    }

    client_socket = pool->client_sockets[pool->head];
    pool->head = (pool->head + 1) % QUEUE_SIZE;
    pool->count--;

    pthread_cond_signal(&pool->not_full);
    pthread_mutex_unlock(&pool->lock);

    return client_socket;
}

// Da aggiungere dentro pool_init(): pool->shutdown = 0;

void pool_shutdown(ThreadPool *pool) {
    pthread_mutex_lock(&pool->lock);
    pool->shutdown = 1;
    // Usiamo broadcast per svegliare TUTTI i thread in attesa su 'not_empty'
    pthread_cond_broadcast(&pool->not_empty); 
    pthread_mutex_unlock(&pool->lock);
}
