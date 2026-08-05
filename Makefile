# Native HA Panel Makefile for TPP01-Z (AArch64)
# Authored by OpenCode

# 1. Compiler Toolchain Configuration
TOOLCHAIN_DIR = /tmp/opencode/toolchain
CC = $(TOOLCHAIN_DIR)/bin/aarch64-none-linux-gnu-gcc
CXX = $(TOOLCHAIN_DIR)/bin/aarch64-none-linux-gnu-g++

# Static linking to prevent runtime dynamic library conflicts on the panel
CFLAGS = -O3 -Wall -Wshadow -static -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -Imbedtls/include
CXXFLAGS = $(CFLAGS) -std=c++11
LDFLAGS = -static -s
MBEDTLS_LIBS = mbedtls/library/libmbedtls.a mbedtls/library/libmbedx509.a mbedtls/library/libmbedcrypto.a

# Output binary name
BIN = ha_panel

# 2. Source Files Discovery
# Automatically find all C files in LVGL source tree
CSRCS += $(shell find -L ./lvgl/src -name "*.c")
CSRCS += src/lv_font_montserrat_12_pl.c
CSRCS += src/lv_font_montserrat_14_pl.c
CSRCS += src/lv_font_montserrat_16_pl.c
CSRCS += src/lv_font_montserrat_20_pl.c
CSRCS += src/lv_font_montserrat_24_pl.c
CSRCS += src/lv_font_control_icons_24.c

# Add your custom sources
CXXSRCS += src/hal.cpp src/ha_logo.cpp src/mqtt_client.cpp src/assist_audio.cpp src/assist_pipeline_bridge.cpp src/wake_word_listener.cpp src/main.cpp

# Objects mapping
COBJS = $(CSRCS:.c=.o)
CXXOBJS = $(CXXSRCS:.cpp=.o)
OBJS = $(COBJS) $(CXXOBJS)

# Isolated x86 build used for reviewing individual LVGL screens locally.
PORTAL_PC_BUILD = /tmp/opencode/tpp01_portal_pc
PORTAL_PC_COBJS = $(addprefix $(PORTAL_PC_BUILD)/,$(CSRCS:.c=.o))
PORTAL_PC_HAL_OBJ = $(PORTAL_PC_BUILD)/src/hal.o
PORTAL_PC_APP_OBJ = $(PORTAL_PC_BUILD)/prototypes/lvgl_portal_www_b.o
PORTAL_PC_BIN = /tmp/opencode/tpp01_portal_www_b
SETTINGS_PC_BUILD = /tmp/opencode/tpp01_settings_pc
SETTINGS_PC_COBJS = $(addprefix $(SETTINGS_PC_BUILD)/,$(CSRCS:.c=.o))
SETTINGS_PC_HAL_OBJ = $(SETTINGS_PC_BUILD)/src/hal.o
SETTINGS_PC_APP_OBJ = $(SETTINGS_PC_BUILD)/prototypes/lvgl_settings_m3.o
SETTINGS_PC_BIN = /tmp/opencode/tpp01_settings_m3
WIFI_PC_BUILD = /tmp/opencode/tpp01_wifi_pc
WIFI_PC_COBJS = $(addprefix $(WIFI_PC_BUILD)/,$(CSRCS:.c=.o))
WIFI_PC_HAL_OBJ = $(WIFI_PC_BUILD)/src/hal.o
WIFI_PC_APP_OBJ = $(WIFI_PC_BUILD)/prototypes/lvgl_wifi_m3.o
WIFI_PC_BIN = /tmp/opencode/tpp01_wifi_m3
SOUND_PC_BUILD = /tmp/opencode/tpp01_sound_pc
SOUND_PC_COBJS = $(addprefix $(SOUND_PC_BUILD)/,$(CSRCS:.c=.o))
SOUND_PC_HAL_OBJ = $(SOUND_PC_BUILD)/src/hal.o
SOUND_PC_APP_OBJ = $(SOUND_PC_BUILD)/prototypes/lvgl_sound_m3.o
SOUND_PC_BIN = /tmp/opencode/tpp01_sound_m3
ASSIST_EDGE_PC_APP_OBJ = $(SOUND_PC_BUILD)/prototypes/lvgl_assist_edge_variants.o
ASSIST_EDGE_PC_BIN = /tmp/opencode/tpp01_assist_edge_variants
MIC_PC_APP_OBJ = $(SOUND_PC_BUILD)/prototypes/lvgl_microphone_m3.o
MIC_PC_BIN = /tmp/opencode/tpp01_microphone_m3

