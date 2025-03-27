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

%include "sha256_job.asm"
%include "sha256_mb_mgr_datastruct.asm"

%include "reg_sizes.asm"

extern sha256_mb_x8_avx2
extern sha256_opt_x1

[bits 64]
default rel
section .text

%ifidn __OUTPUT_FORMAT__, elf64
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; LINUX register definitions
%define arg1    rdi ; rcx
%define arg2    rsi ; rdx

%define tmp4    rdx
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%else

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; WINDOWS register definitions
%define arg1    rcx
%define arg2    rdx

%define tmp4    rsi
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
%endif

; Common register definitions

%define state   arg1
%define job     arg2
%define len2    arg2

; idx must be a register not clobberred by sha256_mb_x8_avx2 and sha256_opt_x1
%define idx             rbp

%define unused_lanes    rbx
%define lane_data       rbx
%define tmp2            rbx

%define job_rax         rax
%define tmp1            rax
%define size_offset     rax
%define tmp             rax
%define start_offset    rax

%define tmp3            arg1

%define extra_blocks    arg2
%define p               arg2


; STACK_SPACE needs to be an odd multiple of 8
_XMM_SAVE_SIZE  equ 10*16
_GPR_SAVE_SIZE  equ 8*8
_ALIGN_SIZE     equ 8

_XMM_SAVE       equ 0
_GPR_SAVE       equ _XMM_SAVE + _XMM_SAVE_SIZE
STACK_SPACE     equ _GPR_SAVE + _GPR_SAVE_SIZE + _ALIGN_SIZE

%define APPEND(a,b) a %+ b

; SHA256_JOB* sha256_mb_mgr_flush_avx2(SHA256_MB_JOB_MGR *state)
; arg 1 : rcx : state
mk_global sha256_mb_mgr_flush_avx2, function
sha256_mb_mgr_flush_avx2:
	endbranch
	sub     rsp, STACK_SPACE
	mov     [rsp + _GPR_SAVE + 8*0], rbx
	mov     [rsp + _GPR_SAVE + 8*3], rbp
	mov     [rsp + _GPR_SAVE + 8*4], r12
	mov     [rsp + _GPR_SAVE + 8*5], r13
	mov     [rsp + _GPR_SAVE + 8*6], r14
	mov     [rsp + _GPR_SAVE + 8*7], r15
%ifidn __OUTPUT_FORMAT__, win64
	mov     [rsp + _GPR_SAVE + 8*1], rsi
	mov     [rsp + _GPR_SAVE + 8*2], rdi
	vmovdqa  [rsp + _XMM_SAVE + 16*0], xmm6
	vmovdqa  [rsp + _XMM_SAVE + 16*1], xmm7
	vmovdqa  [rsp + _XMM_SAVE + 16*2], xmm8
	vmovdqa  [rsp + _XMM_SAVE + 16*3], xmm9
	vmovdqa  [rsp + _XMM_SAVE + 16*4], xmm10
	vmovdqa  [rsp + _XMM_SAVE + 16*5], xmm11
	vmovdqa  [rsp + _XMM_SAVE + 16*6], xmm12
	vmovdqa  [rsp + _XMM_SAVE + 16*7], xmm13
	vmovdqa  [rsp + _XMM_SAVE + 16*8], xmm14
	vmovdqa  [rsp + _XMM_SAVE + 16*9], xmm15
%endif

	; use num_lanes_inuse to judge all lanes are empty
	cmp	dword [state + _num_lanes_inuse], 0
	jz	return_null

	; find a lane with a non-null job
	xor	idx, idx
	cmp	qword [state + _ldata + 1 * _LANE_DATA_size + _job_in_lane], 0
	cmovne	idx, [one]
	cmp	qword [state + _ldata + 2 * _LANE_DATA_size + _job_in_lane], 0
	cmovne	idx, [two]
	cmp	qword [state + _ldata + 3 * _LANE_DATA_size + _job_in_lane], 0
	cmovne	idx, [three]
	cmp	qword [state + _ldata + 4 * _LANE_DATA_size + _job_in_lane], 0
	cmovne	idx, [four]
	cmp	qword [state + _ldata + 5 * _LANE_DATA_size + _job_in_lane], 0
	cmovne	idx, [five]
	cmp	qword [state + _ldata + 6 * _LANE_DATA_size + _job_in_lane], 0
	cmovne	idx, [six]
	cmp	qword [state + _ldata + 7 * _LANE_DATA_size + _job_in_lane], 0
	cmovne	idx, [seven]

	; copy idx to empty lanes
copy_lane_data:
	mov	tmp, [state + _args + _data_ptr + 8*idx]

%assign I 0
%rep 8
	cmp	qword [state + _ldata + I * _LANE_DATA_size + _job_in_lane], 0
	jne	APPEND(skip_,I)
	mov	[state + _args + _data_ptr + 8*I], tmp
	mov	dword [state + _lens + 4*I], 0xFFFFFFFF
