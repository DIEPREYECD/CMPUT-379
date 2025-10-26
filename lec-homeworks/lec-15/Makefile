# Makefile for merge sort with pthreads

CC = gcc
CFLAGS = -Wall -pthread
TARGET = mergesort

all: $(TARGET)

$(TARGET): mergesort.o
	$(CC) -o $(TARGET) mergesort.o $(CFLAGS)

mergesort.o: mergesort.c
	$(CC) -c mergesort.c $(CFLAGS)

clean:
	rm -f $(TARGET) mergesort.o
