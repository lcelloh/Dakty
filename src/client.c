#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "../include/client_controller.h"
#include "../include/protocol.h"

#define PORT 8080
#define SERVER_IP "127.0.0.1"

// GESTIONE SEGNALI

/*
 * Descrizione: Handler per l'interruzione manuale (SIGINT / Ctrl+C).
 * Garantisce che il client termini restituendo un codice di successo,
 * chiudendo implicitamente i descrittori di file aperti dal processo.
 */
void handle_sigint(int sig) {
    printf("\n\n[*] Interruzione manuale (Ctrl+C). Uscita da Dakty. Arrivederci!\n");
    exit(EXIT_SUCCESS); 
}

/*
 * Descrizione: Handler per la rottura del socket (SIGPIPE).
 * Viene innescato dal kernel se il client tenta di fare una send_all verso 
 * un server che è stato spento o è irraggiungibile, evitando un crash silenzioso.
 */
void handle_sigpipe(int sig) {
    printf("\n\n[!] ERRORE FATALE (Broken Pipe): La connessione con il server si è interrotta bruscamente.\n");
    printf("[*] Chiusura del client in corso...\n");
    exit(EXIT_FAILURE); 
}

/*
 * Descrizione: Configura i gestori dei segnali tramite sigaction prima 
 * dell'avvio del ciclo di vita del client.
 */
void setup_signals() {
    struct sigaction sa_pipe, sa_int;

    sa_pipe.sa_handler = handle_sigpipe;
    sigemptyset(&sa_pipe.sa_mask);
    sa_pipe.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa_pipe, NULL) == -1) {
        perror("Errore configurazione SIGPIPE");
        exit(EXIT_FAILURE);
    }

    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        perror("Errore configurazione SIGINT");
        exit(EXIT_FAILURE);
    }
}


/*
 * Descrizione: Pulisce la schermata del terminale tramite codici di escape ANSI.
 */
void clear_screen() {
    printf("\033[2J\033[H");
}

/*
 * Descrizione: Mette in pausa l'esecuzione in attesa che l'utente prema INVIO.
 * Svuota in modo sicuro eventuali residui di caratteri nel buffer di input (stdin) 
 * per evitare che inquinino le letture successive.
 */
void wait_for_enter() {
    char temp[10];
    printf("\n[Premi INVIO per continuare...]");
    
    while (1) {
        if (fgets(temp, sizeof(temp), stdin) != NULL) {
            if (strchr(temp, '\n') == NULL) {
                int c;
                while ((c = getchar()) != '\n' && c != EOF);
            }
            break; // Lettura andata a buon fine, esci dal ciclo
        } else {
            // Se fgets fallisce
            if (errno == EINTR) {
                clearerr(stdin); 
                continue;
            }
            printf("\n\n[*] Chiusura forzata dell'input. Arrivederci!\n");
            exit(EXIT_SUCCESS);
        }
    }
}

 /*
 * Descrizione: Legge una stringa da stdin in modo sicuro. Rimuove il carattere 
 * di newline finale e gli eventuali spazi vuoti in coda.
 * Parametri:
 * buffer - Puntatore all'array di caratteri destinazione.
 * len - Dimensione massima leggibile per prevenire buffer overflow.
 */ 
void safe_read(char* buffer, int len){
    while (1) {
        if (fgets(buffer, len, stdin) != NULL) {
            buffer[strcspn(buffer, "\n")] = '\0'; 

            for(int i = strlen(buffer) - 1; i >= 0 && buffer[i] == ' '; i--){
                buffer[i] = '\0';
            }
            break; 
        } else {
            if (errno == EINTR) {
                clearerr(stdin); 
                continue;
            }
            printf("\n\n[*] Standard Input chiuso. Terminazione client.\n");
            exit(EXIT_SUCCESS);
        }
    }
}

