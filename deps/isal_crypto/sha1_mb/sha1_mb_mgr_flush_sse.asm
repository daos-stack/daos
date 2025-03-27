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
extern sha1_opt_x1

[bits 64]
default rel
section .text

%ifidn __OUTPUT_FORMAT__, elf64
; LINUX register definitions
%define arg1    rdi ; rcx
%define arg2    rsi ; rdx

; idx needs to be other than ARG1, ARG2, rax, r8-r11
%define idx     rdx ; rsi
%else
; WINDOWS register definitions
%define arg1    rcx
%define arg2    rdx

; idx needs to be other than ARG1, ARG2, rax, r8-r11
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
_GPR_SAVE_SIZE  equ 8*2
_ALIGN_SIZE     equ 8

_XMM_SAVE       equ 0
_GPR_SAVE       equ _XMM_SAVE + _XMM_SAVE_SIZE
STACK_SPACE     equ _GPR_SAVE + _GPR_SAVE_SIZE + _ALIGN_SIZE

%define APPEND(a,b) a %+ b

; SHA1_JOB* sha1_mb_mgr_flush_sse(SHA1_MB_JOB_MGR *state)
; arg 1 : rcx : state
mk_global sha1_mb_mgr_flush_sse, function
sha1_mb_mgr_flush_sse:
	endbranch

	sub     rsp, STACK_SPACE
	mov     [rsp + _GPR_SAVE + 8*0], rbx
%ifidn __OUTPUT_FORMAT__, win64
	mov     [rsp + _GPR_SAVE + 8*1], rsi
	movdqa  [rsp + _XMM_SAVE + 16*0], xmm6
	movdqa  [rsp + _XMM_SAVE + 16*1], xmm7
	movdqa  [rsp + _XMM_SAVE + 16*2], xmm8
	movdqa  [rsp + _XMM_SAVE + 16*3], xmm9
	movdqa  [rsp + _XMM_SAVE + 16*4], xmm10
	movdqa  [rsp + _XMM_SAVE + 16*5], xmm11
	movdqa  [rsp + _XMM_SAVE + 16*6], xmm12
	movdqa  [rsp + _XMM_SAVE + 16*7], xmm13
	movdqa  [rsp + _XMM_SAVE + 16*8], xmm14
	movdqa  [rsp + _XMM_SAVE + 16*9], xmm15
%endif

	; use num_lanes_inuse to judge all lanes are empty
	cmp	dword [state + _num_lanes_inuse], 0
	jz	return_null

	; find a lane with a non-null job
	xor     idx, idx
	cmp     qword [state + _ldata + 1 * _LANE_DATA_size + _job_in_lane], 0
	cmovne  idx, [one]
	cmp     qword [state + _ldata + 2 * _LANE_DATA_size + _job_in_lane], 0
	cmovne  idx, [two]
	cmp     qword [state + _ldata + 3 * _LANE_DATA_size + _job_in_lane], 0
	cmovne  idx, [three]

	; copy idx to empty lanes
copy_lane_data:
	mov     tmp, [state + _args + _data_ptr + 8*idx]

%assign I 0
%rep 4
	cmp     qword [state + _ldata + I * _LANE_DATA_size + _job_in_lane], 0
	jne     APPEND(skip_,I)
	mov     [state + _args + _data_ptr + 8*I], tmp
	mov     dword [state + _lens + 4*I], 0xFFFFFFFF
APPEND(skip_,I):
%assign I (I+1)
%endrep

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

	; compare with sha-sb threshold, if num_lanes_inuse <= threshold, using sb func
	cmp	dword [state + _num_lanes_inuse], SHA1_SB_THRESHOLD_SSE
	ja	mb_processing

	; lensN-len2=idx
	shr     len2, 4
	mov     [state + _lens + idx*4], DWORD(idx)
	mov	r10, idx
	or	r10, 0x1000	; sse has 4 lanes *4, r10b is idx, r10b2 is 16
	; "state" and "args" are the same address, arg1
	; len is arg2, idx and nlane in r10
	call    sha1_opt_x1
	; state and idx are intact
	jmp	len_is_0

mb_processing:

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
	mov     qword [lane_data + _job_in_lane], 0
	mov     dword [job_rax + _status], STS_COMPLETED
	mov     unused_lanes, [state + _unused_lanes]
	shl     unused_lanes, 4
	or      unused_lanes, idx
	mov     [state + _unused_lanes], unused_lanes

	sub     dword [state + _num_lanes_inuse], 1

	movd    xmm0, [state + _args_digest + 4*idx + 0*16]
	pinsrd  xmm0, [state + _args_digest + 4*idx + 1*16], 1
	pinsrd  xmm0, [state + _args_digest + 4*idx + 2*16], 2
	pinsrd  xmm0, [state + _args_digest + 4*idx + 3*16], 3
	mov     DWORD(tmp2),  [state + _args_digest + 4*idx + 4*16]

	movdqa  [job_rax + _result_digest + 0*16], xmm0
	mov     [job_rax + _result_digest + 1*16], DWORD(tmp2)

return:

%ifidn __OUTPUT_FORMAT__, win64
	movdqa  xmm6, [rsp + _XMM_SAVE + 16*0]
	movdqa  xmm7, [rsp + _XMM_SAVE + 16*1]
	movdqa  xmm8, [rsp + _XMM_SAVE + 16*2]
	movdqa  xmm9, [rsp + _XMM_SAVE + 16*3]
	movdqa  xmm10, [rsp + _XMM_SAVE + 16*4]
	movdqa  xmm11, [rsp + _XMM_SAVE + 16*5]
	movdqa  xmm12, [rsp + _XMM_SAVE + 16*6]
	movdqa  xmm13, [rsp + _XMM_SAVE + 16*7]
	movdqa  xmm14, [rsp + _XMM_SAVE + 16*8]
	movdqa  xmm15, [rsp + _XMM_SAVE + 16*9]
	mov     rsi, [rsp + _GPR_SAVE + 8*1]
%endif
	mov     rbx, [rsp + _GPR_SAVE + 8*0]
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

