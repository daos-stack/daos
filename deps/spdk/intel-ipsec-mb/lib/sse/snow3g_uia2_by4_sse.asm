;;
;; Copyright (c) 2021, Intel Corporation
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
%include "include/cet.inc"
%include "include/memcpy.asm"
%include "include/const.inc"
%define APPEND(a,b) a %+ b
%define APPEND3(a,b,c) a %+ b %+ c

%ifdef LINUX
%define arg1 rdi
%define arg2 rsi
%define arg3 rdx
%define arg4 rcx
%else
%define arg1 rcx
%define arg2 rdx
%define arg3 r8
%define arg4 r9
%endif

%define E               rax
%define qword_len       r12
%define offset          r10
%define tmp             r10
%define tmp2            arg4
%define tmp3            r11
%define tmp4            r13
%define tmp5            r14
%define tmp6            r15
%define in_ptr          arg1
%define KS              arg2
%define bit_len         arg3
%define end_offset      tmp3

%define EV              xmm2
%define SNOW3G_CONST    xmm7
%define P1              xmm8

%ifndef SNOW3G_F9_1_BUFFER_INTERNAL
%define SNOW3G_F9_1_BUFFER_INTERNAL snow3g_f9_1_buffer_internal_sse
%endif

mksection .rodata
default rel

align 16
snow3g_constant:
dq      0x000000000000001b, 0x0000000000000000

align 16
bswap64:
dq      0x0001020304050607, 0x08090a0b0c0d0e0f

align 16
clear_hi64:
dq      0xffffffffffffffff, 0x0000000000000000

align 16
clear_low32:
dd      0x00000000, 0xffffffff, 0xffffffff, 0xffffffff

mksection .text

%ifidn __OUTPUT_FORMAT__, win64
        %define XMM_STORAGE     16*3
        %define GP_STORAGE      8*8
%else
        %define XMM_STORAGE     0
        %define GP_STORAGE      6*8
%endif

%define VARIABLE_OFFSET XMM_STORAGE + GP_STORAGE
%define GP_OFFSET XMM_STORAGE

%macro FUNC_SAVE 0
        mov     r11, rsp
        sub     rsp, VARIABLE_OFFSET
        and     rsp, ~15

%ifidn __OUTPUT_FORMAT__, win64
        ; xmm6:xmm15 need to be maintained for Windows
        movdqa [rsp + 0*16], xmm6
        movdqa [rsp + 1*16], xmm7
        movdqa [rsp + 2*16], xmm8
        mov     [rsp + GP_OFFSET + 48], rdi
        mov     [rsp + GP_OFFSET + 56], rsi
%endif
        mov     [rsp + GP_OFFSET],      r12
        mov     [rsp + GP_OFFSET + 8],  r13
        mov     [rsp + GP_OFFSET + 16], r14
        mov     [rsp + GP_OFFSET + 24], r15
        mov     [rsp + GP_OFFSET + 32], rbx
        mov     [rsp + GP_OFFSET + 40], r11 ;; rsp pointer
%endmacro

%macro FUNC_RESTORE 0

%ifidn __OUTPUT_FORMAT__, win64
        movdqa xmm6,  [rsp + 0*16]
        movdqa xmm7,  [rsp + 1*16]
        movdqa xmm8,  [rsp + 2*16]
        mov     rdi, [rsp + GP_OFFSET + 48]
        mov     rsi, [rsp + GP_OFFSET + 56]
%endif
        mov     r12, [rsp + GP_OFFSET]
        mov     r13, [rsp + GP_OFFSET + 8]
        mov     r14, [rsp + GP_OFFSET + 16]
        mov     r15, [rsp + GP_OFFSET + 24]
        mov     rbx, [rsp + GP_OFFSET + 32]
        mov     rsp, [rsp + GP_OFFSET + 40]
%endmacro

;; Reduce from 128 bits to 64 bits
%macro REDUCE_TO_64 2
%define %%IN_OUT        %1 ;; [in/out]
%define %%XTMP          %2 ;; [clobbered]

        movdqa          %%XTMP, %%IN_OUT

        pclmulqdq       %%XTMP, SNOW3G_CONST, 0x01
        pxor            %%IN_OUT, %%XTMP

        pclmulqdq       %%XTMP, SNOW3G_CONST, 0x01
        pxor            %%IN_OUT, %%XTMP

%endmacro

;; Multiply 64b x 64b and reduce result to 64 bits
;; Lower 64-bits of xmms are multiplied
%macro MUL_AND_REDUCE_TO_64 2-3
%define %%IN0_OUT       %1 ;; [in/out]
%define %%IN1           %2 ;; [in] Note: clobbered when only 3 args passed
%define %%XTMP          %3 ;; [clobbered]

        pclmulqdq       %%IN0_OUT, %%IN1, 0x00
%if %0 == 2
        ;; clobber XTMP if 3 args passed, otherwise preserve
        REDUCE_TO_64 %%IN0_OUT, %%IN1
%else
        REDUCE_TO_64 %%IN0_OUT, %%XTMP
%endif
%endmacro

