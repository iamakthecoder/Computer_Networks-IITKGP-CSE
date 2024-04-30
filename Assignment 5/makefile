CC = gcc
CFLAGS = -Wall
LIBS = -L. -lmsocket

all: libmsocket.a initmsocket user1 user2

run_initmsocket: initmsocket
	./initmsocket

run_user1: user1
	./user1

run_user2: user2
	./user2

initmsocket: initmsocket.c libmsocket.a
	$(CC) $(CFLAGS) -o $@ initmsocket.c $(LIBS)

user1: user1.c libmsocket.a
	$(CC) $(CFLAGS) -o $@ user1.c $(LIBS)

user2: user2.c libmsocket.a
	$(CC) $(CFLAGS) -o $@ user2.c $(LIBS)

libmsocket.a: msocket.o
	ar rcs $@ msocket.o

msocket.o: msocket.c msocket.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o *.a initmsocket user1 user2
	rm -f *.*.*.*_*.txt
