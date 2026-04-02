# A.E.T.H.E.R. P2P Protocol Makefile
# Builds C and C++ code with GCC/Clang/MinGW

CXX = g++
CC = gcc
AR = ar

# Directories
SRC_DIR = src
INC_DIR = include
LIB_DIR = lib
BUILD_DIR = build
BIN_DIR = bin

# External libraries (pkg-config)
PKG_CONFIG = pkg-config
SODIUM_CFLAGS := $(shell $(PKG_CONFIG) --cflags libsodium 2>/dev/null || echo "-I/usr/include")
SODIUM_LIBS := $(shell $(PKG_CONFIG) --libs libsodium 2>/dev/null || echo "-lsodium")
LMDB_CFLAGS := $(shell $(PKG_CONFIG) --cflags lmdb 2>/dev/null || echo "-I/usr/include")
LMDB_LIBS := $(shell $(PKG_CONFIG) --libs lmdb 2>/dev/null || echo "-llmdb")

# IPFS module source files
IPFS_SRCS = $(SRC_DIR)/ipfs/chunk.c \
            $(SRC_DIR)/ipfs/storage.c \
            $(SRC_DIR)/ipfs/network.c \
            $(SRC_DIR)/ipfs/http.c \
            $(SRC_DIR)/ipfs/node.c \
            $(SRC_DIR)/ipfs/stun.c

# C++ source files
CXX_SRCS = $(SRC_DIR)/aether.cpp \
           $(SRC_DIR)/crypto.cpp \
           $(SRC_DIR)/dht.cpp \
           $(SRC_DIR)/peer.cpp \
           $(SRC_DIR)/protocol.cpp

# C source files (excluding those replaced by C++)
C_SRCS = $(SRC_DIR)/crypto.c \
         $(SRC_DIR)/framing.c \
         $(SRC_DIR)/handshake.c \
         $(SRC_DIR)/message.c \
         $(SRC_DIR)/util.c \
         $(LIB_DIR)/ed25519/ed25519.c \
         $(LIB_DIR)/sha256/sha256.c

# Object files
CXX_OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(CXX_SRCS))
C_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/c_%.o,$(filter-out $(SRC_DIR)/node.c,$(C_SRCS)))
C_OBJS := $(patsubst $(LIB_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_OBJS))
IPFS_OBJS = $(patsubst $(SRC_DIR)/ipfs/%.c,$(BUILD_DIR)/ipfs_%.o,$(IPFS_SRCS))
OBJS = $(CXX_OBJS) $(C_OBJS)

# Headers
CXX_HEADERS = $(INC_DIR)/aether.hpp \
              $(INC_DIR)/crypto.hpp \
              $(INC_DIR)/dht.hpp \
              $(INC_DIR)/peer.hpp \
              $(INC_DIR)/protocol.hpp

C_HEADERS = $(INC_DIR)/aether.h \
            $(INC_DIR)/crypto.h \
            $(INC_DIR)/framing.h \
            $(INC_DIR)/dht.h \
            $(INC_DIR)/peer.h \
            $(INC_DIR)/handshake.h \
            $(INC_DIR)/message.h \
            $(INC_DIR)/protocol.h \
            $(INC_DIR)/util.h

IPFS_HEADERS = $(INC_DIR)/ipfs/chunk.h \
               $(INC_DIR)/ipfs/storage.h \
               $(INC_DIR)/ipfs/network.h \
               $(INC_DIR)/ipfs/http.h \
               $(INC_DIR)/ipfs/node.h \
               $(INC_DIR)/ipfs/stun.h \
               $(LIB_DIR)/mongoose.h

# Output
LIBRARY = $(BIN_DIR)/libaether.a
NODE_BIN = $(BIN_DIR)/aether-node
IPFS_BIN = $(BIN_DIR)/ipfs-node

# Flags
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -I$(INC_DIR) -I$(LIB_DIR)
CFLAGS = -Wall -Wextra -O2 -I$(INC_DIR) -I$(LIB_DIR) $(SODIUM_CFLAGS) $(LMDB_CFLAGS)
LDFLAGS = $(SODIUM_LIBS) $(LMDB_LIBS) -lpthread

# Platform-specific
ifeq ($(OS),Windows_NT)
    CXXFLAGS += -D_WIN32_WINNT=0x0600
    CFLAGS += -D_WIN32_WINNT=0x0600
    LDFLAGS += -lws2_32
    NODE_BIN = $(BIN_DIR)/aether-node.exe
else
    # Cross-compilation for Windows (MinGW)
    ifeq ($(CROSS_COMPILE),mingw)
        CXX = x86_64-w64-mingw32-g++
        CC = x86_64-w64-mingw32-gcc
        AR = x86_64-w64-mingw32-ar
        CXXFLAGS += -D_WIN32_WINNT=0x0600 -static -static-libgcc -static-libstdc++
        CFLAGS += -D_WIN32_WINNT=0x0600
        LDFLAGS += -lws2_32 -static -static-libgcc -static-libstdc++
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
endif

