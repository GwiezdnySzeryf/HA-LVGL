# Native HA Panel Makefile for TPP01-Z (AArch64)
# Authored by OpenCode

# 1. Compiler Toolchain Configuration
TOOLCHAIN_DIR = /tmp/opencode/toolchain
CC = $(TOOLCHAIN_DIR)/bin/aarch64-none-linux-gnu-gcc
CXX = $(TOOLCHAIN_DIR)/bin/aarch64-none-linux-gnu-g++

# Static linking to prevent runtime dynamic library conflicts on the panel
CFLAGS = -O3 -Wall -Wshadow -static -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl
CXXFLAGS = $(CFLAGS) -std=c++11

# Output binary name
BIN = ha_panel

# 2. Source Files Discovery
# Automatically find all C files in LVGL source tree
CSRCS += $(shell find -L ./lvgl/src -name "*.c")

# Add your custom sources
CXXSRCS += src/hal.cpp src/ha_logo.cpp src/main.cpp

# Objects mapping
COBJS = $(CSRCS:.c=.o)
CXXOBJS = $(CXXSRCS:.cpp=.o)
OBJS = $(COBJS) $(CXXOBJS)

# 3. Compilation Rules
all: $(BIN)

$(BIN): $(OBJS)
	@echo "[LINK] $@"
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(BIN)

%.o: %.c
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	@echo "[CXX] $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(BIN)
