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

lsrc_x86_64         += rolling_hash/rolling_hashx_base.c
lsrc_x86_64         += rolling_hash/rolling_hash2.c
lsrc_x86_64         += rolling_hash/rolling_hash2_until_04.asm
lsrc_x86_64         += rolling_hash/rolling_hash2_until_00.asm
lsrc_x86_64         += rolling_hash/rolling_hash2_multibinary.asm

lsrc_x86_32         += $(lsrc_x86_64)

lsrc_base_aliases   += 	rolling_hash/rolling_hashx_base.c	\
			rolling_hash/rolling_hash2.c	\
			rolling_hash/rolling_hash2_base_aliases.c


lsrc_aarch64	    += 	rolling_hash/rolling_hashx_base.c	\
			rolling_hash/rolling_hash2.c	\
			rolling_hash/aarch64/rolling_hash2_aarch64_multibinary.S \
			rolling_hash/aarch64/rolling_hash2_aarch64_dispatcher.c \
			rolling_hash/aarch64/rolling_hash2_run_until_unroll.S

src_include  += -I $(srcdir)/rolling_hash
extern_hdrs  += include/rolling_hashx.h

other_src    += rolling_hash/rolling_hash2_table.h
other_src    += include/test.h include/types.h

check_tests  += rolling_hash/rolling_hash2_test
perf_tests   += rolling_hash/rolling_hash2_perf
other_tests  += rolling_hash/chunking_with_mb_hash