/*
 * Descrizione: Gestisce lo stato di "Non Autenticato". Mostra il menu di accesso 
 * e orchestra le chiamate al client_controller per il Login e la Registrazione.
 *
 * Parametri:
 * sock - File descriptor del socket connesso al server.
 * username - Buffer in cui salvare il nome utente in caso di successo.
 *
 * Ritorno:
 * int - 1 se l'utente si autentica con successo, 0 se decide di uscire dall'app.
 */
int auth_client(int sock, char* username){
    char password[MAX_PASSWORD];
    char choice[10];

    while (1) {
        clear_screen(); 
        printf("=====================================\n");
        printf("           AUTENTICAZIONE DAKTY      \n");
        printf("=====================================\n");
        printf("1. Login\n");
        printf("2. Registrazione\n");
        printf("3. Esci dal programma\n");
        printf("Scelta: ");

        safe_read(choice, sizeof(choice));

        if (strcmp(choice, "3") == 0) {
            return 0; 
        }

        if (strcmp(choice, "1") == 0 || strcmp(choice, "2") == 0) {
            printf("Username: ");
            safe_read(username, MAX_USERNAME);
            
            printf("Password: ");
            safe_read(password, MAX_PASSWORD);

            if (strcmp(choice, "1") == 0) {
                /* Delega al controller la logica di rete per il Login */
                if (dakty_login(sock, username, password)) {
                    printf("\n[+] Accesso consentito! Benvenuto, %s.\n", username);
                    wait_for_enter(); 
                    return 1;
                } else {
                    printf("\n[-] Login fallito o server non raggiungibile.\n");
                    wait_for_enter(); 
                }
            } else {
                /* Delega al controller la logica di rete per la Registrazione */
                if (dakty_register(sock, username, password)) {
                    printf("\n[+] Registrazione completata! Benvenuto, %s.\n", username);
                    wait_for_enter();
                    return 1; 
                } else {
                    printf("\n[-] Registrazione fallita o server non raggiungibile.\n");
                    wait_for_enter();
                }
            }
        } else {
            printf("[-] Scelta non valida, riprova.\n");
            wait_for_enter();
        }
    }
    return 0;
}

/*
 * Descrizione: Gestisce lo stato "Autenticato". Raccoglie gli input utente
 * e instrada le richieste ai metodi del client_controller per la manipolazione
 * della bacheca o il logout.
 *
 * Parametri:
 * sock - File descriptor del socket connesso.
 *
 * Ritorno:
 * int - 1 per tornare al menu di autenticazione (Logout), 0 per chiudere il client.
 */
