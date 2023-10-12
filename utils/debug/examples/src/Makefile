CFLAGS=-g

malloc_overflow: malloc_overflow.c
	${CC} ${CFLAGS} -o $@ $<

.PHONY: clean
clean:
	/bin/rm -f malloc_overflow core
