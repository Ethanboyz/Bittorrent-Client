CC=gcc
CFLAGS=-Wall -I ./hash/includes -Wextra -std=c99 -ggdb
VPATH=src
DEBUG=-DDEBUG

all: client

clean:
	rm -rf client *.o *.txt

.PHONY: clean all