APPEND(skip_,I):
%assign I (I+1)
%endrep

	; Find min length
	vmovdqa xmm0, [state + _lens + 0*16]
	vmovdqa xmm1, [state + _lens + 1*16]

	vpminud xmm2, xmm0, xmm1        ; xmm2 has {D,C,B,A}
	vpalignr xmm3, xmm3, xmm2, 8    ; xmm3 has {x,x,D,C}
	vpminud xmm2, xmm2, xmm3        ; xmm2 has {x,x,E,F}
	vpalignr xmm3, xmm3, xmm2, 4    ; xmm3 has {x,x,x,E}
	vpminud xmm2, xmm2, xmm3        ; xmm2 has min value in low dword

	vmovd   DWORD(idx), xmm2
	mov	len2, idx
	and	idx, 0xF
	shr	len2, 4
	jz	len_is_0

	; compare with sha-sb threshold, if num_lanes_inuse <= threshold, using sb func
	cmp	dword [state + _num_lanes_inuse], SHA256_SB_THRESHOLD_AVX2
	ja	mb_processing

	; lensN-len2=idx
	mov     [state + _lens + idx*4], DWORD(idx)
	mov	r10, idx
	or	r10, 0x2000	; avx2 has 8 lanes *4, r10b is idx, r10b2 is 32
	; "state" and "args" are the same address, arg1
	; len is arg2, idx and nlane in r10
	call    sha256_opt_x1
	; state and idx are intact
	jmp	len_is_0

mb_processing:

	vpand   xmm2, xmm2, [rel clear_low_nibble]
	vpshufd xmm2, xmm2, 0

	vpsubd  xmm0, xmm0, xmm2
	vpsubd  xmm1, xmm1, xmm2

	vmovdqa [state + _lens + 0*16], xmm0
	vmovdqa [state + _lens + 1*16], xmm1

	; "state" and "args" are the same address, arg1
	; len is arg2
	call	sha256_mb_x8_avx2
	; state and idx are intact

len_is_0:
	; process completed job "idx"
	imul	lane_data, idx, _LANE_DATA_size
	lea	lane_data, [state + _ldata + lane_data]

	mov	job_rax, [lane_data + _job_in_lane]
	mov	qword [lane_data + _job_in_lane], 0
	mov	dword [job_rax + _status], STS_COMPLETED
	mov	unused_lanes, [state + _unused_lanes]
	shl	unused_lanes, 4
	or	unused_lanes, idx
	mov	[state + _unused_lanes], unused_lanes

	sub     dword [state + _num_lanes_inuse], 1

	vmovd	xmm0, [state + _args_digest + 4*idx + 0*4*8]
	vpinsrd	xmm0, [state + _args_digest + 4*idx + 1*4*8], 1
	vpinsrd	xmm0, [state + _args_digest + 4*idx + 2*4*8], 2
	vpinsrd	xmm0, [state + _args_digest + 4*idx + 3*4*8], 3
	vmovd	xmm1, [state + _args_digest + 4*idx + 4*4*8]
	vpinsrd	xmm1, [state + _args_digest + 4*idx + 5*4*8], 1
	vpinsrd	xmm1, [state + _args_digest + 4*idx + 6*4*8], 2
	vpinsrd	xmm1, [state + _args_digest + 4*idx + 7*4*8], 3

	vmovdqa	[job_rax + _result_digest + 0*16], xmm0
	vmovdqa	[job_rax + _result_digest + 1*16], xmm1

return:
%ifidn __OUTPUT_FORMAT__, win64
	vmovdqa  xmm6, [rsp + _XMM_SAVE + 16*0]
	vmovdqa  xmm7, [rsp + _XMM_SAVE + 16*1]
	vmovdqa  xmm8, [rsp + _XMM_SAVE + 16*2]
	vmovdqa  xmm9, [rsp + _XMM_SAVE + 16*3]
	vmovdqa  xmm10, [rsp + _XMM_SAVE + 16*4]
	vmovdqa  xmm11, [rsp + _XMM_SAVE + 16*5]
	vmovdqa  xmm12, [rsp + _XMM_SAVE + 16*6]
	vmovdqa  xmm13, [rsp + _XMM_SAVE + 16*7]
	vmovdqa  xmm14, [rsp + _XMM_SAVE + 16*8]
	vmovdqa  xmm15, [rsp + _XMM_SAVE + 16*9]
	mov     rsi, [rsp + _GPR_SAVE + 8*1]
	mov     rdi, [rsp + _GPR_SAVE + 8*2]
%endif
	mov     rbx, [rsp + _GPR_SAVE + 8*0]
	mov     rbp, [rsp + _GPR_SAVE + 8*3]
	mov     r12, [rsp + _GPR_SAVE + 8*4]
	mov     r13, [rsp + _GPR_SAVE + 8*5]
	mov     r14, [rsp + _GPR_SAVE + 8*6]
	mov     r15, [rsp + _GPR_SAVE + 8*7]
	add     rsp, STACK_SPACE

	ret

return_null:
	xor	job_rax, job_rax
	jmp	return

section .data align=16

align 16
clear_low_nibble:
	dq 0x00000000FFFFFFF0, 0x0000000000000000
one:	dq  1
two:	dq  2
three:	dq  3
four:	dq  4
five:	dq  5
six:	dq  6
seven:	dq  7

