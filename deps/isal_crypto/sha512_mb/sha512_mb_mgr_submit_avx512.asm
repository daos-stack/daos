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

%ifidn __OUTPUT_FORMAT__, elf64
; LINUX register definitions
%define arg1    rdi ; rcx
%define arg2    rsi ; rdx

; idx needs to be other than arg1, arg2, rbx, r12
%define idx             rdx ; rsi
%define last_len        rdx ; rsi

%define size_offset     rcx ; rdi
%define tmp2            rcx ; rdi

%else
; WINDOWS register definitions
%define arg1    rcx
%define arg2    rdx

; idx needs to be other than arg1, arg2, rbx, r12
%define last_len        rsi
%define idx             rsi

%define size_offset     rdi
%define tmp2            rdi

%endif

; Common definitions
%define state   arg1
%define job     arg2
%define len2    arg2
%define p2      arg2

%define p               r11
%define start_offset    r11

%define unused_lanes    rbx

%define job_rax         rax
%define len             rax

%define lane            rbp
%define tmp3            rbp
%define lens3           rbp

%define extra_blocks    r8
%define lens0           r8

%define num_lanes_inuse r9
%define tmp             r9
%define lens1           r9

%define lane_data       r10
%define lens2           r10

struc stack_frame
	.xmm: resb 16*10
	.gpr: resb 8*8
	.rsp: resb 8
endstruc

; STACK_SPACE needs to be an odd multiple of 8
%define _XMM_SAVE       stack_frame.gpr
%define _GPR_SAVE       stack_frame.rsp
%define STACK_SPACE     stack_frame_size

; SHA512_JOB* sha512_mb_mgr_submit_avx512(SHA512_MB_JOB_MGR *state, SHA512_JOB *job)
; arg 1 : rcx : state
; arg 2 : rdx : job
mk_global sha512_mb_mgr_submit_avx512, function
sha512_mb_mgr_submit_avx512:
	endbranch

	mov     rax, rsp

	sub     rsp, STACK_SPACE

	mov     [rsp + stack_frame.rsp], rax

	mov     [rsp + _XMM_SAVE + 8*0], rbx
	mov     [rsp + _XMM_SAVE + 8*1], rbp
	mov     [rsp + _XMM_SAVE + 8*2], r12
	mov     [rsp + _XMM_SAVE + 8*5], r13
	mov     [rsp + _XMM_SAVE + 8*6], r14
	mov     [rsp + _XMM_SAVE + 8*7], r15
%ifidn __OUTPUT_FORMAT__, win64
	mov     [rsp + _XMM_SAVE + 8*3], rsi
	mov     [rsp + _XMM_SAVE + 8*4], rdi
	vmovdqu  [rsp + 16*0], xmm6
	vmovdqu  [rsp + 16*1], xmm7
	vmovdqu  [rsp + 16*2], xmm8
	vmovdqu  [rsp + 16*3], xmm9
	vmovdqu  [rsp + 16*4], xmm10
	vmovdqu  [rsp + 16*5], xmm11
	vmovdqu  [rsp + 16*6], xmm12
	vmovdqu  [rsp + 16*7], xmm13
	vmovdqu  [rsp + 16*8], xmm14
	vmovdqu  [rsp + 16*9], xmm15
%endif

	mov     unused_lanes, [state + _unused_lanes]
	movzx   lane, BYTE(unused_lanes)
	shr     unused_lanes, 8
	imul    lane_data, lane, _LANE_DATA_size
	mov     dword [job + _status], STS_BEING_PROCESSED
	lea     lane_data, [state + _ldata + lane_data]
	mov     [state + _unused_lanes], unused_lanes
	mov     DWORD(len), [job + _len]

	mov     [lane_data + _job_in_lane], job
	mov     [state + _lens + 4 + 8*lane], DWORD(len)


	; Load digest words from result_digest
	vmovdqa  xmm0, [job + _result_digest + 0*16]
	vmovdqa  xmm1, [job + _result_digest + 1*16]
	vmovdqa  xmm2, [job + _result_digest + 2*16]
	vmovdqa  xmm3, [job + _result_digest + 3*16]
	vmovq    [state + _args_digest + 8*lane + 0*64], xmm0
	vpextrq  [state + _args_digest + 8*lane + 1*64], xmm0, 1
	vmovq    [state + _args_digest + 8*lane + 2*64], xmm1
	vpextrq  [state + _args_digest + 8*lane + 3*64], xmm1, 1
	vmovq    [state + _args_digest + 8*lane + 4*64], xmm2
	vpextrq  [state + _args_digest + 8*lane + 5*64], xmm2, 1
	vmovq    [state + _args_digest + 8*lane + 6*64], xmm3
	vpextrq  [state + _args_digest + 8*lane + 7*64], xmm3, 1

	mov     p, [job + _buffer]
	mov     [state + _args_data_ptr + 8*lane], p

	mov	DWORD(num_lanes_inuse), [state + _num_lanes_inuse]
        add     num_lanes_inuse, 1
	mov	[state + _num_lanes_inuse], DWORD(num_lanes_inuse)
        cmp     num_lanes_inuse, 8
	jne     return_null

start_loop:
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
	shr     len2, 32
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


	mov     unused_lanes, [state + _unused_lanes]
	mov     qword [lane_data + _job_in_lane], 0
	mov     dword [job_rax + _status], STS_COMPLETED
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
	vmovdqu  xmm6,  [rsp + 16*0]
	vmovdqu  xmm7,  [rsp + 16*1]
	vmovdqu  xmm8,  [rsp + 16*2]
	vmovdqu  xmm9,  [rsp + 16*3]
	vmovdqu  xmm10, [rsp + 16*4]
	vmovdqu  xmm11, [rsp + 16*5]
	vmovdqu  xmm12, [rsp + 16*6]
	vmovdqu  xmm13, [rsp + 16*7]
	vmovdqu  xmm14, [rsp + 16*8]
	vmovdqu  xmm15, [rsp + 16*9]
	mov     rsi, [rsp + _XMM_SAVE + 8*3]
	mov     rdi, [rsp + _XMM_SAVE + 8*4]
%endif
	mov     rbx, [rsp + _XMM_SAVE + 8*0]
	mov     rbp, [rsp + _XMM_SAVE + 8*1]
	mov     r12, [rsp + _XMM_SAVE + 8*2]
	mov     r13, [rsp + _XMM_SAVE + 8*5]
	mov     r14, [rsp + _XMM_SAVE + 8*6]
	mov     r15, [rsp + _XMM_SAVE + 8*7]

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

%else
%ifidn __OUTPUT_FORMAT__, win64
global no_sha512_mb_mgr_submit_avx512
no_sha512_mb_mgr_submit_avx512:
%endif
%endif ; HAVE_AS_KNOWS_AVX512