# Debug build
ifdef DEBUG
    CXXFLAGS = -std=c++17 -Wall -Wextra -g -O0 -I$(INC_DIR) -I$(LIB_DIR)
    CFLAGS = -Wall -Wextra -g -O0 -I$(INC_DIR) -I$(LIB_DIR)
endif

# Release build with optimizations
ifdef RELEASE
    CXXFLAGS += -O3 -DNDEBUG
    CFLAGS += -O3 -DNDEBUG
endif

.PHONY: all clean windows cross-windows linux macos debug release dirs cpp c lib test install ipfs

all: dirs cpp $(NODE_BIN) ipfs

dirs:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BIN_DIR)

# Build C++ components
cpp: dirs $(CXX_OBJS)
	@echo "C++ objects built successfully"

# Build C components
c: dirs $(C_OBJS)
	@echo "C objects built successfully"

# Build static library
lib: dirs $(OBJS)
	$(AR) rcs $(LIBRARY) $(OBJS)
	@echo "Static library built: $(LIBRARY)"

# Windows build (native or cross-compile)
windows:
ifeq ($(OS),Windows_NT)
	@$(MAKE) all
else
	@$(MAKE) CROSS_COMPILE=mingw all
endif

# Explicit cross-compilation for Windows from Linux
cross-windows: clean
	@echo "Cross-compiling for Windows x64..."
	@$(MAKE) CROSS_COMPILE=mingw all
	@echo "Windows binary built: $(BIN_DIR)/aether-node.exe"

linux: all

macos: all

debug:
	$(MAKE) DEBUG=1 all

release:
	$(MAKE) RELEASE=1 all

# Build node binary
$(NODE_BIN): $(OBJS) $(SRC_DIR)/main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRC_DIR)/main.cpp $(CXX_OBJS) $(C_OBJS) $(LDFLAGS)
	@echo "Node binary built: $(NODE_BIN)"

# Build IPFS node binary
$(IPFS_BIN): $(IPFS_OBJS) $(SRC_DIR)/ipfs/main.c $(BUILD_DIR)/mongoose.o
	$(CC) $(CFLAGS) -o $@ $(SRC_DIR)/ipfs/main.c $(IPFS_OBJS) $(BUILD_DIR)/mongoose.o $(LDFLAGS)
	@echo "IPFS node binary built: $(IPFS_BIN)"

# Build IPFS module
ipfs: dirs $(IPFS_OBJS) $(BUILD_DIR)/mongoose.o $(IPFS_BIN)
	@echo "IPFS module built successfully"

# Compile mongoose
$(BUILD_DIR)/mongoose.o: $(LIB_DIR)/mongoose.c $(LIB_DIR)/mongoose.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile C++ source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(CXX_HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Compile C source files
$(BUILD_DIR)/c_%.o: $(SRC_DIR)/%.c $(C_HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile IPFS source files
$(BUILD_DIR)/ipfs_%.o: $(SRC_DIR)/ipfs/%.c $(IPFS_HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(LIB_DIR) -c -o $@ $<

$(BUILD_DIR)/%.o: $(LIB_DIR)/%.c
	@mkdir -p $(BUILD_DIR)/ed25519
	@mkdir -p $(BUILD_DIR)/sha256
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(BIN_DIR)
	@echo "Clean complete"

# Install
install: all
	cp $(NODE_BIN) /usr/local/bin/
	@echo "Installation complete"

# Run tests
test: $(NODE_BIN)
	@echo "Running basic tests..."
	@$(NODE_BIN) --help
	@echo ""
	@echo "All tests passed!"

# Format code
format:
	@echo "Formatting C++ code..."
	-clang-format -i $(SRC_DIR)/*.cpp $(INC_DIR)/*.hpp 2>/dev/null || true
	@echo "Formatting complete"

# Lint
lint:
	@echo "Running linter..."
	-cppcheck --enable=all --std=c++17 $(SRC_DIR)/*.cpp $(INC_DIR)/*.hpp 2>/dev/null || true
	@echo "Lint complete"

# Help
help:
	@echo "A.E.T.H.E.R. P2P Protocol Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build everything (default)"
	@echo "  cpp       - Build C++ components"
	@echo "  c         - Build C components"
	@echo "  ipfs      - Build IPFS module and node"
	@echo "  lib       - Build static library"
	@echo "  debug     - Build with debug symbols"
	@echo "  release   - Build with optimizations"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install to /usr/local"
	@echo "  test      - Run basic tests"
	@echo "  format    - Format code"
	@echo "  lint      - Run linter"
	@echo ""
	@echo "Variables:"
	@echo "  DEBUG=1   - Enable debug build"
	@echo "  RELEASE=1 - Enable release build"
