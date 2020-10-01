
BIN = test

HDRS = 

SRCS = test.c

OBJS := ${SRCS:.c=.o}

DEP     = configini
INCDIRS = ../src
LIBDIRS = ../


CPPFLAGS += $(foreach includedir,$(INCDIRS),-I$(includedir))
LDFLAGS  += $(foreach librarydir,$(LIBDIRS),-L$(librarydir))
LDFLAGS  += $(foreach library,$(DEP),-l$(library))

CFLAGS = -g -ggdb -Wall

CC = gcc

###################################################################################################

.PHONY: all $(BIN) test clean help

all: $(BIN)

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BIN): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)
	./$(BIN)

clean:
	rm -f ~core~ $(OBJS) $(BIN)

help:
	@echo "targets:"
	@echo "   all         Build all"
	@echo "   $(BIN)      Build binary: $(BIN)"
	@echo "   clean       Clean library generated files"
	