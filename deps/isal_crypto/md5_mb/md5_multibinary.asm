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
default rel
[bits 64]

; declare the L3 ctx level symbols (these will then call the appropriate
; L2 symbols)
extern md5_ctx_mgr_init_sse
extern md5_ctx_mgr_submit_sse
extern md5_ctx_mgr_flush_sse

extern md5_ctx_mgr_init_avx
extern md5_ctx_mgr_submit_avx
extern md5_ctx_mgr_flush_avx

extern md5_ctx_mgr_init_avx2
extern md5_ctx_mgr_submit_avx2
extern md5_ctx_mgr_flush_avx2

%ifdef HAVE_AS_KNOWS_AVX512
 extern md5_ctx_mgr_init_avx512
 extern md5_ctx_mgr_submit_avx512
 extern md5_ctx_mgr_flush_avx512
%endif

extern md5_ctx_mgr_init_base
extern md5_ctx_mgr_submit_base
extern md5_ctx_mgr_flush_base

;;; *_mbinit are initial values for *_dispatched; is updated on first call.
;;; Therefore, *_dispatch_init is only executed on first call.

; Initialise symbols
mbin_interface md5_ctx_mgr_init
mbin_interface md5_ctx_mgr_submit
mbin_interface md5_ctx_mgr_flush

%ifdef HAVE_AS_KNOWS_AVX512
 mbin_dispatch_init6 md5_ctx_mgr_init, md5_ctx_mgr_init_base, md5_ctx_mgr_init_sse, md5_ctx_mgr_init_avx, md5_ctx_mgr_init_avx2, md5_ctx_mgr_init_avx512
 mbin_dispatch_init6 md5_ctx_mgr_submit, md5_ctx_mgr_submit_base, md5_ctx_mgr_submit_sse, md5_ctx_mgr_submit_avx, md5_ctx_mgr_submit_avx2, md5_ctx_mgr_submit_avx512
 mbin_dispatch_init6 md5_ctx_mgr_flush, md5_ctx_mgr_flush_base, md5_ctx_mgr_flush_sse, md5_ctx_mgr_flush_avx, md5_ctx_mgr_flush_avx2, md5_ctx_mgr_flush_avx512
%else
 mbin_dispatch_init md5_ctx_mgr_init, md5_ctx_mgr_init_sse, md5_ctx_mgr_init_avx, md5_ctx_mgr_init_avx2
 mbin_dispatch_init md5_ctx_mgr_submit, md5_ctx_mgr_submit_sse, md5_ctx_mgr_submit_avx, md5_ctx_mgr_submit_avx2
 mbin_dispatch_init md5_ctx_mgr_flush, md5_ctx_mgr_flush_sse, md5_ctx_mgr_flush_avx, md5_ctx_mgr_flush_avx2
%endif

;;       func                  core, ver, snum
slversion md5_ctx_mgr_init,	00,   04,  0189
slversion md5_ctx_mgr_submit,	00,   04,  018a
slversion md5_ctx_mgr_flush,	00,   04,  018b
