CC = gcc
CFLAGS = -Wall -std=c11 -O2 -pthread
LDFLAGS = -lsqlite3 -lrt

SRCDIR = src
BINDIR = bin

DEPS = -Iinclude

DAEMON = $(BINDIR)/todo-daemon
CLIENT = $(BINDIR)/todo-client

all: $(DAEMON) $(CLIENT)

$(DAEMON): $(SRCDIR)/daemon.c $(SRCDIR)/db.c $(SRCDIR)/ipc.c | $(BINDIR)
	$(CC) $(CFLAGS) $(DEPS) -o $@ $^ $(LDFLAGS)

$(CLIENT): $(SRCDIR)/client.c | $(BINDIR)
	$(CC) $(CFLAGS) $(DEPS) -o $@ $^ $(LDFLAGS)

$(BINDIR):
	mkdir -p $(BINDIR)

clean:
	rm -rf $(BINDIR) *.o

.PHONY: all clean
