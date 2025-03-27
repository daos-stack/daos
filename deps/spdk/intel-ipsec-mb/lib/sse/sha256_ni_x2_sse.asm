;;
;; Copyright (c) 2012-2021, Intel Corporation
;;
;; Redistribution and use in source and binary forms, with or without
;; modification, are permitted provided that the following conditions are met:
;;
;;     * Redistributions of source code must retain the above copyright notice,
;;       this list of conditions and the following disclaimer.
;;     * Redistributions in binary form must reproduce the above copyright
;;       notice, this list of conditions and the following disclaimer in the
;;       documentation and/or other materials provided with the distribution.
;;     * Neither the name of Intel Corporation nor the names of its contributors
;;       may be used to endorse or promote products derived from this software
;;       without specific prior written permission.
;;
;; THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
;; AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
;; IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
;; DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
;; FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
;; DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
;; SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
;; CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
;; OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
;; OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;;

;; Stack must be aligned to 32 bytes before call
;;
;; Registers:		RAX RBX RCX RDX RBP RSI RDI R8  R9  R10 R11 R12 R13 R14 R15
;;			-----------------------------------------------------------
;; Windows clobbers:	        RCX RDX     RSI RDI             R11
;; Windows preserves:	RAX RBX         RBP         R8  R9  R10     R12 R13 R14 R15
;;			-----------------------------------------------------------
;; Linux clobbers:	        RCX RDX     RSI RDI             R11
;; Linux preserves:	RAX RBX         RBP         R8  R9  R10     R12 R13 R14 R15
;;			-----------------------------------------------------------
;;
;; Linux/Windows clobbers: xmm0 - xmm15

%include "include/os.asm"
;%define DO_DBGPRINT
%include "include/dbgprint.asm"
%include "include/cet.inc"
%include "include/mb_mgr_datastruct.asm"
%include "include/clear_regs.asm"

; resdq = res0 => 16 bytes
struc frame
.ABEF_SAVE	reso	1
.CDGH_SAVE	reso	1
.ABEF_SAVEb	reso	1
.CDGH_SAVEb	reso	1
.align		resq	1
endstruc

%ifdef LINUX
%define arg1	rdi
%define arg2	rsi
%define arg3	rcx
%define arg4	rdx
%else
%define arg1	rcx
%define arg2	rdx
%define arg3	rdi
%define arg4	rsi
%endif

%define args            arg1
%define NUM_BLKS 	arg2

%define INP		arg3
%define INPb		arg4

%define SHA256CONSTANTS	r11

;; MSG MUST be xmm0 (implicit argument)
%define MSG		xmm0
%define STATE0		xmm1
%define STATE1		xmm2
%define MSGTMP0		xmm3
%define MSGTMP1		xmm4
%define MSGTMP2		xmm5
%define MSGTMP3		xmm6
%define MSGTMP4		xmm7

%define STATE0b		xmm8
%define STATE1b		xmm9
%define MSGTMP0b	xmm10
%define MSGTMP1b	xmm11
%define MSGTMP2b	xmm12
%define MSGTMP3b	xmm13
%define MSGTMP		xmm14

%define SHUF_MASK	xmm15

mksection .rodata
default rel
align 64
K256:
	dd	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5
	dd	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5
	dd	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3
	dd	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174
	dd	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc
	dd	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da
	dd	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7
	dd	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967
	dd	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13
	dd	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85
	dd	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3
	dd	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070
	dd	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5
	dd	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3
	dd	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208
	dd	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2

