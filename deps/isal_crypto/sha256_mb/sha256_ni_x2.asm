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

%include "sha256_mb_mgr_datastruct.asm"
%include "reg_sizes.asm"

%ifdef HAVE_AS_KNOWS_SHANI

[bits 64]
default rel
section .text

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%ifidn __OUTPUT_FORMAT__, elf64
 ; Linux
 %define arg0  rdi
 %define arg1  rsi
%else
 ; Windows
 %define arg0   rcx
 %define arg1   rdx
%endif

;; FRAMESZ plus pushes must be an odd multiple of 8
%define FRAMESZ	64		; space for ABCDE
%define RSPSAVE	rax

%define MSG     	xmm0
%define STATE0  	xmm1
%define STATE1  	xmm2
%define MSGTMP0 	xmm3
%define MSGTMP1 	xmm4
%define MSGTMP2 	xmm5
%define MSGTMP3 	xmm6
%define MSGTMP4 	xmm7

%define STATE0b		xmm8
%define STATE1b		xmm9
%define MSGTMP0b	xmm10
%define MSGTMP1b	xmm11
%define MSGTMP2b	xmm12
%define MSGTMP3b	xmm13
%define MSGTMP4b	xmm14

%define SHUF_MASK       xmm15

; arg index is start from 0 while mgr_flush/submit is from 1
%define MGR     arg0
%define NBLK    arg1
%define NLANX4  r10     ; consistent with caller
%define IDX     r8      ; local variable -- consistent with caller
%define DPTR    r11     ; local variable -- input buffer pointer
%define DPTRb   r12
%define TMP     r9      ; local variable -- assistant to address digest
%define TBL     r13
%define TMPb    r14      ; local variable -- assistant to address digest
align 32

; void sha256_ni_x2(SHA256_MB_ARGS_Xn *args, uint32_t size_in_blocks);
; arg 0 : MGR : pointer to args (only 4 of the 16 lanes used)
; arg 1 : NBLK : size (in blocks) ;; assumed to be >= 1
; invisibile arg 2 : IDX : hash on which lane
; invisibile arg 3 : NLANX4 : max lanes*4 for this arch (digest is placed by it)
; 		 (sse/avx is 4, avx2 is 8, avx512 is 16)
;
; Clobbers registers: rax, r9~r14, xmm0-xmm15
;
mk_global sha256_ni_x2, function, internal
sha256_ni_x2:
	endbranch
	mov 	RSPSAVE, rsp
	sub 	rsp, FRAMESZ
	and 	rsp, ~0xF	; Align 16Bytes downward

	shl     NBLK, 6		; transform blk amount into bytes
	jz      backto_mgr

	; detach idx from nlanx4
	mov     IDX, NLANX4
	shr     NLANX4, 8
	and     IDX, 0xff

	lea     TMP, [MGR + 4*0]
	lea     TMPb, [MGR + 4*1]

	;; Initialize digest
	;; digests -> ABEF(state0), CDGH(state1)
	pinsrd  STATE0, [TMP + 0*NLANX4], 3     ; A
	pinsrd  STATE0, [TMP + 1*NLANX4], 2     ; B
	pinsrd  STATE1, [TMP + 2*NLANX4], 3     ; C
	lea     TMP, [TMP + 2*NLANX4]   ; MGR + 4*IDX + 2*NLANX4
	pinsrd  STATE1, [TMP + 1*NLANX4], 2     ; D
	pinsrd  STATE0, [TMP + 2*NLANX4], 1     ; E
	pinsrd  STATE1, [TMP + 4*NLANX4], 1     ; G
	lea     TMP, [TMP + 1*NLANX4]   ; MGR + 4*IDX + 6*NLANX4
	pinsrd  STATE0, [TMP + 2*NLANX4], 0     ; F
	pinsrd  STATE1, [TMP + 4*NLANX4], 0     ; H

	pinsrd  STATE0b, [TMPb + 0*NLANX4], 3     ; A
	pinsrd  STATE0b, [TMPb + 1*NLANX4], 2     ; B
	pinsrd  STATE1b, [TMPb + 2*NLANX4], 3     ; C
	lea     TMPb, [TMPb + 2*NLANX4]   ; MGR + 4*IDX + 2*NLANX4
	pinsrd  STATE1b, [TMPb + 1*NLANX4], 2     ; D
	pinsrd  STATE0b, [TMPb + 2*NLANX4], 1     ; E
	pinsrd  STATE1b, [TMPb + 4*NLANX4], 1     ; G
	lea     TMPb, [TMPb + 1*NLANX4]   ; MGR + 4*IDX + 6*NLANX4
	pinsrd  STATE0b, [TMPb + 2*NLANX4], 0     ; F
	pinsrd  STATE1b, [TMPb + 4*NLANX4], 0     ; H

	movdqa  SHUF_MASK, [PSHUFFLE_SHANI_MASK]
	lea     TBL, [TABLE]

	;; Load input pointers
	mov     DPTR, [MGR + _data_ptr + 8*0]
	mov     DPTRb,[MGR + _data_ptr + 8*1]
	;; nblk is used to indicate data end
	add     NBLK, DPTR

