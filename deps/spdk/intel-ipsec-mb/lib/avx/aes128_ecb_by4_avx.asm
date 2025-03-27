;;
;; Copyright (c) 2019-2021, Intel Corporation
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

; routine to do AES ECB encrypt/decrypt on 16n bytes doing AES by 4

; XMM registers are clobbered. Saving/restoring must be done at a higher level

; void aes_ecb_x_y_avx(void    *in,
;                      UINT128  keys[],
;                      void    *out,
;                      UINT64   len_bytes);
;
; x = direction (enc/dec)
; y = key size (128/192/256)
; arg 1: IN:   pointer to input (cipher text)
; arg 2: KEYS: pointer to keys
; arg 3: OUT:  pointer to output (plain text)
; arg 4: LEN:  length in bytes (multiple of 16)
;

%include "include/os.asm"
%include "include/clear_regs.asm"

%ifndef AES_ECB_ENC_256
%ifndef AES_ECB_ENC_192
%ifndef AES_ECB_ENC_128
%define AES_ECB_ENC_128 aes_ecb_enc_128_avx
%define AES_ECB_DEC_128 aes_ecb_dec_128_avx
%endif
%endif
%endif

%ifdef LINUX
%define IN		rdi
%define KEYS		rsi
%define OUT		rdx
%define LEN		rcx
%else
%define IN		rcx
%define KEYS		rdx
%define OUT		r8
%define LEN		r9
%endif

%define IDX		rax
%define TMP		IDX
%define XDATA0		xmm0
%define XDATA1		xmm1
%define XDATA2		xmm2
%define XDATA3		xmm3
%define XKEY0		xmm4
%define XKEY2		xmm5
%define XKEY4		xmm6
%define XKEY6		xmm7
%define XKEY10		xmm8
%define XKEY_A		xmm9
%define XKEY_B		xmm10

mksection .text

%macro AES_ECB 2
%define %%NROUNDS %1 ; [in] Number of AES rounds, numerical value
%define %%DIR     %2 ; [in] Direction (encrypt/decrypt)

%ifidn %%DIR, ENC
%define AES      vaesenc
%define AES_LAST vaesenclast
%else ; DIR = DEC
%define AES      vaesdec
%define AES_LAST vaesdeclast
%endif
	mov	TMP, LEN
	and	TMP, 3*16
	jz	%%initial_4
	cmp	TMP, 2*16
	jb	%%initial_1
	ja	%%initial_3

%%initial_2:
	; load plain/cipher text
	vmovdqu	XDATA0, [IN + 0*16]
	vmovdqu	XDATA1, [IN + 1*16]

	vmovdqa	XKEY0, [KEYS + 0*16]

	vpxor	XDATA0, XKEY0		; 0. ARK
	vpxor	XDATA1, XKEY0

	vmovdqa	XKEY2, [KEYS + 2*16]

	AES	XDATA0, [KEYS + 1*16]	; 1. ENC
	AES	XDATA1, [KEYS + 1*16]

	mov	IDX, 2*16

	AES	XDATA0, XKEY2		; 2. ENC
	AES	XDATA1, XKEY2

	vmovdqa	XKEY4, [KEYS + 4*16]

	AES	XDATA0, [KEYS + 3*16]	; 3. ENC
	AES	XDATA1, [KEYS + 3*16]

	AES	XDATA0, XKEY4		; 4. ENC
	AES	XDATA1, XKEY4

	vmovdqa	XKEY6, [KEYS + 6*16]

	AES	XDATA0, [KEYS + 5*16]	; 5. ENC
	AES	XDATA1, [KEYS + 5*16]

	AES	XDATA0, XKEY6		; 6. ENC
	AES	XDATA1, XKEY6

	vmovdqa	XKEY_B, [KEYS + 8*16]

	AES	XDATA0, [KEYS + 7*16]	; 7. ENC
	AES	XDATA1, [KEYS + 7*16]

	AES	XDATA0, XKEY_B		; 8. ENC
	AES	XDATA1, XKEY_B

	vmovdqa	XKEY10, [KEYS + 10*16]

	AES	XDATA0, [KEYS + 9*16]	; 9. ENC
	AES	XDATA1, [KEYS + 9*16]