PSHUFFLE_BYTE_FLIP_MASK:
	dq 0x0405060700010203, 0x0c0d0e0f08090a0b

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; void sha256_ni(SHA256_ARGS *args, UINT32 size_in_blocks)
;; arg1 : pointer to args
;; arg2 : size (in blocks) ;; assumed to be >= 1
mksection .text
MKGLOBAL(sha256_ni,function,internal)
align 32
sha256_ni:
	sub		rsp, frame_size

        DBGPRINTL "enter sha256-ni-x2"

	shl		NUM_BLKS, 6	; convert to bytes
	jz		done_hash

        DBGPRINTL64	"jobA/B byte size:", NUM_BLKS

	;; load input pointers
	mov		INP, [args + _data_ptr_sha256 + 0*PTR_SZ]
	mov		INPb, [args + _data_ptr_sha256 + 1*PTR_SZ]

	add		NUM_BLKS, INP	; pointer to end of data

	;; load initial digest
	;; Probably need to reorder these appropriately
	;; DCBA, HGFE -> ABEF, CDGH

	movdqu		STATE0, [args + 0*SHA256NI_DIGEST_ROW_SIZE]
	movdqu		STATE1,	[args + 0*SHA256NI_DIGEST_ROW_SIZE + 16]
	 movdqu		 STATE0b, [args + 1*SHA256NI_DIGEST_ROW_SIZE]
	 movdqu		 STATE1b, [args + 1*SHA256NI_DIGEST_ROW_SIZE + 16]
        DBGPRINTL	"jobA digest in:"
	DBGPRINT_XMM	STATE0
	DBGPRINT_XMM	STATE1
        DBGPRINTL	"jobB digest in:"
	DBGPRINT_XMM	STATE0b
	DBGPRINT_XMM	STATE1b

	pshufd		STATE0, STATE0, 0xB1	; CDAB
	pshufd		STATE1, STATE1, 0x1B	; EFGH
	movdqa		MSGTMP4, STATE0
	 pshufd		 STATE0b, STATE0b, 0xB1	; CDAB
	 pshufd		 STATE1b, STATE1b, 0x1B	; EFGH
	 movdqa		 MSGTMP, STATE0b
	palignr		STATE0, STATE1, 8	; ABEF
	 palignr	 STATE0b, STATE1b, 8	; ABEF
	pblendw		STATE1, MSGTMP4, 0xF0	; CDGH
	 pblendw	 STATE1b, MSGTMP, 0xF0	; CDGH

	lea		SHA256CONSTANTS,[rel K256]
	movdqa		SHUF_MASK, [rel PSHUFFLE_BYTE_FLIP_MASK]

%ifdef DO_DBGPRINT
	;;	prin buffer A
	push		r10
	push		NUM_BLKS
	DBGPRINTL 	"jobA data:"
	xor		r10, r10
	sub		NUM_BLKS, INP
.loop_dbgA:
	movdqu		MSG, [INP + r10 + 0*16]
        DBGPRINT_XMM	MSG
	movdqu		MSG, [INP + r10 + 1*16]
        DBGPRINT_XMM	MSG
	movdqu		MSG, [INP + r10 + 2*16]
        DBGPRINT_XMM	MSG
	movdqu		MSG, [INP + r10 + 3*16]
        DBGPRINT_XMM	MSG
	add		r10, 64
	cmp		NUM_BLKS, r10
	jne		.loop_dbgA
	pop		NUM_BLKS
	pop		r10
%endif

%ifdef DO_DBGPRINT
	;;	prin buffer B
	push		r10
	push		NUM_BLKS
	DBGPRINTL 	"jobB data:"
	xor		r10, r10
	sub		NUM_BLKS, INP
.loop_dbgB:
	movdqu		MSG, [INPb + r10 + 0*16]
        DBGPRINT_XMM	MSG
	movdqu		MSG, [INPb + r10 + 1*16]
        DBGPRINT_XMM	MSG
	movdqu		MSG, [INPb + r10 + 2*16]
        DBGPRINT_XMM	MSG
	movdqu		MSG, [INPb + r10 + 3*16]
        DBGPRINT_XMM	MSG
	add		r10, 64
	cmp		NUM_BLKS, r10
	jne		.loop_dbgB
	pop		NUM_BLKS
	pop		r10
%endif

