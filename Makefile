# A.E.T.H.E.R. P2P Protocol Makefile
# Builds with GCC/MinGW

CC = gcc
AR = ar

# Directories
SRC_DIR = src
INC_DIR = include
LIB_DIR = lib
BUILD_DIR = build
BIN_DIR = bin

# Source files
SRCS = $(SRC_DIR)/crypto.c \
       $(SRC_DIR)/framing.c \
       $(SRC_DIR)/dht.c \
       $(SRC_DIR)/peer.c \
       $(SRC_DIR)/handshake.c \
       $(SRC_DIR)/message.c \
       $(SRC_DIR)/util.c \
       $(SRC_DIR)/protocol.c \
       $(SRC_DIR)/node.c \
       $(LIB_DIR)/ed25519/ed25519.c \
       $(LIB_DIR)/sha256/sha256.c

# Object files
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
OBJS := $(patsubst $(LIB_DIR)/%.c,$(BUILD_DIR)/%.o,$(OBJS))

# Library sources (excluding node.c)
LIB_SRCS = $(filter-out $(SRC_DIR)/node.c,$(SRCS))
LIB_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIB_SRCS))
LIB_OBJS := $(patsubst $(LIB_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIB_OBJS))

# Headers
HEADERS = $(INC_DIR)/aether.h \
          $(INC_DIR)/crypto.h \
          $(INC_DIR)/framing.h \
          $(INC_DIR)/dht.h \
          $(INC_DIR)/peer.h \
          $(INC_DIR)/handshake.h \
          $(INC_DIR)/message.h \
          $(INC_DIR)/protocol.h \
          $(INC_DIR)/util.h

# Output
LIBRARY = $(BIN_DIR)/libaether.a
NODE_BIN = $(BIN_DIR)/aether-node

# Flags
CFLAGS = -Wall -Wextra -O2 -I$(INC_DIR) -I$(LIB_DIR)
LDFLAGS = 

# Platform-specific
ifeq ($(OS),Windows_NT)
    CFLAGS += -D_WIN32_WINNT=0x0600
    LDFLAGS += -lws2_32
    NODE_BIN = $(BIN_DIR)/aether-node.exe
else
    UNAME := $(shell uname -s)
    ifeq ($(UNAME),Linux)
        LDFLAGS += -lpthread
    endif
    ifeq ($(UNAME),Darwin)
        # macOS
    endif
endif

# Debug build
ifdef DEBUG
    CFLAGS = -Wall -Wextra -g -O0 -I$(INC_DIR) -I$(LIB_DIR)
endif

.PHONY: all clean windows linux macos debug dirs

all: dirs $(NODE_BIN)

dirs:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BIN_DIR)

windows: all

linux: all

macos: all

debug:
	$(MAKE) DEBUG=1 all

# Build node binary
$(NODE_BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(LIB_DIR)/%.c $(HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(BIN_DIR)

# Install
install: all
	cp $(NODE_BIN) /usr/local/bin/

# Run tests
test: $(NODE_BIN)
	@echo "Running basic tests..."
	@$(NODE_BIN) --help
