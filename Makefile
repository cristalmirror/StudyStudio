NAME      := studystudio-0.0.2
SRC_DIR   := src
BUILD_DIR := build

# ==========================================
# 1. Compilación para Linux
# ==========================================
CC_LINUX  := gcc
# Forzamos PKG_CONFIG_PATH vacío para que use las rutas por defecto de Linux
GTK_CFLAGS_LINUX := $(shell PKG_CONFIG_PATH="" pkgconf --cflags gtk4)
GTK_LIBS_LINUX   := $(shell PKG_CONFIG_PATH="" pkgconf --libs gtk4)

CFLAGS_LINUX  := -Wall -Wextra -O2 $(GTK_CFLAGS_LINUX)
LDFLAGS_LINUX := $(GTK_LIBS_LINUX)

# ==========================================
# 2. Compilación para Windows (MinGW-w64)
# ==========================================
CC_WIN64  := x86_64-w64-mingw32-gcc
MINGW_PKG_PATH := /usr/x86_64-w64-mingw32/sys-root/mingw/lib/pkgconfig

# Aquí SÍ usamos la ruta de MinGW, pero solo para esta variable
GTK_CFLAGS_WIN := $(shell PKG_CONFIG_PATH=$(MINGW_PKG_PATH) pkgconf --cflags gtk4)
GTK_LIBS_WIN   := $(shell PKG_CONFIG_PATH=$(MINGW_PKG_PATH) pkgconf --libs gtk4)

CFLAGS_WIN  := -Wall -Wextra -O2 $(GTK_CFLAGS_WIN)
LDFLAGS_WIN := $(GTK_LIBS_WIN) -mwindows -static-libgcc

# ==========================================
# Archivos fuente
# ==========================================
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS_LINUX := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/linux/%.o)
OBJS_WIN64 := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/win64/%.o)

# ==========================================
# Reglas
# ==========================================
.PHONY: all linux win64 clean setup

all: linux win64

setup:
	@mkdir -p $(BUILD_DIR)/linux $(BUILD_DIR)/win64

# --- Linux ---
linux: setup $(BUILD_DIR)/$(NAME)_linux

$(BUILD_DIR)/$(NAME)_linux: $(OBJS_LINUX)
	$(CC_LINUX) $(OBJS_LINUX) -o $@ $(LDFLAGS_LINUX)
	@echo "[✓] Compilado para Linux: $@"

$(BUILD_DIR)/linux/%.o: $(SRC_DIR)/%.c
	$(CC_LINUX) $(CFLAGS_LINUX) -c $< -o $@

# --- Windows ---
win64: setup $(BUILD_DIR)/$(NAME)_win64.exe

$(BUILD_DIR)/$(NAME)_win64.exe: $(OBJS_WIN64)
	$(CC_WIN64) $(OBJS_WIN64) -o $@ $(LDFLAGS_WIN)
	@echo "[✓] Compilado para Windows: $@"

$(BUILD_DIR)/win64/%.o: $(SRC_DIR)/%.c
	$(CC_WIN64) $(CFLAGS_WIN) -c $< -o $@

# --- Limpieza ---
clean:
	rm -rf $(BUILD_DIR)
	@echo "[✓] Carpeta build/ eliminada."
