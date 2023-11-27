CFLAGS :=
CFLAGS += -Wall -Wextra

all:
	gcc $(CFLAGS) nsc.c -o nsc

.PHONY: clean
clean:
	rm nsc

