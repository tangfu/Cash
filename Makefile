CC=gcc
CFLAGS= -g -Wall -pedantic -O2 -march=athlon64 -lreadline -std=gnu99
DEP = include.h cash.h
OBJ = cash.o built_ins.o

cash: ${OBJ}
	${CC} ${CFLAGS} cash.c built_ins.c -o cash
clean:
	rm cash 
	rm *.o
