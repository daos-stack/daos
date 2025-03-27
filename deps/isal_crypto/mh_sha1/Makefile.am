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

lsrc_mh_sha1_base = \
		mh_sha1/mh_sha1_block_base.c \
		mh_sha1/mh_sha1_finalize_base.c \
		mh_sha1/mh_sha1_update_base.c \
		mh_sha1/sha1_for_mh_sha1.c \
		mh_sha1/mh_sha1.c

lsrc_x86_64  += \
		$(lsrc_mh_sha1_base) \
		mh_sha1/mh_sha1_multibinary.asm \
		mh_sha1/mh_sha1_block_sse.asm \
		mh_sha1/mh_sha1_block_avx.asm \
		mh_sha1/mh_sha1_block_avx2.asm \
		mh_sha1/mh_sha1_block_avx512.asm \
		mh_sha1/mh_sha1_avx512.c

lsrc_x86_32  += $(lsrc_x86_64)

lsrc_aarch64 += \
		$(lsrc_mh_sha1_base) \
		mh_sha1/aarch64/mh_sha1_multibinary.S \
		mh_sha1/aarch64/mh_sha1_aarch64_dispatcher.c \
		mh_sha1/aarch64/mh_sha1_block_ce.S \
		mh_sha1/aarch64/mh_sha1_ce.c

lsrc_base_aliases += \
		$(lsrc_mh_sha1_base) \
		mh_sha1/mh_sha1_base_aliases.c

other_src    += mh_sha1/mh_sha1_ref.c \
		include/reg_sizes.asm \
		include/multibinary.asm \
		include/test.h \
		mh_sha1/mh_sha1_internal.h

src_include += -I $(srcdir)/mh_sha1

extern_hdrs +=	include/mh_sha1.h

check_tests += 	mh_sha1/mh_sha1_test
unit_tests  += 	mh_sha1/mh_sha1_update_test

perf_tests  += 	mh_sha1/mh_sha1_perf


mh_sha1_test: mh_sha1_ref.o
mh_sha1_mh_sha1_test_LDADD = mh_sha1/mh_sha1_ref.lo libisal_crypto.la

mh_sha1_update_test: mh_sha1_ref.o
mh_sha1_mh_sha1_update_test_LDADD = mh_sha1/mh_sha1_ref.lo libisal_crypto.la

mh_sha1_mh_sha1_perf_LDADD = libisal_crypto.la
