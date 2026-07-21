# Native HA Panel Makefile for TPP01-Z (AArch64)
# Authored by OpenCode

# 1. Compiler Toolchain Configuration
TOOLCHAIN_DIR = /tmp/opencode/toolchain
CC = $(TOOLCHAIN_DIR)/bin/aarch64-none-linux-gnu-gcc
CXX = $(TOOLCHAIN_DIR)/bin/aarch64-none-linux-gnu-g++

# Static linking to prevent runtime dynamic library conflicts on the panel
CFLAGS = -O3 -Wall -Wshadow -static -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl
CXXFLAGS = $(CFLAGS) -std=c++11
LDFLAGS = -static -s

# Output binary name
BIN = ha_panel

# 2. Source Files Discovery
# Automatically find all C files in LVGL source tree
CSRCS += $(shell find -L ./lvgl/src -name "*.c")
CSRCS += src/lv_font_montserrat_16_pl.c

# Add your custom sources
CXXSRCS += src/hal.cpp src/ha_logo.cpp src/main.cpp

# Objects mapping
COBJS = $(CSRCS:.c=.o)
CXXOBJS = $(CXXSRCS:.cpp=.o)
OBJS = $(COBJS) $(CXXOBJS)

# 3. Compilation Rules
all: $(BIN)

# Local PC Simulator target (x86_64)
# We compile C files using gcc and C++ files using g++ to prevent C++ type casting errors
pc:
	@echo "[COMPILE FOR PC SIMULATOR]"
	@for f in $(CSRCS); do \
		echo "[CC_PC] $$f"; \
		gcc -O3 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -c $$f -o $${f%.c}.o || exit 1; \
	done
	@echo "[CXX_PC] src/hal.cpp"
	g++ -O3 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c++11 -c src/hal.cpp -o src/hal.o
	@echo "[CXX_PC] src/ha_logo.cpp"
	g++ -O3 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c++11 -c src/ha_logo.cpp -o src/ha_logo.o
	@echo "[CXX_PC] src/main.cpp"
	g++ -O3 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c++11 -c src/main.cpp -o src/main.o
	@echo "[LINK_PC] ha_panel_pc"
	g++ src/hal.o src/ha_logo.o src/main.o $(COBJS) -lSDL2 -o ha_panel_pc

$(BIN): $(OBJS) Makefile
	@echo "[LINK] $@"
	$(CXX) $(LDFLAGS) $(OBJS) -o $(BIN)

%.o: %.c
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	@echo "[CXX] $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(BIN) ha_panel_pc
