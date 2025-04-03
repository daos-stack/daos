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

;
; the following defines control the operation of the macros below and
; need to be defines in the including file
; KEY_ROUNDS - number of key rounds needed based on key length: 128bit - 11, 192bit - 13 or 256bit - 15
; EARLY_BLOCKS - number of data block to load before starting computations
; PARALLEL_BLOCKS - number of blocks of data to process in parallel also the number of xmm regs to reserve for data
; IV_CNT - number of xmm regs to use for IV data valid values of 0 or 1
; TMP_CNT - number of tmp xmm register to reserve
; XMM_USAGE    - number of xmm registers to use. must be at least the same as PARALLEL_BLOCKS + 2
;

%include "reg_sizes.asm"

[bits 64]
default rel
section .text

;
; the following instructions set specific macros must be defined in the user file
; to make use of the AES macros below
; MOVDQ - move from memory to xmm reg
; PXOR - XOR of two xmm registers         pxor
; AES_DEC - AES block decode for early key rounds
; AES_DEC_LAST  - AES block decode for last key round
; or
; AES_ENC - AES block encode for early key rounds
; AES_ENC_LAST  - AES block encode for last key round

; Three usages of xmm regs: key round cache, blocks data and one temp
; CKEY_CNT are (number of xmm regs) - PARALLEL_BLOCKS - IV holder - 2 TMP mmx reg
%assign FIRST_XDATA     (0)
%assign IV_IDX         (FIRST_XDATA + PARALLEL_BLOCKS)
%ifndef IV_CNT
%define IV_CNT          (1)
%endif
%assign TMP             (IV_IDX + IV_CNT)
%assign TMP_CNT         (2)
%assign FIRST_CKEY      (TMP + TMP_CNT)
%assign CKEY_CNT        (XMM_USAGE - (PARALLEL_BLOCKS + IV_CNT + TMP_CNT))

; Abstract xmm register usages that identify the expected contents of the register
%define reg(i)      xmm %+ i
%define XDATA(i)    xmm %+ i
%define KEY_REG(i)  xmm %+ i
%define IV_REG(i)   xmm %+ i

%define IDX		rax




;
;
;	AES CBC ENCODE MACROS
;
;

;
;	CBC_DECRYPT_BLOCKS
; Decrypts a number of blocks using AES_PARALLEL_ENC_BLOCKS macro
; Finalized the decryption and saves results in the output
; places last last buffers crypto text in IV for next buffer
; updates the index and number of bytes left
;
%macro CBC_DECRYPT_BLOCKS 17
%define %%TOT_ROUNDS	%1
%define %%num_blocks    %2      ; can be 0..13
%define %%EARLY_LOADS   %3	; number of data blocks to laod before processing
%define %%MOVDQ		%4
%define %%PXOR          %5
%define %%AES_DEC       %6
%define %%AES_DEC_LAST  %7
%define %%CACHED_KEYS   %8	; number of key data cached in xmm regs
%define %%TMP		%9
%define %%TMP_CNT	%10
%define %%FIRST_CKEY    %11
%define %%KEY_DATA	%12
%define %%FIRST_XDATA	%13
%define %%IN		%14	; input data
%define %%OUT		%15	; output data
%define %%IDX		%16	; index into input and output data buffers
%define %%LEN           %17

        AES_PARALLEL_ENC_BLOCKS %%TOT_ROUNDS, %%num_blocks, %%EARLY_LOADS, %%MOVDQ, %%PXOR, %%AES_DEC, %%AES_DEC_LAST, %%CACHED_KEYS, %%TMP, %%TMP_CNT, %%FIRST_CKEY, %%KEY_DATA, %%FIRST_XDATA, %%IN, %%OUT, %%IDX

        ;
        ; XOR the result of each block's decrypt with the previous block's cypher text (C)
        ;
        %assign i 0
        %rep (%%num_blocks)
	       %%PXOR	XDATA(i), XDATA(IV_IDX)     		; XOR result with previous block's C
	       %%MOVDQ	[%%OUT + %%IDX + i*16], XDATA(i)	; save plain text to out
	       %%MOVDQ	XDATA(IV_IDX), [%%IN + IDX + i*16]	; load IV with current block C
               %assign i (i+1)
        %endrep

	add	%%IDX, %%num_blocks*16
	sub	%%LEN, %%num_blocks*16
