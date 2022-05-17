SRC	=	mem_wrapper.c
CC	?=	gcc
CFLAGS	=	-fPIC -shared
LIBS	=	-ldl

all: mem_wrapper.so

mem_wrapper.so:	$(SRC)
	$(CC) $(CFLAGS) $(LIBS) -o $@ $^

.PHONY: clean

clean:
	rm mem_wrapper.so
