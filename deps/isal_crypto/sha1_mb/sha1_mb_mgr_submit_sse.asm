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

%include "sha1_job.asm"
%include "sha1_mb_mgr_datastruct.asm"

%include "reg_sizes.asm"

extern sha1_mb_x4_sse

[bits 64]
default rel
section .text

%ifidn __OUTPUT_FORMAT__, win64
; WINDOWS register definitions
%define arg1    rcx
%define arg2    rdx

; idx needs to be other than ARG2, rax, r8-r11
%define last_len        rsi
%define idx             rsi

%define size_offset     rdi
%define tmp2            rdi

%else
; LINUX register definitions
%define arg1    rdi
%define arg2    rsi

; idx needs to be other than ARG2, rax, r8-r11
%define last_len        rdx
%define idx             rdx

%define size_offset     rcx
%define tmp2            rcx

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

%define tmp             r9
%define lens1           r9

%define lane_data       r10
%define lens2           r10

; STACK_SPACE needs to be an odd multiple of 8
%define STACK_SPACE     8*4 + 16*10 + 8

; SHA1_JOB* sha1_mb_mgr_submit_sse(SHA1_MB_JOB_MGR *state, SHA1_JOB *job)
; arg 1 : rcx : state
; arg 2 : rdx : job
mk_global sha1_mb_mgr_submit_sse, function
sha1_mb_mgr_submit_sse:
	endbranch

	sub     rsp, STACK_SPACE
	mov     [rsp + 8*0], rbx
	mov     [rsp + 8*3], rbp
%ifidn __OUTPUT_FORMAT__, win64
	mov     [rsp + 8*1], rsi
	mov     [rsp + 8*2], rdi
	movdqa  [rsp + 8*4 + 16*0], xmm6
	movdqa  [rsp + 8*4 + 16*1], xmm7
	movdqa  [rsp + 8*4 + 16*2], xmm8
	movdqa  [rsp + 8*4 + 16*3], xmm9
	movdqa  [rsp + 8*4 + 16*4], xmm10
	movdqa  [rsp + 8*4 + 16*5], xmm11
	movdqa  [rsp + 8*4 + 16*6], xmm12
	movdqa  [rsp + 8*4 + 16*7], xmm13
	movdqa  [rsp + 8*4 + 16*8], xmm14
	movdqa  [rsp + 8*4 + 16*9], xmm15
%endif

	mov     unused_lanes, [state + _unused_lanes]
	movzx   lane, BYTE(unused_lanes)
	and     lane, 0xF
	shr     unused_lanes, 4
	imul    lane_data, lane, _LANE_DATA_size
	mov     dword [job + _status], STS_BEING_PROCESSED
	lea     lane_data, [state + _ldata + lane_data]
	mov     [state + _unused_lanes], unused_lanes
	mov     DWORD(len), [job + _len]

	shl	len, 4
	or	len, lane

	mov     [lane_data + _job_in_lane], job
	mov     [state + _lens + 4*lane], DWORD(len)

	; Load digest words from result_digest
	movdqa	xmm0, [job + _result_digest + 0*16]
	mov	DWORD(tmp), [job + _result_digest + 1*16]
	movd    [state + _args_digest + 4*lane + 0*16], xmm0
	pextrd  [state + _args_digest + 4*lane + 1*16], xmm0, 1
	pextrd  [state + _args_digest + 4*lane + 2*16], xmm0, 2
	pextrd  [state + _args_digest + 4*lane + 3*16], xmm0, 3
	mov     [state + _args_digest + 4*lane + 4*16], DWORD(tmp)

	mov     p, [job + _buffer]
	mov     [state + _args_data_ptr + 8*lane], p

	add	dword [state + _num_lanes_inuse], 1
	cmp     unused_lanes, 0xF
	jne     return_null

start_loop:
	; Find min length
	mov     DWORD(lens0), [state + _lens + 0*4]
	mov     idx, lens0
	mov     DWORD(lens1), [state + _lens + 1*4]
	cmp     lens1, idx
	cmovb   idx, lens1
	mov     DWORD(lens2), [state + _lens + 2*4]
	cmp     lens2, idx
	cmovb   idx, lens2
	mov     DWORD(lens3), [state + _lens + 3*4]
	cmp     lens3, idx
	cmovb   idx, lens3
	mov     len2, idx
	and     idx, 0xF
	and     len2, ~0xF
	jz      len_is_0

	sub     lens0, len2
	sub     lens1, len2
	sub     lens2, len2
	sub     lens3, len2
	shr     len2, 4
	mov     [state + _lens + 0*4], DWORD(lens0)
	mov     [state + _lens + 1*4], DWORD(lens1)
	mov     [state + _lens + 2*4], DWORD(lens2)
	mov     [state + _lens + 3*4], DWORD(lens3)

	; "state" and "args" are the same address, arg1
	; len is arg2
	call    sha1_mb_x4_sse
	; state and idx are intact

len_is_0:
	; process completed job "idx"
	imul    lane_data, idx, _LANE_DATA_size
	lea     lane_data, [state + _ldata + lane_data]

	mov     job_rax, [lane_data + _job_in_lane]
	mov     unused_lanes, [state + _unused_lanes]
	mov     qword [lane_data + _job_in_lane], 0
	mov     dword [job_rax + _status], STS_COMPLETED
	shl     unused_lanes, 4
	or      unused_lanes, idx
	mov     [state + _unused_lanes], unused_lanes

	sub     dword [state + _num_lanes_inuse], 1

	movd    xmm0, [state + _args_digest + 4*idx + 0*16]
	pinsrd  xmm0, [state + _args_digest + 4*idx + 1*16], 1
	pinsrd  xmm0, [state + _args_digest + 4*idx + 2*16], 2
	pinsrd  xmm0, [state + _args_digest + 4*idx + 3*16], 3
	mov     DWORD(tmp),  [state + _args_digest + 4*idx + 4*16]

	movdqa  [job_rax + _result_digest + 0*16], xmm0
	mov     [job_rax + _result_digest + 1*16], DWORD(tmp)

return:

%ifidn __OUTPUT_FORMAT__, win64
	movdqa  xmm6, [rsp + 8*4 + 16*0]
	movdqa  xmm7, [rsp + 8*4 + 16*1]
	movdqa  xmm8, [rsp + 8*4 + 16*2]
	movdqa  xmm9, [rsp + 8*4 + 16*3]
	movdqa  xmm10, [rsp + 8*4 + 16*4]
	movdqa  xmm11, [rsp + 8*4 + 16*5]
	movdqa  xmm12, [rsp + 8*4 + 16*6]
	movdqa  xmm13, [rsp + 8*4 + 16*7]
	movdqa  xmm14, [rsp + 8*4 + 16*8]
	movdqa  xmm15, [rsp + 8*4 + 16*9]
	mov     rsi, [rsp + 8*1]
	mov     rdi, [rsp + 8*2]
%endif
	mov     rbx, [rsp + 8*0]
	mov     rbp, [rsp + 8*3]
	add     rsp, STACK_SPACE

	ret

return_null:
	xor     job_rax, job_rax
	jmp     return


section .data align=16

align 16
H0:     dd  0x67452301
H1:     dd  0xefcdab89
H2:     dd  0x98badcfe
H3:     dd  0x10325476
H4:     dd  0xc3d2e1f0