lloop:
	; /* Save hash values for addition after rounds */
	movdqa		[rsp + 0*16], STATE0
	movdqa		[rsp + 1*16], STATE1

	movdqa		[rsp + 2*16], STATE0b
	movdqa		[rsp + 3*16], STATE1b

	; /* Rounds 0-3 */
	movdqu  	MSG, [DPTR + 0*16]
	pshufb  	MSG, SHUF_MASK
	movdqa  	MSGTMP0, MSG
		paddd   	MSG, [TBL + 0*16]
		sha256rnds2     STATE1, STATE0, MSG
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0, STATE1, MSG

	movdqu  	MSG, [DPTRb + 0*16]
	pshufb  	MSG, SHUF_MASK
	movdqa  	MSGTMP0b, MSG
		paddd   	MSG, [TBL + 0*16]
		sha256rnds2     STATE1b, STATE0b, MSG
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0b, STATE1b, MSG

	; /* Rounds 4-7 */
	movdqu  	MSG, [DPTR + 1*16]
	pshufb  	MSG, SHUF_MASK
	movdqa  	MSGTMP1, MSG
		paddd   	MSG, [TBL + 1*16]
		sha256rnds2     STATE1, STATE0, MSG
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0, STATE1, MSG
	sha256msg1      MSGTMP0, MSGTMP1

	movdqu  	MSG, [DPTRb + 1*16]
	pshufb  	MSG, SHUF_MASK
	movdqa  	MSGTMP1b, MSG
		paddd   	MSG, [TBL + 1*16]
		sha256rnds2     STATE1b, STATE0b, MSG
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0b, STATE1b, MSG
	sha256msg1      MSGTMP0b, MSGTMP1b

	; /* Rounds 8-11 */
	movdqu  	MSG, [DPTR + 2*16]
	pshufb  	MSG, SHUF_MASK
	movdqa  	MSGTMP2, MSG
		paddd   	MSG, [TBL + 2*16]
		sha256rnds2     STATE1, STATE0, MSG
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0, STATE1, MSG
	sha256msg1      MSGTMP1, MSGTMP2

	movdqu  	MSG, [DPTRb + 2*16]
	pshufb  	MSG, SHUF_MASK
	movdqa  	MSGTMP2b, MSG
		paddd   	MSG, [TBL + 2*16]
		sha256rnds2     STATE1b, STATE0b, MSG
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0b, STATE1b, MSG
	sha256msg1      MSGTMP1b, MSGTMP2b

	; /* Rounds 12-15 */
	movdqu  	MSG, [DPTR + 3*16]
	pshufb  	MSG, SHUF_MASK
	movdqa  	MSGTMP3, MSG
		paddd   	MSG, [TBL + 3*16]
		sha256rnds2     STATE1, STATE0, MSG
	movdqa  	MSGTMP4, MSGTMP3
	palignr 	MSGTMP4, MSGTMP2, 4
	paddd   	MSGTMP0, MSGTMP4
	sha256msg2      MSGTMP0, MSGTMP3
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0, STATE1, MSG
	sha256msg1      MSGTMP2, MSGTMP3

	movdqu  	MSG, [DPTRb + 3*16]
	pshufb  	MSG, SHUF_MASK
	movdqa  	MSGTMP3b, MSG
		paddd   	MSG, [TBL + 3*16]
		sha256rnds2     STATE1b, STATE0b, MSG
	movdqa  	MSGTMP4b, MSGTMP3b
	palignr 	MSGTMP4b, MSGTMP2b, 4
	paddd   	MSGTMP0b, MSGTMP4b
	sha256msg2      MSGTMP0b, MSGTMP3b
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0b, STATE1b, MSG
	sha256msg1      MSGTMP2b, MSGTMP3b

	; /* Rounds 16-19 */
	movdqa  	MSG, MSGTMP0
		paddd   	MSG, [TBL + 4*16]
		sha256rnds2     STATE1, STATE0, MSG
	movdqa  	MSGTMP4, MSGTMP0
	palignr 	MSGTMP4, MSGTMP3, 4
	paddd   	MSGTMP1, MSGTMP4
	sha256msg2      MSGTMP1, MSGTMP0
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0, STATE1, MSG
	sha256msg1      MSGTMP3, MSGTMP0

	movdqa  	MSG, MSGTMP0b
		paddd   	MSG, [TBL + 4*16]
		sha256rnds2     STATE1b, STATE0b, MSG
	movdqa  	MSGTMP4b, MSGTMP0b
	palignr 	MSGTMP4b, MSGTMP3b, 4
	paddd   	MSGTMP1b, MSGTMP4b
	sha256msg2      MSGTMP1b, MSGTMP0b
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0b, STATE1b, MSG
	sha256msg1      MSGTMP3b, MSGTMP0b

	; /* Rounds 20-23 */
	movdqa  	MSG, MSGTMP1
		paddd   	MSG, [TBL + 5*16]
		sha256rnds2     STATE1, STATE0, MSG
	movdqa  	MSGTMP4, MSGTMP1
	palignr 	MSGTMP4, MSGTMP0, 4
	paddd   	MSGTMP2, MSGTMP4
	sha256msg2      MSGTMP2, MSGTMP1
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0, STATE1, MSG
	sha256msg1      MSGTMP0, MSGTMP1

	movdqa  	MSG, MSGTMP1b
		paddd   	MSG, [TBL + 5*16]
		sha256rnds2     STATE1b, STATE0b, MSG
	movdqa  	MSGTMP4b, MSGTMP1b
	palignr 	MSGTMP4b, MSGTMP0b, 4
	paddd   	MSGTMP2b, MSGTMP4b
	sha256msg2      MSGTMP2b, MSGTMP1b
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0b, STATE1b, MSG
	sha256msg1      MSGTMP0b, MSGTMP1b

	; /* Rounds 24-27 */
	movdqa  	MSG, MSGTMP2
		paddd   	MSG, [TBL + 6*16]
		sha256rnds2     STATE1, STATE0, MSG
	movdqa  	MSGTMP4, MSGTMP2
	palignr 	MSGTMP4, MSGTMP1, 4
	paddd   	MSGTMP3, MSGTMP4
	sha256msg2      MSGTMP3, MSGTMP2
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0, STATE1, MSG
	sha256msg1      MSGTMP1, MSGTMP2

	movdqa  	MSG, MSGTMP2b
		paddd   	MSG, [TBL + 6*16]
		sha256rnds2     STATE1b, STATE0b, MSG
	movdqa  	MSGTMP4b, MSGTMP2b
	palignr 	MSGTMP4b, MSGTMP1b, 4
	paddd   	MSGTMP3b, MSGTMP4b
	sha256msg2      MSGTMP3b, MSGTMP2b
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0b, STATE1b, MSG
	sha256msg1      MSGTMP1b, MSGTMP2b

	; /* Rounds 28-31 */
	movdqa  	MSG, MSGTMP3
		paddd   	MSG, [TBL + 7*16]
		sha256rnds2     STATE1, STATE0, MSG
	movdqa  	MSGTMP4, MSGTMP3
	palignr 	MSGTMP4, MSGTMP2, 4
	paddd   	MSGTMP0, MSGTMP4
	sha256msg2      MSGTMP0, MSGTMP3
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0, STATE1, MSG
	sha256msg1      MSGTMP2, MSGTMP3

	movdqa  	MSG, MSGTMP3b
		paddd   	MSG, [TBL + 7*16]
		sha256rnds2     STATE1b, STATE0b, MSG
	movdqa  	MSGTMP4b, MSGTMP3b
	palignr 	MSGTMP4b, MSGTMP2b, 4
	paddd   	MSGTMP0b, MSGTMP4b
	sha256msg2      MSGTMP0b, MSGTMP3b
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0b, STATE1b, MSG
	sha256msg1      MSGTMP2b, MSGTMP3b

	; /* Rounds 32-35 */
	movdqa  	MSG, MSGTMP0
		paddd   	MSG, [TBL + 8*16]
		sha256rnds2     STATE1, STATE0, MSG
	movdqa  	MSGTMP4, MSGTMP0
	palignr 	MSGTMP4, MSGTMP3, 4
	paddd   	MSGTMP1, MSGTMP4
	sha256msg2      MSGTMP1, MSGTMP0
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0, STATE1, MSG
	sha256msg1      MSGTMP3, MSGTMP0

	movdqa  	MSG, MSGTMP0b
		paddd   	MSG, [TBL + 8*16]
		sha256rnds2     STATE1b, STATE0b, MSG
	movdqa  	MSGTMP4b, MSGTMP0b
	palignr 	MSGTMP4b, MSGTMP3b, 4
	paddd   	MSGTMP1b, MSGTMP4b
	sha256msg2      MSGTMP1b, MSGTMP0b
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0b, STATE1b, MSG
	sha256msg1      MSGTMP3b, MSGTMP0b

	; /* Rounds 36-39 */
	movdqa  	MSG, MSGTMP1
		paddd   	MSG, [TBL + 9*16]
		sha256rnds2     STATE1, STATE0, MSG
	movdqa  	MSGTMP4, MSGTMP1
	palignr 	MSGTMP4, MSGTMP0, 4
	paddd   	MSGTMP2, MSGTMP4
	sha256msg2      MSGTMP2, MSGTMP1
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0, STATE1, MSG
	sha256msg1      MSGTMP0, MSGTMP1

	movdqa  	MSG, MSGTMP1b
		paddd   	MSG, [TBL + 9*16]
		sha256rnds2     STATE1b, STATE0b, MSG
	movdqa  	MSGTMP4b, MSGTMP1b
	palignr 	MSGTMP4b, MSGTMP0b, 4
	paddd   	MSGTMP2b, MSGTMP4b
	sha256msg2      MSGTMP2b, MSGTMP1b
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0b, STATE1b, MSG
	sha256msg1      MSGTMP0b, MSGTMP1b

	; /* Rounds 40-43 */
	movdqa  	MSG, MSGTMP2
		paddd   	MSG, [TBL + 10*16]
		sha256rnds2     STATE1, STATE0, MSG
	movdqa  	MSGTMP4, MSGTMP2
	palignr 	MSGTMP4, MSGTMP1, 4
	paddd   	MSGTMP3, MSGTMP4
	sha256msg2      MSGTMP3, MSGTMP2
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0, STATE1, MSG
	sha256msg1      MSGTMP1, MSGTMP2

	movdqa  	MSG, MSGTMP2b
		paddd   	MSG, [TBL + 10*16]
		sha256rnds2     STATE1b, STATE0b, MSG
	movdqa  	MSGTMP4b, MSGTMP2b
	palignr 	MSGTMP4b, MSGTMP1b, 4
	paddd   	MSGTMP3b, MSGTMP4b
	sha256msg2      MSGTMP3b, MSGTMP2b
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0b, STATE1b, MSG
	sha256msg1      MSGTMP1b, MSGTMP2b

	; /* Rounds 44-47 */
	movdqa  	MSG, MSGTMP3
		paddd   	MSG, [TBL + 11*16]
		sha256rnds2     STATE1, STATE0, MSG
	movdqa  	MSGTMP4, MSGTMP3
	palignr 	MSGTMP4, MSGTMP2, 4
	paddd   	MSGTMP0, MSGTMP4
	sha256msg2      MSGTMP0, MSGTMP3
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0, STATE1, MSG
	sha256msg1      MSGTMP2, MSGTMP3

	movdqa  	MSG, MSGTMP3b
		paddd   	MSG, [TBL + 11*16]
		sha256rnds2     STATE1b, STATE0b, MSG
	movdqa  	MSGTMP4b, MSGTMP3b
	palignr 	MSGTMP4b, MSGTMP2b, 4
	paddd   	MSGTMP0b, MSGTMP4b
	sha256msg2      MSGTMP0b, MSGTMP3b
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0b, STATE1b, MSG
	sha256msg1      MSGTMP2b, MSGTMP3b

	; /* Rounds 48-51 */
	movdqa  	MSG, MSGTMP0
		paddd   	MSG, [TBL + 12*16]
		sha256rnds2     STATE1, STATE0, MSG
	movdqa  	MSGTMP4, MSGTMP0
	palignr 	MSGTMP4, MSGTMP3, 4
	paddd   	MSGTMP1, MSGTMP4
	sha256msg2      MSGTMP1, MSGTMP0
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0, STATE1, MSG
	sha256msg1      MSGTMP3, MSGTMP0

	movdqa  	MSG, MSGTMP0b
		paddd   	MSG, [TBL + 12*16]
		sha256rnds2     STATE1b, STATE0b, MSG
	movdqa  	MSGTMP4b, MSGTMP0b
	palignr 	MSGTMP4b, MSGTMP3b, 4
	paddd   	MSGTMP1b, MSGTMP4b
	sha256msg2      MSGTMP1b, MSGTMP0b
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0b, STATE1b, MSG
	sha256msg1      MSGTMP3b, MSGTMP0b

	; /* Rounds 52-55 */
	movdqa  	MSG, MSGTMP1
		paddd   	MSG, [TBL + 13*16]
		sha256rnds2     STATE1, STATE0, MSG
	movdqa  	MSGTMP4, MSGTMP1
	palignr 	MSGTMP4, MSGTMP0, 4
	paddd   	MSGTMP2, MSGTMP4
	sha256msg2      MSGTMP2, MSGTMP1
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0, STATE1, MSG

	movdqa  	MSG, MSGTMP1b
		paddd   	MSG, [TBL + 13*16]
		sha256rnds2     STATE1b, STATE0b, MSG
	movdqa  	MSGTMP4b, MSGTMP1b
	palignr 	MSGTMP4b, MSGTMP0b, 4
	paddd   	MSGTMP2b, MSGTMP4b
	sha256msg2      MSGTMP2b, MSGTMP1b
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0b, STATE1b, MSG

	; /* Rounds 56-59 */
	movdqa  	MSG, MSGTMP2
		paddd   	MSG, [TBL + 14*16]
		sha256rnds2     STATE1, STATE0, MSG
	movdqa  	MSGTMP4, MSGTMP2
	palignr 	MSGTMP4, MSGTMP1, 4
	paddd   	MSGTMP3, MSGTMP4
	sha256msg2      MSGTMP3, MSGTMP2
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0, STATE1, MSG

	movdqa  	MSG, MSGTMP2b
		paddd   	MSG, [TBL + 14*16]
		sha256rnds2     STATE1b, STATE0b, MSG
	movdqa  	MSGTMP4b, MSGTMP2b
	palignr 	MSGTMP4b, MSGTMP1b, 4
	paddd   	MSGTMP3b, MSGTMP4b
	sha256msg2      MSGTMP3b, MSGTMP2b
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0b, STATE1b, MSG

	; /* Rounds 60-63 */
	movdqa  	MSG, MSGTMP3
		paddd   	MSG, [TBL + 15*16]
		sha256rnds2     STATE1, STATE0, MSG
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0, STATE1, MSG

	movdqa  	MSG, MSGTMP3b
		paddd   	MSG, [TBL + 15*16]
		sha256rnds2     STATE1b, STATE0b, MSG
		pshufd  	MSG, MSG, 0x0E
		sha256rnds2     STATE0b, STATE1b, MSG

	; /* Add current hash values with previously saved */
	paddd   	STATE0, [rsp + 0*16]
	paddd   	STATE1, [rsp + 1*16]

	paddd   	STATE0b, [rsp + 2*16]
	paddd   	STATE1b, [rsp + 3*16]

	; Increment data pointer and loop if more to process
	add     	DPTR, 64
	add     	DPTRb, 64
	cmp     	DPTR, NBLK
	jne     	lloop

	; write out digests
	lea     TMP, [MGR + 4*0]
	;; ABEF(state0), CDGH(state1) -> digests
	pextrd  [TMP + 0*NLANX4], STATE0, 3     ; A
	pextrd  [TMP + 1*NLANX4], STATE0, 2     ; B
	pextrd  [TMP + 2*NLANX4], STATE1, 3     ; C
	lea     TMP, [TMP + 2*NLANX4]   ; MGR + 4*IDX + 2*NLANX4
	pextrd  [TMP + 1*NLANX4], STATE1, 2     ; D
	pextrd  [TMP + 2*NLANX4], STATE0, 1     ; E
	pextrd  [TMP + 4*NLANX4], STATE1, 1     ; G
	lea     TMP, [TMP + 1*NLANX4]   ; MGR + 4*IDX + 6*NLANX4
	pextrd  [TMP + 2*NLANX4], STATE0, 0     ; F
	pextrd  [TMP + 4*NLANX4], STATE1, 0     ; H

	lea     TMPb, [MGR + 4*1]
	;; ABEF(state0), CDGH(state1) -> digests
	pextrd  [TMPb + 0*NLANX4], STATE0b, 3     ; A
	pextrd  [TMPb + 1*NLANX4], STATE0b, 2     ; B
	pextrd  [TMPb + 2*NLANX4], STATE1b, 3     ; C
	lea     TMPb, [TMPb + 2*NLANX4]   ; MGR + 4*IDX + 2*NLANX4
	pextrd  [TMPb + 1*NLANX4], STATE1b, 2     ; D
	pextrd  [TMPb + 2*NLANX4], STATE0b, 1     ; E
	pextrd  [TMPb + 4*NLANX4], STATE1b, 1     ; G
	lea     TMPb, [TMPb + 1*NLANX4]   ; MGR + 4*IDX + 6*NLANX4
	pextrd  [TMPb + 2*NLANX4], STATE0b, 0     ; F
	pextrd  [TMPb + 4*NLANX4], STATE1b, 0     ; H

	; update input pointers
	mov     [MGR + _data_ptr + 0*8], DPTR
	mov     [MGR + _data_ptr + 1*8], DPTRb

backto_mgr:
	;;;;;;;;;;;;;;;;
	;; Postamble
	mov     rsp, RSPSAVE

	ret

section .data align=16
PSHUFFLE_SHANI_MASK:    dq 0x0405060700010203, 0x0c0d0e0f08090a0b
TABLE:	dd	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5
	dd      0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5
	dd      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3
	dd      0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174
	dd      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc
	dd      0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da
	dd      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7
	dd      0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967
	dd      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13
	dd      0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85
	dd      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3
	dd      0xd192e819,0xd6990624,0xf40e3585,0x106aa070
	dd      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5
	dd      0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3
	dd      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208
	dd      0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
%else
%ifidn __OUTPUT_FORMAT__, win64
global no_sha256_ni_x2
no_sha256_ni_x2:
%endif
%endif ; HAVE_AS_KNOWS_SHANI
