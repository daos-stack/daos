########################################################################
#  Copyright(c) 2011-2017 Intel Corporation All rights reserved.
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

lsrc_sha256     =	mh_sha256/sha256_for_mh_sha256.c

lsrc_mh_sha256  =	mh_sha256/mh_sha256.c \
		mh_sha256/mh_sha256_block_sse.asm \
		mh_sha256/mh_sha256_block_avx.asm \
		mh_sha256/mh_sha256_block_avx2.asm \
		mh_sha256/mh_sha256_multibinary.asm \
		mh_sha256/mh_sha256_finalize_base.c \
		mh_sha256/mh_sha256_update_base.c \
		mh_sha256/mh_sha256_block_base.c

lsrc_mh_sha256 += mh_sha256/mh_sha256_block_avx512.asm \
		mh_sha256/mh_sha256_avx512.c

lsrc_x86_64    += $(lsrc_sha256) \
		$(lsrc_mh_sha256)

lsrc_x86_32    += $(lsrc_x86_64)

other_src   += 	mh_sha256/mh_sha256_ref.c \
		include/reg_sizes.asm \
		include/multibinary.asm \
		include/test.h \
		mh_sha256/mh_sha256_internal.h

lsrc_aarch64 += $(lsrc_sha256)	\
		mh_sha256/aarch64/mh_sha256_multibinary.S \
		mh_sha256/aarch64/mh_sha256_aarch64_dispatcher.c \
		mh_sha256/aarch64/mh_sha256_block_ce.S \
		mh_sha256/aarch64/mh_sha256_ce.c \
		mh_sha256/mh_sha256.c \
		mh_sha256/mh_sha256_finalize_base.c \
		mh_sha256/mh_sha256_update_base.c \
		mh_sha256/mh_sha256_block_base.c

lsrc_base_aliases += $(lsrc_sha256)	\
		mh_sha256/mh_sha256_base_aliases.c \
		mh_sha256/mh_sha256.c \
		mh_sha256/mh_sha256_finalize_base.c \
		mh_sha256/mh_sha256_update_base.c \
		mh_sha256/mh_sha256_block_base.c

src_include += -I $(srcdir)/mh_sha256

extern_hdrs +=	include/mh_sha256.h

check_tests += 	mh_sha256/mh_sha256_test
unit_tests  += 	mh_sha256/mh_sha256_update_test

perf_tests  += 	mh_sha256/mh_sha256_perf


mh_sha256_test: mh_sha256_ref.o
mh_sha256_mh_sha256_test_LDADD = mh_sha256/mh_sha256_ref.lo libisal_crypto.la

mh_sha256_update_test: mh_sha256_ref.o
mh_sha256_mh_sha256_update_test_LDADD = mh_sha256/mh_sha256_ref.lo libisal_crypto.la

mh_sha256_mh_sha256_perf_LDADD = libisal_crypto.la
