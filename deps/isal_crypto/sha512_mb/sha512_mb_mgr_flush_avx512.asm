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

%include "sha512_job.asm"
%include "sha512_mb_mgr_datastruct.asm"
%include "reg_sizes.asm"

%ifdef HAVE_AS_KNOWS_AVX512
extern sha512_mb_x8_avx512

[bits 64]
default rel
section .text

%ifidn __OUTPUT_FORMAT__, elf64
; LINUX register definitions
%define arg1    rdi ; rcx
%define arg2    rsi ; rdx

; idx needs to be other than arg1, arg2, rbx, r12
%define idx     rdx ; rsi
%else
; WINDOWS register definitions
%define arg1    rcx
%define arg2    rdx

; idx needs to be other than arg1, arg2, rbx, r12
%define idx     rsi
%endif

; Common definitions
%define state   arg1
%define job     arg2
%define len2    arg2

%define num_lanes_inuse r9
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

%define tmp4            r8
%define lens0           r8

%define num_lanes_inuse r9
%define lens1           r9
%define lens2           r10
%define lens3           r11

struc stack_frame
	.xmm: resb 16*10
	.gpr: resb 8*8
	.rsp: resb 8
endstruc

; STACK_SPACE needs to be an odd multiple of 8
%define _XMM_SAVE       stack_frame.xmm
%define _GPR_SAVE       stack_frame.gpr
%define STACK_SPACE     stack_frame_size

%define APPEND(a,b) a %+ b

; SHA512_JOB* sha512_mb_mgr_flush_avx512(SHA512_MB_JOB_MGR *state)
; arg 1 : rcx : state
mk_global sha512_mb_mgr_flush_avx512, function
sha512_mb_mgr_flush_avx512:
	endbranch

	mov     rax, rsp

	sub     rsp, STACK_SPACE

	mov     [rsp + stack_frame.rsp], rax

	mov     [rsp + _GPR_SAVE + 8*0], rbx
	mov     [rsp + _GPR_SAVE + 8*3], rbp
	mov     [rsp + _GPR_SAVE + 8*4], r12
	mov     [rsp + _GPR_SAVE + 8*5], r13
	mov     [rsp + _GPR_SAVE + 8*6], r14
	mov     [rsp + _GPR_SAVE + 8*7], r15
%ifidn __OUTPUT_FORMAT__, win64
	mov     [rsp + _GPR_SAVE + 8*1], rsi
	mov     [rsp + _GPR_SAVE + 8*2], rdi
	vmovdqu  [rsp + _XMM_SAVE + 16*0], xmm6
	vmovdqu  [rsp + _XMM_SAVE + 16*1], xmm7
	vmovdqu  [rsp + _XMM_SAVE + 16*2], xmm8
	vmovdqu  [rsp + _XMM_SAVE + 16*3], xmm9
	vmovdqu  [rsp + _XMM_SAVE + 16*4], xmm10
	vmovdqu  [rsp + _XMM_SAVE + 16*5], xmm11
	vmovdqu  [rsp + _XMM_SAVE + 16*6], xmm12
	vmovdqu  [rsp + _XMM_SAVE + 16*7], xmm13
	vmovdqu  [rsp + _XMM_SAVE + 16*8], xmm14
	vmovdqu  [rsp + _XMM_SAVE + 16*9], xmm15
%endif

	mov	DWORD(num_lanes_inuse), [state + _num_lanes_inuse]
	cmp	num_lanes_inuse, 0
	jz	return_null

	; find a lane with a non-null job
	xor     idx, idx
%assign I 1
%rep 7
	cmp	qword [state + _ldata + I * _LANE_DATA_size + _job_in_lane], 0
	cmovne	idx, [APPEND(lane_,I)]
%assign I (I+1)
%endrep

	; copy idx to empty lanes
copy_lane_data:
	mov     tmp, [state + _args + _data_ptr + 8*idx]

%assign I 0
%rep 8
	cmp     qword [state + _ldata + I * _LANE_DATA_size + _job_in_lane], 0
	jne     APPEND(skip_,I)
	mov     [state + _args + _data_ptr + 8*I], tmp
	mov     dword [state + _lens + 4 + 8*I], 0xFFFFFFFF
