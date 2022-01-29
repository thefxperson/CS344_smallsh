CC=gcc
CFLAGS=-std=gnu99 -g
EXE=smallsh

all: main.c 
	$(CC) $(CFLAGS) main.c -o $(EXE)

#ll.o: ll.c ll.h 
#$(CC) $(CFLAGS) -c ll.c -o ll.o

#movie.o: movie.c movie.h 
#$(CC) $(CFLAGS) -c movie.c -o movie.o

clean:
	rm -rf *.o $(EXE)
