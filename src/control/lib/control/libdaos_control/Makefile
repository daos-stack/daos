run: test
	LD_LIBRARY_PATH=$(LD_LIBRARY_PATH):$(CURDIR) ./test

libdaos_control.so: *.go ../*.go
	go build -buildmode=c-shared -o $@ *.go

test: test.c libdaos_control.so
	gcc -o test test.c $(CFLAGS) $(LDFLAGS) -luuid -ldaos -lgurt -L. -ldaos_control
