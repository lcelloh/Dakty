# Compilatore e flag di compilazione
CC = gcc
CFLAGS = -Wall -Wextra -I./include -g
LDFLAGS = -pthread

# Nomi degli eseguibili per il progetto Dakty
SERVER_BIN = dakty_server
CLIENT_BIN = dakty_client

# Definizione dei file sorgente
SERVER_SRC = src/server.c src/threadpool.c src/server_handler.c src/persistence.c
CLIENT_SRC = src/client.c src/client_controller.c

# Regola di default: compila sia il server che il client
all: $(SERVER_BIN) $(CLIENT_BIN)

# Regola per generare l'eseguibile del Server Dakty
# $@ rappresenta il target (SERVER_BIN), $^ rappresenta le dipendenze (SERVER_SRC)
$(SERVER_BIN): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Regola per generare l'eseguibile del Client Dakty
$(CLIENT_BIN): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# !----------------------------! TESTS !----------------------------! 
TEST_BIN = test_threadpool
TEST_SRC = test/test_threadpool.c src/threadpool.c

# Aggiungi $(TEST_BIN) alla regola 'clean' esistente
clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN) $(TEST_BIN)

# Nuova regola per compilare il test
$(TEST_BIN): $(TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Regola di utilità per compilare ed eseguire subito i test
test: $(TEST_BIN)
	./$(TEST_BIN)

# Phony targets per evitare conflitti con file omonimi
.PHONY: all clean