%endmacro


;
;	CBC_ENC_INIT
; XOR first data block with the IV data
%macro CBC_ENC_INIT 7
%define %%P_FIRST	%1
%define %%IV_IDX	%2
%define %%MOVDQ		%3
%define %%PXOR		%4
%define %%IV		%5
%define %%IN		%6	; input data
%define %%IDX		%7	; index into input and output data buffers

	%%MOVDQ	XDATA(%%P_FIRST), [%%IN + %%IDX + 0*16]
	%%MOVDQ	reg(%%IV_IDX), [%%IV]
	%%PXOR	XDATA(%%P_FIRST), reg(%%IV_IDX)
%endmacro

;
; assumptions:
; LEN is length of data remaining
; IDX is offset into the data buffer
;
; subloops
; if data > 16 load next block into a next XDATA reg (XDATA(p_next))
; load first uncached key into TMP0 (if any)
; AES block encript XDATA(P_FIRST)
; if data > 16 XOR next2 block (XDATA(p_next)) with current (XDATA(P_FIRST))
; save current (XDATA(P_FIRST))
; update indexes for P_FIRST
; end if data zero
;
%macro CBC_ENC_SUBLOOP 17
%define %%TOT_ROUNDS	%1
%define %%BLOCKS	%2      ; can be 1...14
%define %%START_DATA	%3
%define %%MOVDQ         %4
%define %%PXOR          %5
%define %%AES_DEC       %6
%define %%AES_DEC_LAST  %7
%define %%TMP		%8
%define %%TMP_CNT	%9
%define %%FIRST_CKEY	%10
%define %%CKEY_CNT	%11
%define %%KEYS		%12
%define %%CACHED_KEYS   %13
%define %%IN		%14	; input data
%define %%OUT		%15	; output data
%define %%IDX		%16	; index into input and output data buffers
%define %%LEN		%17

	%assign this_blk	0
	%assign next_blk	1
	%assign p_first		%%START_DATA
	%assign p_next		(p_first+1)
	; for number of blocks to be processed in a loop
	%assign blk	1
	%rep %%BLOCKS
		; if data > 16 load next block into a next XDATA reg (XDATA(p_next))
		cmp	%%LEN, 16
		%push	skip_read
		je	%$skip_read_next
		%%MOVDQ	XDATA(p_next), [%%IN + %%IDX + next_blk*16]
		%$skip_read_next:
		%pop

		AES_ENC_BLOCKS	  %%TOT_ROUNDS, p_first, %%TMP, %%TMP_CNT, %%FIRST_CKEY, %%CKEY_CNT, %%KEYS, %%MOVDQ, %%PXOR, %%AES_DEC, %%AES_DEC_LAST

		; if data > 16 XOR next2 block (XDATA(p_next)) with current (XDATA(p_first))
		cmp	%%LEN, 16
		%push	skip_next
		je %$skip_next_blk_start
		%%PXOR	XDATA(p_next), XDATA(p_first)
		%$skip_next_blk_start:
		%pop

		; save current (XDATA(p_first))
		%%MOVDQ	[%%OUT + %%IDX + this_blk*16], XDATA(p_first)
		; update indexes for p_first
		add	%%IDX, 16
		sub	%%LEN, 16

		%if (blk < %%BLOCKS) ; only insert jz if NOT last block
		    ; end if data zero
		    jz	%%END_CBC_ENC_SUBLOOP
		%endif ; (p_next < %%BLOCKS)

		%assign p_first	(p_next)
		%assign blk	(blk+1)
		%if (blk == %%BLOCKS) ; the last rep loop's read of the next block needs to be into START_DATA
			%assign p_next (%%START_DATA)
		%elif (1 == %%BLOCKS)
			%%MOVDQ	XDATA(%%START_DATA), XDATA(p_next)
		%else
			%assign p_next	(p_next+1)
		%endif
	%endrep ; %%BLOCKS

	%%END_CBC_ENC_SUBLOOP:
%endm ; CBC_ENC_SUBLOOP


;
;
;	AES BLOCK ENCODE MACROS
;
;

