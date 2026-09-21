NAME      := studystudio-0.0.10
SRC_DIR   := src
INCLUDE_DIR := include
BUILD_DIR := build
RESOURCE_SRC := $(BUILD_DIR)/resources.c

# ==========================================
# 1. Linux compilation
# ==========================================
CC_LINUX  := gcc
GTK_CFLAGS_LINUX := $(shell PKG_CONFIG_PATH="" pkgconf --cflags gtk4)
GTK_LIBS_LINUX   := $(shell PKG_CONFIG_PATH="" pkgconf --libs gtk4)

INCLUDES := -I$(INCLUDE_DIR)

CFLAGS_LINUX  := -Wall -Wextra -O2 $(INCLUDES) $(GTK_CFLAGS_LINUX)
LDFLAGS_LINUX := $(GTK_LIBS_LINUX) -larchive -llzma

# --- Debug variant (unoptimized, with symbols for GDB) ---
CFLAGS_LINUX_DEBUG := -Wall -Wextra -g -O0 -DDEBUG $(INCLUDES) $(GTK_CFLAGS_LINUX)

# ==========================================
# 2. Windows compilation (MinGW-w64)
# ==========================================
CC_WIN64  := x86_64-w64-mingw32-gcc
MINGW_PKG_PATH := /usr/x86_64-w64-mingw32/sys-root/mingw/lib/pkgconfig

GTK_CFLAGS_WIN := $(shell PKG_CONFIG_PATH=$(MINGW_PKG_PATH) pkgconf --cflags gtk4)
GTK_LIBS_WIN   := $(shell PKG_CONFIG_PATH=$(MINGW_PKG_PATH) pkgconf --libs gtk4)

CFLAGS_WIN  := -Wall -Wextra -O2 $(INCLUDES) $(GTK_CFLAGS_WIN)
LDFLAGS_WIN := $(GTK_LIBS_WIN) -larchive -llzma -mwindows -static-libgcc

# ==========================================
# Source files
# ==========================================
SRCS := $(wildcard $(SRC_DIR)/*.c)
HEADERS := $(wildcard $(INCLUDE_DIR)/.h)

OBJS_LINUX := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/linux/%.o) $(BUILD_DIR)/linux/resources.o
OBJS_WIN64 := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/win64/%.o) $(BUILD_DIR)/win64/resources.o
OBJS_LINUX_DEBUG := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/linux-debug/%.o) $(BUILD_DIR)/linux-debug/resources.o

# ==========================================
# Rules
# ==========================================
.PHONY: all linux win64 debug gdb clean setup

all: linux win64

setup:
	@mkdir -p $(BUILD_DIR)/linux $(BUILD_DIR)/win64 $(BUILD_DIR)/linux-debug

# --- Generate resources.c ---
$(RESOURCE_SRC): resources.xml interface.ui | setup
	glib-compile-resources --generate-source --target=$@ $<

# --- Linux ---
linux: $(BUILD_DIR)/$(NAME)_linux

$(BUILD_DIR)/$(NAME)_linux: $(OBJS_LINUX)
	$(CC_LINUX) $(OBJS_LINUX) -o $@ $(LDFLAGS_LINUX)
	@echo "[✓] Compilado para Linux: $@"

$(BUILD_DIR)/linux/%.o: $(SRC_DIR)/%.c | setup
	$(CC_LINUX) $(CFLAGS_LINUX) -c $< -o $@

$(BUILD_DIR)/linux/resources.o: $(RESOURCE_SRC) | setup
	$(CC_LINUX) $(CFLAGS_LINUX) -c $< -o $@

# --- Linux (debug, for GDB) ---
debug: $(BUILD_DIR)/$(NAME)_linux_debug

$(BUILD_DIR)/$(NAME)_linux_debug: $(OBJS_LINUX_DEBUG)
	$(CC_LINUX) $(OBJS_LINUX_DEBUG) -o $@ $(LDFLAGS_LINUX)
	@echo "[✓] Compilado para Linux (debug): $@"

$(BUILD_DIR)/linux-debug/%.o: $(SRC_DIR)/%.c | setup
	$(CC_LINUX) $(CFLAGS_LINUX_DEBUG) -c $< -o $@

$(BUILD_DIR)/linux-debug/resources.o: $(RESOURCE_SRC) | setup
	$(CC_LINUX) $(CFLAGS_LINUX_DEBUG) -c $< -o $@

gdb: debug
	gdb $(BUILD_DIR)/$(NAME)_linux_debug

# --- Windows ---
win64: $(BUILD_DIR)/$(NAME)_win64.exe

$(BUILD_DIR)/$(NAME)_win64.exe: $(OBJS_WIN64)
	$(CC_WIN64) $(OBJS_WIN64) -o $@ $(LDFLAGS_WIN)
	@echo "[✓] Compilado para Windows: $@"

$(BUILD_DIR)/win64/%.o: $(SRC_DIR)/%.c | setup
	$(CC_WIN64) $(CFLAGS_WIN) -c $< -o $@

$(BUILD_DIR)/win64/resources.o: $(RESOURCE_SRC) | setup
	$(CC_WIN64) $(CFLAGS_WIN) -c $< -o $@

# --- Cleanup ---
clean:
	rm -rf $(BUILD_DIR) resources.c
	@echo "[✓] Carpeta build/ y resources.c eliminados."
