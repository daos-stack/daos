all: libsl_project3.so

libsl_project3.so: sl_project3.h sl_project3.c
	$(CC) -shared sl_project3.c -fPIC -o libsl_project3.so \
	-L$(SL_PROJECT2)/lib -I$(SL_PROJECT2)/include \
	-lsl_project2

install: libsl_project3.so
	install -D sl_project3.h $(PREFIX)/include/sl_project3.h
	install -D libsl_project3.so $(PREFIX)/lib/libsl_project3.so

clean:
	rm -f libsl_project3.so
