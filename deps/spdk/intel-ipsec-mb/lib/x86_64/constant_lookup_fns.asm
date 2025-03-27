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

%include "include/os.asm"
%include "include/reg_sizes.asm"
%include "include/constant_lookup.asm"

mksection .rodata
default rel

align 16
idx_tab8:
        db 0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7,
        db 0x8,  0x9,  0xA,  0xB,  0xC,  0xD,  0xE,  0xF,

align 16
add_16:
        db 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
        db 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10

align 16
idx_tab16:
        dw 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7

align 16
add_8:
        dw 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8

align 16
idx_tab32:
        dd 0x0,  0x1,  0x2,  0x3

align 16
add_4:
        dd 0x4, 0x4, 0x4, 0x4

align 16
idx_tab64:
        dq 0x0,  0x1

align 16
add_2:
        dq 0x2, 0x2

align 16
bcast_mask:
        db 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01,
        db 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01

align 64
idx_rows_avx:
times 4 dd 0x00000000
times 4 dd 0x10101010
times 4 dd 0x20202020
times 4 dd 0x30303030
times 4 dd 0x40404040
times 4 dd 0x50505050
times 4 dd 0x60606060
times 4 dd 0x70707070
times 4 dd 0x80808080
times 4 dd 0x90909090
times 4 dd 0xa0a0a0a0
times 4 dd 0xb0b0b0b0
times 4 dd 0xc0c0c0c0
times 4 dd 0xd0d0d0d0
times 4 dd 0xe0e0e0e0
times 4 dd 0xf0f0f0f0

align 64
idx_rows_avx2:
times 8 dd 0x00000000
times 8 dd 0x10101010
times 8 dd 0x20202020
times 8 dd 0x30303030
times 8 dd 0x40404040
times 8 dd 0x50505050
times 8 dd 0x60606060
times 8 dd 0x70707070
times 8 dd 0x80808080
times 8 dd 0x90909090
times 8 dd 0xa0a0a0a0
times 8 dd 0xb0b0b0b0
times 8 dd 0xc0c0c0c0
times 8 dd 0xd0d0d0d0
times 8 dd 0xe0e0e0e0
times 8 dd 0xf0f0f0f0

mksection .text

%ifdef LINUX
        %define arg1    rdi
        %define arg2    rsi
        %define arg3    rdx
%else
        %define arg1    rcx
        %define arg2    rdx
        %define arg3    r8
%endif

%define bcast_idx xmm0
%define xadd      xmm1
%define accum_val xmm2
%define xindices  xmm3
%define xtmp      xmm4
%define xtmp2     xmm5
%define tmp       r9
%define offset    r10

%define table   arg1
%define idx     arg2
%define size    arg3

; uint8_t lookup_8bit_sse(const void *table, const uint32_t idx, const uint32_t size);
; arg 1 : pointer to table to look up
; arg 2 : index to look up
; arg 3 : size of table to look up (multiple of 16 bytes)
align 32
MKGLOBAL(lookup_8bit_sse,function,internal)
lookup_8bit_sse:

        ;; Number of loop iters = matrix size / 4 (number of values in XMM)
        shr     size, 4
        je      exit8_sse

        xor     offset, offset

        ;; Broadcast idx to look up
        movd    bcast_idx, DWORD(idx)
        pxor    xtmp, xtmp
        pxor    accum_val, accum_val
        pshufb  bcast_idx, xtmp

        movdqa  xadd,     [rel add_16]
        movdqa  xindices, [rel idx_tab8]

loop8_sse:
        movdqa  xtmp, xindices

        ;; Compare indices with idx
        ;; This generates a mask with all 0s except for the position where idx matches (all 1s here)
        pcmpeqb xtmp, bcast_idx

        ;; Load next 16 values
        movdqa  xtmp2, [table + offset]

        ;; This generates data with all 0s except the value we are looking for in the index to look up
        pand    xtmp2, xtmp

        por     accum_val, xtmp2

        ;; Get next 16 indices
        paddb   xindices, xadd

        add     offset, 16
        dec     size

        jne     loop8_sse

        ;; Extract value from XMM register
        movdqa  xtmp, accum_val
        pslldq  xtmp, 8      ; shift left by 64 bits
        por     accum_val, xtmp

        movdqa  xtmp, accum_val
        pslldq  xtmp, 4      ; shift left by 32 bits
        por     accum_val, xtmp

        movdqa  xtmp, accum_val
        pslldq  xtmp, 2      ; shift left by 16 bits
        por     accum_val, xtmp

        movdqa  xtmp, accum_val
        pslldq  xtmp, 1      ; shift left by 8 bits
        por     accum_val, xtmp

        pextrb  rax, accum_val, 15

exit8_sse:
        ret

; uint8_t lookup_8bit_avx(const void *table, const uint32_t idx, const uint32_t size);
; arg 1 : pointer to table to look up
; arg 2 : index to look up
; arg 3 : size of table to look up (multiple of 16 bytes)
align 32
MKGLOBAL(lookup_8bit_avx,function,internal)
lookup_8bit_avx:
        ;; Number of loop iters = matrix size / 4 (number of values in XMM)
        shr     size, 4
        je      exit8_avx

        xor     offset, offset

        ;; Broadcast idx to look up
        vmovd   bcast_idx, DWORD(idx)
        vpxor   xtmp, xtmp
        vpxor   accum_val, accum_val
        vpshufb bcast_idx, xtmp

        vmovdqa xadd,     [rel add_16]
        vmovdqa xindices, [rel idx_tab8]

