CC = gcc
CFLAGS = -Wall -Wextra -I ./hash/includes -Iinclude -Iheapless-bencode -std=c99 -ggdb
LDFLAGS = 

DEBUG=-DDEBUG

SRC_DIR = src
BENCODE_DIR = heapless-bencode

# change and add as needed?
TARGET = torrent-demo

# add paths to obj files here
OBJS = $(SRC_DIR)/torrent_parser.o $(SRC_DIR)/torrent_demo.o $(BENCODE_DIR)/bencode.o


all: $(TARGET)

# Link the target executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Compile source files
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile bencode library
$(BENCODE_DIR)/%.o: $(BENCODE_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Clean up
clean:
	rm -f $(TARGET) $(OBJS)


.PHONY: all clean
