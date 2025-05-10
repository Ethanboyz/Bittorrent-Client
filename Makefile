# **ADDTOME** -- marks the code that you need to add to
# please feel free to change this -- i'm not picky as to how code is organized, just as long as it works LOL

CC = gcc
CFLAGS = -Wall -Wextra -I ./hash/includes -Iinclude -Iheapless-bencode -ggdb
LDFLAGS = -lcrypto -lssl -lm

DEBUG=-DDEBUG

# Directories
BUILD_DIR = build
SRC_DIR = src
BENCODE_DIR = heapless-bencode


# Targets -- change and add as needed?
TARGET = btclient


# ADDTOME
# Object files -- do for each relevant source file we write
OBJS = $(BUILD_DIR)/torrent_parser.o \
       $(BUILD_DIR)/bencode.o \
	   $(BUILD_DIR)/hash.o \
	   $(BUILD_DIR)/arg_parser.o \
	   $(BUILD_DIR)/peer_manager.o \
	   $(BUILD_DIR)/tracker.o \
	   $(BUILD_DIR)/piece_manager.o \
	   $(BUILD_DIR)/btclient.o 


# Ensure directories exist before building
$(shell mkdir -p $(BUILD_DIR) $(BIN_DIR))

# Default target
all: $(TARGET)

# Link the target executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "\n"
	@echo "Build successful! Executable: $@"


# ADDTOME -- for each c file we write, add a rule to compile it like the below list of chunks
$(BUILD_DIR)/torrent_parser.o: $(SRC_DIR)/torrent_parser.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/bencode.o: $(BENCODE_DIR)/bencode.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/hash.o: $(SRC_DIR)/hash.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/arg_parser.o: $(SRC_DIR)/arg_parser.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/peer_manager.o: $(SRC_DIR)/peer_manager.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/tracker.o: $(SRC_DIR)/tracker.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/piece_manager.o: $(SRC_DIR)/piece_manager.c 
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/btclient.o: $(SRC_DIR)/btclient.c
	$(CC) $(CFLAGS) -c -o $@ $<


# Clean up
clean:
	rm -rf $(BUILD_DIR) btclient

.PHONY: all clean