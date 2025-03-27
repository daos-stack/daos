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

;;; uint64_t rolling_hash2_run_until_00(uint32_t *idx, uint32_t buffer_length, uint64_t *t1,
;;; 			uint64_t *t2, uint8_t *b1, uint8_t *b2, uint64_t h, uint64_t mask,
;;;			uint64_t trigger)

%include "reg_sizes.asm"

%ifidn __OUTPUT_FORMAT__, elf64
 %define arg0  rdi
 %define arg1  rsi
 %define arg2  rdx
 %define arg3  rcx
 %define arg4  r8
 %define arg5  r9

 %define arg6  r10
 %define arg7  r11
 %define arg8  r12		; must be saved and loaded
 %define tmp1  rbp		; must be saved and loaded
 %define tmp2  rbx		; must be saved and loaded
 %define tmp3  r13		; must be saved and loaded
 %define tmp4  r14		; must be saved and loaded
 %define tmp5  r15		; must be saved and loaded
 %define return rax
 %define PS 8
 %define frame_size 6*8
 %define arg(x)      [rsp + frame_size + PS + PS*x]

 %define func(x) x:
 %macro FUNC_SAVE 0
	push	rbp
	push	rbx
	push	r12
	push	r13
	push	r14
	push	r15
	mov	arg6, arg(0)
	mov	arg7, arg(1)
	mov	arg8, arg(2)
 %endmacro
 %macro FUNC_RESTORE 0
	pop	r15
	pop	r14
	pop	r13
	pop	r12
	pop	rbx
	pop	rbp
 %endmacro
%endif

%ifidn __OUTPUT_FORMAT__, win64
 %define arg0   rcx
 %define arg1   rdx
 %define arg2   r8
 %define arg3   r9
 %define arg4   r12 		; must be saved and loaded
 %define arg5   r13 		; must be saved and loaded
 %define arg6   r14 		; must be saved and loaded
 %define arg7   r15 		; must be saved and loaded
 %define arg8   rbx 		; must be saved and loaded
 %define tmp1   r10
 %define tmp2   r11
 %define tmp3   rdi 		; must be saved and loaded
 %define tmp4   rsi 		; must be saved and loaded
 %define tmp5   rbp 		; must be saved and loaded
 %define return rax
 %define PS 8
 %define frame_size 8*8
 %define arg(x)      [rsp + frame_size + PS + PS*x]
 %define func(x) proc_frame x
 %macro FUNC_SAVE 0
	push_reg	r12
	push_reg	r13
	push_reg	r14
	push_reg	r15
	push_reg	rbx
	push_reg	rdi
	push_reg	rsi
	push_reg	rbp
	end_prolog
	mov	arg4, arg(4)
	mov	arg5, arg(5)
	mov	arg6, arg(6)
	mov	arg7, arg(7)
	mov	arg8, arg(8)
 %endmacro

 %macro FUNC_RESTORE 0
	pop	rbp
	pop	rsi
	pop	rdi
	pop	rbx
	pop	r15
	pop	r14
	pop	r13
	pop	r12
 %endmacro
%endif

%define idx   arg0
%define max   arg1
%define t1    arg2
%define t2    arg3
%define b1    arg4
%define b2    arg5
%define hash  arg6
%define mask  arg7
%define trigger arg8

%define pos   rax
%define pos.w eax
%define x     tmp2
%define y     tmp3
%define z     tmp4
%define h     tmp1
%define a     tmp5

default rel
[bits 64]
section .text

align 16
mk_global rolling_hash2_run_until_00, function
func(rolling_hash2_run_until_00)
	endbranch
	FUNC_SAVE
	mov	pos.w, dword [idx]
	sub	max, 2
	cmp	pos, max
	jg	.less_than_2

.loop2:	ror	hash, 0x3f
	movzx	x, byte [b1 + pos]
	movzx	a, byte [b1 + pos + 1]
	movzx	y, byte [b2 + pos]
	movzx	h, byte [b2 + pos + 1]
	mov	z, [t1 + x * 8]
	xor	z, [t2 + y * 8]
	xor	hash, z
	mov	x, hash
	and	x, mask
	cmp	x, trigger
	je	.ret_0

	ror	hash, 0x3f
	mov	z, [t1 + a * 8]
	xor	z, [t2 + h * 8]
	xor	hash, z
	mov	y, hash
	and	y, mask
	cmp	y, trigger
	je	.ret_1

	add	pos, 2
	cmp	pos, max
	jle	.loop2

.less_than_2:
	add	max, 1
	cmp	pos, max
	jg	.ret_0
	ror	hash, 0x3f
	movzx	x, byte [b1 + pos]
	movzx	y, byte [b2 + pos]
	mov	z, [t1 + x * 8]
	xor	z, [t2 + y * 8]
	xor	hash, z
.ret_1:	add	pos, 1
.ret_0:	mov	dword [idx], pos.w
	mov	rax, hash
	FUNC_RESTORE
	ret

endproc_frame

section .data
