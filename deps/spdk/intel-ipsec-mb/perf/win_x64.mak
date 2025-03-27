#
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
#

APP = ipsec_perf
INSTNAME = intel-ipsec-mb

!if !defined(PREFIX)
PREFIX = C:\Program Files
!endif

!if exist("$(PREFIX)\$(INSTNAME)\libIPSec_MB.lib")
IPSECLIB = "$(PREFIX)\$(INSTNAME)\libIPSec_MB.lib"
INCDIR = -I"$(PREFIX)\$(INSTNAME)"
!else
!if !defined(LIB_DIR)
LIB_DIR = ..\lib
!endif
IPSECLIB = "$(LIB_DIR)\libIPSec_MB.lib"
INCDIR = -I$(LIB_DIR) -I.\
!endif

!ifdef WINRING0_DIR
EXTRA_CFLAGS = $(EXTRA_CFLAGS) /DWIN_MSR
INCDIR = $(INCDIR) -I$(WINRING0_DIR)
!endif

!if !defined(DEBUG_OPT)
DEBUG_OPT = /Od
!endif

!ifdef DEBUG
DCFLAGS = $(DEBUG_OPT) /DDEBUG /Z7
DLFLAGS = /debug
!else
DCFLAGS = /O2 /Oi
DLFLAGS =
!endif

CC = cl
# _CRT_SECURE_NO_WARNINGS disables warning C4996 about unsecure strtok() being used
CFLAGS = /nologo /DNO_COMPAT_IMB_API_053 /D_CRT_SECURE_NO_WARNINGS $(DCFLAGS) /Y- /W3 /WX- /Gm- /fp:precise /EHsc $(EXTRA_CFLAGS) $(INCDIR)

LNK = link
LFLAGS = /out:$(APP).exe $(DLFLAGS)

AS = nasm
AFLAGS = -Werror -fwin64 -Xvc -DWIN_ABI

OBJECTS = ipsec_perf.obj msr.obj misc.obj

# dependency
!ifndef DEPTOOL
DEPTOOL = ..\mkdep.bat
!endif
DEPFLAGS = $(INCDIR)

all: $(APP).exe perf.dep

$(APP).exe: $(OBJECTS) $(IPSECLIB)
        $(LNK) $(LFLAGS) $(OBJECTS) $(IPSECLIB)

perf.dep: $(OBJECTS)
        @type *.obj.dep > $@ 2> nul

.c.obj:
	$(CC) /c $(CFLAGS) $<
        $(DEPTOOL) $< $@ "$(DEPFLAGS)" > $@.dep

.asm.obj:
	$(AS) -MD $@.dep -o $@ $(AFLAGS) $<

clean:
	del /q $(OBJECTS) perf.dep *.obj.dep $(APP).exe $(APP).pdb $(APP).ilk
