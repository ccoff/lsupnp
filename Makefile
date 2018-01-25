# lsupnp Makefile 

CC = gcc
CFLAGS = -g -O0 -Wall -std=gnu99
LDFLAGS = -g

LIBSRC = utils.c
LIBOBJ = $(LIBSRC:%.c=%.o)

all: lsupnp

lsupnp: lsupnp.o $(LIBOBJ)
	$(CC) $(LDFLAGS) $(LIBOBJ) lsupnp.o -o lsupnp

clean:
	rm -f $(LIBOBJ) lsupnp.o lsupnp

.PHONY: clean