loop8_avx:
        ;; Compare indices with idx
        ;; This generates a mask with all 0s except for the position where idx matches (all 1s here)
        vpcmpeqb xtmp, xindices, bcast_idx

        ;; Load next 16 values
        vmovdqa xtmp2, [table + offset]

        ;; This generates data with all 0s except the value we are looking for in the index to look up
        vpand   xtmp2, xtmp

        vpor    accum_val, xtmp2

        ;; Get next 16 indices
        vpaddb  xindices, xadd

        add     offset, 16
        dec     size

        jne     loop8_avx

        ;; Extract value from XMM register
        vpslldq xtmp, accum_val, 8      ; shift left by 64 bits
        vpor    accum_val, xtmp

        vpslldq xtmp, accum_val, 4      ; shift left by 32 bits
        vpor    accum_val, xtmp

        vpslldq xtmp, accum_val, 2      ; shift left by 16 bits
        vpor    accum_val, xtmp

        vpslldq xtmp, accum_val, 1      ; shift left by 8 bits
        vpor    accum_val, xtmp

        vpextrb rax, accum_val, 15

exit8_avx:

        ret

; uint8_t lookup_16bit_sse(const void *table, const uint32_t idx, const uint32_t size);
; arg 1 : pointer to table to look up
; arg 2 : index to look up
; arg 3 : size of table to look up
align 32
MKGLOBAL(lookup_16bit_sse,function,internal)
lookup_16bit_sse:

        ;; Number of loop iters = matrix size / 8 (number of values in XMM)
        shr     size, 3
        je      exit16_sse

        xor     offset, offset

        ;; Broadcast idx to look up
        movd    bcast_idx, DWORD(idx)
        movdqa  xtmp, [rel bcast_mask]
        pxor    accum_val, accum_val
        pshufb  bcast_idx, xtmp

        movdqa  xadd,     [rel add_8]
        movdqa  xindices, [rel idx_tab16]

loop16_sse:

        movdqa  xtmp, xindices

        ;; Compare indices with idx
        ;; This generates a mask with all 0s except for the position where idx matches (all 1s here)
        pcmpeqw xtmp, bcast_idx

        ;; Load next 8 values
        movdqa  xtmp2, [table + offset]

        ;; This generates data with all 0s except the value we are looking for in the index to look up
        pand    xtmp2, xtmp

        por     accum_val, xtmp2

        ;; Get next 8 indices
        paddw   xindices, xadd
        add     offset, 16
        dec     size

        jne     loop16_sse

        ;; Extract value from XMM register
        movdqa  xtmp, accum_val
        pslldq  xtmp, 8      ; shift left by 64 bits
        por     accum_val, xtmp

        movdqa  xtmp, accum_val
        pslldq  xtmp, 4      ; shift left by 32 bits
        por     accum_val, xtmp

        movdqa  xtmp, accum_val
        pslldq  xtmp, 2      ; shift left by 16 bits
        por     accum_val, xtmp

        pextrw  rax, accum_val, 7

exit16_sse:
        ret

; uint8_t lookup_16bit_avx(const void *table, const uint32_t idx, const uint32_t size);
; arg 1 : pointer to table to look up
; arg 2 : index to look up
; arg 3 : size of table to look up
align 32
MKGLOBAL(lookup_16bit_avx,function,internal)
lookup_16bit_avx:

        ;; Number of loop iters = matrix size / 8 (number of values in XMM)
        shr     size, 3
        je      exit16_avx

        xor     offset, offset

        ;; Broadcast idx to look up
        vmovd   bcast_idx, DWORD(idx)
        vmovdqa xtmp, [rel bcast_mask]
        vpxor   accum_val, accum_val
        vpshufb bcast_idx, xtmp

        vmovdqa xadd,     [rel add_8]
        vmovdqa xindices, [rel idx_tab16]

loop16_avx:

        ;; Compare indices with idx
        ;; This generates a mask with all 0s except for the position where idx matches (all 1s here)
        vpcmpeqw xtmp, xindices, bcast_idx

        ;; Load next 16 values
        vmovdqa xtmp2, [table + offset]

        ;; This generates data with all 0s except the value we are looking for in the index to look up
        vpand   xtmp2, xtmp

        vpor    accum_val, xtmp2

        ;; Get next 8 indices
        vpaddw  xindices, xadd
        add     offset, 16
        dec     size

        jne     loop16_avx

        ;; Extract value from XMM register
        vpslldq xtmp, accum_val, 8 ; shift left by 64 bits
        vpor    accum_val, xtmp

        vpslldq xtmp, accum_val, 4 ; shift left by 32 bits
        vpor    accum_val, xtmp

        vpslldq xtmp, accum_val, 2 ; shift left by 16 bits
        vpor    accum_val, xtmp

        vpextrw rax, accum_val, 7

exit16_avx:
        ret

; uint32_t lookup_32bit_sse(const void *table, const uint32_t idx, const uint32_t size);
; arg 1 : pointer to table to look up
; arg 2 : index to look up
; arg 3 : size of table to look up
align 32
MKGLOBAL(lookup_32bit_sse,function,internal)
lookup_32bit_sse:

        ;; Number of loop iters = matrix size / 4 (number of values in XMM)
        shr     size, 2
        je      exit32_sse

        xor     offset, offset

        ;; Broadcast idx to look up
        movd    bcast_idx, DWORD(idx)
        pxor    accum_val, accum_val
        pshufd  bcast_idx, bcast_idx, 0

        movdqa  xadd,     [rel add_4]
        movdqa  xindices, [rel idx_tab32]

