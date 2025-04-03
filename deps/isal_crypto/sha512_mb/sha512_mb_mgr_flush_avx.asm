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

extern sha512_mb_x2_avx

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

%define lens1           r9
%define lens2           r10
%define lens3           r11

; STACK_SPACE needs to be an odd multiple of 8
_XMM_SAVE_SIZE  equ 10*16
_GPR_SAVE_SIZE  equ 8*3
_ALIGN_SIZE     equ 0

_XMM_SAVE       equ 0
_GPR_SAVE       equ _XMM_SAVE + _XMM_SAVE_SIZE
STACK_SPACE     equ _GPR_SAVE + _GPR_SAVE_SIZE + _ALIGN_SIZE

%define APPEND(a,b) a %+ b

; SHA512_JOB* sha512_mb_mgr_flush_avx(SHA512_MB_JOB_MGR *state)
; arg 1 : rcx : state
mk_global sha512_mb_mgr_flush_avx, function
sha512_mb_mgr_flush_avx:
	endbranch

	sub     rsp, STACK_SPACE
	mov     [rsp + _GPR_SAVE + 8*0], rbx
	mov     [rsp + _GPR_SAVE + 8*1], r12
%ifidn __OUTPUT_FORMAT__, win64
	mov     [rsp + _GPR_SAVE + 8*2], rsi
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

	mov     unused_lanes, [state + _unused_lanes]
	bt      unused_lanes, 16+7
	jc      return_null

	; find a lane with a non-null job
	xor     idx, idx
	cmp     qword [state + _ldata + 1 * _LANE_DATA_size + _job_in_lane], 0
	cmovne  idx, [one]

	; copy idx to empty lanes
copy_lane_data:
	mov     tmp, [state + _args + _data_ptr + 8*idx]

%assign I 0
%rep 2
	cmp     qword [state + _ldata + I * _LANE_DATA_size + _job_in_lane], 0
	jne     APPEND(skip_,I)
	mov     [state + _args + _data_ptr + 8*I], tmp
	mov     dword [state + _lens + 4 + 8*I], 0xFFFFFFFF
APPEND(skip_,I):
%assign I (I+1)
%endrep

	; Find min length
	mov     lens0, [state + _lens + 0*8]
	mov     idx, lens0
	mov     lens1, [state + _lens + 1*8]
	cmp     lens1, idx
	cmovb   idx, lens1

	mov     len2, idx
	and     idx, 0xF
	and     len2, ~0xFF
	jz      len_is_0

	sub     lens0, len2
	sub     lens1, len2
	shr     len2, 32
	mov     [state + _lens + 0*8], lens0
	mov     [state + _lens + 1*8], lens1

	; "state" and "args" are the same address, arg1
	; len is arg2
	call    sha512_mb_x2_avx
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

	sub     dword [state + _num_lanes_inuse], 1

	vmovq    xmm0, [state + _args_digest + 8*idx + 0*32]
	vpinsrq  xmm0, [state + _args_digest + 8*idx + 1*32], 1
	vmovq    xmm1, [state + _args_digest + 8*idx + 2*32]
	vpinsrq  xmm1, [state + _args_digest + 8*idx + 3*32], 1
	vmovq    xmm2, [state + _args_digest + 8*idx + 4*32]
	vpinsrq  xmm2, [state + _args_digest + 8*idx + 5*32], 1
	vmovq    xmm3, [state + _args_digest + 8*idx + 6*32]
	vpinsrq  xmm3, [state + _args_digest + 8*idx + 7*32], 1

	vmovdqa  [job_rax + _result_digest + 0*16], xmm0
	vmovdqa  [job_rax + _result_digest + 1*16], xmm1
	vmovdqa  [job_rax + _result_digest + 2*16], xmm2
	vmovdqa  [job_rax + _result_digest + 3*16], xmm3

return:

%ifidn __OUTPUT_FORMAT__, win64
	vmovdqa  xmm6,  [rsp + _XMM_SAVE + 16*0]
	vmovdqa  xmm7,  [rsp + _XMM_SAVE + 16*1]
	vmovdqa  xmm8,  [rsp + _XMM_SAVE + 16*2]
	vmovdqa  xmm9,  [rsp + _XMM_SAVE + 16*3]
	vmovdqa  xmm10, [rsp + _XMM_SAVE + 16*4]
	vmovdqa  xmm11, [rsp + _XMM_SAVE + 16*5]
	vmovdqa  xmm12, [rsp + _XMM_SAVE + 16*6]
	vmovdqa  xmm13, [rsp + _XMM_SAVE + 16*7]
	vmovdqa  xmm14, [rsp + _XMM_SAVE + 16*8]
	vmovdqa  xmm15, [rsp + _XMM_SAVE + 16*9]
	mov     rsi, [rsp + _GPR_SAVE + 8*2]
%endif
	mov     rbx, [rsp + _GPR_SAVE + 8*0]
	mov     r12, [rsp + _GPR_SAVE + 8*1]
	add     rsp, STACK_SPACE

	ret

return_null:
	xor     job_rax, job_rax
	jmp     return

section .data align=16

align 16
one:    dq  1
two:    dq  2
three:  dq  3

