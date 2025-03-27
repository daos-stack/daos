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

lsrc_x86_64 += 	sha256_mb/sha256_ctx_sse.c \
		sha256_mb/sha256_ctx_avx.c \
		sha256_mb/sha256_ctx_avx2.c \
		sha256_mb/sha256_ctx_base.c

lsrc_x86_64 += 	sha256_mb/sha256_mb_mgr_init_sse.c \
		sha256_mb/sha256_mb_mgr_init_avx2.c


lsrc_x86_64 += 	sha256_mb/sha256_mb_mgr_submit_sse.asm \
		sha256_mb/sha256_mb_mgr_submit_avx.asm \
		sha256_mb/sha256_mb_mgr_submit_avx2.asm \
		sha256_mb/sha256_mb_mgr_flush_sse.asm \
		sha256_mb/sha256_mb_mgr_flush_avx.asm \
		sha256_mb/sha256_mb_mgr_flush_avx2.asm \
		sha256_mb/sha256_mb_x4_sse.asm \
		sha256_mb/sha256_mb_x4_avx.asm \
		sha256_mb/sha256_mb_x8_avx2.asm \
		sha256_mb/sha256_multibinary.asm

lsrc_x86_64 += 	sha256_mb/sha256_ctx_avx512.c \
		sha256_mb/sha256_mb_mgr_init_avx512.c \
		sha256_mb/sha256_mb_mgr_submit_avx512.asm \
		sha256_mb/sha256_mb_mgr_flush_avx512.asm \
		sha256_mb/sha256_mb_x16_avx512.asm

lsrc_x86_64 += 	sha256_mb/sha256_opt_x1.asm

lsrc_x86_64 += 	sha256_mb/sha256_ni_x1.asm \
		sha256_mb/sha256_ni_x2.asm \
		sha256_mb/sha256_ctx_sse_ni.c \
		sha256_mb/sha256_ctx_avx512_ni.c \
		sha256_mb/sha256_mb_mgr_submit_sse_ni.asm \
		sha256_mb/sha256_mb_mgr_flush_sse_ni.asm \
		sha256_mb/sha256_mb_mgr_flush_avx512_ni.asm

lsrc_x86_32 += 	$(lsrc_x86_64)

lsrc_aarch64 += sha256_mb/sha256_ctx_base.c \
		sha256_mb/sha256_ref.c

lsrc_aarch64 += sha256_mb/aarch64/sha256_mb_multibinary.S \
		sha256_mb/aarch64/sha256_mb_aarch64_dispatcher.c  \
		sha256_mb/aarch64/sha256_ctx_ce.c	\
		sha256_mb/aarch64/sha256_mb_mgr_ce.c	\
		sha256_mb/aarch64/sha256_mb_x1_ce.S	\
		sha256_mb/aarch64/sha256_mb_x2_ce.S	\
		sha256_mb/aarch64/sha256_mb_x3_ce.S	\
		sha256_mb/aarch64/sha256_mb_x4_ce.S


lsrc_base_aliases += sha256_mb/sha256_ctx_base_aliases.c	\
		sha256_mb/sha256_ctx_base.c	\
		sha256_mb/sha256_ref.c

src_include += -I $(srcdir)/sha256_mb

extern_hdrs +=  include/sha256_mb.h \
		include/multi_buffer.h

other_src += 	include/datastruct.asm \
		include/multibinary.asm \
		sha256_mb/sha256_job.asm \
		sha256_mb/sha256_mb_mgr_datastruct.asm \
		include/reg_sizes.asm \
		sha256_mb/sha256_ref.c \
		include/memcpy_inline.h \
		include/memcpy.asm \
		include/intrinreg.h

check_tests  +=	sha256_mb/sha256_mb_test  \
		sha256_mb/sha256_mb_rand_test  \
		sha256_mb/sha256_mb_rand_update_test \
		sha256_mb/sha256_mb_flush_test

unit_tests   += sha256_mb/sha256_mb_rand_ssl_test

perf_tests  +=  sha256_mb/sha256_mb_vs_ossl_perf \
		sha256_mb/sha256_mb_vs_ossl_shortage_perf

sha256_mb_rand_ssl_test: sha256_ref.o
sha256_mb_rand_test: sha256_ref.o
sha256_mb_sha256_mb_rand_test_LDADD = sha256_mb/sha256_ref.lo libisal_crypto.la

sha256_mb_rand_update_test: sha256_ref.o
sha256_mb_sha256_mb_rand_update_test_LDADD = sha256_mb/sha256_ref.lo libisal_crypto.la

sha256_mb_flush_test: sha256_ref.o
sha256_mb_sha256_mb_flush_test_LDADD = sha256_mb/sha256_ref.lo libisal_crypto.la

sha256_mb_rand_ssl_test: LDLIBS += -lcrypto
sha256_mb_sha256_mb_rand_ssl_test_LDFLAGS = -lcrypto

sha256_mb_vs_ossl_perf: LDLIBS += -lcrypto
sha256_mb_sha256_mb_vs_ossl_perf_LDFLAGS = -lcrypto

sha256_mb_vs_ossl_shortage_perf: LDLIBS += -lcrypto
sha256_mb_sha256_mb_vs_ossl_shortage_perf_LDFLAGS = -lcrypto
