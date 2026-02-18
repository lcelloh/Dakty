#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include "../include/threadpool.h"

// 1. Test di Inizializzazione
void test_pool_init() {
    ThreadPool pool;
    pool_init(&pool);
    
    assert(pool.count == 0);
    assert(pool.head == 0);
    assert(pool.tail == 0);
    assert(pool.shutdown == 0); // Assicurati di aver aggiunto questa variabile alla struct!
    
    printf("[OK] test_pool_init superato.\n");
}

// 2. Test dell'ordine FIFO e della logica circolare
void test_pool_fifo() {
    ThreadPool pool;
    pool_init(&pool);
    
    // Inseriamo 3 finti "socket"
    pool_submit(&pool, 10);
    pool_submit(&pool, 20);
    pool_submit(&pool, 30);
    
    assert(pool.count == 3);
    
    // Estraiamo e verifichiamo l'ordine (First-In, First-Out)
    assert(pool_fetch(&pool) == 10);
    assert(pool_fetch(&pool) == 20);
    assert(pool.count == 1);
    
    // Testiamo la circolarità forzando il riempimento parziale
    for(int i = 0; i < QUEUE_SIZE - 2; i++) {
        pool_submit(&pool, 100 + i);
    }
    
    assert(pool_fetch(&pool) == 30); // Il 30 era ancora in coda
    
    printf("[OK] test_pool_fifo superato.\n");
}

// 3. Test di Concorrenza e Shutdown
void* mock_worker(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;
    int task = pool_fetch(pool);
    
    // Se riceviamo -1, il pool si sta spegnendo
    if (task == -1) {
        pthread_exit((void*)1); // Uscita con codice 1 (Successo per lo shutdown)
    }
    pthread_exit((void*)0); // Uscita anomala
}

void test_pool_shutdown() {
    ThreadPool pool;
    pool_init(&pool);
    
    pthread_t thread;
    // Creiamo un thread che si bloccherà in attesa su una coda vuota
    pthread_create(&thread, NULL, mock_worker, &pool);
    
    // Diamo al thread il tempo di bloccarsi sulla condition variable
    usleep(100000); 
    
    // Invochiamo lo shutdown
    pool_shutdown(&pool);
    
    void* result;
    pthread_join(thread, &result);
    
    // Verifichiamo che il thread sia uscito correttamente restituendo 1
    assert((long)result == 1);
    
    printf("[OK] test_pool_shutdown superato.\n");
}

// Nuova funzione di test da aggiungere in tests/test_threadpool.c
void test_pool_full() {
    ThreadPool pool;
    pool_init(&pool);
    
    // Riempiamo artificialmente la coda fino al limite (QUEUE_SIZE)
    // Nota: in un test single-thread non usiamo pool_submit per superare
    // il limite, altrimenti il test andrebbe in deadlock (si bloccherebbe) 
    // aspettando all'infinito un worker thread che liberi spazio!
    for (int i = 0; i < QUEUE_SIZE; i++) {
        pool.client_sockets[pool.tail] = i;
        pool.tail = (pool.tail + 1) % QUEUE_SIZE;
        pool.count++;
    }
    
    // Verifichiamo che il contatore indichi esattamente la capienza massima
    assert(pool.count == QUEUE_SIZE);
    
    // Verifichiamo che l'head e il tail coincidano (circolarità completa)
    assert(pool.head == pool.tail);
    
    printf("[OK] test_pool_full superato.\n");
}

int main() {
    printf("=== Avvio Test Suite Thread Pool Dakty ===\n");
    
    test_pool_init();
    test_pool_fifo();
    test_pool_shutdown();
    test_pool_full();
    
    printf("==========================================\n");
    printf("TUTTI I TEST SUPERATI CON SUCCESSO!\n");
    
    return 0;
}

