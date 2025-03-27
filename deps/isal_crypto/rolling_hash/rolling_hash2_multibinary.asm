;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;  Copyright(c) 2011-2017 Intel Corporation All rights reserved.
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

%ifidn __OUTPUT_FORMAT__, elf32

[bits 32]
%define def_wrd		dd
%define wrd_sz  	dword
%define arg1		esi

%else

default rel
[bits 64]
%define def_wrd 	dq
%define wrd_sz  	qword
%define arg1		rsi

extern rolling_hash2_run_until_00
extern rolling_hash2_run_until_04
%endif

extern rolling_hash2_run_until_base


section .data
;;; *_mbinit are initial values for *_dispatched; is updated on first call.
;;; Therefore, *_dispatch_init is only executed on first call.

rolling_hash2_run_until_dispatched:
	def_wrd      rolling_hash2_run_until_mbinit

section .text

;;;;
; rolling_hash2_run_until multibinary function
;;;;
mk_global rolling_hash2_run_until, function
rolling_hash2_run_until_mbinit:
	endbranch
	call	rolling_hash2_run_until_dispatch_init

rolling_hash2_run_until:
	jmp	wrd_sz [rolling_hash2_run_until_dispatched]

rolling_hash2_run_until_dispatch_init:
	push    arg1
%ifidn __OUTPUT_FORMAT__, elf32		;; 32-bit check
	lea     arg1, [rolling_hash2_run_until_base]
%else
	push    rax
	push    rbx
	push    rcx
	push    rdx
	lea     arg1, [rolling_hash2_run_until_base WRT_OPT] ; Default

	mov     eax, 1
	cpuid
	lea     rbx, [rolling_hash2_run_until_00 WRT_OPT]
	test    ecx, FLAG_CPUID1_ECX_SSE4_1
	cmovne  arg1, rbx

	and	ecx, (FLAG_CPUID1_ECX_AVX | FLAG_CPUID1_ECX_OSXSAVE)
	cmp	ecx, (FLAG_CPUID1_ECX_AVX | FLAG_CPUID1_ECX_OSXSAVE)
	lea	rbx, [rolling_hash2_run_until_00 WRT_OPT]

	jne	_done_rolling_hash2_run_until_data_init
	mov	rsi, rbx

	;; Try for AVX2
	xor	ecx, ecx
	mov	eax, 7
	cpuid
	test	ebx, FLAG_CPUID1_EBX_AVX2
	lea     rbx, [rolling_hash2_run_until_04 WRT_OPT]
	cmovne	rsi, rbx

	;;  Does it have xmm and ymm support
	xor     ecx, ecx
	xgetbv
	and     eax, FLAG_XGETBV_EAX_XMM_YMM
	cmp     eax, FLAG_XGETBV_EAX_XMM_YMM
	je      _done_rolling_hash2_run_until_data_init
	lea     rsi, [rolling_hash2_run_until_00 WRT_OPT]

_done_rolling_hash2_run_until_data_init:
	pop     rdx
	pop     rcx
	pop     rbx
	pop     rax
%endif			;; END 32-bit check
	mov     [rolling_hash2_run_until_dispatched], arg1
	pop     arg1
	ret