;; uint32_t
;; snow3g_f9_1_buffer_internal_sse(const uint64_t *pBufferIn,
;;                                 const uint32_t KS[5],
;;                                 const uint64_t lengthInBits);
align 16
MKGLOBAL(SNOW3G_F9_1_BUFFER_INTERNAL,function,internal)
SNOW3G_F9_1_BUFFER_INTERNAL:
        endbranch64

        FUNC_SAVE

        movdqa  SNOW3G_CONST, [rel snow3g_constant]
        pxor    EV, EV

        ;; P = ((uint64_t)KS[0] << 32) | ((uint64_t)KS[1])
        movq    P1, [KS]
        pshufd  P1, P1, 1110_0001b

        xor     offset, offset

        mov     qword_len, bit_len              ;; lenInBits -> lenInQwords
        shr     qword_len, 6
        je      partial_blk

        mov     end_offset, qword_len
        and     end_offset, 0xfffffffffffffffc  ;; round down to nearest 4 blocks

        cmp     qword_len, 4                    ;; check at least 4 qwords
        jb      start_single_blk_loop

        movdqa  xmm1, P1
        MUL_AND_REDUCE_TO_64 xmm1, P1, xmm4     ;; xmm1 = P2
        movdqa  xmm5, xmm1
        MUL_AND_REDUCE_TO_64 xmm5, P1, xmm4     ;; xmm5 = P3
        pand    xmm5, [rel clear_hi64]
        movdqa  xmm3, xmm5
        MUL_AND_REDUCE_TO_64 xmm3, P1, xmm4     ;; xmm3 = P4

        movdqa     xmm0, P1
        punpcklqdq xmm0, xmm1                   ;; xmm0 = p1p2
        movdqa     xmm1, xmm5
        punpcklqdq xmm1, xmm3                   ;; xmm1 = p3p4

start_4_blk_loop:
        movdqu          xmm3, [in_ptr + offset * 8]
        movdqu          xmm4, [in_ptr + offset * 8 + 16]

        pshufb          xmm3, [rel bswap64]
        pshufb          xmm4, [rel bswap64]

        pxor            xmm3, EV                ;; m1 XOR EV

        movdqa          xmm5, xmm4

        pclmulqdq       xmm5, xmm0, 0x10        ;; t1 = pclmulqdq_wrap(m2, p1p2, 0x10);
        pclmulqdq       xmm4, xmm0, 0x01        ;; t2 = pclmulqdq_wrap(m2, p1p2, 0x01);

        pxor            xmm5, xmm4

        movdqa          EV, xmm3
        pclmulqdq       EV, xmm1, 0x10          ;; t2 = pclmulqdq_wrap(m1, p3p4, 0x10);
        pclmulqdq       xmm3, xmm1, 0x01        ;; t3 = pclmulqdq_wrap(m1, p3p4, 0x01);

        pxor            EV, xmm3                ;; t2 = _mm_xor_si128(t2, t3);
        pxor            EV, xmm5                ;; t1 = _mm_xor_si128(t2, t1);

        REDUCE_TO_64    EV, xmm3                ;; EV = reduce128_to_64(t1);

        pand            EV, [rel clear_hi64]    ;; EV = _mm_and_si128(EV, clear_hi64);

        add     offset, 4                       ;; move to next 4 blocks
        cmp     end_offset, offset
        jne     start_4_blk_loop                ;; at least 4 blocks left

        ;; less than 4 blocks left
        jmp     single_blk_chk

start_single_blk_loop:
        movq    xmm0, [in_ptr + offset * 8]
        pshufb  xmm0, [rel bswap64]
        pxor    EV, xmm0
        MUL_AND_REDUCE_TO_64 EV, P1, xmm1

        inc     offset

single_blk_chk:
        cmp     qword_len, offset
        ja      start_single_blk_loop

partial_blk:
        mov     tmp5, 0x3f      ;; len_in_bits % 64
        and     tmp5, bit_len
        jz      skip_rem_bits

        ;; load last N bytes
        mov     tmp2, tmp5      ;; (rem_bits + 7) / 8
        add     tmp2, 7
        shr     tmp2, 3

        shl     offset, 3       ;; qwords -> bytes
        add     in_ptr, offset  ;; in + offset to last block

        simd_load_sse_15_1 xmm3, in_ptr, tmp2
        movq    tmp3, xmm3
        bswap   tmp3

        mov     tmp, 0xffffffffffffffff
        mov     tmp6, 64
        sub     tmp6, tmp5

        SHIFT_GP tmp, tmp6, tmp, tmp5, left

        and     tmp3, tmp       ;;  V &= (((uint64_t)-1) << (64 - rem_bits)); /* mask extra bits */
        movq    xmm0, tmp3
        pxor    EV, xmm0

        MUL_AND_REDUCE_TO_64 EV, P1, xmm3

skip_rem_bits:
        ;; /* Multiply by Q */
        ;; E = multiply_and_reduce64(E ^ lengthInBits,
        ;;                           (((uint64_t)z[2] << 32) | ((uint64_t)z[3])));
        ;; /* Final MAC */
        ;; *(uint32_t *)pDigest =
        ;;        (uint32_t)BSWAP64(E ^ ((uint64_t)z[4] << 32));
        movq    xmm3, bit_len
        pxor    EV, xmm3

        movq    xmm1, [KS + 8]                  ;; load z[2:3]
        pshufd  xmm1, xmm1, 1110_0001b

        mov     DWORD(tmp4), [KS + (4 * 4)]         ;; tmp4 == z[4] << 32
        shl     tmp4, 32

        MUL_AND_REDUCE_TO_64 EV, xmm1, xmm3
        movq    E, EV
        xor     E, tmp4

        bswap   E                               ;; return E (rax/eax)

        FUNC_RESTORE

        ret

mksection stack-noexec
