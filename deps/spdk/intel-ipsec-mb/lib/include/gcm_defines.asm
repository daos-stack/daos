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

%ifndef GCM_DEFINES_ASM_INCLUDED
%define GCM_DEFINES_ASM_INCLUDED

;
; Authors:
;       Erdinc Ozturk
;       Vinodh Gopal
;       James Guilford

mksection .rodata
default rel

align 16
POLY:   dq     0x0000000000000001, 0xC200000000000000

align 64
POLY2:
        dq     0x00000001C2000000, 0xC200000000000000
        dq     0x00000001C2000000, 0xC200000000000000
        dq     0x00000001C2000000, 0xC200000000000000
        dq     0x00000001C2000000, 0xC200000000000000

align 16
TWOONE: dq     0x0000000000000001, 0x0000000100000000

;;; @note Order of these constants should not change.
;;; More specifically, ALL_F should follow SHIFT_MASK, and ZERO should follow ALL_F
align 64
SHUF_MASK:
        dq     0x08090A0B0C0D0E0F, 0x0001020304050607
        dq     0x08090A0B0C0D0E0F, 0x0001020304050607
        dq     0x08090A0B0C0D0E0F, 0x0001020304050607
        dq     0x08090A0B0C0D0E0F, 0x0001020304050607

align 16
SHIFT_MASK:
        dq     0x0706050403020100, 0x0f0e0d0c0b0a0908

ALL_F:
        dq     0xffffffffffffffff, 0xffffffffffffffff

ZERO:
        dq     0x0000000000000000, 0x0000000000000000

align 16
ONE:
        dq     0x0000000000000001, 0x0000000000000000

align 16
TWO:
        dq     0x0000000000000002, 0x0000000000000000

align 16
ONEf:
        dq     0x0000000000000000, 0x0100000000000000

align 16
TWOf:
        dq     0x0000000000000000, 0x0200000000000000

align 64
ddq_add_1234:
        dq	0x0000000000000001, 0x0000000000000000
        dq	0x0000000000000002, 0x0000000000000000
        dq	0x0000000000000003, 0x0000000000000000
        dq	0x0000000000000004, 0x0000000000000000

align 64
ddq_add_5678:
        dq	0x0000000000000005, 0x0000000000000000
        dq	0x0000000000000006, 0x0000000000000000
        dq	0x0000000000000007, 0x0000000000000000
        dq	0x0000000000000008, 0x0000000000000000

align 64
ddq_add_4444:
        dq	0x0000000000000004, 0x0000000000000000
        dq	0x0000000000000004, 0x0000000000000000
        dq	0x0000000000000004, 0x0000000000000000
        dq	0x0000000000000004, 0x0000000000000000

align 64
ddq_add_8888:
        dq	0x0000000000000008, 0x0000000000000000
        dq	0x0000000000000008, 0x0000000000000000
        dq	0x0000000000000008, 0x0000000000000000
        dq	0x0000000000000008, 0x0000000000000000

align 64
ddq_addbe_1234:
        dq	0x0000000000000000, 0x0100000000000000
        dq	0x0000000000000000, 0x0200000000000000
        dq	0x0000000000000000, 0x0300000000000000
        dq	0x0000000000000000, 0x0400000000000000

align 64
ddq_addbe_5678:
        dq	0x0000000000000000, 0x0500000000000000
        dq	0x0000000000000000, 0x0600000000000000
        dq	0x0000000000000000, 0x0700000000000000
        dq	0x0000000000000000, 0x0800000000000000

align 64
ddq_addbe_4444:
        dq	0x0000000000000000, 0x0400000000000000
        dq	0x0000000000000000, 0x0400000000000000
        dq	0x0000000000000000, 0x0400000000000000
        dq	0x0000000000000000, 0x0400000000000000

align 64
ddq_addbe_8888:
        dq	0x0000000000000000, 0x0800000000000000
        dq	0x0000000000000000, 0x0800000000000000
        dq	0x0000000000000000, 0x0800000000000000
        dq	0x0000000000000000, 0x0800000000000000

align 64
byte_len_to_mask_table:
        dw      0x0000, 0x0001, 0x0003, 0x0007,
        dw      0x000f, 0x001f, 0x003f, 0x007f,
        dw      0x00ff, 0x01ff, 0x03ff, 0x07ff,
        dw      0x0fff, 0x1fff, 0x3fff, 0x7fff,
        dw      0xffff

