.PHONY:all
all: libdaos_mocks.so

libdaos_mocks.so: libdaos_mocks.go
	go build -buildmode=c-shared -o $@
