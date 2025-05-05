# **ADDTOME** -- marks the code that you need to add to
# please feel free to change this -- i'm not picky as to how code is organized, just as long as it works LOL

CC = gcc
CFLAGS = -Wall -Wextra -I ./hash/includes -Iinclude -Iheapless-bencode -ggdb
LDFLAGS = -lcrypto

DEBUG=-DDEBUG

# Directories
BUILD_DIR = build
BIN_DIR = bin
SRC_DIR = src
BENCODE_DIR = heapless-bencode


# Targets -- change and add as needed?
TARGET = torrent-demo


# ADDTOME
# Object files -- do for each relevant source file we write
OBJS = $(BUILD_DIR)/torrent_parser.o \
       $(BUILD_DIR)/torrent_demo.o \
       $(BUILD_DIR)/bencode.o \
	   $(BUILD_DIR)/hash.o



# Ensure directories exist before building
$(shell mkdir -p $(BUILD_DIR) $(BIN_DIR))

# Default target
all: $(BIN_DIR)/$(TARGET)

# Link the target executable
$(BIN_DIR)/$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "\n"
	@echo "Build successful! Executable: $(BIN_DIR)/$(TARGET)"




# ADDTOME -- for each c file we write, add a rule to compile it like the below list of chunks
$(BUILD_DIR)/torrent_parser.o: $(SRC_DIR)/torrent_parser.c
	$(CC) $(CFLAGS) -c -o $@ $<


$(BUILD_DIR)/torrent_demo.o: $(SRC_DIR)/torrent_demo.c
	$(CC) $(CFLAGS) -c -o $@ $<


$(BUILD_DIR)/bencode.o: $(BENCODE_DIR)/bencode.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/hash.o: $(SRC_DIR)/hash.c
	$(CC) $(CFLAGS) -c -o $@ $<




# Clean up
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

.PHONY: all clean