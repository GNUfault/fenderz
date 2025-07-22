CC = gcc
BIN = main
SRC = main.c
BITS = 32
ARCH = i686
OPT = 3
LIBS = -lX11 -lGL -lGLU -lm

all:
	$(CC) -o $(BIN) $(SRC) -m$(BITS) -march=$(ARCH) -O$(OPT) $(LIBS)
	strip -sv $(BIN)

clean:
	rm -f $(BIN)
