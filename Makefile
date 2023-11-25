CFLAGS :=
CFLAGS += -Wall -Wextra

all:
	gcc $(CFLAGS) sandbox.c -o sandbox

.PHONY: clean
clean:
	rm sandbox

