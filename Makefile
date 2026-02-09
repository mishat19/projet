CC = gcc
CFLAGS = -Wall -Wextra -lrt

all: daemon worker

daemon: daemon.c
	$(CC) $(CFLAGS) daemon.c -o daemon

worker: worker.c
	$(CC) $(CFLAGS) worker.c -o worker

clean:
	rm -f daemon worker
