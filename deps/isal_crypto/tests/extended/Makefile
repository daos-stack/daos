CFLAGS += -I ../../include
libs += ../../bin/isa-l_crypto.a
tests = $(patsubst %test.c, %test, $(wildcard *_test.c))

tests: $(tests)
$(tests): $(libs)
%test: %test.c
	$(CC) $< $(libs) $(CFLAGS) $(LDLIBS) -o $@
$(libs):
	$(MAKE) -C ../../ -f Makefile.unx
test: $(addsuffix .run,$(tests))
	@echo ALL PASS
$(addsuffix .run,$(tests)): %.run: %
	$(SIM) ./$<
	@echo Completed run: $<
clean:
	$(RM) *.o $(tests)

$(tests): LDLIBS += -lcrypto
