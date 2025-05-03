CC = gcc
CFLAGS = -Wall -Wextra -g -std=c99
TARGET = qemu-config

all: $(TARGET)

$(TARGET): qemu-config.c
	$(CC) $(CFLAGS) qemu-config.c -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