loop32_sse:
        movdqa  xtmp, xindices

        ;; Compare indices with idx
        ;; This generates a mask with all 0s except for the position where idx matches (all 1s here)
        pcmpeqd xtmp, bcast_idx

        ;; Load next 4 values
        movdqa  xtmp2, [table + offset]

        ;; This generates data with all 0s except the value we are looking for in the index to look up
        pand    xtmp2, xtmp

        por     accum_val, xtmp2

        ;; Get next 4 indices
        paddd   xindices, xadd
        add     offset, 16
        dec     size

        jne     loop32_sse

        ;; Extract value from XMM register
        movdqa  xtmp, accum_val
        psrldq  xtmp, 8      ; shift right by 64 bits
        por     accum_val, xtmp

        movdqa  xtmp, accum_val
        psrldq  xtmp, 4      ; shift right by 32 bits
        por     accum_val, xtmp

        movd    eax, accum_val

exit32_sse:
        ret

; uint32_t lookup_32bit_avx(const void *table, const uint32_t idx, const uint32_t size);
; arg 1 : pointer to table to look up
; arg 2 : index to look up
; arg 3 : size of table to look up
align 32
MKGLOBAL(lookup_32bit_avx,function,internal)
lookup_32bit_avx:
        ;; Number of loop iters = matrix size / 4 (number of values in XMM)
        shr     size, 2
        je      exit32_avx

        xor     offset, offset

        ;; Broadcast idx to look up
        vmovd   bcast_idx, DWORD(idx)
        vpxor   accum_val, accum_val
        vpshufd bcast_idx, bcast_idx, 0

        vmovdqa xadd,     [rel add_4]
        vmovdqa xindices, [rel idx_tab32]

loop32_avx:
        ;; Compare indices with idx
        ;; This generates a mask with all 0s except for the position where idx matches (all 1s here)
        vpcmpeqd xtmp, xindices, bcast_idx

        ;; Load next 4 values
        vmovdqa xtmp2, [table + offset]

        ;; This generates data with all 0s except the value we are looking for in the index to look up
        vpand   xtmp2, xtmp

        vpor    accum_val, xtmp2

        ;; Get next 4 indices
        vpaddd  xindices, xadd
        add     offset, 16
        dec     size

        jne     loop32_avx

        ;; Extract value from XMM register
        vpsrldq xtmp, accum_val, 8 ; shift right by 64 bits
        vpor    accum_val, xtmp

        vpsrldq xtmp, accum_val, 4 ; shift right by 32 bits
        vpor    accum_val, xtmp

        vmovd   eax, accum_val

exit32_avx:
        ret

; uint64_t lookup_64bit_sse(const void *table, const uint32_t idx, const uint32_t size);
; arg 1 : pointer to table to look up
; arg 2 : index to look up
; arg 3 : size of table to look up
align 32
MKGLOBAL(lookup_64bit_sse,function,internal)
lookup_64bit_sse:
        ;; Number of loop iters = matrix size / 2 (number of values in XMM)
        shr     size, 1
        je      exit64_sse

        xor     offset, offset

        ;; Broadcast idx to look up
        movq    bcast_idx, idx
        pxor    accum_val, accum_val
        pinsrq  bcast_idx, idx, 1

        movdqa  xadd,     [rel add_2]
        movdqa  xindices, [rel idx_tab64]

loop64_sse:
        movdqa  xtmp, xindices

        ;; Compare indices with idx
        ;; This generates a mask with all 0s except for the position where idx matches (all 1s here)
        pcmpeqq xtmp, bcast_idx

        ;; Load next 2 values
        movdqa  xtmp2, [table + offset]

        ;; This generates data with all 0s except the value we are looking for in the index to look up
        pand    xtmp2, xtmp

        por     accum_val, xtmp2

        ;; Get next 2 indices
        paddq   xindices, xadd
        add     offset, 16
        dec     size

        jne     loop64_sse

        ;; Extract value from XMM register
        movdqa  xtmp, accum_val
        psrldq  xtmp, 8      ; shift right by 64 bits
        por     accum_val, xtmp

        movq     rax, accum_val

exit64_sse:
        ret

; uint64_t lookup_64bit_avx(const void *table, const uint32_t idx, const uint32_t size);
; arg 1 : pointer to table to look up
; arg 2 : index to look up
; arg 3 : size of table to look up
align 32
MKGLOBAL(lookup_64bit_avx,function,internal)
lookup_64bit_avx:
        ;; Number of loop iters = matrix size / 2 (number of values in XMM)
        shr     size, 1
        je      exit64_avx

        xor     offset, offset

        vmovq    bcast_idx, idx
        vpxor    accum_val, accum_val
        vpinsrq  bcast_idx, idx, 1

        vmovdqa xadd,     [rel add_2]
        vmovdqa xindices, [rel idx_tab64]

loop64_avx:
        ;; Compare indices with idx
        ;; This generates a mask with all 0s except for the position where idx matches (all 1s here)
        vpcmpeqq xtmp, xindices, bcast_idx

        ;; Load next 2 values
        vmovdqa xtmp2, [table + offset]

        ;; This generates data with all 0s except the value we are looking for in the index to look up
        vpand   xtmp2, xtmp

        vpor    accum_val, xtmp2

        ;; Get next 2 indices
        vpaddq  xindices, xadd
        add     offset, 16
        dec     size

        jne     loop64_avx

        ;; Extract value from XMM register
        vpsrldq xtmp, accum_val, 8 ; shift right by 64 bits
        vpor    accum_val, xtmp

        vmovq   rax, accum_val

