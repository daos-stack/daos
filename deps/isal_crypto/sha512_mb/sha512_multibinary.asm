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

;;;;;
; mbin_dispatch_init_avoton parameters
; Use this function when SSE/00/01 is a minimum requirement
; if AVOTON is true, then use avoton_func instead of sse_func
; 1-> function name
; 2-> SSE/00/01 optimized function used as base
; 3-> AVX or AVX/02 opt func
; 4-> AVX2 or AVX/04 opt func
; 5-> AVOTON opt func
;;;;;
%macro mbin_dispatch_init_avoton 5
	section .text
	%1_dispatch_init:
		push	mbin_rsi
		push	mbin_rax
		push	mbin_rbx
		push	mbin_rcx
		push	mbin_rdx
		push	mbin_rdi
		lea	mbin_rsi, [%2 WRT_OPT] ; Default to SSE 00/01

		mov	eax, 1
		cpuid
		lea	mbin_rdi, [%5 WRT_OPT]
		and     eax, FLAG_CPUID1_EAX_STEP_MASK
		cmp     eax, FLAG_CPUID1_EAX_AVOTON
		; If Avoton, set Avoton symbol and exit
		cmove   mbin_rsi, mbin_rdi
		je	_%1_init_done

		and	ecx, (FLAG_CPUID1_ECX_AVX | FLAG_CPUID1_ECX_OSXSAVE)
		cmp	ecx, (FLAG_CPUID1_ECX_AVX | FLAG_CPUID1_ECX_OSXSAVE)
		lea	mbin_rbx, [%3 WRT_OPT] ; AVX (gen2) opt func
		jne	_%1_init_done ; AVX is not available so end
		mov	mbin_rsi, mbin_rbx

		;; Try for AVX2
		xor	ecx, ecx
		mov	eax, 7
		cpuid
		test	ebx, FLAG_CPUID7_EBX_AVX2
		lea	mbin_rbx, [%4 WRT_OPT] ; AVX (gen4) opt func
		cmovne	mbin_rsi, mbin_rbx

		;; Does it have xmm and ymm support
		xor	ecx, ecx
		xgetbv
		and	eax, FLAG_XGETBV_EAX_XMM_YMM
		cmp	eax, FLAG_XGETBV_EAX_XMM_YMM
		je	_%1_init_done
		lea	mbin_rsi, [%2 WRT_OPT]

	_%1_init_done:
		pop	mbin_rdi
		pop	mbin_rdx
		pop	mbin_rcx
		pop	mbin_rbx
		pop	mbin_rax
		mov	[%1_dispatched], mbin_rsi
		pop	mbin_rsi
		ret
%endmacro

;;;;;
; mbin_dispatch_init6_avoton parameters
; if AVOTON is true, then use avoton_func instead of sse_func
; 1-> function name
; 2-> base function
; 3-> SSE4_1 or 00/01 optimized function
; 4-> AVX/02 opt func
; 5-> AVX2/04 opt func
; 6-> AVX512/06 opt func
; 7-> AVOTON opt func
;;;;;
%macro mbin_dispatch_init6_avoton 7
	section .text
	%1_dispatch_init:
		push	mbin_rsi
		push	mbin_rax
		push	mbin_rbx
		push	mbin_rcx
		push	mbin_rdx
		push	mbin_rdi
		lea	mbin_rsi, [%2 WRT_OPT] ; Default - use base function

		mov	eax, 1
		cpuid
		mov	ebx, ecx ; save cpuid1.ecx
		test	ecx, FLAG_CPUID1_ECX_SSE4_1
		je	_%1_init_done	  ; Use base function if no SSE4_1
		lea	mbin_rsi, [%3 WRT_OPT] ; SSE possible so use 00/01 opt

		lea	mbin_rdi, [%7 WRT_OPT]
		and     eax, FLAG_CPUID1_EAX_STEP_MASK
		cmp     eax, FLAG_CPUID1_EAX_AVOTON
		; If Avoton, set Avoton symbol and exit
		cmove   mbin_rsi, mbin_rdi
		je	_%1_init_done


		;; Test for XMM_YMM support/AVX
		test	ecx, FLAG_CPUID1_ECX_OSXSAVE
		je	_%1_init_done
		xor	ecx, ecx
		xgetbv	; xcr -> edx:eax
		mov	edi, eax	  ; save xgetvb.eax

		and	eax, FLAG_XGETBV_EAX_XMM_YMM
		cmp	eax, FLAG_XGETBV_EAX_XMM_YMM
		jne	_%1_init_done
		test	ebx, FLAG_CPUID1_ECX_AVX
		je	_%1_init_done
		lea	mbin_rsi, [%4 WRT_OPT] ; AVX/02 opt

		;; Test for AVX2
		xor	ecx, ecx
		mov	eax, 7
		cpuid
		test	ebx, FLAG_CPUID7_EBX_AVX2
		je	_%1_init_done		; No AVX2 possible
		lea	mbin_rsi, [%5 WRT_OPT] 	; AVX2/04 opt func

		;; Test for AVX512
		and	edi, FLAG_XGETBV_EAX_ZMM_OPM
		cmp	edi, FLAG_XGETBV_EAX_ZMM_OPM
		jne	_%1_init_done	  ; No AVX512 possible
		and	ebx, FLAGS_CPUID7_EBX_AVX512_G1
		cmp	ebx, FLAGS_CPUID7_EBX_AVX512_G1
		lea	mbin_rbx, [%6 WRT_OPT] ; AVX512/06 opt
		cmove	mbin_rsi, mbin_rbx

	_%1_init_done:
		pop	mbin_rdi
		pop	mbin_rdx
		pop	mbin_rcx
		pop	mbin_rbx
		pop	mbin_rax
		mov	[%1_dispatched], mbin_rsi
		pop	mbin_rsi
		ret