%if %%NROUNDS >= 12
	AES	XDATA0, XKEY10		; 10. ENC
	AES	XDATA1, XKEY10

	AES	XDATA0, [KEYS + 11*16]	; 11. ENC
	AES	XDATA1, [KEYS + 11*16]
%endif

%if %%NROUNDS == 14
	AES	XDATA0, [KEYS + 12*16]	; 12. ENC
	AES	XDATA1, [KEYS + 12*16]

	AES	XDATA0, [KEYS + 13*16]	; 13. ENC
	AES	XDATA1, [KEYS + 13*16]
%endif

%if %%NROUNDS == 10
	AES_LAST	XDATA0, XKEY10	; 10. ENC
	AES_LAST	XDATA1, XKEY10
%elif %%NROUNDS == 12
	AES_LAST	XDATA0, [KEYS + 12*16]	; 12. ENC
	AES_LAST	XDATA1, [KEYS + 12*16]
%else
	AES_LAST	XDATA0, [KEYS + 14*16]	; 14. ENC
	AES_LAST	XDATA1, [KEYS + 14*16]
%endif
	vmovdqu	[OUT + 0*16], XDATA0
	vmovdqu	[OUT + 1*16], XDATA1

	cmp	LEN, 2*16
	je	%%done
	jmp	%%main_loop

	align 16
%%initial_1:
	; load plain/cipher text
	vmovdqu	XDATA0, [IN + 0*16]

	vmovdqa	XKEY0, [KEYS + 0*16]

	vpxor	XDATA0, XKEY0		; 0. ARK

	vmovdqa	XKEY2, [KEYS + 2*16]

	AES	XDATA0, [KEYS + 1*16]	; 1. ENC

	mov	IDX, 1*16

	AES	XDATA0, XKEY2		; 2. ENC

	vmovdqa	XKEY4, [KEYS + 4*16]

	AES	XDATA0, [KEYS + 3*16]	; 3. ENC

	AES	XDATA0, XKEY4		; 4. ENC

	vmovdqa	XKEY6, [KEYS + 6*16]

	AES	XDATA0, [KEYS + 5*16]	; 5. ENC

	AES	XDATA0, XKEY6		; 6. ENC

	vmovdqa	XKEY_B, [KEYS + 8*16]

	AES	XDATA0, [KEYS + 7*16]	; 7. ENC

	AES	XDATA0, XKEY_B		; 8. ENC

	vmovdqa	XKEY10, [KEYS + 10*16]

	AES	XDATA0, [KEYS + 9*16]	; 9. ENC

%if %%NROUNDS >= 12
	AES	XDATA0, XKEY10		; 10. ENC

	AES	XDATA0, [KEYS + 11*16]	; 11. ENC
%endif

%if %%NROUNDS == 14
	AES	XDATA0, [KEYS + 12*16]	; 12. ENC

	AES	XDATA0, [KEYS + 13*16]	; 13. ENC
%endif

%if %%NROUNDS == 10

	AES_LAST	XDATA0, XKEY10	        ; 10. ENC
%elif %%NROUNDS == 12
	AES_LAST	XDATA0, [KEYS + 12*16]	; 12. ENC
%else
	AES_LAST	XDATA0, [KEYS + 14*16]	; 14. ENC
%endif

	vmovdqu	[OUT + 0*16], XDATA0

	cmp	LEN, 1*16
	je	%%done
	jmp	%%main_loop