exit64_avx:
        ret

; __m128i lookup_16x8bit_sse(const __m128i indexes, const void *table)
; arg 1 : vector with 16 8-bit indexes to be looked up
; arg 2 : pointer to a 256 element table
align 32
MKGLOBAL(lookup_16x8bit_sse,function,internal)
lookup_16x8bit_sse:
%define arg_indexes xmm0
%define arg_return  xmm0
%define arg_table   arg1

%ifndef LINUX
%undef arg_table
%define arg_table   arg2

        ; Read indices from memory, as __m128i parameters are stored
        ; in stack (aligned to 16 bytes) and its address is passed through GP register on Windows
        movdqa          arg_indexes, [arg1]
        mov             rax, rsp
        sub             rsp, (10 * 16)
        and             rsp, ~15
        ;; xmm6:xmm15 need to be maintained for Windows
        movdqa          [rsp + 0*16], xmm6
        movdqa          [rsp + 1*16], xmm7
        movdqa          [rsp + 2*16], xmm8
        movdqa          [rsp + 3*16], xmm9
        movdqa          [rsp + 4*16], xmm10
        movdqa          [rsp + 5*16], xmm11
        movdqa          [rsp + 6*16], xmm12
        movdqa          [rsp + 7*16], xmm13
        movdqa          [rsp + 8*16], xmm14
        movdqa          [rsp + 9*16], xmm15
%endif
        movdqa          xmm15, [rel idx_rows_avx + (15 * 16)]
        movdqa          xmm14, xmm15
        psrlq           xmm14, 4
        movdqa          xmm1, arg_indexes
        movdqa          xmm2, arg_indexes
        pand            xmm1, xmm15        ;; top nibble part of the index
        pand            xmm2, xmm14        ;; low nibble part of the index

        movdqa          xmm9,  xmm1
        movdqa          xmm10, xmm1
        movdqa          xmm11, xmm1
        movdqa          xmm12, xmm1
        movdqa          xmm13, xmm1
        movdqa          xmm14, xmm1
        pcmpeqb         xmm9, [rel idx_rows_avx + (0 * 16)]
        movdqa          xmm3, [arg_table + (0 * 16)]
        pcmpeqb         xmm10, [rel idx_rows_avx + (1 * 16)]
        movdqa          xmm4, [arg_table + (1 * 16)]
        pcmpeqb         xmm11, [rel idx_rows_avx + (2 * 16)]
        movdqa          xmm5, [arg_table + (2 * 16)]
        pcmpeqb         xmm12, [rel idx_rows_avx + (3 * 16)]
        movdqa          xmm6, [arg_table + (3 * 16)]
        pcmpeqb         xmm13, [rel idx_rows_avx + (4 * 16)]
        movdqa          xmm7, [arg_table + (4 * 16)]
        pcmpeqb         xmm14, [rel idx_rows_avx + (5 * 16)]
        movdqa          xmm8, [arg_table + (5 * 16)]

        pshufb          xmm3, xmm2
        pshufb          xmm4, xmm2
        pshufb          xmm5, xmm2
        pshufb          xmm6, xmm2
        pshufb          xmm7, xmm2
        pshufb          xmm8, xmm2

        pand            xmm9,  xmm3
        pand            xmm10, xmm4
        pand            xmm11, xmm5
        pand            xmm12, xmm6
        pand            xmm13, xmm7
        pand            xmm14, xmm8

        por             xmm9,  xmm10
        por             xmm11, xmm12
        por             xmm14, xmm13
        movdqa          arg_return, xmm9
        por             arg_return, xmm11

        ;; xmm8 and xmm14 are used for final OR result from now on.
        ;; arg_return & xmm14 carry current OR result.

        movdqa          xmm9,  xmm1
        movdqa          xmm10, xmm1
        movdqa          xmm11, xmm1
        movdqa          xmm12, xmm1
        movdqa          xmm13, xmm1

        pcmpeqb         xmm9,  [rel idx_rows_avx + (6 * 16)]
        movdqa          xmm3, [arg_table + (6 * 16)]
        pcmpeqb         xmm10, [rel idx_rows_avx + (7 * 16)]
        movdqa          xmm4, [arg_table + (7 * 16)]
        pcmpeqb         xmm11, [rel idx_rows_avx + (8 * 16)]
        movdqa          xmm5, [arg_table + (8 * 16)]
        pcmpeqb         xmm12, [rel idx_rows_avx + (9 * 16)]
        movdqa          xmm6, [arg_table + (9 * 16)]
        pcmpeqb         xmm13, [rel idx_rows_avx + (10 * 16)]
        movdqa          xmm7, [arg_table + (10 * 16)]

        pshufb          xmm3, xmm2
        pshufb          xmm4, xmm2
        pshufb          xmm5, xmm2
        pshufb          xmm6, xmm2
        pshufb          xmm7, xmm2

        pand            xmm9,  xmm3
        pand            xmm10, xmm4
        pand            xmm11, xmm5
        pand            xmm12, xmm6
        pand            xmm13, xmm7

        por             xmm9,  xmm10
        por             xmm11, xmm12
        por             xmm14, xmm13
        por             arg_return, xmm9
        por             xmm14, xmm11

        ;; arg_return & xmm15 carry current OR result

        movdqa          xmm9,  xmm1
        movdqa          xmm10, xmm1
        movdqa          xmm11, xmm1
        movdqa          xmm12, xmm1
        movdqa          xmm13, xmm1

        pcmpeqb         xmm9,  [rel idx_rows_avx + (11 * 16)]
        movdqa          xmm3, [arg_table + (11 * 16)]
        pcmpeqb         xmm10, [rel idx_rows_avx + (12 * 16)]
        movdqa          xmm4, [arg_table + (12 * 16)]
        pcmpeqb         xmm11, [rel idx_rows_avx + (13 * 16)]
        movdqa          xmm5, [arg_table + (13 * 16)]
        pcmpeqb         xmm12, [rel idx_rows_avx + (14 * 16)]
        movdqa          xmm6, [arg_table + (14 * 16)]
        pcmpeqb         xmm13, [rel idx_rows_avx + (15 * 16)]
        movdqa          xmm7, [arg_table + (15 * 16)]

        pshufb          xmm3, xmm2
        pshufb          xmm4, xmm2
        pshufb          xmm5, xmm2
        pshufb          xmm6, xmm2
        pshufb          xmm7, xmm2

        pand            xmm9,  xmm3
        pand            xmm10, xmm4
        pand            xmm11, xmm5
        pand            xmm12, xmm6
        pand            xmm13, xmm7

        por             xmm9,  xmm10
        por             xmm11, xmm12
        por             xmm14, xmm13
        por             arg_return, xmm9
        por             xmm14, xmm11
        por             arg_return, xmm14