# 3. Compilation Rules
all: $(BIN)

# Local PC Simulator target (x86_64)
# We compile C files using gcc and C++ files using g++ to prevent C++ type casting errors
pc:
	@echo "[COMPILE FOR PC SIMULATOR]"
	@for f in $(CSRCS); do \
		echo "[CC_PC] $$f"; \
		gcc -O3 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -Imbedtls/include -c $$f -o $${f%.c}.o || exit 1; \
	done
	@echo "[CXX_PC] src/hal.cpp"
	g++ -O3 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -Imbedtls/include -std=c++11 -c src/hal.cpp -o src/hal.o
	@echo "[CXX_PC] src/ha_logo.cpp"
	g++ -O3 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -Imbedtls/include -std=c++11 -c src/ha_logo.cpp -o src/ha_logo.o
	@echo "[CXX_PC] src/mqtt_client.cpp"
	g++ -O3 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -Imbedtls/include -std=c++11 -c src/mqtt_client.cpp -o src/mqtt_client.o
	@echo "[CXX_PC] src/main.cpp"
	g++ -O3 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -Imbedtls/include -std=c++11 -c src/main.cpp -o src/main.o
	@echo "[LINK_PC] ha_panel_pc"
	g++ src/hal.o src/ha_logo.o src/mqtt_client.o src/main.o $(COBJS) $(MBEDTLS_LIBS) -lSDL2 -lpthread -o ha_panel_pc

portal-preview: $(PORTAL_PC_COBJS) $(PORTAL_PC_HAL_OBJ) $(PORTAL_PC_APP_OBJ)
	@echo "[LINK_PC] $(PORTAL_PC_BIN)"
	g++ $(PORTAL_PC_HAL_OBJ) $(PORTAL_PC_APP_OBJ) $(PORTAL_PC_COBJS) -lSDL2 -lpthread -o $(PORTAL_PC_BIN)

settings-preview: $(SETTINGS_PC_COBJS) $(SETTINGS_PC_HAL_OBJ) $(SETTINGS_PC_APP_OBJ)
	@echo "[LINK_PC] $(SETTINGS_PC_BIN)"
	g++ $(SETTINGS_PC_HAL_OBJ) $(SETTINGS_PC_APP_OBJ) $(SETTINGS_PC_COBJS) -lSDL2 -lpthread -o $(SETTINGS_PC_BIN)

wifi-preview: $(WIFI_PC_COBJS) $(WIFI_PC_HAL_OBJ) $(WIFI_PC_APP_OBJ)
	@echo "[LINK_PC] $(WIFI_PC_BIN)"
	g++ $(WIFI_PC_HAL_OBJ) $(WIFI_PC_APP_OBJ) $(WIFI_PC_COBJS) -lSDL2 -lpthread -o $(WIFI_PC_BIN)

sound-preview: $(SOUND_PC_COBJS) $(SOUND_PC_HAL_OBJ) $(SOUND_PC_APP_OBJ)
	@echo "[LINK_PC] $(SOUND_PC_BIN)"
	g++ $(SOUND_PC_HAL_OBJ) $(SOUND_PC_APP_OBJ) $(SOUND_PC_COBJS) -lSDL2 -lpthread -o $(SOUND_PC_BIN)