%%initial_3:
	; load plain/cipher text
	vmovdqu	XDATA0, [IN + 0*16]
	vmovdqu	XDATA1, [IN + 1*16]
	vmovdqu	XDATA2, [IN + 2*16]

	vmovdqa	XKEY0, [KEYS + 0*16]

	vmovdqa	XKEY_A, [KEYS + 1*16]

	vpxor	XDATA0, XKEY0		; 0. ARK
	vpxor	XDATA1, XKEY0
	vpxor	XDATA2, XKEY0

	vmovdqa	XKEY2, [KEYS + 2*16]

	AES	XDATA0, XKEY_A		; 1. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A

	vmovdqa	XKEY_A, [KEYS + 3*16]
	mov	IDX, 3*16

	AES	XDATA0, XKEY2		; 2. ENC
	AES	XDATA1, XKEY2
	AES	XDATA2, XKEY2

	vmovdqa	XKEY4, [KEYS + 4*16]

	AES	XDATA0, XKEY_A		; 3. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A

	vmovdqa	XKEY_A, [KEYS + 5*16]

	AES	XDATA0, XKEY4		; 4. ENC
	AES	XDATA1, XKEY4
	AES	XDATA2, XKEY4

	vmovdqa	XKEY6, [KEYS + 6*16]

	AES	XDATA0, XKEY_A		; 5. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A

	vmovdqa	XKEY_A, [KEYS + 7*16]

	AES	XDATA0, XKEY6		; 6. ENC
	AES	XDATA1, XKEY6
	AES	XDATA2, XKEY6

	vmovdqa	XKEY_B, [KEYS + 8*16]

	AES	XDATA0, XKEY_A		; 7. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A

	vmovdqa	XKEY_A, [KEYS + 9*16]

	AES	XDATA0, XKEY_B		; 8. ENC
	AES	XDATA1, XKEY_B
	AES	XDATA2, XKEY_B

	vmovdqa	XKEY_B, [KEYS + 10*16]

	AES	XDATA0, XKEY_A		; 9. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A

%if %%NROUNDS >= 12
	vmovdqa	XKEY_A, [KEYS + 11*16]

	AES	XDATA0, XKEY_B		; 10. ENC
	AES	XDATA1, XKEY_B
	AES	XDATA2, XKEY_B

	vmovdqa	XKEY_B, [KEYS + 12*16]

	AES	XDATA0, XKEY_A		; 11. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A

%endif

%if %%NROUNDS == 14
	vmovdqa	XKEY_A, [KEYS + 13*16]

	AES	XDATA0, XKEY_B		; 12. ENC
	AES	XDATA1, XKEY_B
	AES	XDATA2, XKEY_B

	vmovdqa	XKEY_B, [KEYS + 14*16]

	AES	XDATA0, XKEY_A		; 13. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A
%endif

	AES_LAST	XDATA0, XKEY_B	; 10/12/14. ENC (depending on key size)
	AES_LAST	XDATA1, XKEY_B
	AES_LAST	XDATA2, XKEY_B

	vmovdqu	[OUT + 0*16], XDATA0
	vmovdqu	[OUT + 1*16], XDATA1
	vmovdqu	[OUT + 2*16], XDATA2

	cmp	LEN, 3*16
	je	%%done
	jmp	%%main_loop

	align 16
%%initial_4:
	; load plain/cipher text
	vmovdqu	XDATA0, [IN + 0*16]
	vmovdqu	XDATA1, [IN + 1*16]
	vmovdqu	XDATA2, [IN + 2*16]
	vmovdqu	XDATA3, [IN + 3*16]

	vmovdqa	XKEY0, [KEYS + 0*16]

	vmovdqa	XKEY_A, [KEYS + 1*16]

	vpxor	XDATA0, XKEY0		; 0. ARK
	vpxor	XDATA1, XKEY0
	vpxor	XDATA2, XKEY0
	vpxor	XDATA3, XKEY0

	vmovdqa	XKEY2, [KEYS + 2*16]

	AES	XDATA0, XKEY_A		; 1. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A
	AES	XDATA3, XKEY_A

	vmovdqa	XKEY_A, [KEYS + 3*16]

	mov	IDX, 4*16

	AES	XDATA0, XKEY2		; 2. ENC
	AES	XDATA1, XKEY2
	AES	XDATA2, XKEY2
	AES	XDATA3, XKEY2

	vmovdqa	XKEY4, [KEYS + 4*16]

	AES	XDATA0, XKEY_A		; 3. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A
	AES	XDATA3, XKEY_A

	vmovdqa	XKEY_A, [KEYS + 5*16]

	AES	XDATA0, XKEY4		; 4. ENC
	AES	XDATA1, XKEY4
	AES	XDATA2, XKEY4
	AES	XDATA3, XKEY4

	vmovdqa	XKEY6, [KEYS + 6*16]

	AES	XDATA0, XKEY_A		; 5. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A
	AES	XDATA3, XKEY_A

	vmovdqa	XKEY_A, [KEYS + 7*16]

	AES	XDATA0, XKEY6		; 6. ENC
	AES	XDATA1, XKEY6
	AES	XDATA2, XKEY6
	AES	XDATA3, XKEY6

	vmovdqa	XKEY_B, [KEYS + 8*16]

	AES	XDATA0, XKEY_A		; 7. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A
	AES	XDATA3, XKEY_A

	vmovdqa	XKEY_A, [KEYS + 9*16]

	AES	XDATA0, XKEY_B		; 8. ENC
	AES	XDATA1, XKEY_B
	AES	XDATA2, XKEY_B
	AES	XDATA3, XKEY_B

	vmovdqa	XKEY_B, [KEYS + 10*16]

	AES	XDATA0, XKEY_A		; 9. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A
	AES	XDATA3, XKEY_A