align 64
byte64_len_to_mask_table:
        dq      0x0000000000000000, 0x0000000000000001
        dq      0x0000000000000003, 0x0000000000000007
        dq      0x000000000000000f, 0x000000000000001f
        dq      0x000000000000003f, 0x000000000000007f
        dq      0x00000000000000ff, 0x00000000000001ff
        dq      0x00000000000003ff, 0x00000000000007ff
        dq      0x0000000000000fff, 0x0000000000001fff
        dq      0x0000000000003fff, 0x0000000000007fff
        dq      0x000000000000ffff, 0x000000000001ffff
        dq      0x000000000003ffff, 0x000000000007ffff
        dq      0x00000000000fffff, 0x00000000001fffff
        dq      0x00000000003fffff, 0x00000000007fffff
        dq      0x0000000000ffffff, 0x0000000001ffffff
        dq      0x0000000003ffffff, 0x0000000007ffffff
        dq      0x000000000fffffff, 0x000000001fffffff
        dq      0x000000003fffffff, 0x000000007fffffff
        dq      0x00000000ffffffff, 0x00000001ffffffff
        dq      0x00000003ffffffff, 0x00000007ffffffff
        dq      0x0000000fffffffff, 0x0000001fffffffff
        dq      0x0000003fffffffff, 0x0000007fffffffff
        dq      0x000000ffffffffff, 0x000001ffffffffff
        dq      0x000003ffffffffff, 0x000007ffffffffff
        dq      0x00000fffffffffff, 0x00001fffffffffff
        dq      0x00003fffffffffff, 0x00007fffffffffff
        dq      0x0000ffffffffffff, 0x0001ffffffffffff
        dq      0x0003ffffffffffff, 0x0007ffffffffffff
        dq      0x000fffffffffffff, 0x001fffffffffffff
        dq      0x003fffffffffffff, 0x007fffffffffffff
        dq      0x00ffffffffffffff, 0x01ffffffffffffff
        dq      0x03ffffffffffffff, 0x07ffffffffffffff
        dq      0x0fffffffffffffff, 0x1fffffffffffffff
        dq      0x3fffffffffffffff, 0x7fffffffffffffff
        dq      0xffffffffffffffff

align 64
mask_out_top_block:
        dq      0xffffffffffffffff, 0xffffffffffffffff
        dq      0xffffffffffffffff, 0xffffffffffffffff
        dq      0xffffffffffffffff, 0xffffffffffffffff
        dq      0x0000000000000000, 0x0000000000000000

;; GCM NIST standard: len(M) < 2^39 - 256
align 8
gcm_max_len:
dq      (((1<<39) - 256) - 1)

%ifdef WIN_ABI
        %xdefine GCM_MAX_LENGTH r10
%else
        %xdefine GCM_MAX_LENGTH [rel gcm_max_len]
%endif

%macro INIT_GCM_MAX_LENGTH 0
%ifdef WIN_ABI
       mov     GCM_MAX_LENGTH, qword [rel gcm_max_len]
%endif
%endm

mksection .text

;;define the fields of gcm_context_data struct
;; struct gcm_context_data {
;;         // init, update and finalize context data
;;         uint8_t  aad_hash[IMB_GCM_BLOCK_LEN];
;;         uint64_t aad_length;
;;         uint64_t in_length;
;;         uint8_t  partial_block_enc_key[IMB_GCM_BLOCK_LEN];
;;         uint8_t  orig_IV[IMB_GCM_BLOCK_LEN];
;;         uint8_t  current_counter[IMB_GCM_BLOCK_LEN];
;;         uint64_t  partial_block_length;
;; };

%define AadHash		(16*0)	  ; store current Hash of data which has been input
%define AadLen		(16*1)	  ; store length of input data which will not be encrypted or decrypted
%define InLen		((16*1)+8); store length of input data which will be encrypted or decrypted
%define PBlockEncKey	(16*2)	  ; encryption key for the partial block at the end of the previous update
%define OrigIV		(16*3)	  ; input IV
%define CurCount	(16*4)	  ; Current counter for generation of encryption key
%define PBlockLen	(16*5)	  ; length of partial block at the end of the previous update

%define reg(q) xmm %+ q
%define regy(q) ymm %+ q
%define regz(q) zmm %+ q

%ifdef WIN_ABI
	%xdefine arg1 rcx
	%xdefine arg2 rdx
	%xdefine arg3 r8
	%xdefine arg4 r9
	%xdefine arg5  qword [r14 + STACK_OFFSET + 8*5]
	%xdefine arg6  qword [r14 + STACK_OFFSET + 8*6]
	%xdefine arg7  qword [r14 + STACK_OFFSET + 8*7]
	%xdefine arg8  qword [r14 + STACK_OFFSET + 8*8]
	%xdefine arg9  qword [r14 + STACK_OFFSET + 8*9]
	%xdefine arg10 qword [r14 + STACK_OFFSET + 8*10]
	%xdefine arg11 qword [r14 + STACK_OFFSET + 8*11]
%else
	%xdefine arg1 rdi
	%xdefine arg2 rsi
	%xdefine arg3 rdx
	%xdefine arg4 rcx
	%xdefine arg5 r8
	%xdefine arg6 r9
	%xdefine arg7  qword [r14 + STACK_OFFSET + 8*1]
	%xdefine arg8  qword [r14 + STACK_OFFSET + 8*2]
	%xdefine arg9  qword [r14 + STACK_OFFSET + 8*3]
	%xdefine arg10 qword [r14 + STACK_OFFSET + 8*4]
	%xdefine arg11 qword [r14 + STACK_OFFSET + 8*5]
%endif

%ifdef NT_LDST
	%define NT_LD
	%define NT_ST
%endif

;;; Use Non-temporal load/stor
%ifdef NT_LD
	%define	XLDR	 movntdqa
	%define	VXLDR	 vmovntdqa
	%define	VX512LDR vmovntdqa
%else
	%define	XLDR	 movdqu
	%define	VXLDR	 vmovdqu
	%define	VX512LDR vmovdqu8
%endif

;;; Use Non-temporal load/stor
%ifdef NT_ST
	%define	XSTR	 movntdq
	%define	VXSTR	 vmovntdq
	%define	VX512STR vmovntdq
%else
	%define	XSTR	 movdqu
	%define	VXSTR	 vmovdqu
	%define	VX512STR vmovdqu8
%endif

%endif ; GCM_DEFINES_ASM_INCLUDED