;
;	FILL_KEY_CACHE
; Load key data into the cache key xmm regs
%macro FILL_KEY_CACHE 4
%define %%CACHED_KEYS	%1
%define %%CKEY_START	%2
%define %%KEY_DATA	%3
%define %%MOVDQ		%4

	%assign rnd	0
	%rep	KEY_ROUNDS
	  %if	(rnd < %%CACHED_KEYS)                   ; find the round's key data
	  	%assign c	(rnd + %%CKEY_START)
	        %%MOVDQ	KEY_REG(c), [%%KEY_DATA + rnd*16]	;load sub key into an available register
	  %endif
	  %assign rnd	(rnd+1)
	%endrep
%endmacro

;
;	SCHEDULE_DATA_LOAD
; pre-loades message data into xmm regs
; updates global 'blocks_loaded' that tracks which data blocks have been loaded
; 'blocks_loaded' is an in/out global and must be declared in the using macro or function
%macro SCHEDULE_DATA_LOAD 5
%define %%PARALLEL_DATA	%1
%define %%EARLY_LOADS 	%2
%define %%MOVDQ         %3
%define %%IN		%4
%define %%IDX		%5

        %if (blocks_loaded < %%PARALLEL_DATA)
                ; load cipher text
                %%MOVDQ  XDATA(blocks_loaded), [%%IN + %%IDX + blocks_loaded*16]
                %assign blocks_loaded (blocks_loaded+1)
        %endif ; (blocks_loaded < %%PARALLEL_DATA)
%endmacro ; SCHEDULED_EARLY_DATA_LOADS

;
;	INIT_SELECT_KEY
; determine which xmm reg holds the key data needed or loades it into the temp register if not cached
; 'current_tmp' is an in/out global and must be declared in the using macro or function
%macro INIT_SELECT_KEY 6
%define %%TOT_ROUNDS	%1
%define %%CACHED_KEYS	%2
%define %%KEY_DATA	%3
%define %%FIRST_TMP	%4
%define %%TMP_CNT	%5
%define %%MOVDQ		%6

	%assign current_tmp (%%FIRST_TMP)
	%if (%%TOT_ROUNDS > %%CACHED_KEYS)		; load the first uncached key into temp reg
		%%MOVDQ	KEY_REG(current_tmp), [%%KEY_DATA + %%CACHED_KEYS*16]
	%endif ; (KEY_ROUNDS > CKEY_CNT)
%endmacro ; SELECT_KEY

;
;	SELECT_KEY
; determine which xmm reg holds the key data needed or loades it into the temp register if not cached
; 'current_tmp' is an in/out global and must be declared in the using macro or function
%macro SELECT_KEY 8
%define %%ROUND		%1
%define %%TOT_ROUNDS    %2
%define %%CACHED_KEYS	%3
%define %%FIRST_KEY	%4
%define %%KEY_DATA	%5
%define %%FIRST_TMP     %6
%define %%TMP_CNT	%7
%define %%MOVDQ		%8

	; find the key data for this round
	%if (%%ROUND < %%CACHED_KEYS)                   ; is it cached
		%assign key (%%ROUND + %%FIRST_KEY)
	%else
		; Load non-cached key %%ROUND data ping-ponging between temp regs if more than one
		%assign key (current_tmp)                              ; use the previous loaded key data
		%if (1 == %%TMP_CNT)
			%%MOVDQ	KEY_REG(current_tmp), [%%KEY_DATA + %%ROUND*16] ; load the next rounds key data
		%else
			%assign next_round (%%ROUND+1)
			%if (next_round < %%TOT_ROUNDS)                      ; if more rounds to be done
				%if (current_tmp == %%FIRST_TMP)                            ; calc the next temp reg to use
					%assign current_tmp (current_tmp + 1)
				%else
					%assign current_tmp (%%FIRST_TMP)
				%endif ; (current_tmp == %%FIRST_TMP)
				%%MOVDQ	KEY_REG(current_tmp), [%%KEY_DATA + next_round*16] ; load the next rounds key data

			%endif ; (%%ROUND < KEY_ROUNDS)
		%endif ; (1 < %%TMP_CNT)
	%endif ; (%%ROUND < %%CACHED_KEYS)
%endmacro ; SELECT_KEY