.loop0:
	;; Save digests
	movdqa		[rsp + frame.ABEF_SAVE], STATE0
	movdqa		[rsp + frame.CDGH_SAVE], STATE1
	movdqa		 [rsp + frame.ABEF_SAVEb], STATE0b
	movdqa		 [rsp + frame.CDGH_SAVEb], STATE1b

	;; Rounds 0-3
	movdqu		MSG, [INP + 0*16]
	pshufb		MSG, SHUF_MASK
	movdqa		MSGTMP0, MSG
		paddd		MSG, [SHA256CONSTANTS + 0*16]
		sha256rnds2	STATE1, STATE0, MSG	; MSG is implicit argument
		pshufd 		MSG, MSG, 0x0E
		sha256rnds2	STATE0, STATE1, MSG	; MSG is implicit argument
	 movdqu		 MSG, [INPb + 0*16]
	 pshufb		 MSG, SHUF_MASK
	 movdqa		 MSGTMP0b, MSG
		 paddd		 MSG, [SHA256CONSTANTS + 0*16]
		 sha256rnds2	 STATE1b, STATE0b, MSG	; MSG is implicit argument
		 pshufd		 MSG, MSG, 0x0E
		 sha256rnds2	 STATE0b, STATE1b, MSG	; MSG is implicit argument

	;; Rounds 4-7
	movdqu		MSG, [INP + 1*16]
	pshufb		MSG, SHUF_MASK
	movdqa		MSGTMP1, MSG
		paddd		MSG, [SHA256CONSTANTS + 1*16]
		sha256rnds2	STATE1, STATE0, MSG	; MSG is implicit argument
		pshufd 		MSG, MSG, 0x0E
		sha256rnds2	STATE0, STATE1, MSG	; MSG is implicit argument
	 movdqu		 MSG, [INPb + 1*16]
	 pshufb		 MSG, SHUF_MASK
	 movdqa		 MSGTMP1b, MSG
		 paddd		 MSG, [SHA256CONSTANTS + 1*16]
		 sha256rnds2	 STATE1b, STATE0b, MSG	; MSG is implicit argument
		 pshufd		 MSG, MSG, 0x0E
		 sha256rnds2	 STATE0b, STATE1b, MSG	; MSG is implicit argument
	sha256msg1	MSGTMP0, MSGTMP1
	 sha256msg1	 MSGTMP0b, MSGTMP1b

	;; Rounds 8-11
	movdqu		MSG, [INP + 2*16]
	pshufb		MSG, SHUF_MASK
	movdqa		MSGTMP2, MSG
		paddd		MSG, [SHA256CONSTANTS + 2*16]
		sha256rnds2	STATE1, STATE0, MSG	; MSG is implicit argument
		pshufd 		MSG, MSG, 0x0E
		sha256rnds2	STATE0, STATE1, MSG	; MSG is implicit argument
	 movdqu		 MSG, [INPb + 2*16]
	 pshufb		 MSG, SHUF_MASK
	 movdqa		 MSGTMP2b, MSG
		 paddd		 MSG, [SHA256CONSTANTS + 2*16]
		 sha256rnds2	 STATE1b, STATE0b, MSG	; MSG is implicit argument
		 pshufd		 MSG, MSG, 0x0E
		 sha256rnds2	 STATE0b, STATE1b, MSG	; MSG is implicit argument
	sha256msg1	MSGTMP1, MSGTMP2
	 sha256msg1	 MSGTMP1b, MSGTMP2b

	;; Rounds 12-15
	movdqu		MSG, [INP + 3*16]
	pshufb		MSG, SHUF_MASK
	movdqa		MSGTMP3, MSG
		paddd		MSG, [SHA256CONSTANTS + 3*16]
		sha256rnds2	STATE1, STATE0, MSG	; MSG is implicit argument
	movdqa		MSGTMP, MSGTMP3
	palignr		MSGTMP, MSGTMP2, 4
	paddd		MSGTMP0, MSGTMP
	sha256msg2	MSGTMP0, MSGTMP3
		pshufd 		MSG, MSG, 0x0E
		sha256rnds2	STATE0, STATE1, MSG	; MSG is implicit argument
	 movdqu		 MSG, [INPb + 3*16]
	 pshufb		 MSG, SHUF_MASK
	 movdqa		 MSGTMP3b, MSG
		 paddd		 MSG, [SHA256CONSTANTS + 3*16]
		 sha256rnds2	 STATE1b, STATE0b, MSG	; MSG is implicit argument
	 movdqa		 MSGTMP, MSGTMP3b
	 palignr	 MSGTMP, MSGTMP2b, 4
	 paddd		 MSGTMP0b, MSGTMP
	 sha256msg2	 MSGTMP0b, MSGTMP3b
		 pshufd		 MSG, MSG, 0x0E
		 sha256rnds2	 STATE0b, STATE1b, MSG	; MSG is implicit argument
	sha256msg1	MSGTMP2, MSGTMP3
	 sha256msg1	 MSGTMP2b, MSGTMP3b

	;; Rounds 16-19
	movdqa		MSG, MSGTMP0
		paddd		MSG, [SHA256CONSTANTS + 4*16]
		sha256rnds2	STATE1, STATE0, MSG	; MSG is implicit argument
	movdqa		MSGTMP, MSGTMP0
	palignr		MSGTMP, MSGTMP3, 4
	paddd		MSGTMP1, MSGTMP
	sha256msg2	MSGTMP1, MSGTMP0
		pshufd 		MSG, MSG, 0x0E
		sha256rnds2	STATE0, STATE1, MSG	; MSG is implicit argument
	 movdqa		 MSG, MSGTMP0b
		 paddd		 MSG, [SHA256CONSTANTS + 4*16]
		 sha256rnds2	 STATE1b, STATE0b, MSG	; MSG is implicit argument
	 movdqa		 MSGTMP, MSGTMP0b
	 palignr	 MSGTMP, MSGTMP3b, 4
	 paddd		 MSGTMP1b, MSGTMP
	 sha256msg2	 MSGTMP1b, MSGTMP0b
		 pshufd		 MSG, MSG, 0x0E
		 sha256rnds2	 STATE0b, STATE1b, MSG	; MSG is implicit argument
	sha256msg1	MSGTMP3, MSGTMP0
	 sha256msg1	 MSGTMP3b, MSGTMP0b

	;; Rounds 20-23
	movdqa		MSG, MSGTMP1
		paddd		MSG, [SHA256CONSTANTS + 5*16]
		sha256rnds2	STATE1, STATE0, MSG	; MSG is implicit argument
	movdqa		MSGTMP, MSGTMP1
	palignr		MSGTMP, MSGTMP0, 4
	paddd		MSGTMP2, MSGTMP
	sha256msg2	MSGTMP2, MSGTMP1
		pshufd 		MSG, MSG, 0x0E
		sha256rnds2	STATE0, STATE1, MSG	; MSG is implicit argument
	 movdqa		 MSG, MSGTMP1b
		 paddd		 MSG, [SHA256CONSTANTS + 5*16]
		 sha256rnds2	 STATE1b, STATE0b, MSG	; MSG is implicit argument
	 movdqa		 MSGTMP, MSGTMP1b
	 palignr	 MSGTMP, MSGTMP0b, 4
	 paddd		 MSGTMP2b, MSGTMP
	 sha256msg2	 MSGTMP2b, MSGTMP1b
		 pshufd		 MSG, MSG, 0x0E
		 sha256rnds2	 STATE0b, STATE1b, MSG	; MSG is implicit argument
	sha256msg1	MSGTMP0, MSGTMP1
	 sha256msg1	 MSGTMP0b, MSGTMP1b

	;; Rounds 24-27
	movdqa		MSG, MSGTMP2
		paddd		MSG, [SHA256CONSTANTS + 6*16]
		sha256rnds2	STATE1, STATE0, MSG	; MSG is implicit argument
	movdqa		MSGTMP, MSGTMP2
	palignr		MSGTMP, MSGTMP1, 4
	paddd		MSGTMP3, MSGTMP
	sha256msg2	MSGTMP3, MSGTMP2
		pshufd 		MSG, MSG, 0x0E
		sha256rnds2	STATE0, STATE1, MSG	; MSG is implicit argument
	 movdqa		 MSG, MSGTMP2b
		 paddd		 MSG, [SHA256CONSTANTS + 6*16]
		 sha256rnds2	 STATE1b, STATE0b, MSG	; MSG is implicit argument
	 movdqa		 MSGTMP, MSGTMP2b
	 palignr	 MSGTMP, MSGTMP1b, 4
	 paddd		 MSGTMP3b, MSGTMP
	 sha256msg2	 MSGTMP3b, MSGTMP2b
		 pshufd		 MSG, MSG, 0x0E
		 sha256rnds2	 STATE0b, STATE1b, MSG	; MSG is implicit argument
	sha256msg1	MSGTMP1, MSGTMP2
	 sha256msg1	 MSGTMP1b, MSGTMP2b

	;; Rounds 28-31
	movdqa		MSG, MSGTMP3
		paddd		MSG, [SHA256CONSTANTS + 7*16]
		sha256rnds2	STATE1, STATE0, MSG	; MSG is implicit argument
	movdqa		MSGTMP, MSGTMP3
	palignr		MSGTMP, MSGTMP2, 4
	paddd		MSGTMP0, MSGTMP
	sha256msg2	MSGTMP0, MSGTMP3
		pshufd 		MSG, MSG, 0x0E
		sha256rnds2	STATE0, STATE1, MSG	; MSG is implicit argument
	 movdqa		 MSG, MSGTMP3b
		 paddd		 MSG, [SHA256CONSTANTS + 7*16]
		 sha256rnds2	 STATE1b, STATE0b, MSG	; MSG is implicit argument
	 movdqa		 MSGTMP, MSGTMP3b
	 palignr	 MSGTMP, MSGTMP2b, 4
	 paddd		 MSGTMP0b, MSGTMP
	 sha256msg2	 MSGTMP0b, MSGTMP3b
		 pshufd		 MSG, MSG, 0x0E
		 sha256rnds2	 STATE0b, STATE1b, MSG	; MSG is implicit argument
	sha256msg1	MSGTMP2, MSGTMP3
	 sha256msg1	 MSGTMP2b, MSGTMP3b

	;; Rounds 32-35
	movdqa		MSG, MSGTMP0
		paddd		MSG, [SHA256CONSTANTS + 8*16]
		sha256rnds2	STATE1, STATE0, MSG	; MSG is implicit argument
	movdqa		MSGTMP, MSGTMP0
	palignr		MSGTMP, MSGTMP3, 4
	paddd		MSGTMP1, MSGTMP
	sha256msg2	MSGTMP1, MSGTMP0
		pshufd 		MSG, MSG, 0x0E
		sha256rnds2	STATE0, STATE1, MSG	; MSG is implicit argument
	 movdqa		 MSG, MSGTMP0b
		 paddd		 MSG, [SHA256CONSTANTS + 8*16]
		 sha256rnds2	 STATE1b, STATE0b, MSG	; MSG is implicit argument
	 movdqa		 MSGTMP, MSGTMP0b
	 palignr	 MSGTMP, MSGTMP3b, 4
	 paddd		 MSGTMP1b, MSGTMP
	 sha256msg2	 MSGTMP1b, MSGTMP0b
		 pshufd		 MSG, MSG, 0x0E
		 sha256rnds2	 STATE0b, STATE1b, MSG	; MSG is implicit argument
	sha256msg1	MSGTMP3, MSGTMP0
	 sha256msg1	 MSGTMP3b, MSGTMP0b

	;; Rounds 36-39
	movdqa		MSG, MSGTMP1
		paddd		MSG, [SHA256CONSTANTS + 9*16]
		sha256rnds2	STATE1, STATE0, MSG	; MSG is implicit argument
	movdqa		MSGTMP, MSGTMP1
	palignr		MSGTMP, MSGTMP0, 4
	paddd		MSGTMP2, MSGTMP
	sha256msg2	MSGTMP2, MSGTMP1
		pshufd 		MSG, MSG, 0x0E
		sha256rnds2	STATE0, STATE1, MSG	; MSG is implicit argument
	 movdqa		 MSG, MSGTMP1b
		 paddd		 MSG, [SHA256CONSTANTS + 9*16]
		 sha256rnds2	 STATE1b, STATE0b, MSG	; MSG is implicit argument
	 movdqa		 MSGTMP, MSGTMP1b
	 palignr	 MSGTMP, MSGTMP0b, 4
	 paddd		 MSGTMP2b, MSGTMP
	 sha256msg2	 MSGTMP2b, MSGTMP1b
		 pshufd		 MSG, MSG, 0x0E
		 sha256rnds2	 STATE0b, STATE1b, MSG	; MSG is implicit argument
	sha256msg1	MSGTMP0, MSGTMP1
	 sha256msg1	 MSGTMP0b, MSGTMP1b

	;; Rounds 40-43
	movdqa		MSG, MSGTMP2
		paddd		MSG, [SHA256CONSTANTS + 10*16]
		sha256rnds2	STATE1, STATE0, MSG	; MSG is implicit argument
	movdqa		MSGTMP, MSGTMP2
	palignr		MSGTMP, MSGTMP1, 4
	paddd		MSGTMP3, MSGTMP
	sha256msg2	MSGTMP3, MSGTMP2
		pshufd 		MSG, MSG, 0x0E
		sha256rnds2	STATE0, STATE1, MSG	; MSG is implicit argument
	 movdqa		 MSG, MSGTMP2b
		 paddd		 MSG, [SHA256CONSTANTS + 10*16]
		 sha256rnds2	 STATE1b, STATE0b, MSG	; MSG is implicit argument
	 movdqa		 MSGTMP, MSGTMP2b
	 palignr	 MSGTMP, MSGTMP1b, 4
	 paddd		 MSGTMP3b, MSGTMP
	 sha256msg2	 MSGTMP3b, MSGTMP2b
		 pshufd		 MSG, MSG, 0x0E
		 sha256rnds2	 STATE0b, STATE1b, MSG	; MSG is implicit argument
	sha256msg1	MSGTMP1, MSGTMP2
	 sha256msg1	 MSGTMP1b, MSGTMP2b

	;; Rounds 44-47
	movdqa		MSG, MSGTMP3
		paddd		MSG, [SHA256CONSTANTS + 11*16]
		sha256rnds2	STATE1, STATE0, MSG	; MSG is implicit argument
	movdqa		MSGTMP, MSGTMP3
	palignr		MSGTMP, MSGTMP2, 4
	paddd		MSGTMP0, MSGTMP
	sha256msg2	MSGTMP0, MSGTMP3
		pshufd 		MSG, MSG, 0x0E
		sha256rnds2	STATE0, STATE1, MSG	; MSG is implicit argument
	 movdqa		 MSG, MSGTMP3b
		 paddd		 MSG, [SHA256CONSTANTS + 11*16]
		 sha256rnds2	 STATE1b, STATE0b, MSG	; MSG is implicit argument
	 movdqa		 MSGTMP, MSGTMP3b
	 palignr	 MSGTMP, MSGTMP2b, 4
	 paddd		 MSGTMP0b, MSGTMP
	 sha256msg2	 MSGTMP0b, MSGTMP3b
		 pshufd		 MSG, MSG, 0x0E
		 sha256rnds2	 STATE0b, STATE1b, MSG	; MSG is implicit argument
	sha256msg1	MSGTMP2, MSGTMP3
	 sha256msg1	 MSGTMP2b, MSGTMP3b

	;; Rounds 48-51
	movdqa		MSG, MSGTMP0
		paddd		MSG, [SHA256CONSTANTS + 12*16]
		sha256rnds2	STATE1, STATE0, MSG	; MSG is implicit argument
	movdqa		MSGTMP, MSGTMP0
	palignr		MSGTMP, MSGTMP3, 4
	paddd		MSGTMP1, MSGTMP
	sha256msg2	MSGTMP1, MSGTMP0
		pshufd 		MSG, MSG, 0x0E
		sha256rnds2	STATE0, STATE1, MSG	; MSG is implicit argument
	 movdqa		 MSG, MSGTMP0b
		 paddd		 MSG, [SHA256CONSTANTS + 12*16]
		 sha256rnds2	 STATE1b, STATE0b, MSG	; MSG is implicit argument
	 movdqa		 MSGTMP, MSGTMP0b
	 palignr	 MSGTMP, MSGTMP3b, 4
	 paddd		 MSGTMP1b, MSGTMP
	 sha256msg2	 MSGTMP1b, MSGTMP0b
		 pshufd		 MSG, MSG, 0x0E
		 sha256rnds2	 STATE0b, STATE1b, MSG	; MSG is implicit argument
	sha256msg1	MSGTMP3, MSGTMP0
	 sha256msg1	 MSGTMP3b, MSGTMP0b

	;; Rounds 52-55
	movdqa		MSG, MSGTMP1
		paddd		MSG, [SHA256CONSTANTS + 13*16]
		sha256rnds2	STATE1, STATE0, MSG	; MSG is implicit argument
	movdqa		MSGTMP, MSGTMP1
	palignr		MSGTMP, MSGTMP0, 4
	paddd		MSGTMP2, MSGTMP
	sha256msg2	MSGTMP2, MSGTMP1
		pshufd 		MSG, MSG, 0x0E
		sha256rnds2	STATE0, STATE1, MSG	; MSG is implicit argument
	 movdqa		 MSG, MSGTMP1b
		 paddd		 MSG, [SHA256CONSTANTS + 13*16]
		 sha256rnds2	 STATE1b, STATE0b, MSG	; MSG is implicit argument
	 movdqa		 MSGTMP, MSGTMP1b
	 palignr	 MSGTMP, MSGTMP0b, 4
	 paddd		 MSGTMP2b, MSGTMP
	 sha256msg2	 MSGTMP2b, MSGTMP1b
		 pshufd		 MSG, MSG, 0x0E
		 sha256rnds2	 STATE0b, STATE1b, MSG	; MSG is implicit argument

	;; Rounds 56-59
	movdqa		MSG, MSGTMP2
		paddd		MSG, [SHA256CONSTANTS + 14*16]
		sha256rnds2	STATE1, STATE0, MSG	; MSG is implicit argument
	movdqa		MSGTMP, MSGTMP2
	palignr		MSGTMP, MSGTMP1, 4
	paddd		MSGTMP3, MSGTMP
	sha256msg2	MSGTMP3, MSGTMP2
		pshufd 		MSG, MSG, 0x0E
		sha256rnds2	STATE0, STATE1, MSG	; MSG is implicit argument
	 movdqa		 MSG, MSGTMP2b
		 paddd		 MSG, [SHA256CONSTANTS + 14*16]
		 sha256rnds2	 STATE1b, STATE0b, MSG	; MSG is implicit argument
	 movdqa		 MSGTMP, MSGTMP2b
	 palignr	 MSGTMP, MSGTMP1b, 4
	 paddd		 MSGTMP3b, MSGTMP
	 sha256msg2	 MSGTMP3b, MSGTMP2b
		 pshufd		 MSG, MSG, 0x0E
		 sha256rnds2	 STATE0b, STATE1b, MSG	; MSG is implicit argument

	;; Rounds 60-63
	movdqa		MSG, MSGTMP3
		paddd		MSG, [SHA256CONSTANTS + 15*16]
		sha256rnds2	STATE1, STATE0, MSG	; MSG is implicit argument
		pshufd 		MSG, MSG, 0x0E
		sha256rnds2	STATE0, STATE1, MSG	; MSG is implicit argument
	 movdqa		 MSG, MSGTMP3b
		 paddd		 MSG, [SHA256CONSTANTS + 15*16]
		 sha256rnds2	 STATE1b, STATE0b, MSG	; MSG is implicit argument
		 pshufd 	 MSG, MSG, 0x0E
		 sha256rnds2	 STATE0b, STATE1b, MSG	; MSG is implicit argument

	paddd		STATE0, [rsp + frame.ABEF_SAVE]
	paddd		STATE1, [rsp + frame.CDGH_SAVE]
	 paddd		 STATE0b, [rsp + frame.ABEF_SAVEb]
	 paddd		 STATE1b, [rsp + frame.CDGH_SAVEb]

	add		INP, 64
	 add		 INPb, 64
	cmp		INP, NUM_BLKS
	jne		.loop0

	;; update data pointers
	mov		[args + _data_ptr_sha256 + 0*PTR_SZ], INP
	mov		 [args + _data_ptr_sha256 + 1*PTR_SZ], INPb

	; Reorder for writeback
	pshufd		STATE0, STATE0, 0x1B	; FEBA
	pshufd		STATE1, STATE1, 0xB1	; DCHG
	movdqa		MSGTMP4, STATE0
	 pshufd		 STATE0b, STATE0b, 0x1B	; FEBA
	 pshufd		 STATE1b, STATE1b, 0xB1	; DCHG
	 movdqa		 MSGTMP, STATE0b
	pblendw		STATE0, STATE1,  0xF0	; DCBA
	 pblendw	 STATE0b, STATE1b,  0xF0 ; DCBA
	palignr		STATE1, MSGTMP4,  8	; HGFE
	 palignr	 STATE1b, MSGTMP,  8	; HGFE

	;; update digests
	movdqu		[args + 0*SHA256NI_DIGEST_ROW_SIZE + 0*16], STATE0
	movdqu		[args + 0*SHA256NI_DIGEST_ROW_SIZE + 1*16], STATE1
	 movdqu		 [args + 1*SHA256NI_DIGEST_ROW_SIZE + 0*16], STATE0b
	 movdqu		 [args + 1*SHA256NI_DIGEST_ROW_SIZE + 1*16], STATE1b

        DBGPRINTL	"jobA digest out:"
	DBGPRINT_XMM	STATE0
	DBGPRINT_XMM	STATE1
        DBGPRINTL	"jobB digest out:"
	DBGPRINT_XMM	STATE0b
	DBGPRINT_XMM	STATE1b

done_hash:
        DBGPRINTL	"exit sha256-ni-x2"

        ;; Clear stack frame (4*16 bytes)
%ifdef SAFE_DATA
        clear_all_xmms_sse_asm
%assign i 0
%rep 4
        movdqa	[rsp + i*16], xmm0
%assign i (i+1)
%endrep
%endif

	add		rsp, frame_size
	ret

mksection stack-noexec