%ifndef LINUX
        movdqa          xmm15, [rsp + 9*16]
        movdqa          xmm14, [rsp + 8*16]
        movdqa          xmm13, [rsp + 7*16]
        movdqa          xmm12, [rsp + 6*16]
        movdqa          xmm11, [rsp + 5*16]
        movdqa          xmm10, [rsp + 4*16]
        movdqa          xmm9,  [rsp + 3*16]
        movdqa          xmm8,  [rsp + 2*16]
        movdqa          xmm7,  [rsp + 1*16]
        movdqa          xmm6,  [rsp + 0*16]
%ifdef SAFE_DATA
        pxor            xmm5, xmm5
        movdqa          [rsp + 0*16], xmm5
        movdqa          [rsp + 1*16], xmm5
        movdqa          [rsp + 2*16], xmm5
        movdqa          [rsp + 3*16], xmm5
        movdqa          [rsp + 4*16], xmm5
        movdqa          [rsp + 5*16], xmm5
        movdqa          [rsp + 6*16], xmm5
        movdqa          [rsp + 7*16], xmm5
        movdqa          [rsp + 8*16], xmm5
        movdqa          [rsp + 9*16], xmm5
%endif                          ; SAFE_DATA
        mov             rsp, rax
%endif                          ; !LINUX
        ret
%undef arg_indexes
%undef arg_return
%undef arg_table

; __m128i lookup_16x8bit_avx(const __m128i indexes, const void *table)
; arg 1 : vector with 16 8-bit indexes to be looked up
; arg 2 : pointer to a 256 element table
align 32
MKGLOBAL(lookup_16x8bit_avx,function,internal)
lookup_16x8bit_avx:
%define arg_indexes xmm0
%define arg_return  xmm0
%define arg_table   arg1

%ifndef LINUX
%undef arg_table
%define arg_table   arg2

        ; Read indices from memory, as __m128i parameters are stored
        ; in stack (aligned to 16 bytes) and its address is passed through GP register on Windows
        vmovdqa         arg_indexes, [arg1]
        mov             rax, rsp
        sub             rsp, (10 * 16)
        and             rsp, ~15
        ;; xmm6:xmm15 need to be maintained for Windows
        vmovdqa         [rsp + 0*16], xmm6
        vmovdqa         [rsp + 1*16], xmm7
        vmovdqa         [rsp + 2*16], xmm8
        vmovdqa         [rsp + 3*16], xmm9
        vmovdqa         [rsp + 4*16], xmm10
        vmovdqa         [rsp + 5*16], xmm11
        vmovdqa         [rsp + 6*16], xmm12
        vmovdqa         [rsp + 7*16], xmm13
        vmovdqa         [rsp + 8*16], xmm14
        vmovdqa         [rsp + 9*16], xmm15
