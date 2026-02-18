# Dakty - Sistema di Bacheca Elettronica

Dakty è un'applicazione Client-Server scritta in linguaggio C basata su socket TCP. Il sistema permette agli utenti di registrarsi, autenticarsi e interagire con una bacheca centralizzata leggendo, scrivendo e cancellando messaggi. Il progetto è stato progettato con un'architettura multithreading robusta, un protocollo di rete personalizzato agnostico rispetto all'hardware e una gestione sicura della persistenza dei dati.

## Architettura Generale

Il sistema adotta un modello Client-Server concorrente. 
* **Il Server** utilizza un approccio a **Thread Pool** (paradigma Produttore-Consumatore) per gestire simultaneamente le richieste di molteplici client senza il sovraccarico derivante dalla creazione continua di nuovi thread.
* **Il Client** è strutturato in moduli che separano nettamente l'interfaccia utente (gestione dei menu e input da tastiera) dalla logica di comunicazione di rete. Questa chiara divisione dei ruoli permette di mantenere il codice leggibile e facilmente manutenibile.

## Struttura del Progetto e Responsabilità dei Moduli

Il codice sorgente è rigorosamente modularizzato per garantire la separazione delle responsabilità (Separation of Concerns).

### Moduli Condivisi
* **`protocol.h`**: Definisce l'infrastruttura del protocollo di rete. Contiene i codici operativi (OpCodes) e definisce le struct utilizzate per i payload.
* **`net_utils.c` / `.h`**: Fornisce i wrapper sicuri per le operazioni di I/O sui socket (`recv_all`, `send_all`, `safe_accept`). Queste funzioni gestiscono autonomamente la frammentazione dei pacchetti TCP e prevengono le interruzioni di sistema (gestione di `EINTR`).

### Lato Server
* **`server.c`**: Entry-point del server. Si occupa del setup della rete (socket, bind, listen), dell'orchestrazione dei segnali (`SIGINT`, `SIGTERM`), dell'accettazione dei client (`accept`) e dell'avvio della procedura di Graceful Shutdown.
* **`server_handler.c`**: Contiene la logica applicativa del server. Interpreta gli header di rete, estrae i payload e instrada le richieste verso le operazioni appropriate (routing), comunicando infine con il modulo di persistenza.
* **`threadpool.c`**: Gestisce la concorrenza. Implementa una coda circolare thread-safe utilizzando mutex e variabili di condizione per orchestrare il lavoro dei thread in attesa, proteggendo il sistema dai "spurious wakeups".
* **`persistence.c`**: Motore di database in memoria con salvataggio asincrono su disco. Gestisce la cache in RAM per letture veloci e delega le scritture su file a thread di I/O dedicati per non bloccare le operazioni di rete.

### Lato Client
* **`client.c`**: Entry-point del client. Gestisce la navigazione dei menu (macchina a stati), l'input sicuro da tastiera (pulizia del buffer stdin) e l'intercettazione dei segnali (`SIGPIPE`, `SIGINT`).
* **`client_controller.c`**: Interfaccia di comunicazione. Espone funzioni ad alto livello per il client nascondendo la complessità dei socket, assemblando i pacchetti e analizzando le risposte del server.

## Protocollo di Rete Custom

La comunicazione avviene tramite un protocollo a livello applicativo fortemente standardizzato, progettato per essere esente da problemi di allineamento della memoria e incompatibilità tra architetture hardware.

