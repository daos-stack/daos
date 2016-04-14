all: libsl_project2.so

libsl_project2.so: sl_project2.h sl_project2.c
	$(CC) -shared sl_project2.c -fPIC -o libsl_project2.so

install: libsl_project2.so
	install -D sl_project2.h $(PREFIX)/include/sl_project2.h
	install -D libsl_project2.so $(PREFIX)/lib/libsl_project2.so

clean:
	rm -f libsl_project2.so
