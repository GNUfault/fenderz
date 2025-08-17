CC = gcc
BIN = main
SRC = main.c
BITS = 64
OPT = s
LIBS = -lX11 -lGL -lGLU -lm

all:
	$(CC) -o $(BIN) $(SRC) -m$(BITS) -O$(OPT) $(LIBS)
	strip -sv $(BIN)

clean:
	rm -f $(BIN)
