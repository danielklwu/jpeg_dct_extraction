# Compiler settings
CC = gcc
CFLAGS = -I/opt/homebrew/include -L/opt/homebrew/lib -ljpeg
TARGET = main
SRC = main.c

# Build the executable
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS)

# Run the program 
run: $(TARGET)
	./$(TARGET) input.jpg output.txt

# Clean up
clean:
	rm -f $(TARGET) output.txt

.PHONY: all run clean