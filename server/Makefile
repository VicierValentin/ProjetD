# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2

# Target executable name
TARGET = server

# Source files
SRC = server.c

# Default target
all: $(TARGET)

# Link the executable
$(TARGET): $(SRC)
	$(CC)   $(CFLAGS)   -o    $(TARGET)  $(SRC)

# Clean up build files
clean:
	rm -f $(TARGET)
