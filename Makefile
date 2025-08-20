# Compiler
CC = gcc

# Compiler flags
CFLAGS = -std=c11 -Wall -pthread

# Executable name
TARGET = counter

# Source files
SRCS = counter_ver2.c

# Object files
OBJS = $(SRCS:.c=.o)

# Default target: Build the program
all: $(TARGET)

# Rule to build the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Rule to compile .c files into .o files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up the generated files
clean:
	rm -f $(OBJS) $(TARGET) delta_times.txt exit_duration.txt time_error.txt

# Phony targets
.PHONY: all clean