APPEND(skip_,I):
%assign I (I+1)
%endrep

	; Find min length, len in sha512_mgr is 64bit, high 32bit is block num, low 8bit is idx
	vmovdqu ymm0, [state + _lens + 0*32]	; ymm0 has {D,d,C,c,B,b,A,a}
	vmovdqu ymm1, [state + _lens + 1*32]

	vpminuq ymm2, ymm0, ymm1	; ymm2 has {D,i,C,i,B,i,A,i}
	vpalignr ymm3, ymm3, ymm2, 8	; ymm3 has {x,i,D,i,x,i,B,i}
	vpminuq ymm2, ymm2, ymm3	; ymm2 has {x,i,F,i,x,i,E,i}
	vperm2i128 ymm3, ymm2, ymm2, 1	; ymm3 has {x,i,x,i,x,i,F,i}
	vpminuq ymm2, ymm2, ymm3	; ymm2 has min value in high dword

	vmovq   idx, xmm2
	mov     len2, idx
	and     idx, 0xF
	shr     len2, 32		; SHA512 blocksize is 1024bit
	jz      len_is_0

	vperm2i128 ymm2, ymm2, ymm2, 0	; ymm2 has {x,x,E,i,x,x,E,i}
	vpand   ymm2, ymm2, [rel clear_low_nibble]	; ymm2 has {0,0,E,0,0,0,E,0}
	vpshufd ymm2, ymm2, 0x44	; ymm2 has {E,0,E,0,E,0,E,0}

	vpsubd  ymm0, ymm0, ymm2
	vpsubd  ymm1, ymm1, ymm2

	vmovdqu [state + _lens + 0*32], ymm0
	vmovdqu [state + _lens + 1*32], ymm1

	; "state" and "args" are the same address, arg1
	; len is arg2
	call    sha512_mb_x8_avx512
	; state and idx are intact

len_is_0:
	; process completed job "idx"
	imul    lane_data, idx, _LANE_DATA_size
	lea     lane_data, [state + _ldata + lane_data]

	mov     job_rax, [lane_data + _job_in_lane]
	mov     qword [lane_data + _job_in_lane], 0
	mov     dword [job_rax + _status], STS_COMPLETED
	mov     unused_lanes, [state + _unused_lanes]
	shl     unused_lanes, 8
	or      unused_lanes, idx
	mov     [state + _unused_lanes], unused_lanes

        mov     DWORD(num_lanes_inuse), [state + _num_lanes_inuse]
        sub     num_lanes_inuse, 1
        mov     [state + _num_lanes_inuse], DWORD(num_lanes_inuse)
	vmovq    xmm0, [state + _args_digest + 8*idx + 0*64]
	vpinsrq  xmm0, [state + _args_digest + 8*idx + 1*64], 1
	vmovq    xmm1, [state + _args_digest + 8*idx + 2*64]
	vpinsrq  xmm1, [state + _args_digest + 8*idx + 3*64], 1
	vmovq    xmm2, [state + _args_digest + 8*idx + 4*64]
	vpinsrq  xmm2, [state + _args_digest + 8*idx + 5*64], 1
	vmovq    xmm3, [state + _args_digest + 8*idx + 6*64]
	vpinsrq  xmm3, [state + _args_digest + 8*idx + 7*64], 1

	vmovdqa  [job_rax + _result_digest + 0*16], xmm0
	vmovdqa  [job_rax + _result_digest + 1*16], xmm1
	vmovdqa  [job_rax + _result_digest + 2*16], xmm2
	vmovdqa  [job_rax + _result_digest + 3*16], xmm3

return:

%ifidn __OUTPUT_FORMAT__, win64
	vmovdqu  xmm6,  [rsp + _XMM_SAVE + 16*0]
	vmovdqu  xmm7,  [rsp + _XMM_SAVE + 16*1]
	vmovdqu  xmm8,  [rsp + _XMM_SAVE + 16*2]
	vmovdqu  xmm9,  [rsp + _XMM_SAVE + 16*3]
	vmovdqu  xmm10, [rsp + _XMM_SAVE + 16*4]
	vmovdqu  xmm11, [rsp + _XMM_SAVE + 16*5]
	vmovdqu  xmm12, [rsp + _XMM_SAVE + 16*6]
	vmovdqu  xmm13, [rsp + _XMM_SAVE + 16*7]
	vmovdqu  xmm14, [rsp + _XMM_SAVE + 16*8]
	vmovdqu  xmm15, [rsp + _XMM_SAVE + 16*9]
	mov     rsi, [rsp + _GPR_SAVE + 8*1]
	mov     rdi, [rsp + _GPR_SAVE + 8*2]
%endif
	mov     rbx, [rsp + _GPR_SAVE + 8*0]
	mov     rbp, [rsp + _GPR_SAVE + 8*3]
	mov     r12, [rsp + _GPR_SAVE + 8*4]
	mov     r13, [rsp + _GPR_SAVE + 8*5]
	mov     r14, [rsp + _GPR_SAVE + 8*6]
	mov     r15, [rsp + _GPR_SAVE + 8*7]

	mov	rsp, [rsp + stack_frame.rsp]

	ret

return_null:
	xor     job_rax, job_rax
	jmp     return

section .data align=32

align 32
clear_low_nibble:	; mgr len element 0xnnnnnnnn 0000000m, nnnnnnnn is blocknum, m is index
	dq 0xFFFFFFFF00000000, 0x0000000000000000
	dq 0xFFFFFFFF00000000, 0x0000000000000000
lane_1:     dq  1
lane_2:     dq  2
lane_3:     dq  3
lane_4:     dq  4
lane_5:     dq  5
lane_6:     dq  6
lane_7:     dq  7

%else
%ifidn __OUTPUT_FORMAT__, win64
global no_sha512_mb_mgr_flush_avx512
no_sha512_mb_mgr_flush_avx512:
%endif
%endif ; HAVE_AS_KNOWS_AVX512
