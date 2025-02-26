# Compiler and flags
CC = gcc
CFLAGS = -std=c11 -g2 -ggdb -pedantic -W -Wall -Wextra 

# Directories
SRC_DIR = src
BUILD_DIR = build/linux
DEBUG_DIR = $(BUILD_DIR)/debug
RELEASE_DIR = $(BUILD_DIR)/release

# Output directory
OUT_DIR = $(DEBUG_DIR)

ifeq ($(MODE), release)
  CFLAGS = -std=c11 -pedantic -W -Wall -Wextra -Werror
  OUT_DIR = $(RELEASE_DIR)
endif

# Source and object files
SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(patsubst $(SRC_DIR)/%.c,$(OUT_DIR)/%.o,$(SRC))

# Program name
PROG = $(OUT_DIR)/test

# Default target
all: $(OUT_DIR) $(PROG)

# Create output directory
$(OUT_DIR):
	mkdir -p $(OUT_DIR)

# Link object files to create the executable
$(PROG): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@

# Compile source files into object files
$(OUT_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean 
clean:
	@rm -rf $(BUILD_DIR)/* test