;
;	AES_PARALLEL_ENC_BLOCKS
; preloads some data blocks to be worked on
; starts the aes block encoding while loading the other blocks to be done in parallel
; aes block encodes each key round on each block
%macro AES_PARALLEL_ENC_BLOCKS 16
%define %%KEY_ROUNDS	%1
%define %%PARALLEL_DATA	%2
%define %%EARLY_LOADS	%3
%define %%MOVDQ		%4
%define %%PXOR		%5
%define %%AES_DEC	%6
%define %%AES_DEC_LAST	%7
%define %%CACHED_KEYS	%8
%define %%TMP		%9
%define %%TMP_CNT	%10
%define %%FIRST_CKEY    %11
%define %%KEY_DATA	%12
%define %%FIRST_XDATA   %13
%define %%IN		%14	; input data
%define %%OUT		%15	; output data
%define %%IDX		%16	; index into input and output data buffers

	%assign	blocks_loaded	0

	%rep	%%EARLY_LOADS
		SCHEDULE_DATA_LOAD  %%PARALLEL_DATA, %%EARLY_LOADS, %%MOVDQ, %%IN, %%IDX ; updates blocks_loaded
	%endrep ; %%EARLY_LOADS

	%assign current_tmp (TMP)
	INIT_SELECT_KEY  %%KEY_ROUNDS, %%CACHED_KEYS, %%KEY_DATA, %%TMP, %%TMP_CNT, %%MOVDQ

	%assign	round	0
	%assign	key	0
	%rep	KEY_ROUNDS			; for all key rounds
		SELECT_KEY round, %%KEY_ROUNDS, %%CACHED_KEYS, %%FIRST_CKEY, %%KEY_DATA, %%TMP, %%TMP_CNT, %%MOVDQ

		%assign	i	%%FIRST_XDATA
		%rep 	%%PARALLEL_DATA		; for each block do the EAS block encode step
			%if	(0 == round)
				%%PXOR		XDATA(i), KEY_REG(key)		         ; first round's step
				SCHEDULE_DATA_LOAD  %%PARALLEL_DATA, %%EARLY_LOADS, %%MOVDQ, %%IN, %%IDX

			%elif	( (%%KEY_ROUNDS-1) == round )
				%%AES_DEC_LAST	XDATA(i), KEY_REG(key)		 ; last round's step

			%else
			        %%AES_DEC	XDATA(i), KEY_REG(key)		 ; middle round's (1..last-1) step

		        %endif
		        %assign i (i+1)
		%endrep ;%%PARALLEL_DATA
		%assign round (round+1)
	%endrep ;KEY_ROUNDS
%endmacro ; AES_PARALLEL_ENC_BLOCKS



;
;	AES_ENC_BLOCKS
; load first uncached key into TMP0 (if any)
; AES block encript XDATA(p_first)
;   before using uncached key in TMP0, load next key in TMP1
;   before using uncached key in TMP1, load next key in TMP0
%macro AES_ENC_BLOCKS 11
%define %%TOT_ROUNDS    %1
%define %%ENC_BLOCK	%2
%define %%TMP		%3
%define %%TMP_CNT	%4
%define %%FIRST_CKEY    %5
%define %%CACHED_KEYS	%6
%define %%KEY_DATA	%7
%define %%MOVDQ		%8
%define %%PXOR		%9
%define %%AES_ENC	%10
%define %%AES_ENC_LAST	%11

	%assign current_tmp (%%TMP)
	INIT_SELECT_KEY %%TOT_ROUNDS, %%CACHED_KEYS, %%KEY_DATA, %%TMP, %%TMP_CNT, %%MOVDQ

	%assign round	0
	%assign key	(round + %%FIRST_CKEY)
	%rep %%TOT_ROUNDS                                 ; for all key rounds
		; find the key data for this round
		SELECT_KEY round, %%TOT_ROUNDS, %%CACHED_KEYS, %%FIRST_CKEY, %%KEY_DATA, %%TMP, %%TMP_CNT, %%MOVDQ

		; encrypt block
		%if (0 == round)
			%%PXOR	XDATA(%%ENC_BLOCK), KEY_REG(key)		; round zero step
		%elif ( (%%TOT_ROUNDS-1) == round )
			%%AES_ENC_LAST	XDATA(%%ENC_BLOCK), KEY_REG(key)	; last round's step
		%else
			%%AES_ENC	XDATA(%%ENC_BLOCK), KEY_REG(key)	; rounds 1..last-1 step
		%endif ; (0 == round)

		%assign round	(round+1)
	%endrep ; KEY_ROUNDS
%endmacro ; AES_ENC


