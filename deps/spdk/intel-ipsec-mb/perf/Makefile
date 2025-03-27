# Copyright (c) 2017-2021, Intel Corporation
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
#     * Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of Intel Corporation nor the names of its contributors
#       may be used to endorse or promote products derived from this software
#       without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

EXE=ipsec_perf
INSTPATH ?= /usr/include/intel-ipsec-mb.h
LIB_DIR ?= ../lib
NASM ?= nasm

MINGW ?= $(shell $(CC) -dM -E - < /dev/null | grep -i mingw | wc -l)

CFLAGS = -D_GNU_SOURCE -DNO_COMPAT_IMB_API_053 $(INCLUDES) \
	-W -Wall -Wextra -Wmissing-declarations -Wpointer-arith \
	-Wcast-qual -Wundef -Wwrite-strings  \
	-Wformat -Wformat-security \
	-Wunreachable-code -Wmissing-noreturn -Wsign-compare -Wno-endif-labels \
	-Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition \
	-pthread -fno-strict-overflow -fno-delete-null-pointer-checks -fwrapv

ifeq ($(MINGW),0)
CFLAGS += -DLINUX
NASM_FLAGS := -Werror -felf64 -Xgnu -gdwarf -DLINUX -D__linux__
else
NASM_FLAGS := -Werror -fwin64 -Xvc -gcv8 -DWIN_ABI
endif

# if "-z ibt" is supported then assume "-z shstk, -z cet-report=error" are also supported
# "-fcf-protection" needs to be checked separately
ifeq ($(MINGW),0)
CC_HAS_CET = $(and $(shell $(CC) --target-help 2> /dev/null | grep -m1 -e "-z ibt" | wc -l), \
	$(shell $(CC) --help=common 2> /dev/null | grep -m1 -e "-fcf-protection" | wc -l))
CET_LDFLAGS=-r -z ibt -z shstk
endif
ifeq ($(CC_HAS_CET),1)
CFLAGS += -fcf-protection=full
endif

ifeq ($(MINGW),0)
LDFLAGS = -fPIE -z noexecstack -z relro -z now -pthread
endif
ifeq ($(CC_HAS_CET),1)
LDFLAGS += -fcf-protection=full -Wl,-z,ibt -Wl,-z,shstk -Wl,-z,cet-report=error
endif
LDLIBS = -lIPSec_MB

ifeq ("$(shell test -e $(INSTPATH) && echo -n yes)","yes")
# library installed
CFLAGS +=
else
# library not installed
CFLAGS +=  -I../lib/include -I../lib
LDFLAGS += -L$(LIB_DIR)
endif

DEBUG_OPT ?= -O0
ifeq ($(DEBUG),y)
CFLAGS += -g -DDEBUG $(DEBUG_OPT)
LDFLAGS += -g
else
ifeq ($(MINGW),0)
CFLAGS += -O3 -fPIE -fstack-protector -D_FORTIFY_SOURCE=2
else
CFLAGS += -O2 -fPIE
endif
endif

SOURCES := ipsec_perf.c msr.c
ASM_SOURCES := misc.asm
OBJECTS := $(SOURCES:%.c=%.o) $(ASM_SOURCES:%.asm=%.o)

CHECKPATCH ?= checkpatch.pl
CPPCHECK ?= cppcheck

.PHONY: all clean style cppcheck

# rule for compiling assembly code with producing dependencies
%.o:%.asm
	$(NASM) -MD $(@:.o=.d) -MT $@ -o $@ $(NASM_FLAGS) $<
ifeq ($(CC_HAS_CET),1)
	$(LD) $(CET_LDFLAGS) -o $@.tmp $@
	mv $@.tmp $@
endif

all: $(EXE)

$(EXE): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

ipsec_perf.o: $(SOURCES)

.PHONY: clean
clean:
	-rm -f $(OBJECTS)
	-rm -f $(EXE)

SOURCES_STYLE := $(foreach infile,$(SOURCES),-f $(infile))
CHECKPATCH?=checkpatch.pl
.PHONY: style
style:
	$(CHECKPATCH) --no-tree --no-signoff --emacs --no-color \
--ignore CODE_INDENT,INITIALISED_STATIC,LEADING_SPACE,SPLIT_STRING,\
UNSPECIFIED_INT,ARRAY_SIZE,BLOCK_COMMENT_STYLE,GLOBAL_INITIALISERS,\
COMPLEX_MACRO,SPACING,STORAGE_CLASS,USE_FUNC,NEW_TYPEDEFS $(SOURCES_STYLE)