assist-edge-preview: $(SOUND_PC_COBJS) $(SOUND_PC_HAL_OBJ) $(ASSIST_EDGE_PC_APP_OBJ)
	@echo "[LINK_PC] $(ASSIST_EDGE_PC_BIN)"
	g++ $(SOUND_PC_HAL_OBJ) $(ASSIST_EDGE_PC_APP_OBJ) $(SOUND_PC_COBJS) -lSDL2 -lpthread -o $(ASSIST_EDGE_PC_BIN)

microphone-preview: $(SOUND_PC_COBJS) $(SOUND_PC_HAL_OBJ) $(MIC_PC_APP_OBJ)
	@echo "[LINK_PC] $(MIC_PC_BIN)"
	g++ $(SOUND_PC_HAL_OBJ) $(MIC_PC_APP_OBJ) $(SOUND_PC_COBJS) -lSDL2 -lpthread -o $(MIC_PC_BIN)

$(SOUND_PC_BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "[CC_PC] $<"
	gcc -O2 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c11 -c $< -o $@

$(SOUND_PC_HAL_OBJ): src/hal.cpp
	@mkdir -p $(dir $@)
	@echo "[CXX_PC] $<"
	g++ -O2 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c++11 -c $< -o $@

$(SOUND_PC_APP_OBJ): prototypes/lvgl_sound_m3.cpp
	@mkdir -p $(dir $@)
	@echo "[CXX_PC] $<"
	g++ -O2 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c++11 -c $< -o $@

$(ASSIST_EDGE_PC_APP_OBJ): prototypes/lvgl_assist_edge_variants.cpp
	@mkdir -p $(dir $@)
	@echo "[CXX_PC] $<"
	g++ -O2 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c++11 -c $< -o $@

$(MIC_PC_APP_OBJ): prototypes/lvgl_microphone_m3.cpp
	@mkdir -p $(dir $@)
	@echo "[CXX_PC] $<"
	g++ -O2 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c++11 -c $< -o $@

$(PORTAL_PC_BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "[CC_PC] $<"
	gcc -O2 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c11 -c $< -o $@

$(PORTAL_PC_HAL_OBJ): src/hal.cpp
	@mkdir -p $(dir $@)
	@echo "[CXX_PC] $<"
	g++ -O2 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c++11 -c $< -o $@

$(PORTAL_PC_APP_OBJ): prototypes/lvgl_portal_www_b.cpp
	@mkdir -p $(dir $@)
	@echo "[CXX_PC] $<"
	g++ -O2 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c++11 -c $< -o $@

$(SETTINGS_PC_BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "[CC_PC] $<"
	gcc -O2 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c11 -c $< -o $@

$(SETTINGS_PC_HAL_OBJ): src/hal.cpp
	@mkdir -p $(dir $@)
	@echo "[CXX_PC] $<"
	g++ -O2 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c++11 -c $< -o $@

$(SETTINGS_PC_APP_OBJ): prototypes/lvgl_settings_m3.cpp
	@mkdir -p $(dir $@)
	@echo "[CXX_PC] $<"
	g++ -O2 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c++11 -c $< -o $@

$(WIFI_PC_BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "[CC_PC] $<"
	gcc -O2 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c11 -c $< -o $@

$(WIFI_PC_HAL_OBJ): src/hal.cpp
	@mkdir -p $(dir $@)
	@echo "[CXX_PC] $<"
	g++ -O2 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c++11 -c $< -o $@

$(WIFI_PC_APP_OBJ): prototypes/lvgl_wifi_m3.cpp
	@mkdir -p $(dir $@)
	@echo "[CXX_PC] $<"
	g++ -O2 -Wall -Wshadow -DPC_SIMULATOR -DLV_CONF_INCLUDE_SIMPLE -I. -I./lvgl -std=c++11 -c $< -o $@

$(BIN): $(OBJS) Makefile
	@echo "[LINK] $@"
	$(CXX) $(LDFLAGS) $(OBJS) $(MBEDTLS_LIBS) -lpthread -o $(BIN)

%.o: %.c
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	@echo "[CXX] $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(BIN) ha_panel_pc