%endif                          ; !LINUX

        vmovdqa         xmm15, [rel idx_rows_avx + (15 * 16)]
        vpsrlq          xmm2, xmm15, 4

        vpand           xmm1, xmm15, arg_indexes        ;; top nibble part of the index
        vpand           xmm2, xmm2, arg_indexes         ;; low nibble part of the index

        vpcmpeqb        xmm9,  xmm1, [rel idx_rows_avx + (0 * 16)]
        vmovdqa         xmm3, [arg_table + (0 * 16)]
        vpcmpeqb        xmm10, xmm1, [rel idx_rows_avx + (1 * 16)]
        vmovdqa         xmm4, [arg_table + (1 * 16)]
        vpcmpeqb        xmm11, xmm1, [rel idx_rows_avx + (2 * 16)]
        vmovdqa         xmm5, [arg_table + (2 * 16)]
        vpcmpeqb        xmm12, xmm1, [rel idx_rows_avx + (3 * 16)]
        vmovdqa         xmm6, [arg_table + (3 * 16)]
        vpcmpeqb        xmm13, xmm1, [rel idx_rows_avx + (4 * 16)]
        vmovdqa         xmm7, [arg_table + (4 * 16)]
        vpcmpeqb        xmm14, xmm1, [rel idx_rows_avx + (5 * 16)]
        vmovdqa         xmm8, [arg_table + (5 * 16)]

        vpshufb         xmm3, xmm3, xmm2
        vpshufb         xmm4, xmm4, xmm2
        vpshufb         xmm5, xmm5, xmm2
        vpshufb         xmm6, xmm6, xmm2
        vpshufb         xmm7, xmm7, xmm2
        vpshufb         xmm8, xmm8, xmm2

        vpand           xmm9,  xmm9,  xmm3
        vpand           xmm10, xmm10, xmm4
        vpand           xmm11, xmm11, xmm5
        vpand           xmm12, xmm12, xmm6
        vpand           xmm13, xmm13, xmm7
        vpand           xmm14, xmm14, xmm8

        vpor            xmm9,  xmm9,  xmm10
        vpor            xmm11, xmm11, xmm12
        vpor            xmm14, xmm13, xmm14
        vpor            arg_return, xmm9, xmm11

        ;; xmm8 and xmm14 are used for final OR result from now on.
        ;; arg_return & xmm14 carry current OR result.

        vpcmpeqb        xmm9,  xmm1, [rel idx_rows_avx + (6 * 16)]
        vmovdqa         xmm3, [arg_table + (6 * 16)]
        vpcmpeqb        xmm10, xmm1, [rel idx_rows_avx + (7 * 16)]
        vmovdqa         xmm4, [arg_table + (7 * 16)]
        vpcmpeqb        xmm11, xmm1, [rel idx_rows_avx + (8 * 16)]
        vmovdqa         xmm5, [arg_table + (8 * 16)]
        vpcmpeqb        xmm12, xmm1, [rel idx_rows_avx + (9 * 16)]
        vmovdqa         xmm6, [arg_table + (9 * 16)]
        vpcmpeqb        xmm13, xmm1, [rel idx_rows_avx + (10 * 16)]
        vmovdqa         xmm7, [arg_table + (10 * 16)]

        vpshufb         xmm3, xmm3, xmm2
        vpshufb         xmm4, xmm4, xmm2
        vpshufb         xmm5, xmm5, xmm2
        vpshufb         xmm6, xmm6, xmm2
        vpshufb         xmm7, xmm7, xmm2

        vpand           xmm9,  xmm9,  xmm3
        vpand           xmm10, xmm10, xmm4
        vpand           xmm11, xmm11, xmm5
        vpand           xmm12, xmm12, xmm6
        vpand           xmm13, xmm13, xmm7

        vpor            xmm9,  xmm9,  xmm10
        vpor            xmm11, xmm11, xmm12
        vpor            xmm15, xmm9,  xmm11
        vpor            xmm8,  xmm14, xmm13

        ;; arg_return, xmm15 & xmm8 carry current OR result

        vpcmpeqb        xmm9,  xmm1, [rel idx_rows_avx + (11 * 16)]
        vmovdqa         xmm3, [arg_table + (11 * 16)]
        vpcmpeqb        xmm10, xmm1, [rel idx_rows_avx + (12 * 16)]
        vmovdqa         xmm4, [arg_table + (12 * 16)]
        vpcmpeqb        xmm11, xmm1, [rel idx_rows_avx + (13 * 16)]
        vmovdqa         xmm5, [arg_table + (13 * 16)]
        vpcmpeqb        xmm12, xmm1, [rel idx_rows_avx + (14 * 16)]
        vmovdqa         xmm6, [arg_table + (14 * 16)]
        vpcmpeqb        xmm13, xmm1, [rel idx_rows_avx + (15 * 16)]
        vmovdqa         xmm7, [arg_table + (15 * 16)]

        vpshufb         xmm3, xmm3, xmm2
        vpshufb         xmm4, xmm4, xmm2
        vpshufb         xmm5, xmm5, xmm2
        vpshufb         xmm6, xmm6, xmm2
        vpshufb         xmm7, xmm7, xmm2

        vpand           xmm9,  xmm9,  xmm3
        vpand           xmm10, xmm10, xmm4
        vpand           xmm11, xmm11, xmm5
        vpand           xmm12, xmm12, xmm6
        vpand           xmm13, xmm13, xmm7

        vpor            xmm14, xmm15, xmm8
        vpor            xmm9,  xmm9,  xmm10
        vpor            xmm11, xmm11, xmm12
        vpor            xmm13, xmm13, xmm14
        vpor            xmm15, xmm9,  xmm11
        vpor            arg_return, arg_return, xmm13
        vpor            arg_return, arg_return, xmm15

%ifndef LINUX
        vmovdqa         xmm15, [rsp + 9*16]
        vmovdqa         xmm14, [rsp + 8*16]
        vmovdqa         xmm13, [rsp + 7*16]
        vmovdqa         xmm12, [rsp + 6*16]
        vmovdqa         xmm11, [rsp + 5*16]
        vmovdqa         xmm10, [rsp + 4*16]
        vmovdqa         xmm9,  [rsp + 3*16]
        vmovdqa         xmm8,  [rsp + 2*16]
        vmovdqa         xmm7,  [rsp + 1*16]
        vmovdqa         xmm6,  [rsp + 0*16]
