# Define the compiler and flags
CC = gcc
CFLAGS = -Wall -g

# Define the target executable
TARGET = hello

# List of source files
SRCS = main.c

# Default target
all: $(TARGET)

# Rule to build the target executable
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

# Rule to clean up build artifacts
clean:
	rm -f $(TARGET)
