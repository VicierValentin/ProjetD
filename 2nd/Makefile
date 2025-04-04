# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2 -lpthread -ljpeg -DIO_READ -DIO_MMAP -DIO_USERPTR

# Target executable
TARGET = application

# Source and header files
SRC = example.c client.c
HEADERS = client.h

# Object files (automatically generated from the source files)
OBJ = $(SRC:.c=.o)

# Default target to build the application
all: $(TARGET)

# Link the object files into the final executable
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

# Rule to compile each .c file into a .o file
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build artifacts
clean:
	rm -f $(OBJ) $(TARGET)
