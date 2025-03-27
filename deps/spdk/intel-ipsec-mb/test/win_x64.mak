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

TEST_APP = ipsec_MB_testapp
XVALID_APP = ipsec_xvalid_test
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

# compiler
CC = cl
# _CRT_SECURE_NO_WARNINGS disables warning C4996 about unsecure snprintf() being used
CFLAGS = /nologo /DNO_COMPAT_IMB_API_053 /D_CRT_SECURE_NO_WARNINGS $(DCFLAGS) /Y- /W3 /WX- /Gm- /fp:precise /EHsc $(EXTRA_CFLAGS) $(INCDIR)

#linker
LNK = link
TEST_LFLAGS = /out:$(TEST_APP).exe $(DLFLAGS)
XVALID_LFLAGS = /out:$(XVALID_APP).exe $(DLFLAGS)

AS = nasm
AFLAGS = -Werror -fwin64 -Xvc -DWIN_ABI

# dependency
!ifndef DEPTOOL
DEPTOOL = ..\mkdep.bat
!endif
DEPFLAGS = $(INCDIR)

TEST_OBJS = main.obj gcm_test.obj ctr_test.obj customop_test.obj des_test.obj ccm_test.obj cmac_test.obj hmac_sha1_test.obj hmac_sha256_sha512_test.obj utils.obj hmac_md5_test.obj aes_test.obj sha_test.obj chained_test.obj api_test.obj pon_test.obj ecb_test.obj zuc_test.obj kasumi_test.obj snow3g_test.obj direct_api_test.obj clear_mem_test.obj hec_test.obj xcbc_test.obj aes_cbcs_test.obj crc_test.obj chacha_test.obj poly1305_test.obj chacha20_poly1305_test.obj null_test.obj snow_v_test.obj direct_api_param_test.obj

XVALID_OBJS = ipsec_xvalid.obj misc.obj utils.obj

all: $(TEST_APP).exe $(XVALID_APP).exe tests.dep

$(TEST_APP).exe: $(TEST_OBJS) $(IPSECLIB)
        $(LNK) $(TEST_LFLAGS) $(TEST_OBJS) $(IPSECLIB)

$(XVALID_APP).exe: $(XVALID_OBJS) $(IPSECLIB)
        $(LNK) $(XVALID_LFLAGS) $(XVALID_OBJS) $(IPSECLIB)

tests.dep: $(TEST_OBJS) $(XVALID_OBJS)
        @type *.obj.dep > $@ 2> nul

.c.obj:
	$(CC) /c $(CFLAGS) $<
        $(DEPTOOL) $< $@ "$(DEPFLAGS)" > $@.dep

.asm.obj:
	$(AS) -MD $@.dep -o $@ $(AFLAGS) $<

clean:
        del /q $(TEST_OBJS) tests.dep *.obj.dep $(TEST_APP).* $(XVALID_OBJS) $(XVALID_APP).*

!if exist(tests.dep)
!include tests.dep
!endif
