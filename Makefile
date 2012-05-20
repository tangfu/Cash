CC=gcc
CFLAGS= -g -Wall -pedantic -O2 -march=athlon64 -lreadline -std=gnu99
DEP = include.h cash.h
OBJ = cash.o built_ins.o

cash:
	${CC} cash.c built_ins.c job_control.c -o cash ${CFLAGS}

clean:
	rm -f cash