%if %%NROUNDS >= 12
	vmovdqa	XKEY_A, [KEYS + 11*16]

	AES	XDATA0, XKEY_B	; 10. ENC
	AES	XDATA1, XKEY_B
	AES	XDATA2, XKEY_B
	AES	XDATA3, XKEY_B

	vmovdqa	XKEY_B, [KEYS + 12*16]

	AES	XDATA0, XKEY_A		; 11. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A
	AES	XDATA3, XKEY_A
%endif

%if %%NROUNDS == 14
	vmovdqa	XKEY_A, [KEYS + 13*16]

	AES	XDATA0, XKEY_B		; 12. ENC
	AES	XDATA1, XKEY_B
	AES	XDATA2, XKEY_B
	AES	XDATA3, XKEY_B

	vmovdqa	XKEY_B, [KEYS + 14*16]

	AES	XDATA0, XKEY_A		; 13. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A
	AES	XDATA3, XKEY_A
%endif

	AES_LAST	XDATA0, XKEY_B	; 10/12/14. ENC (depending on key size)
	AES_LAST	XDATA1, XKEY_B
	AES_LAST	XDATA2, XKEY_B
	AES_LAST	XDATA3, XKEY_B

	vmovdqu	[OUT + 0*16], XDATA0
	vmovdqu	[OUT + 1*16], XDATA1
	vmovdqu	[OUT + 2*16], XDATA2
	vmovdqu	[OUT + 3*16], XDATA3

	cmp	LEN, 4*16
	jz	%%done
	jmp	%%main_loop

	align 16
%%main_loop:
	; load plain/cipher text
	vmovdqu	XDATA0, [IN + IDX + 0*16]
	vmovdqu	XDATA1, [IN + IDX + 1*16]
	vmovdqu	XDATA2, [IN + IDX + 2*16]
	vmovdqu	XDATA3, [IN + IDX + 3*16]

	vmovdqa	XKEY_A, [KEYS + 1*16]

	vpxor	XDATA0, XKEY0		; 0. ARK
	vpxor	XDATA1, XKEY0
	vpxor	XDATA2, XKEY0
	vpxor	XDATA3, XKEY0

	add	IDX, 4*16

	AES	XDATA0, XKEY_A		; 1. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A
	AES	XDATA3, XKEY_A

	vmovdqa	XKEY_A, [KEYS + 3*16]

	AES	XDATA0, XKEY2		; 2. ENC
	AES	XDATA1, XKEY2
	AES	XDATA2, XKEY2
	AES	XDATA3, XKEY2

	AES	XDATA0, XKEY_A		; 3. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A
	AES	XDATA3, XKEY_A

	vmovdqa	XKEY_A, [KEYS + 5*16]

	AES	XDATA0, XKEY4		; 4. ENC
	AES	XDATA1, XKEY4
	AES	XDATA2, XKEY4
	AES	XDATA3, XKEY4

	AES	XDATA0, XKEY_A		; 5. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A
	AES	XDATA3, XKEY_A

	vmovdqa	XKEY_A, [KEYS + 7*16]

	AES	XDATA0, XKEY6		; 6. ENC
	AES	XDATA1, XKEY6
	AES	XDATA2, XKEY6
	AES	XDATA3, XKEY6

	vmovdqa	XKEY_B, [KEYS + 8*16]

	AES	XDATA0, XKEY_A		; 7. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A
	AES	XDATA3, XKEY_A

	vmovdqa	XKEY_A, [KEYS + 9*16]

	AES	XDATA0, XKEY_B		; 8. ENC
	AES	XDATA1, XKEY_B
	AES	XDATA2, XKEY_B
	AES	XDATA3, XKEY_B

	vmovdqa	XKEY_B, [KEYS + 10*16]

        AES	XDATA0, XKEY_A		; 9. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A
	AES	XDATA3, XKEY_A

