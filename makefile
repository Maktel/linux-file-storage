CC = gcc
ARGS = -Wall -Wextra
SRV = server.out
CLI = client.out

all: $(SRV) $(CLI)

$(SRV): server.c settings.h settings.c
	$(CC) $(ARGS) -o $(SRV) server.c settings.c

$(CLI): client.c settings.h settings.c
	$(CC) $(ARGS) -o $(CLI) client.c settings.c

remove:
	rm -f server.out client.out bhrcyzehrncjfrg.fifo queue.tmp fsfile.dat
