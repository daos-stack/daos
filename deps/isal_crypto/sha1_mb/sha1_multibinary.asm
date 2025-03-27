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

%ifidn __OUTPUT_FORMAT__, elf64
%define WRT_OPT		wrt ..plt
%else
%define WRT_OPT
%endif

%include "reg_sizes.asm"
%include "multibinary.asm"
default rel
[bits 64]

; declare the L3 ctx level symbols (these will then call the appropriate
; L2 symbols)
extern sha1_ctx_mgr_init_sse
extern sha1_ctx_mgr_submit_sse
extern sha1_ctx_mgr_flush_sse

extern sha1_ctx_mgr_init_avx
extern sha1_ctx_mgr_submit_avx
extern sha1_ctx_mgr_flush_avx

extern sha1_ctx_mgr_init_avx2
extern sha1_ctx_mgr_submit_avx2
extern sha1_ctx_mgr_flush_avx2

extern sha1_ctx_mgr_init_base
extern sha1_ctx_mgr_submit_base
extern sha1_ctx_mgr_flush_base

%ifdef HAVE_AS_KNOWS_AVX512
 extern sha1_ctx_mgr_init_avx512
 extern sha1_ctx_mgr_submit_avx512
 extern sha1_ctx_mgr_flush_avx512
%endif

%ifdef HAVE_AS_KNOWS_SHANI
 extern sha1_ctx_mgr_init_sse_ni
 extern sha1_ctx_mgr_submit_sse_ni
 extern sha1_ctx_mgr_flush_sse_ni
%endif

%ifdef HAVE_AS_KNOWS_AVX512
 %ifdef HAVE_AS_KNOWS_SHANI
  extern sha1_ctx_mgr_init_avx512_ni
  extern sha1_ctx_mgr_submit_avx512_ni
  extern sha1_ctx_mgr_flush_avx512_ni
 %endif
%endif

;;; *_mbinit are initial values for *_dispatched; is updated on first call.
;;; Therefore, *_dispatch_init is only executed on first call.

; Initialise symbols
mbin_interface sha1_ctx_mgr_init
mbin_interface sha1_ctx_mgr_submit
mbin_interface sha1_ctx_mgr_flush

%ifdef HAVE_AS_KNOWS_AVX512
 ; Reuse mbin_dispatch_init6's extension through replacing base by sse version
 %ifdef HAVE_AS_KNOWS_SHANI
  mbin_dispatch_base_to_avx512_shani sha1_ctx_mgr_init, sha1_ctx_mgr_init_base, \
	sha1_ctx_mgr_init_sse, sha1_ctx_mgr_init_avx, sha1_ctx_mgr_init_avx2, \
	sha1_ctx_mgr_init_avx512, sha1_ctx_mgr_init_sse_ni, sha1_ctx_mgr_init_avx512_ni
  mbin_dispatch_base_to_avx512_shani sha1_ctx_mgr_submit, sha1_ctx_mgr_submit_base, \
	sha1_ctx_mgr_submit_sse, sha1_ctx_mgr_submit_avx, sha1_ctx_mgr_submit_avx2, \
	sha1_ctx_mgr_submit_avx512, sha1_ctx_mgr_submit_sse_ni, sha1_ctx_mgr_submit_avx512_ni
  mbin_dispatch_base_to_avx512_shani sha1_ctx_mgr_flush, sha1_ctx_mgr_flush_base, \
	sha1_ctx_mgr_flush_sse, sha1_ctx_mgr_flush_avx, sha1_ctx_mgr_flush_avx2, \
	sha1_ctx_mgr_flush_avx512, sha1_ctx_mgr_flush_sse_ni, sha1_ctx_mgr_flush_avx512_ni
 %else
  mbin_dispatch_init6 sha1_ctx_mgr_init, sha1_ctx_mgr_init_base, \
	sha1_ctx_mgr_init_sse, sha1_ctx_mgr_init_avx, sha1_ctx_mgr_init_avx2, \
	sha1_ctx_mgr_init_avx512
  mbin_dispatch_init6 sha1_ctx_mgr_submit, sha1_ctx_mgr_submit_base, \
	sha1_ctx_mgr_submit_sse, sha1_ctx_mgr_submit_avx, sha1_ctx_mgr_submit_avx2, \
	sha1_ctx_mgr_submit_avx512
  mbin_dispatch_init6 sha1_ctx_mgr_flush, sha1_ctx_mgr_flush_base, \
	sha1_ctx_mgr_flush_sse, sha1_ctx_mgr_flush_avx, sha1_ctx_mgr_flush_avx2, \
	sha1_ctx_mgr_flush_avx512
 %endif
%else
 %ifdef HAVE_AS_KNOWS_SHANI
  mbin_dispatch_sse_to_avx2_shani sha1_ctx_mgr_init, sha1_ctx_mgr_init_sse, \
	sha1_ctx_mgr_init_avx, sha1_ctx_mgr_init_avx2, sha1_ctx_mgr_init_sse_ni
  mbin_dispatch_sse_to_avx2_shani sha1_ctx_mgr_submit, sha1_ctx_mgr_submit_sse, \
	sha1_ctx_mgr_submit_avx, sha1_ctx_mgr_submit_avx2, sha1_ctx_mgr_submit_sse_ni
  mbin_dispatch_sse_to_avx2_shani sha1_ctx_mgr_flush, sha1_ctx_mgr_flush_sse, \
	sha1_ctx_mgr_flush_avx, sha1_ctx_mgr_flush_avx2, sha1_ctx_mgr_flush_sse_ni
 %else
  mbin_dispatch_init sha1_ctx_mgr_init, sha1_ctx_mgr_init_sse, \
	sha1_ctx_mgr_init_avx, sha1_ctx_mgr_init_avx2
  mbin_dispatch_init sha1_ctx_mgr_submit, sha1_ctx_mgr_submit_sse, \
	sha1_ctx_mgr_submit_avx, sha1_ctx_mgr_submit_avx2
  mbin_dispatch_init sha1_ctx_mgr_flush, sha1_ctx_mgr_flush_sse, \
	sha1_ctx_mgr_flush_avx, sha1_ctx_mgr_flush_avx2
 %endif
%endif

;;;       func                  core, ver, snum
slversion sha1_ctx_mgr_init,	00,   04,  0148
slversion sha1_ctx_mgr_submit,	00,   04,  0149
slversion sha1_ctx_mgr_flush,	00,   04,  0150