%ifdef SAFE_DATA
        vpxor           xmm5, xmm5, xmm5
        vmovdqa         [rsp + 0*16], xmm5
        vmovdqa         [rsp + 1*16], xmm5
        vmovdqa         [rsp + 2*16], xmm5
        vmovdqa         [rsp + 3*16], xmm5
        vmovdqa         [rsp + 4*16], xmm5
        vmovdqa         [rsp + 5*16], xmm5
        vmovdqa         [rsp + 6*16], xmm5
        vmovdqa         [rsp + 7*16], xmm5
        vmovdqa         [rsp + 8*16], xmm5
        vmovdqa         [rsp + 9*16], xmm5
%endif
        mov             rsp, rax
%endif                          ; !LINUX
        ret
%undef arg_indexes
%undef arg_return
%undef arg_table

; __m256i lookup_32x8bit_avx2(const __m256i indexes, const void *table)
; arg 1 : vector with 32 8-bit indexes to be looked up
; arg 2 : pointer to a 256 element table
align 32
MKGLOBAL(lookup_32x8bit_avx2,function,internal)
lookup_32x8bit_avx2:
%define arg_indexes ymm0
%define arg_return  ymm0
%define arg_table   arg1

%ifndef LINUX
%undef arg_table
%define arg_table   arg2

        mov             rax, rsp
        sub             rsp, (10 * 16)
        and             rsp, ~31
        ;; xmm6:xmm15 need to be maintained for Windows
        vmovdqa         [rsp + 0*16], xmm6
        vmovdqa         [rsp + 1*16], xmm7
        vmovdqa         [rsp + 2*16], xmm8
        vmovdqa         [rsp + 3*16], xmm9
        vmovdqa         [rsp + 4*16], xmm10
        vmovdqa         [rsp + 5*16], xmm11
        vmovdqa         [rsp + 6*16], xmm12
        vmovdqa         [rsp + 7*16], xmm13
        vmovdqa         [rsp + 8*16], xmm14
        vmovdqa         [rsp + 9*16], xmm15
%endif                          ; !LINUX

        vmovdqa         ymm15, [rel idx_rows_avx2 + (15 * 32)]
        vpsrlq          ymm2, ymm15, 4

        vpand           ymm1, ymm15, arg_indexes        ;; top nibble part of the index
        vpand           ymm2, ymm2, arg_indexes         ;; low nibble part of the index

        vpcmpeqb        ymm9,  ymm1, [rel idx_rows_avx2 + (0 * 32)]
        vbroadcastf128  ymm3, [arg_table + (0 * 16)]
        vpcmpeqb        ymm10, ymm1, [rel idx_rows_avx2 + (1 * 32)]
        vbroadcastf128  ymm4, [arg_table + (1 * 16)]
        vpcmpeqb        ymm11, ymm1, [rel idx_rows_avx2 + (2 * 32)]
        vbroadcastf128  ymm5, [arg_table + (2 * 16)]
        vpcmpeqb        ymm12, ymm1, [rel idx_rows_avx2 + (3 * 32)]
        vbroadcastf128  ymm6, [arg_table + (3 * 16)]
        vpcmpeqb        ymm13, ymm1, [rel idx_rows_avx2 + (4 * 32)]
        vbroadcastf128  ymm7, [arg_table + (4 * 16)]
        vpcmpeqb        ymm14, ymm1, [rel idx_rows_avx2 + (5 * 32)]
        vbroadcastf128  ymm8, [arg_table + (5 * 16)]

        vpshufb         ymm3, ymm3, ymm2
        vpshufb         ymm4, ymm4, ymm2
        vpshufb         ymm5, ymm5, ymm2
        vpshufb         ymm6, ymm6, ymm2
        vpshufb         ymm7, ymm7, ymm2
        vpshufb         ymm8, ymm8, ymm2

        vpand           ymm9,  ymm9,  ymm3
        vpand           ymm10, ymm10, ymm4
        vpand           ymm11, ymm11, ymm5
        vpand           ymm12, ymm12, ymm6
        vpand           ymm13, ymm13, ymm7
        vpand           ymm14, ymm14, ymm8

        vpor            ymm9,  ymm9,  ymm10
        vpor            ymm11, ymm11, ymm12
        vpor            ymm14, ymm13, ymm14
        vpor            arg_return, ymm9, ymm11

        ;; ymm8 and ymm14 are used for final OR result from now on.
        ;; arg_return & ymm14 carry current OR result.

        vpcmpeqb        ymm9,  ymm1, [rel idx_rows_avx2 + (6 * 32)]
        vbroadcastf128  ymm3, [arg_table + (6 * 16)]
        vpcmpeqb        ymm10, ymm1, [rel idx_rows_avx2 + (7 * 32)]
        vbroadcastf128  ymm4, [arg_table + (7 * 16)]
        vpcmpeqb        ymm11, ymm1, [rel idx_rows_avx2 + (8 * 32)]
        vbroadcastf128  ymm5, [arg_table + (8 * 16)]
        vpcmpeqb        ymm12, ymm1, [rel idx_rows_avx2 + (9 * 32)]
        vbroadcastf128  ymm6, [arg_table + (9 * 16)]
        vpcmpeqb        ymm13, ymm1, [rel idx_rows_avx2 + (10 * 32)]
        vbroadcastf128  ymm7, [arg_table + (10 * 16)]

        vpshufb         ymm3, ymm3, ymm2
        vpshufb         ymm4, ymm4, ymm2
        vpshufb         ymm5, ymm5, ymm2
        vpshufb         ymm6, ymm6, ymm2
        vpshufb         ymm7, ymm7, ymm2

        vpand           ymm9,  ymm9,  ymm3
        vpand           ymm10, ymm10, ymm4
        vpand           ymm11, ymm11, ymm5
        vpand           ymm12, ymm12, ymm6
        vpand           ymm13, ymm13, ymm7

        vpor            ymm9,  ymm9,  ymm10
        vpor            ymm11, ymm11, ymm12
        vpor            ymm15, ymm9, ymm11
        vpor            ymm8,  ymm14, ymm13

        ;; arg_return, ymm15 & ymm8 carry current OR result

        vpcmpeqb        ymm9,  ymm1, [rel idx_rows_avx2 + (11 * 32)]
        vbroadcastf128  ymm3, [arg_table + (11 * 16)]
        vpcmpeqb        ymm10, ymm1, [rel idx_rows_avx2 + (12 * 32)]
        vbroadcastf128  ymm4, [arg_table + (12 * 16)]
        vpcmpeqb        ymm11, ymm1, [rel idx_rows_avx2 + (13 * 32)]
        vbroadcastf128  ymm5, [arg_table + (13 * 16)]
        vpcmpeqb        ymm12, ymm1, [rel idx_rows_avx2 + (14 * 32)]
        vbroadcastf128  ymm6, [arg_table + (14 * 16)]
        vpcmpeqb        ymm13, ymm1, [rel idx_rows_avx2 + (15 * 32)]
        vbroadcastf128  ymm7, [arg_table + (15 * 16)]

        vpshufb         ymm3, ymm3, ymm2
        vpshufb         ymm4, ymm4, ymm2
        vpshufb         ymm5, ymm5, ymm2
        vpshufb         ymm6, ymm6, ymm2
        vpshufb         ymm7, ymm7, ymm2

        vpand           ymm9,  ymm9,  ymm3
        vpand           ymm10, ymm10, ymm4
        vpand           ymm11, ymm11, ymm5
        vpand           ymm12, ymm12, ymm6
        vpand           ymm13, ymm13, ymm7

        vpor            ymm14, ymm15, ymm8
        vpor            ymm9,  ymm9,  ymm10
        vpor            ymm11, ymm11, ymm12
        vpor            ymm13, ymm13, ymm14
        vpor            ymm15, ymm9, ymm11
        vpor            arg_return, arg_return, ymm13
        vpor            arg_return, arg_return, ymm15