int user_loop(int sock) {
    char choice[10];

    while (1) {
        clear_screen(); 
        printf("=====================================\n");
        printf("         MENU PRINCIPALE DAKTY       \n");
        printf("=====================================\n");
        printf("1. Leggi i messaggi in bacheca\n");
        printf("2. Scrivi un nuovo messaggio\n");
        printf("3. Elimina un tuo messaggio\n");
        printf("4. Logout\n");
        printf("5. Esci dal programma\n");
        printf("Scelta: ");

        safe_read(choice, sizeof(choice));

        if (strcmp(choice, "1") == 0) {
            clear_screen(); 
            ResponsePayload* bacheca = NULL;
            
            /* Delega al controller la richiesta ed estrae il numero di messaggi */
            int count = dakty_read_messages(sock, &bacheca);

            if (count < 0) {
                printf("[-] Errore fatale di rete: il server si è disconnesso.\n");
                wait_for_enter();
                return 0; 
            } else if (count == 0) {
                printf("======================================================\n");
                printf("                  BACHECA DAKTY                       \n");
                printf("======================================================\n");
                printf("\n--- La bacheca è vuota ---\n");
            } else {
                printf("======================================================\n");
                printf("                  BACHECA DAKTY                       \n");
                printf("======================================================\n");
                
                for (int i = 0; i < count; i++) {
                    printf("\n[#%u] Da: @%s\n", bacheca[i].message_id, bacheca[i].sender);
                    printf("Oggetto: %s\n", bacheca[i].subject);
                    printf("------------------------------------------------------\n");
                    printf("%s\n", bacheca[i].body);
                    printf("======================================================\n");
                }
                free(bacheca); 
            }
            wait_for_enter(); 
        } 
        else if (strcmp(choice, "2") == 0) {
            char subject[MAX_SUBJECT];
            char body[MAX_BODY];
            
            clear_screen();
            printf("--- Scrivi un nuovo messaggio ---\n");
            printf("Oggetto (max %d caratteri): ", MAX_SUBJECT - 1);
            safe_read(subject, sizeof(subject));
            
            printf("Testo (max %d caratteri): ", MAX_BODY - 1);
            safe_read(body, sizeof(body));

            if (strlen(subject) == 0 || strlen(body) == 0) {
                printf("[-] L'oggetto e il testo non possono essere vuoti.\n");
            } else {
                if (dakty_post_message(sock, subject, body)) {
                    printf("[+] Messaggio pubblicato con successo!\n");
                } else {
                    printf("[-] Errore: pubblicazione fallita o server disconnesso.\n");
                }
            }
            wait_for_enter();
        } 
        else if (strcmp(choice, "3") == 0) {
            char id_str[20];
            clear_screen();
            printf("--- Elimina messaggio ---\n");
            printf("Inserisci l'ID del messaggio da eliminare: ");
            safe_read(id_str, sizeof(id_str));
            
            int target_id = atoi(id_str); 
            
            if (target_id <= 0) {
                printf("[-] ID non valido.\n");
            } else {
                if (dakty_delete_message(sock, target_id)) {
                    printf("[+] Messaggio #%d eliminato con successo!\n", target_id);
                } else {
                    printf("[-] Eliminazione negata o server irraggiungibile.\n");
                }
            }
            wait_for_enter();
        }
        else if (strcmp(choice, "4") == 0) {
            if (dakty_logout(sock)) {
                printf("\n[*] Logout effettuato con successo. A presto!\n");
                wait_for_enter();
                return 1; 
            } else {
                printf("\n[-] Errore fatale: server disconnesso.\n");
                wait_for_enter();
                return 0; 
            }
        } 
        else if (strcmp(choice, "5") == 0) {
            printf("\n[*] Uscita da Dakty.\n");
            return 0; 
        } 
        else {
            printf("\n[-] Scelta non valida, riprova.\n");
            wait_for_enter();
        }
    }
}

// ENTRY-POINT 

/*
 * Descrizione: Funzione principale del Client. Inizializza i segnali, tenta
 * la connessione di rete e gestisce il ciclo di vita dell'applicazione alternando
 * le viste (macchina a stati) in base ai risultati delle operazioni utente.
 */
int main() {
    int server_sock;
    char user[MAX_USERNAME]; 

    /* 1. Setup per prevenire crash da SIGPIPE o SIGINT */
    setup_signals(); 

    clear_screen(); 
    printf("=====================================\n");
    printf("        BENVENUTO IN DAKTY \n");
    printf("=====================================\n");

    printf("[*] Connessione al server in corso...\n");
    
    /* 2. Setup di Rete (Richiesta connessione TCP) */
    server_sock = dakty_connect(SERVER_IP, PORT);
    if (server_sock < 0) {
        printf("[-] Impossibile contattare il server Dakty. Esco.\n");
        exit(EXIT_FAILURE);
    }
    printf("[+] Connesso con successo!\n");
    wait_for_enter(); 

    /* 3. Avvio della Macchina a Stati (Loop dell'App) */
    int running = 1;
    while (running) {
        /* auth_client passa il controllo a user_loop solo se l'autenticazione ha successo */
        if (auth_client(server_sock, user)) {
            running = user_loop(server_sock);   
        } else {
            /* Se auth_client o user_loop restituiscono 0, l'utente vuole uscire */
            break; 
        }
    }

    /* 4. Cleanup Finale */
    clear_screen(); 
    printf("[*] Disconnessione dal server...\n");
    dakty_disconnect(server_sock);
    printf("Grazie per aver usato Dakty. Arrivederci!\n\n");
    return 0;
}
