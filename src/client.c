#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/client_controller.h"
#include "../include/protocol.h"

#define PORT 8080
#define SERVER_IP "127.0.0.1"

// ==========================================
// FUNZIONI DI UTILITA' (UX / UI)
// ==========================================

void clear_screen() {
    printf("\033[2J\033[H");
}

void wait_for_enter() {
    char temp[10];
    printf("\n[Premi INVIO per continuare...]");
    if (fgets(temp, sizeof(temp), stdin) != NULL) {
        if (strchr(temp, '\n') == NULL) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
        }
    }
}

void safe_read(char* buffer, int len){
    if (fgets(buffer, len, stdin) != NULL) {
        buffer[strcspn(buffer, "\n")] = '\0'; 

        for(int i = strlen(buffer) - 1; i >= 0 && buffer[i] == ' '; i--){
            buffer[i] = '\0';
        }
    }
}   

// ==========================================
// FLUSSI DELL'APPLICAZIONE
// ==========================================

int auth_client(int sock, char* username){
    char password[MAX_PASSWORD];
    char choice[10];

    while (1) {
        clear_screen(); // Pulisce prima di mostrare il menu
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
                if (dakty_login(sock, username, password)) {
                    printf("\n[+] Accesso consentito! Benvenuto, %s.\n", username);
                    wait_for_enter(); // Pausa per far leggere il benvenuto
                    return 1;
                } else {
                    printf("\n[-] Login fallito. Credenziali errate o utente inesistente.\n");
                    wait_for_enter(); // Pausa per far leggere l'errore prima di ripulire
                }
            } else {
                if (dakty_register(sock, username, password)) {
                    printf("\n[+] Registrazione completata! Benvenuto, %s.\n", username);
                    wait_for_enter();
                    return 1; 
                } else {
                    printf("\n[-] Registrazione fallita. Nome utente forse già in uso.\n");
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

int user_loop(int sock) {
    char choice[10];

    while (1) {
        clear_screen(); // Pulisce prima del menu principale
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
            clear_screen(); // Pulisce lo schermo prima di stampare l'intera bacheca
            ResponsePayload* bacheca = NULL;
            int count = dakty_read_messages(sock, &bacheca);

            if (count < 0) {
                printf("[-] Errore di comunicazione con il server.\n");
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
            wait_for_enter(); // Lascia all'utente il tempo di leggere la bacheca!
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
                    printf("[-] Errore durante la pubblicazione del messaggio.\n");
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
                    printf("[-] Eliminazione negata. Sei sicuro di esserne l'autore?\n");
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
                printf("\n[-] Errore di comunicazione durante il logout.\n");
                wait_for_enter();
            }
        } 
        else if (strcmp(choice, "5") == 0) {
            printf("\n[*] Uscita da Dakty BBS.\n");
            return 0; 
        } 
        else {
            printf("\n[-] Scelta non valida, riprova.\n");
            wait_for_enter();
        }
    }
}

int main() {
    int server_sock;
    char user[MAX_USERNAME]; 

    clear_screen(); 
    printf("=====================================\n");
    printf("        BENVENUTO IN DAKTY \n");
    printf("=====================================\n");

    printf("[*] Connessione al server in corso...\n");
    server_sock = dakty_connect(SERVER_IP, PORT);
    if (server_sock < 0) {
        printf("[-] Impossibile contattare il server Dakty. Esco.\n");
        exit(EXIT_FAILURE);
    }
    printf("[+] Connesso con successo!\n");
    wait_for_enter(); // Lascia leggere che il server si è connesso

    int running = 1;
    while (running) {
        if (auth_client(server_sock, user)) {
            running = user_loop(server_sock);   
        } else {
            break; 
        }
    }

    clear_screen(); // Pulizia finale prima di restituire il prompt
    printf("[*] Disconnessione dal server...\n");
    dakty_disconnect(server_sock);
    printf("Grazie per aver usato Dakty BBS. Arrivederci!\n\n");
    return 0;
}
