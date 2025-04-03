;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2011-2016 Intel Corporation All rights reserved.
;
;  Redistribution and use in source and binary forms, with or without
;  modification, are permitted provided that the following conditions
;  are met:
;    * Redistributions of source code must retain the above copyright
;      notice, this list of conditions and the following disclaimer.
;    * Redistributions in binary form must reproduce the above copyright
;      notice, this list of conditions and the following disclaimer in
;      the documentation and/or other materials provided with the
;      distribution.
;    * Neither the name of Intel Corporation nor the names of its
;      contributors may be used to endorse or promote products derived
;      from this software without specific prior written permission.
;
;  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
;  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
;  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
;  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
;  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
;  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
;  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
;  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
;  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
;  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
;  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%include "reg_sizes.asm"
%include "multibinary.asm"

%ifidn __OUTPUT_FORMAT__, elf32
 [bits 32]
%else
 default rel
 [bits 64]

 extern mh_sha1_murmur3_x64_128_update_sse
 extern mh_sha1_murmur3_x64_128_update_avx
 extern mh_sha1_murmur3_x64_128_update_avx2
 extern mh_sha1_murmur3_x64_128_finalize_sse
 extern mh_sha1_murmur3_x64_128_finalize_avx
 extern mh_sha1_murmur3_x64_128_finalize_avx2

 %ifdef HAVE_AS_KNOWS_AVX512
  extern mh_sha1_murmur3_x64_128_update_avx512
  extern mh_sha1_murmur3_x64_128_finalize_avx512
 %endif

%endif

extern mh_sha1_murmur3_x64_128_update_base
extern mh_sha1_murmur3_x64_128_finalize_base

mbin_interface mh_sha1_murmur3_x64_128_update
mbin_interface mh_sha1_murmur3_x64_128_finalize

%ifidn __OUTPUT_FORMAT__, elf64

 %ifdef HAVE_AS_KNOWS_AVX512
  mbin_dispatch_init6 mh_sha1_murmur3_x64_128_update, mh_sha1_murmur3_x64_128_update_base, mh_sha1_murmur3_x64_128_update_sse, mh_sha1_murmur3_x64_128_update_avx, mh_sha1_murmur3_x64_128_update_avx2, mh_sha1_murmur3_x64_128_update_avx512
  mbin_dispatch_init6 mh_sha1_murmur3_x64_128_finalize, mh_sha1_murmur3_x64_128_finalize_base, mh_sha1_murmur3_x64_128_finalize_sse, mh_sha1_murmur3_x64_128_finalize_avx, mh_sha1_murmur3_x64_128_finalize_avx2, mh_sha1_murmur3_x64_128_finalize_avx512
 %else
  mbin_dispatch_init5 mh_sha1_murmur3_x64_128_update, mh_sha1_murmur3_x64_128_update_base, mh_sha1_murmur3_x64_128_update_sse, mh_sha1_murmur3_x64_128_update_avx, mh_sha1_murmur3_x64_128_update_avx2
  mbin_dispatch_init5 mh_sha1_murmur3_x64_128_finalize, mh_sha1_murmur3_x64_128_finalize_base, mh_sha1_murmur3_x64_128_finalize_sse, mh_sha1_murmur3_x64_128_finalize_avx, mh_sha1_murmur3_x64_128_finalize_avx2
 %endif

%else
 mbin_dispatch_init2 mh_sha1_murmur3_x64_128_update, mh_sha1_murmur3_x64_128_update_base
 mbin_dispatch_init2 mh_sha1_murmur3_x64_128_finalize, mh_sha1_murmur3_x64_128_finalize_base
%endif

;;;       func                 				core, ver, snum
slversion mh_sha1_murmur3_x64_128_update,		00, 02, 0252
slversion mh_sha1_murmur3_x64_128_finalize,		00, 02, 0253