%ifndef LINUX
        vmovdqa         xmm15, [rsp + 9*16]
        vmovdqa         xmm14, [rsp + 8*16]
        vmovdqa         xmm13, [rsp + 7*16]
        vmovdqa         xmm12, [rsp + 6*16]
        vmovdqa         xmm11, [rsp + 5*16]
        vmovdqa         xmm10, [rsp + 4*16]
        vmovdqa         xmm9,  [rsp + 3*16]
        vmovdqa         xmm8,  [rsp + 2*16]
        vmovdqa         xmm7,  [rsp + 1*16]
        vmovdqa         xmm6,  [rsp + 0*16]
%ifdef SAFE_DATA
        vpxor           ymm5, ymm5, ymm5
        vmovdqa         [rsp + 0*16], ymm5
        vmovdqa         [rsp + 2*16], ymm5
        vmovdqa         [rsp + 4*16], ymm5
        vmovdqa         [rsp + 6*16], ymm5
        vmovdqa         [rsp + 8*16], ymm5
%endif
        mov             rsp, rax
%endif                          ; !LINUX
        ret
%undef arg_indexes
%undef arg_return
%undef arg_table

; void lookup_64x8bit_avx512(const void *indices, void *ret, const void *table)
; arg 1 : memory with 64 8-bit indices to be looked up
; arg 2 : memory to write 64 8-bit values from the table
; arg 3 : pointer to a 256 8-bit element table
align 32
MKGLOBAL(lookup_64x8bit_avx512,function,internal)
lookup_64x8bit_avx512:

%ifndef LINUX

        mov             rax, rsp
        sub             rsp, (2 * 16)
        and             rsp, ~31
        ;; xmm6:xmm7 need to be maintained for Windows
        vmovdqa         [rsp + 0*16], xmm6
        vmovdqa         [rsp + 1*16], xmm7
%endif                          ; !LINUX

        ; Read the indices into zmm0
        vmovdqu64       zmm0, [arg1]
        LOOKUP8_64_AVX512 zmm0, zmm0, arg3, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, \
                          k1, k2, k3, k4

        ; Write the values to arg2
        vmovdqu64       [arg2], zmm0

%ifndef LINUX
        vmovdqa         xmm7,  [rsp + 1*16]
        vmovdqa         xmm6,  [rsp + 0*16]
%ifdef SAFE_DATA
        vpxorq          ymm5, ymm5
        vmovdqa64       [rsp], ymm5
%endif
        mov             rsp, rax
%endif                          ; !LINUX
        ret

; void lookup_64x8bit_avx512_vbmi(const void *indices, void *ret, const void *table)
; arg 1 : memory with 64 8-bit indices to be looked up
; arg 2 : memory to write 64 8-bit values from the table
; arg 3 : pointer to a 256 8-bit element table
align 32
MKGLOBAL(lookup_64x8bit_avx512_vbmi,function,internal)
lookup_64x8bit_avx512_vbmi:

        ; Read the indices into zmm0
        vmovdqu64       zmm0, [arg1]
        LOOKUP8_64_AVX512_VBMI zmm0, zmm0, arg3, zmm1, zmm2, zmm3, \
                               zmm4, zmm5, zmm16, zmm17, k1

        ; Write the values to arg2
        vmovdqu64       [arg2], zmm0

        ret

mksection stack-noexec
