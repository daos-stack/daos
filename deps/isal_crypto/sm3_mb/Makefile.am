########################################################################
#  Copyright(c) 2011-2020 Intel Corporation All rights reserved.
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

lsrc_x86_64 += sm3_mb/sm3_ctx_base.c \
	sm3_mb/sm3_multibinary.asm

lsrc_base_aliases += sm3_mb/sm3_ctx_base.c \
	sm3_mb/sm3_ctx_base_aliases.c

lsrc_aarch64 += sm3_mb/sm3_ctx_base.c \
	sm3_mb/aarch64/sm3_mb_aarch64_dispatcher.c \
	sm3_mb/aarch64/sm3_mb_multibinary_aarch64.S \
	sm3_mb/aarch64/sm3_mb_mgr_sm_aarch64.c	\
	sm3_mb/aarch64/sm3_mb_ctx_sm_aarch64.c	\
	sm3_mb/aarch64/sm3_mb_sm_x1.S		\
	sm3_mb/aarch64/sm3_mb_sm_x2.S		\
	sm3_mb/aarch64/sm3_mb_sm_x3.S		\
	sm3_mb/aarch64/sm3_mb_sm_x4.S		\
	sm3_mb/aarch64/sm3_mb_mgr_asimd_aarch64.c	\
	sm3_mb/aarch64/sm3_mb_ctx_asimd_aarch64.c	\
	sm3_mb/aarch64/sm3_mb_asimd_x1.S	\
	sm3_mb/aarch64/sm3_mb_asimd_x4.S


src_include += -I $(srcdir)/sm3_mb

extern_hdrs +=	include/sm3_mb.h \
		include/multi_buffer.h

lsrc_x86_64 +=	sm3_mb/sm3_ctx_avx512.c \
		sm3_mb/sm3_mb_mgr_submit_avx512.asm \
		sm3_mb/sm3_mb_mgr_flush_avx512.asm \
		sm3_mb/sm3_mb_x16_avx512.asm

other_src +=	include/datastruct.asm \
		include/multibinary.asm \
		include/reg_sizes.asm \
		include/memcpy_inline.h \
		include/memcpy.asm \
		include/intrinreg.h \
		sm3_mb/sm3_job.asm \
		sm3_mb/sm3_mb_mgr_datastruct.asm \
		sm3_mb/sm3_test_helper.c

check_tests  +=	sm3_mb/sm3_ref_test

unit_tests   +=	sm3_mb/sm3_mb_rand_ssl_test \
		sm3_mb/sm3_mb_rand_test \
		sm3_mb/sm3_mb_rand_update_test \
		sm3_mb/sm3_mb_flush_test \
		sm3_mb/sm3_mb_test

perf_tests   += sm3_mb/sm3_mb_vs_ossl_perf \
		sm3_mb/sm3_mb_vs_ossl_shortage_perf

sm3_mb_rand_ssl_test: LDLIBS += -lcrypto
sm3_mb_sm3_mb_rand_ssl_test_LDFLAGS = -lcrypto

sm3_mb_rand_ssl_test: sm3_test_helper.o
sm3_mb_sm3_mb_rand_ssl_test_LDADD = sm3_mb/sm3_test_helper.lo libisal_crypto.la

sm3_mb_rand_update_test: LDLIBS += -lcrypto
sm3_mb_sm3_mb_rand_update_test_LDFLAGS = -lcrypto

sm3_mb_rand_update_test: sm3_test_helper.o
sm3_mb_sm3_mb_rand_update_test_LDADD = sm3_mb/sm3_test_helper.lo libisal_crypto.la

sm3_mb_flush_test: LDLIBS += -lcrypto
sm3_mb_sm3_mb_flush_test_LDFLAGS = -lcrypto

sm3_mb_flush_test: sm3_test_helper.o
sm3_mb_sm3_mb_flush_test_LDADD = sm3_mb/sm3_test_helper.lo libisal_crypto.la

sm3_mb_rand_test: LDLIBS += -lcrypto
sm3_mb_sm3_mb_rand_test_LDFLAGS = -lcrypto

sm3_mb_rand_test: sm3_test_helper.o
sm3_mb_sm3_mb_rand_test_LDADD = sm3_mb/sm3_test_helper.lo libisal_crypto.la

sm3_mb_vs_ossl_perf: LDLIBS += -lcrypto
sm3_mb_sm3_mb_vs_ossl_perf_LDFLAGS = -lcrypto

sm3_mb_vs_ossl_perf: sm3_test_helper.o
sm3_mb_sm3_mb_vs_ossl_perf_LDADD = sm3_mb/sm3_test_helper.lo libisal_crypto.la

sm3_mb_vs_ossl_shortage_perf: LDLIBS += -lcrypto
sm3_mb_sm3_mb_vs_ossl_shortage_perf_LDFLAGS = -lcrypto

sm3_mb_vs_ossl_shortage_perf: sm3_test_helper.o
sm3_mb_sm3_mb_vs_ossl_shortage_perf_LDADD = sm3_mb/sm3_test_helper.lo libisal_crypto.la
