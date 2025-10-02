CC = gcc
BIN = main
SRC = main.c
MARCH = native
MTUNE = $(MARCH)
OPT = fast
LIBS = -lX11 -lGL -lGLU -lm

all:
	$(CC) -o $(BIN) $(SRC) -march=$(MARCH) -mtune=$(MTUNE) -O$(OPT) $(LIBS)
	objcopy --strip-all $(BIN)

clean:
	rm -f $(BIN)
