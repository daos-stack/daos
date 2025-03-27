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
;; Windows clobbers:	            RDX                     R10 R11
;; Windows preserves:	RAX RBX RCX     RBP RSI RDI R8  R9          R12 R13 R14 R15
;;			-----------------------------------------------------------
;; Linux clobbers:	                        RDI         R10 R11
;; Linux preserves:	RAX RBX RCX RDX RBP RSI     R8  R9          R12 R13 R14 R15
;;			-----------------------------------------------------------
;;
;; Linux/Windows clobbers: xmm0 - xmm15

%include "include/os.asm"
;%define DO_DBGPRINT
%include "include/dbgprint.asm"
%include "include/clear_regs.asm"
%include "include/cet.inc"
%include "include/mb_mgr_datastruct.asm"

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

; reso = resdq => 16 bytes
struc frame
.ABCD_SAVE	reso	1
.E_SAVE		reso	1
.ABCD_SAVEb	reso	1
.E_SAVEb	reso	1
.align		resq	1
endstruc

%define INP		r10
%define INPb		r11

%define ABCD		xmm0
%define E0		xmm1	; Need two E's b/c they ping pong
%define E1		xmm2
%define MSG0		xmm3
%define MSG1		xmm4
%define MSG2		xmm5
%define MSG3		xmm6

%define ABCDb		xmm7
%define E0b		xmm8	; Need two E's b/c they ping pong
%define E1b		xmm9
%define MSG0b		xmm10
%define MSG1b		xmm11
%define MSG2b		xmm12
%define MSG3b		xmm13

%define SHUF_MASK	xmm14
%define E_MASK		xmm15

mksection .rodata
default rel
align 64
PSHUFFLE_BYTE_FLIP_MASK: ;ddq 0x000102030405060708090a0b0c0d0e0f
	dq 0x08090a0b0c0d0e0f, 0x0001020304050607
UPPER_WORD_MASK:         ;ddq 0xFFFFFFFF000000000000000000000000
	dq 0x0000000000000000, 0xFFFFFFFF00000000

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; void sha1_ni(SHA1_ARGS *args, UINT32 size_in_blocks)
;; arg1 : pointer to args
;; arg2 : size (in blocks) ;; assumed to be >= 1

mksection .text
MKGLOBAL(sha1_ni,function,internal)
align 32
sha1_ni:
	sub		rsp, frame_size

        DBGPRINTL "enter sha1-ni-x2"

	shl		NUM_BLKS, 6	; convert to bytes
	jz		done_hash

	;; load input pointers
	mov		INP, [args + _data_ptr_sha1 + 0*PTR_SZ]
	DBGPRINTL64 "jobA: pointer", INP
	mov		INPb, [args + _data_ptr_sha1 + 1*PTR_SZ]

	add		NUM_BLKS, INP	; pointer to end of data block -> loop exit condition

	;; load initial digest
	movdqu		ABCD, [args + 0*SHA1NI_DIGEST_ROW_SIZE]
	pxor		E0, E0
	pinsrd		E0, [args + 0*SHA1NI_DIGEST_ROW_SIZE + 4*SHA1_DIGEST_WORD_SIZE], 3
	pshufd		ABCD, ABCD, 0x1B

        DBGPRINTL_XMM	"jobA: digest in words[0-3]", ABCD
        DBGPRINTL_XMM	"jobA: digest in word 4", E0

	 movdqu		 ABCDb, [args + 1*SHA1NI_DIGEST_ROW_SIZE]
	 pxor		 E0b, E0b
	 pinsrd		 E0b,   [args + 1*SHA1NI_DIGEST_ROW_SIZE + 4*SHA1_DIGEST_WORD_SIZE], 3
	 pshufd		 ABCDb, ABCDb, 0x1B

	movdqa		SHUF_MASK, [rel PSHUFFLE_BYTE_FLIP_MASK]
	movdqa		E_MASK, [rel UPPER_WORD_MASK]

	DBGPRINTL "jobA data:"