%if %%NROUNDS >= 12
	vmovdqa	XKEY_A, [KEYS + 11*16]

	AES	XDATA0, XKEY_B		; 10. ENC
	AES	XDATA1, XKEY_B
	AES	XDATA2, XKEY_B
	AES	XDATA3, XKEY_B

	vmovdqa	XKEY_B, [KEYS + 12*16]

	AES	XDATA0, XKEY_A		; 11. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A
	AES	XDATA3, XKEY_A
%endif

%if %%NROUNDS == 14
	vmovdqa	XKEY_A, [KEYS + 13*16]

	AES	XDATA0, XKEY_B		; 12. ENC
	AES	XDATA1, XKEY_B
	AES	XDATA2, XKEY_B
	AES	XDATA3, XKEY_B

	vmovdqa	XKEY_B, [KEYS + 14*16]

	AES	XDATA0, XKEY_A		; 13. ENC
	AES	XDATA1, XKEY_A
	AES	XDATA2, XKEY_A
	AES	XDATA3, XKEY_A
%endif

	AES_LAST	XDATA0, XKEY_B	; 10/12/14. ENC (depending on key size)
	AES_LAST	XDATA1, XKEY_B
	AES_LAST	XDATA2, XKEY_B
	AES_LAST	XDATA3, XKEY_B

	vmovdqu	[OUT + IDX + 0*16 - 4*16], XDATA0
	vmovdqu	[OUT + IDX + 1*16 - 4*16], XDATA1
	vmovdqu	[OUT + IDX + 2*16 - 4*16], XDATA2
	vmovdqu	[OUT + IDX + 3*16 - 4*16], XDATA3

	cmp	IDX, LEN
	jne	%%main_loop

%%done:

%ifdef SAFE_DATA
	clear_all_xmms_avx_asm
%endif ;; SAFE_DATA

	ret

%endmacro

;;
;; AES-ECB 128 functions
;;
%ifdef AES_ECB_ENC_128
align 16
MKGLOBAL(AES_ECB_ENC_128,function,internal)
AES_ECB_ENC_128:

        AES_ECB 10, ENC

align 16
MKGLOBAL(AES_ECB_DEC_128,function,internal)
AES_ECB_DEC_128:

        AES_ECB 10, DEC

%endif

;;
;; AES-ECB 192 functions
;;
%ifdef AES_ECB_ENC_192
align 16
MKGLOBAL(AES_ECB_ENC_192,function,internal)
AES_ECB_ENC_192:

        AES_ECB 12, ENC

align 16
MKGLOBAL(AES_ECB_DEC_192,function,internal)
AES_ECB_DEC_192:

        AES_ECB 12, DEC

%endif

;;
;; AES-ECB 256 functions
;;
%ifdef AES_ECB_ENC_256
align 16
MKGLOBAL(AES_ECB_ENC_256,function,internal)
AES_ECB_ENC_256:

        AES_ECB 14, ENC

align 16
MKGLOBAL(AES_ECB_DEC_256,function,internal)
AES_ECB_DEC_256:

        AES_ECB 14, DEC

%endif

mksection stack-noexec