* **Struttura a Pacchetto Fisso**: Ogni trasmissione inizia sempre con un `DKTHeader` di 5 byte esatti (1 byte per il `type` dell'operazione, 4 byte per `payload_length`).
* **Prevenzione del Padding**: Tutte le strutture di rete utilizzano la direttiva `__attribute__((packed))`. Questo disabilita l'allineamento automatico del compilatore, garantendo che le dimensioni dei pacchetti siano identiche al singolo byte su qualsiasi sistema operativo o CPU.
* **Gestione dell'Endianness**: Ogni variabile numerica multi-byte viene convertita in Network Byte Order tramite `htonl()` prima dell'invio e ripristinata in Host Byte Order tramite `ntohl()` in ricezione.

### Esempio di Flusso: Scrittura di un Messaggio (REQ_POST_MSG)

Per comprendere il funzionamento del protocollo, ecco cosa accade a livello di socket quando un utente pubblica un messaggio in bacheca:

1. **Fase di Invio (Client -> Server):**
   * Il client popola una struct `DKTHeader` impostando `type = REQ_POST_MSG` e `payload_length = htonl(sizeof(MessagePayload))`.
   * Il client invia i 5 byte dell'header tramite la funzione `send_all`.
   * Subito dopo, il client popola una struct `MessagePayload` con l'oggetto e il corpo del messaggio.
   * Il client invia i byte del payload tramite una seconda chiamata a `send_all`.

2. **Fase di Ricezione e Processing (Server):**
   * Il thread worker del server esegue una `recv_all` di 5 byte per leggere il `DKTHeader`.
   * Analizza il campo `type` e instrada la richiesta alla funzione `process_post_msg`.
   * La funzione estrae il `payload_length` convertendolo con `ntohl()`.
   * Il server esegue una seconda `recv_all` pari alla lunghezza appena letta, acquisendo il `MessagePayload` completo in memoria.
   * Il server valida la sicurezza (verifica che l'utente sia loggato) e salva il messaggio nel modulo di persistenza.

3. **Fase di Risposta (Server -> Client):**
   * Il server prepara un nuovo `DKTHeader` di risposta.
   * Imposta `type = RESP_SUCCESS` e `payload_length = htonl(0)` (nessun payload aggiuntivo richiesto per il successo).
   * Il server invia i 5 byte dell'header al client tramite `send_all`.

4. **Conclusione (Client):**
   * Il client, che era in attesa bloccante su una `recv_all` di 5 byte, riceve l'header di risposta.
   * Legge `RESP_SUCCESS` e notifica all'utente la corretta pubblicazione del messaggio.

## Gestione della Persistenza

Il modulo di persistenza implementa un meccanismo ottimizzato per un server multithread:
1. **Cache in Memoria**: Tutte le entità (utenti e messaggi) sono caricate in RAM all'avvio del server. Le letture avvengono in tempo reale e sono protette da mutex.
2. **I/O Asincrono**: Le scritture su disco sono demandate a thread worker specifici. I salvataggi avvengono senza bloccare le operazioni di rete dei client. I messaggi sfruttano un dump dinamico locale per ridurre al minimo il tempo in cui il mutex della cache principale rimane bloccato.

## Graceful Shutdown e Gestione Segnali

Il server e il client sono progettati per essere resilienti alle terminazioni forzate e agli errori di connessione.

* **Lato Client**: Ignora attivamente il segnale `SIGPIPE` (assegnandogli un handler custom che chiude pulitamente il programma) per prevenire crash applicativi silenziosi se il server viene disconnesso bruscamente.
* **Lato Server (Graceful Shutdown)**: L'interruzione manuale (`Ctrl+C` -> `SIGINT`) innesca una chiusura controllata:
    1. Il thread principale interrompe l'accettazione di nuovi client.
    2. Viene invocata la syscall `shutdown(sockfd, SHUT_RDWR)` sui socket attivi, sbloccando forzatamente i thread worker sospesi in attesa di dati.
    3. Viene inviato un segnale di broadcast (`pthread_cond_broadcast`) al Thread Pool per svegliare e far terminare i thread addormentati.
    4. Il main esegue la `pthread_join` sui worker e ordina al modulo di persistenza di scaricare i buffer finali su disco, garantendo zero perdite di dati.# Dakty - Sistema di Bacheca Elettronica

Dakty è un'applicazione Client-Server scritta in linguaggio C basata su socket TCP. Il sistema permette agli utenti di registrarsi, autenticarsi e interagire con una bacheca centralizzata leggendo, scrivendo e cancellando messaggi. Il progetto è stato progettato con un'architettura multithreading robusta, un protocollo di rete personalizzato agnostico rispetto all'hardware e una gestione sicura della persistenza dei dati.

## Architettura Generale

Il sistema adotta un modello Client-Server concorrente. 
* **Il Server** utilizza un approccio a **Thread Pool** (paradigma Produttore-Consumatore) per gestire simultaneamente le richieste di molteplici client senza il sovraccarico derivante dalla creazione continua di nuovi thread.
* **Il Client** è strutturato in moduli che separano nettamente l'interfaccia utente (gestione dei menu e input da tastiera) dalla logica di comunicazione di rete. Questa chiara divisione dei ruoli permette di mantenere il codice leggibile e facilmente manutenibile.

## Struttura del Progetto e Responsabilità dei Moduli

Il codice sorgente è rigorosamente modularizzato per garantire la separazione delle responsabilità (Separation of Concerns).

### Moduli Condivisi
* **`protocol.h`**: Definisce l'infrastruttura del protocollo di rete. Contiene i codici operativi (OpCodes) e definisce le struct utilizzate per i payload.
* **`net_utils.c` / `.h`**: Fornisce i wrapper sicuri per le operazioni di I/O sui socket (`recv_all`, `send_all`, `safe_accept`). Queste funzioni gestiscono autonomamente la frammentazione dei pacchetti TCP e prevengono le interruzioni di sistema (gestione di `EINTR`).

### Lato Server
* **`server.c`**: Entry-point del server. Si occupa del setup della rete (socket, bind, listen), dell'orchestrazione dei segnali (`SIGINT`, `SIGTERM`), dell'accettazione dei client (`accept`) e dell'avvio della procedura di Graceful Shutdown.
* **`server_handler.c`**: Contiene la logica applicativa del server. Interpreta gli header di rete, estrae i payload e instrada le richieste verso le operazioni appropriate (routing), comunicando infine con il modulo di persistenza.
* **`threadpool.c`**: Gestisce la concorrenza. Implementa una coda circolare thread-safe utilizzando mutex e variabili di condizione per orchestrare il lavoro dei thread in attesa, proteggendo il sistema dai "spurious wakeups".
* **`persistence.c`**: Motore di database in memoria con salvataggio asincrono su disco. Gestisce la cache in RAM per letture veloci e delega le scritture su file a thread di I/O dedicati per non bloccare le operazioni di rete.

### Lato Client
* **`client.c`**: Entry-point del client. Gestisce la navigazione dei menu (macchina a stati), l'input sicuro da tastiera (pulizia del buffer stdin) e l'intercettazione dei segnali (`SIGPIPE`, `SIGINT`).
* **`client_controller.c`**: Interfaccia di comunicazione. Espone funzioni ad alto livello per il client nascondendo la complessità dei socket, assemblando i pacchetti e analizzando le risposte del server.

## Protocollo di Rete Custom

La comunicazione avviene tramite un protocollo a livello applicativo fortemente standardizzato, progettato per essere esente da problemi di allineamento della memoria e incompatibilità tra architetture hardware.

* **Struttura a Pacchetto Fisso**: Ogni trasmissione inizia sempre con un `DKTHeader` di 5 byte esatti (1 byte per il `type` dell'operazione, 4 byte per `payload_length`).
* **Prevenzione del Padding**: Tutte le strutture di rete utilizzano la direttiva `__attribute__((packed))`. Questo disabilita l'allineamento automatico del compilatore, garantendo che le dimensioni dei pacchetti siano identiche al singolo byte su qualsiasi sistema operativo o CPU.
* **Gestione dell'Endianness**: Ogni variabile numerica multi-byte viene convertita in Network Byte Order tramite `htonl()` prima dell'invio e ripristinata in Host Byte Order tramite `ntohl()` in ricezione.

### Esempio di Flusso: Scrittura di un Messaggio (REQ_POST_MSG)

Per comprendere il funzionamento del protocollo, ecco cosa accade a livello di socket quando un utente pubblica un messaggio in bacheca:

1. **Fase di Invio (Client -> Server):**
   * Il client popola una struct `DKTHeader` impostando `type = REQ_POST_MSG` e `payload_length = htonl(sizeof(MessagePayload))`.
   * Il client invia i 5 byte dell'header tramite la funzione `send_all`.
   * Subito dopo, il client popola una struct `MessagePayload` con l'oggetto e il corpo del messaggio.
   * Il client invia i byte del payload tramite una seconda chiamata a `send_all`.

2. **Fase di Ricezione e Processing (Server):**
   * Il thread worker del server esegue una `recv_all` di 5 byte per leggere il `DKTHeader`.
   * Analizza il campo `type` e instrada la richiesta alla funzione `process_post_msg`.
   * La funzione estrae il `payload_length` convertendolo con `ntohl()`.
   * Il server esegue una seconda `recv_all` pari alla lunghezza appena letta, acquisendo il `MessagePayload` completo in memoria.
   * Il server valida la sicurezza (verifica che l'utente sia loggato) e salva il messaggio nel modulo di persistenza.

3. **Fase di Risposta (Server -> Client):**
   * Il server prepara un nuovo `DKTHeader` di risposta.
   * Imposta `type = RESP_SUCCESS` e `payload_length = htonl(0)` (nessun payload aggiuntivo richiesto per il successo).
   * Il server invia i 5 byte dell'header al client tramite `send_all`.

4. **Conclusione (Client):**
   * Il client, che era in attesa bloccante su una `recv_all` di 5 byte, riceve l'header di risposta.
   * Legge `RESP_SUCCESS` e notifica all'utente la corretta pubblicazione del messaggio.

## Gestione della Persistenza

Il modulo di persistenza implementa un meccanismo ottimizzato per un server multithread:
1. **Cache in Memoria**: Tutte le entità (utenti e messaggi) sono caricate in RAM all'avvio del server. Le letture avvengono in tempo reale e sono protette da mutex.
2. **I/O Asincrono**: Le scritture su disco sono demandate a thread worker specifici. I salvataggi avvengono senza bloccare le operazioni di rete dei client. I messaggi sfruttano un dump dinamico locale per ridurre al minimo il tempo in cui il mutex della cache principale rimane bloccato.

## Graceful Shutdown e Gestione Segnali

Il server e il client sono progettati per essere resilienti alle terminazioni forzate e agli errori di connessione.

* **Lato Client**: Ignora attivamente il segnale `SIGPIPE` (assegnandogli un handler custom che chiude pulitamente il programma) per prevenire crash applicativi silenziosi se il server viene disconnesso bruscamente.
* **Lato Server (Graceful Shutdown)**: L'interruzione manuale (`Ctrl+C` -> `SIGINT`) innesca una chiusura controllata:
    1. Il thread principale interrompe l'accettazione di nuovi client.
    2. Viene invocata la syscall `shutdown(sockfd, SHUT_RDWR)` sui socket attivi, sbloccando forzatamente i thread worker sospesi in attesa di dati.
    3. Viene inviato un segnale di broadcast (`pthread_cond_broadcast`) al Thread Pool per svegliare e far terminare i thread addormentati.
    4. Il main esegue la `pthread_join` sui worker e ordina al modulo di persistenza di scaricare i buffer finali su disco, garantendo zero perdite di dati.