%endmacro

default rel
[bits 64]

%define def_wrd 	dq
%define wrd_sz  	qword
%define arg1		rsi

; declare the L3 ctx level symbols (these will then call the appropriate
; L2 symbols)
extern sha512_ctx_mgr_init_sse
extern sha512_ctx_mgr_submit_sse
extern sha512_ctx_mgr_flush_sse

extern sha512_ctx_mgr_init_avx
extern sha512_ctx_mgr_submit_avx
extern sha512_ctx_mgr_flush_avx

extern sha512_ctx_mgr_init_avx2
extern sha512_ctx_mgr_submit_avx2
extern sha512_ctx_mgr_flush_avx2

extern sha512_ctx_mgr_init_base
extern sha512_ctx_mgr_submit_base
extern sha512_ctx_mgr_flush_base

%ifdef HAVE_AS_KNOWS_AVX512
 extern sha512_ctx_mgr_init_avx512
 extern sha512_ctx_mgr_submit_avx512
 extern sha512_ctx_mgr_flush_avx512
%endif

extern sha512_ctx_mgr_init_sb_sse4
extern sha512_ctx_mgr_submit_sb_sse4
extern sha512_ctx_mgr_flush_sb_sse4

;;; *_mbinit are initial values for *_dispatched; is updated on first call.
;;; Therefore, *_dispatch_init is only executed on first call.

; Initialise symbols
mbin_interface sha512_ctx_mgr_init
mbin_interface sha512_ctx_mgr_submit
mbin_interface sha512_ctx_mgr_flush

%ifdef HAVE_AS_KNOWS_AVX512
 ; Reuse mbin_dispatch_init6 through replacing base by sse version
 mbin_dispatch_init6_avoton sha512_ctx_mgr_init, sha512_ctx_mgr_init_base, \
			sha512_ctx_mgr_init_sse, sha512_ctx_mgr_init_avx, \
			sha512_ctx_mgr_init_avx2, sha512_ctx_mgr_init_avx512, \
			sha512_ctx_mgr_init_sb_sse4

 mbin_dispatch_init6_avoton sha512_ctx_mgr_submit, sha512_ctx_mgr_submit_base, \
			sha512_ctx_mgr_submit_sse, sha512_ctx_mgr_submit_avx, \
			sha512_ctx_mgr_submit_avx2, sha512_ctx_mgr_submit_avx512, \
			sha512_ctx_mgr_submit_sb_sse4

 mbin_dispatch_init6_avoton sha512_ctx_mgr_flush, sha512_ctx_mgr_flush_base, \
			sha512_ctx_mgr_flush_sse, sha512_ctx_mgr_flush_avx, \
			sha512_ctx_mgr_flush_avx2, sha512_ctx_mgr_flush_avx512, \
			sha512_ctx_mgr_flush_sb_sse4
%else
 mbin_dispatch_init_avoton sha512_ctx_mgr_init, sha512_ctx_mgr_init_sse, \
			sha512_ctx_mgr_init_avx, sha512_ctx_mgr_init_avx2, \
			sha512_ctx_mgr_init_sb_sse4

 mbin_dispatch_init_avoton sha512_ctx_mgr_submit, sha512_ctx_mgr_submit_sse, \
			sha512_ctx_mgr_submit_avx, sha512_ctx_mgr_submit_avx2, \
			sha512_ctx_mgr_submit_sb_sse4

 mbin_dispatch_init_avoton sha512_ctx_mgr_flush, sha512_ctx_mgr_flush_sse, \
			sha512_ctx_mgr_flush_avx, sha512_ctx_mgr_flush_avx2, \
			sha512_ctx_mgr_flush_sb_sse4
%endif


;;;       func				core, ver, snum
slversion sha512_ctx_mgr_init,		00,   04,  0175
slversion sha512_ctx_mgr_submit,	00,   04,  0176
slversion sha512_ctx_mgr_flush,		00,   04,  0177
