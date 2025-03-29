########################################################################
#  Copyright(c) 2011-2016 Intel Corporation All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in
#      the documentation and/or other materials provided with the
#      distribution.
#    * Neither the name of Intel Corporation nor the names of its
#      contributors may be used to endorse or promote products derived
#      from this software without specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
########################################################################

lsrc_x86_64 +=	md5_mb/md5_ctx_sse.c \
		md5_mb/md5_ctx_avx.c \
		md5_mb/md5_ctx_avx2.c \
		md5_mb/md5_ctx_base.c

lsrc_x86_64 += 	md5_mb/md5_mb_mgr_init_sse.c \
		md5_mb/md5_mb_mgr_init_avx2.c \
		md5_mb/md5_mb_mgr_init_avx512.c

lsrc_x86_64 += 	md5_mb/md5_mb_mgr_submit_sse.asm \
		md5_mb/md5_mb_mgr_submit_avx.asm \
		md5_mb/md5_mb_mgr_submit_avx2.asm \
		md5_mb/md5_mb_mgr_flush_sse.asm \
		md5_mb/md5_mb_mgr_flush_avx.asm \
		md5_mb/md5_mb_mgr_flush_avx2.asm \
		md5_mb/md5_mb_x4x2_sse.asm \
		md5_mb/md5_mb_x4x2_avx.asm \
		md5_mb/md5_mb_x8x2_avx2.asm \
		md5_mb/md5_multibinary.asm

lsrc_x86_64 += 	md5_mb/md5_mb_mgr_submit_avx512.asm \
		md5_mb/md5_mb_mgr_flush_avx512.asm \
		md5_mb/md5_mb_x16x2_avx512.asm \
		md5_mb/md5_ctx_avx512.c

lsrc_x86_32 += $(lsrc_x86_64)

lsrc_aarch64 += md5_mb/md5_ctx_base.c \
		md5_mb/aarch64/md5_ctx_aarch64_asimd.c  \
		md5_mb/aarch64/md5_mb_aarch64_dispatcher.c  \
		md5_mb/aarch64/md5_mb_mgr_aarch64_asimd.c  \
		md5_mb/aarch64/md5_mb_asimd_x4.S  \
		md5_mb/aarch64/md5_mb_asimd_x1.S  \
		md5_mb/aarch64/md5_mb_multibinary.S


lsrc_base_aliases += md5_mb/md5_ctx_base.c \
		md5_mb/md5_ctx_base_aliases.c
src_include  += -I $(srcdir)/md5_mb
extern_hdrs  += include/md5_mb.h \
		include/multi_buffer.h

other_src += 	include/datastruct.asm \
		md5_mb/md5_job.asm \
		md5_mb/md5_mb_mgr_datastruct.asm \
		md5_mb/md5_ref.c \
		include/reg_sizes.asm \
		include/multibinary.asm \
		include/memcpy_inline.h \
		include/intrinreg.h

check_tests  += md5_mb/md5_mb_test \
		md5_mb/md5_mb_rand_test \
		md5_mb/md5_mb_rand_update_test

unit_tests  += md5_mb/md5_mb_rand_ssl_test

perf_tests  +=  md5_mb/md5_mb_vs_ossl_perf


md5_mb_rand_test: md5_ref.o
md5_mb_md5_mb_rand_test_LDADD = md5_mb/md5_ref.lo libisal_crypto.la
md5_mb_rand_update_test: md5_ref.o
md5_mb_md5_mb_rand_update_test_LDADD = md5_mb/md5_ref.lo libisal_crypto.la
md5_mb_rand_ssl_test: LDLIBS += -lcrypto
md5_mb_md5_mb_rand_ssl_test_LDFLAGS = -lcrypto
md5_mb_vs_ossl_perf: LDLIBS += -lcrypto
md5_mb_md5_mb_vs_ossl_perf_LDFLAGS = -lcrypto