loop0:
	;; Copy digests
	movdqa		[rsp + frame.ABCD_SAVE], ABCD
	movdqa		[rsp + frame.E_SAVE],    E0
	 movdqa		 [rsp + frame.ABCD_SAVEb], ABCDb
	 movdqa		 [rsp + frame.E_SAVEb],    E0b

	;; Only needed if not using sha1nexte for rounds 0-3
	pand		E0,   E_MASK
	 pand		 E0b,   E_MASK

	;; Needed if using sha1nexte for rounds 0-3
	;; Need to rotate E right by 30
	;movdqa		E1, E0
	;psrld		E0, 30
	;pslld		E1, 2
	;pxor		E0, E1

	;; Rounds 0-3
	movdqu		MSG0, [INP + 0*16]
	pshufb		MSG0, SHUF_MASK
        DBGPRINT_XMM	MSG0
		;sha1nexte	E0, MSG0
		paddd		E0, MSG0 ; instead of sha1nexte
		movdqa		E1, ABCD
		sha1rnds4	ABCD, E0, 0
	 movdqu		 MSG0b, [INPb + 0*16]
	 pshufb		 MSG0b, SHUF_MASK
		 ;sha1nexte	 E0b, MSG0b
		 paddd		 E0b, MSG0b ; instead of sha1nexte
		 movdqa		 E1b, ABCDb
		 sha1rnds4	 ABCDb, E0b, 0

	;; Rounds 4-7
	movdqu		MSG1, [INP + 1*16]
	pshufb		MSG1, SHUF_MASK
        DBGPRINT_XMM	MSG1
		sha1nexte	E1, MSG1
		movdqa		E0, ABCD
		sha1rnds4	ABCD, E1, 0
	sha1msg1	MSG0, MSG1
	 movdqu		 MSG1b, [INPb + 1*16]
	 pshufb		 MSG1b, SHUF_MASK
		 sha1nexte	 E1b, MSG1b
		 movdqa		 E0b, ABCDb
		 sha1rnds4	 ABCDb, E1b, 0
	 sha1msg1	 MSG0b, MSG1b

	;; Rounds 8-11
	movdqu		MSG2, [INP + 2*16]
	pshufb		MSG2, SHUF_MASK
        DBGPRINT_XMM	MSG2
		sha1nexte	E0, MSG2
		movdqa		E1, ABCD
		sha1rnds4	ABCD, E0, 0
	sha1msg1	MSG1, MSG2
	pxor		MSG0, MSG2
	 movdqu		 MSG2b, [INPb + 2*16]
	 pshufb		 MSG2b, SHUF_MASK
		 sha1nexte	 E0b, MSG2b
		 movdqa		 E1b, ABCDb
		 sha1rnds4	 ABCDb, E0b, 0
	 sha1msg1	 MSG1b, MSG2b
	 pxor		 MSG0b, MSG2b

	;; Rounds 12-15
	movdqu		MSG3, [INP + 3*16]
	pshufb		MSG3, SHUF_MASK
        DBGPRINT_XMM	MSG3
		sha1nexte	E1, MSG3
		movdqa		E0, ABCD
	sha1msg2	MSG0, MSG3
		sha1rnds4	ABCD, E1, 0
	sha1msg1	MSG2, MSG3
	pxor		MSG1, MSG3
	 movdqu		 MSG3b, [INPb + 3*16]
	 pshufb		 MSG3b, SHUF_MASK
		 sha1nexte	 E1b, MSG3b
		 movdqa		 E0b, ABCDb
	 sha1msg2	 MSG0b, MSG3b
		 sha1rnds4	 ABCDb, E1b, 0
	 sha1msg1	 MSG2b, MSG3b
	 pxor		 MSG1b, MSG3b

	;; Rounds 16-19
		sha1nexte	E0, MSG0
		movdqa		E1, ABCD
	sha1msg2	MSG1, MSG0
		sha1rnds4	ABCD, E0, 0
	sha1msg1	MSG3, MSG0
	pxor		MSG2, MSG0
		 sha1nexte	 E0b, MSG0b
		 movdqa		 E1b, ABCDb
	 sha1msg2	 MSG1b, MSG0b
		 sha1rnds4	 ABCDb, E0b, 0
	 sha1msg1	 MSG3b, MSG0b
	 pxor		 MSG2b, MSG0b

	;; Rounds 20-23
		sha1nexte	E1, MSG1
		movdqa		E0, ABCD
	sha1msg2	MSG2, MSG1
		sha1rnds4	ABCD, E1, 1
	sha1msg1	MSG0, MSG1
	pxor		MSG3, MSG1
		 sha1nexte	 E1b, MSG1b
		 movdqa		 E0b, ABCDb
	 sha1msg2	 MSG2b, MSG1b
		 sha1rnds4	 ABCDb, E1b, 1
	 sha1msg1	 MSG0b, MSG1b
	 pxor		 MSG3b, MSG1b

	;; Rounds 24-27
		sha1nexte	E0, MSG2
		movdqa		E1, ABCD
	sha1msg2	MSG3, MSG2
		sha1rnds4	ABCD, E0, 1
	sha1msg1	MSG1, MSG2
	pxor		MSG0, MSG2
		 sha1nexte	 E0b, MSG2b
		 movdqa		 E1b, ABCDb
	 sha1msg2	 MSG3b, MSG2b
		 sha1rnds4	 ABCDb, E0b, 1
	 sha1msg1	 MSG1b, MSG2b
	 pxor		 MSG0b, MSG2b

	;; Rounds 28-31
		sha1nexte	E1, MSG3
		movdqa		E0, ABCD
	sha1msg2	MSG0, MSG3
		sha1rnds4	ABCD, E1, 1
	sha1msg1	MSG2, MSG3
	pxor		MSG1, MSG3
		 sha1nexte	 E1b, MSG3b
		 movdqa		 E0b, ABCDb
	 sha1msg2	 MSG0b, MSG3b
		 sha1rnds4	 ABCDb, E1b, 1
	 sha1msg1	 MSG2b, MSG3b
	 pxor		 MSG1b, MSG3b

	;; Rounds 32-35
		sha1nexte	E0, MSG0
		movdqa		E1, ABCD
	sha1msg2	MSG1, MSG0
		sha1rnds4	ABCD, E0, 1
	sha1msg1	MSG3, MSG0
	pxor		MSG2, MSG0
		 sha1nexte	 E0b, MSG0b
		 movdqa		 E1b, ABCDb
	 sha1msg2	 MSG1b, MSG0b
		 sha1rnds4	 ABCDb, E0b, 1
	 sha1msg1	 MSG3b, MSG0b
	 pxor		 MSG2b, MSG0b

	;; Rounds 36-39
		sha1nexte	E1, MSG1
		movdqa		E0, ABCD
	sha1msg2	MSG2, MSG1
		sha1rnds4	ABCD, E1, 1
	sha1msg1	MSG0, MSG1
	pxor		MSG3, MSG1
		 sha1nexte	 E1b, MSG1b
		 movdqa		 E0b, ABCDb
	 sha1msg2	 MSG2b, MSG1b
		 sha1rnds4	 ABCDb, E1b, 1
	 sha1msg1	 MSG0b, MSG1b
	 pxor		 MSG3b, MSG1b

	;; Rounds 40-43
		sha1nexte	E0, MSG2
		movdqa		E1, ABCD
	sha1msg2	MSG3, MSG2
		sha1rnds4	ABCD, E0, 2
	sha1msg1	MSG1, MSG2
	pxor		MSG0, MSG2
		 sha1nexte	 E0b, MSG2b
		 movdqa		 E1b, ABCDb
	 sha1msg2	 MSG3b, MSG2b
		 sha1rnds4	 ABCDb, E0b, 2
	 sha1msg1	 MSG1b, MSG2b
	 pxor		 MSG0b, MSG2b

	;; Rounds 44-47
		sha1nexte	E1, MSG3
		movdqa		E0, ABCD
	sha1msg2	MSG0, MSG3
		sha1rnds4	ABCD, E1, 2
	sha1msg1	MSG2, MSG3
	pxor		MSG1, MSG3
		 sha1nexte	 E1b, MSG3b
		 movdqa		 E0b, ABCDb
	 sha1msg2	 MSG0b, MSG3b
		 sha1rnds4	 ABCDb, E1b, 2
	 sha1msg1	 MSG2b, MSG3b
	 pxor		 MSG1b, MSG3b

	;; Rounds 48-51
		sha1nexte	E0, MSG0
		movdqa		E1, ABCD
	sha1msg2	MSG1, MSG0
		sha1rnds4	ABCD, E0, 2
	sha1msg1	MSG3, MSG0
	pxor		MSG2, MSG0
		 sha1nexte	 E0b, MSG0b
		 movdqa		 E1b, ABCDb
	 sha1msg2	 MSG1b, MSG0b
		 sha1rnds4	 ABCDb, E0b, 2
	 sha1msg1	 MSG3b, MSG0b
	 pxor		 MSG2b, MSG0b

	;; Rounds 52-55
		sha1nexte	E1, MSG1
		movdqa		E0, ABCD
	sha1msg2	MSG2, MSG1
		sha1rnds4	ABCD, E1, 2
	sha1msg1	MSG0, MSG1
	pxor		MSG3, MSG1
		 sha1nexte	 E1b, MSG1b
		 movdqa		 E0b, ABCDb
	 sha1msg2	 MSG2b, MSG1b
		 sha1rnds4	 ABCDb, E1b, 2
	 sha1msg1	 MSG0b, MSG1b
	 pxor		 MSG3b, MSG1b

	;; Rounds 56-59
		sha1nexte	E0, MSG2
		movdqa		E1, ABCD
	sha1msg2	MSG3, MSG2
		sha1rnds4	ABCD, E0, 2
	sha1msg1	MSG1, MSG2
	pxor		MSG0, MSG2
		 sha1nexte	 E0b, MSG2b
		 movdqa		 E1b, ABCDb
	 sha1msg2	 MSG3b, MSG2b
		 sha1rnds4	 ABCDb, E0b, 2
	 sha1msg1	 MSG1b, MSG2b
	 pxor		 MSG0b, MSG2b

	;; Rounds 60-63
		sha1nexte	E1, MSG3
		movdqa		E0, ABCD
	sha1msg2	MSG0, MSG3
		sha1rnds4	ABCD, E1, 3
	sha1msg1	MSG2, MSG3
	pxor		MSG1, MSG3
		 sha1nexte	 E1b, MSG3b
		 movdqa		 E0b, ABCDb
	 sha1msg2	 MSG0b, MSG3b
		 sha1rnds4	 ABCDb, E1b, 3
	 sha1msg1	 MSG2b, MSG3b
	 pxor		 MSG1b, MSG3b

	;; Rounds 64-67
		sha1nexte	E0, MSG0
		movdqa		E1, ABCD
	sha1msg2	MSG1, MSG0
		sha1rnds4	ABCD, E0, 3
	sha1msg1	MSG3, MSG0
	pxor		MSG2, MSG0
		 sha1nexte	 E0b, MSG0b
		 movdqa		 E1b, ABCDb
	 sha1msg2	 MSG1b, MSG0b
		 sha1rnds4	 ABCDb, E0b, 3
	 sha1msg1	 MSG3b, MSG0b
	 pxor		 MSG2b, MSG0b

	;; Rounds 68-71
		sha1nexte	E1, MSG1
		movdqa		E0, ABCD
	sha1msg2	MSG2, MSG1
		sha1rnds4	ABCD, E1, 3
	pxor		MSG3, MSG1
		 sha1nexte	 E1b, MSG1b
		 movdqa		 E0b, ABCDb
	 sha1msg2	 MSG2b, MSG1b
		 sha1rnds4	 ABCDb, E1b, 3
	 pxor		 MSG3b, MSG1b

	;; Rounds 72-75
		sha1nexte	E0, MSG2
		movdqa		E1, ABCD
	sha1msg2	MSG3, MSG2
		sha1rnds4	ABCD, E0, 3
		 sha1nexte	 E0b, MSG2b
		 movdqa		 E1b, ABCDb
	 sha1msg2	 MSG3b, MSG2b
		 sha1rnds4	 ABCDb, E0b, 3

	;; Rounds 76-79
		sha1nexte	E1, MSG3
		movdqa		E0, ABCD
		sha1rnds4	ABCD, E1, 3
		 sha1nexte	 E1b, MSG3b
		 movdqa		 E0b, ABCDb
		 sha1rnds4	 ABCDb, E1b, 3

	;; Need to rotate E left by 30
	movdqa		E1, E0
	pslld		E0, 30
	psrld		E1, 2
	pxor		E0, E1
	 movdqa		 E1b, E0b
	 pslld		 E0b, 30
	 psrld		 E1b, 2
	 pxor		 E0b, E1b

	paddd		ABCD, [rsp + frame.ABCD_SAVE]
	paddd		E0,   [rsp + frame.E_SAVE]
	 paddd		 ABCDb, [rsp + frame.ABCD_SAVEb]
	 paddd		 E0b,   [rsp + frame.E_SAVEb]

	add		INP, 64
	 add		 INPb, 64
	cmp		INP, NUM_BLKS
	jne		loop0

	;; write out digests
	pshufd		ABCD, ABCD, 0x1B
	movdqu		[args + 0*SHA1NI_DIGEST_ROW_SIZE], ABCD
	pextrd		[args + 0*SHA1NI_DIGEST_ROW_SIZE + 4*SHA1_DIGEST_WORD_SIZE], E0, 3
        DBGPRINTL_XMM "jobA: digest out words[0-3]", ABCD
        DBGPRINTL_XMM "jobA: digest out word 4", E0

	 pshufd		 ABCDb, ABCDb, 0x1B
	 movdqu		 [args + 1*SHA1NI_DIGEST_ROW_SIZE], ABCDb
	 pextrd		 [args + 1*SHA1NI_DIGEST_ROW_SIZE + 4*SHA1_DIGEST_WORD_SIZE], E0b, 3

	;; update input pointers
	mov		[args + _data_ptr_sha1 + 0*PTR_SZ], INP
	 mov		 [args + _data_ptr_sha1 + 1*PTR_SZ], INPb

done_hash:

        ;; Clear stack frame (4*16 bytes)
%ifdef SAFE_DATA
        pxor    xmm0, xmm0
%assign i 0
%rep 4
        movdqa	[rsp + i*16], xmm0
%assign i (i+1)
%endrep
	clear_all_xmms_sse_asm
%endif

	add		rsp, frame_size

	ret

mksection stack-noexec
