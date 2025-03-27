
INCLUDE = /usr/include
CFLAGS = -O2 -I$(INCLUDE)
LDLIBS = -lisal_crypto -lcrypto -lpthread
test = isal_multithread_perf

source += isal_multithread_perf.c
source += md5_thread.c \
        sha1_thread.c \
        sha256_thread.c \
        sha512_thread.c \
        aes_thread.c

ODIR = bin
objects = $(addprefix $(ODIR)/, $(patsubst %.c, %.o, $(source)))

$(test): $(objects)
	gcc   $? $(LDLIBS) -o $@

$(ODIR): ; mkdir -p $(ODIR)
$(objects): | $(ODIR)
$(ODIR)/%.o: %.c
	gcc -c  $(CFLAGS) $< -o $@

clean:
	@echo Cleaning up
	@rm -fr $(ODIR) $(test)
