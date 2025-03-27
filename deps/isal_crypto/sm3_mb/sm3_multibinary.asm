;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2011-2020 Intel Corporation All rights reserved.
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

extern sm3_ctx_mgr_init_base
extern sm3_ctx_mgr_submit_base
extern sm3_ctx_mgr_flush_base

%ifdef HAVE_AS_KNOWS_AVX512
 extern sm3_ctx_mgr_init_avx512
 extern sm3_ctx_mgr_submit_avx512
 extern sm3_ctx_mgr_flush_avx512
%endif

;;; *_mbinit are initial values for *_dispatched; is updated on first call.
;;; Therefore, *_dispatch_init is only executed on first call.

; Initialise symbols
mbin_interface sm3_ctx_mgr_init
mbin_interface sm3_ctx_mgr_submit
mbin_interface sm3_ctx_mgr_flush


;;; don't have imlement see/avx/avx2 yet
%ifdef HAVE_AS_KNOWS_AVX512
  mbin_dispatch_init6 sm3_ctx_mgr_init, sm3_ctx_mgr_init_base, \
	sm3_ctx_mgr_init_base, sm3_ctx_mgr_init_base, sm3_ctx_mgr_init_base, \
	sm3_ctx_mgr_init_avx512
  mbin_dispatch_init6 sm3_ctx_mgr_submit, sm3_ctx_mgr_submit_base, \
	sm3_ctx_mgr_submit_base, sm3_ctx_mgr_submit_base, sm3_ctx_mgr_submit_base, \
	sm3_ctx_mgr_submit_avx512
  mbin_dispatch_init6 sm3_ctx_mgr_flush, sm3_ctx_mgr_flush_base, \
	sm3_ctx_mgr_flush_base, sm3_ctx_mgr_flush_base, sm3_ctx_mgr_flush_base, \
	sm3_ctx_mgr_flush_avx512
%else
  mbin_dispatch_init2 sm3_ctx_mgr_init, sm3_ctx_mgr_init_base
  mbin_dispatch_init2 sm3_ctx_mgr_submit, sm3_ctx_mgr_submit_base
  mbin_dispatch_init2 sm3_ctx_mgr_flush, sm3_ctx_mgr_flush_base
%endif



;;;       func  			core, ver, snum
slversion sm3_ctx_mgr_init,  	00,   00, 2300
slversion sm3_ctx_mgr_submit,	00,   00, 2301
slversion sm3_ctx_mgr_flush, 	00,   00, 2302

