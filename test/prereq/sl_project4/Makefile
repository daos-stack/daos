all: libsl_project4.so

libsl_project4.so: sl_project4.h sl_project4.c
	$(CC) -shared sl_project4.c -fPIC -o libsl_project4.so \
	-L$(SL_PROJECT2)/lib -I$(SL_PROJECT2)/include \
	-lsl_project2 -I$(SL_PROJECT1)/include -DSL_PROJECT1_REQUIRED_DEFINE

install: libsl_project4.so
	install -D sl_project4.h $(PREFIX)/include/sl_project4.h
	install -D libsl_project4.so $(PREFIX)/lib/libsl_project4.so

clean:
	rm -f libsl_project4.so
