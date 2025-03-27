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
%include "include/memcpy.asm"
%include "include/imb_job.asm"
%include "include/clear_regs.asm"
%include "include/cet.inc"

[bits 64]
default rel

align 64
mask_44:
dq      0xfffffffffff, 0xfffffffffff, 0xfffffffffff, 0xfffffffffff,
dq      0xfffffffffff, 0xfffffffffff, 0xfffffffffff, 0xfffffffffff

align 64
mask_42:
dq      0x3ffffffffff, 0x3ffffffffff, 0x3ffffffffff, 0x3ffffffffff,
dq      0x3ffffffffff, 0x3ffffffffff, 0x3ffffffffff, 0x3ffffffffff

align 64
high_bit:
dq      0x10000000000, 0x10000000000, 0x10000000000, 0x10000000000,
dq      0x10000000000, 0x10000000000, 0x10000000000, 0x10000000000

align 64
byte_len_to_mask_table:
        dw      0x0000, 0x0001, 0x0003, 0x0007,
        dw      0x000f, 0x001f, 0x003f, 0x007f,
        dw      0x00ff, 0x01ff, 0x03ff, 0x07ff,
        dw      0x0fff, 0x1fff, 0x3fff, 0x7fff,
        dw      0xffff

align 16
pad16_bit:
dq      0x01, 0x0
dq      0x0100, 0x0
dq      0x010000, 0x0
dq      0x01000000, 0x0
dq      0x0100000000, 0x0
dq      0x010000000000, 0x0
dq      0x01000000000000, 0x0
dq      0x0100000000000000, 0x0
dq      0x0, 0x01
dq      0x0, 0x0100
dq      0x0, 0x010000
dq      0x0, 0x01000000
dq      0x0, 0x0100000000
dq      0x0, 0x010000000000
dq      0x0, 0x01000000000000
dq      0x0, 0x0100000000000000

align 64
pad64_bit:
times 4 dq 0x0101010101010100, 0x0101010101010101

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

qword_high_bit_mask:
dw      0, 0x1, 0x5, 0x15, 0x55, 0x57, 0x5f, 0x7f, 0xff

%ifdef LINUX
%define arg1    rdi
%define arg2    rsi
%define arg3    rdx
%define arg4    rcx

%define job     arg1
%define gp1     rsi
%define gp2     rcx

%else
%define arg1    rcx
%define arg2    rdx
%define arg3    r8
%define arg4    r9

%define job     rdi
%define gp1     rcx     ;; 'arg1' copied to 'job' at start
%define gp2     rsi
%endif

;; don't use rdx and rax - they are needed for multiply operation
%define gp3     rbp
%define gp4     r8
%define gp5     r9
%define gp6     r10
%define gp7     r11
%define gp8     r12
%define gp9     r13
%define gp10    r14
%define gp11    r15

%xdefine len    gp11
%xdefine msg    gp10

%define POLY1305_BLOCK_SIZE 16

%define APPEND(a,b) a %+ b

struc STACKFRAME
_r_save:        resz    6  ; Memory to save limbs of powers of R
_gpr_save:      resq    8  ; Memory to save GP registers
_rsp_save:      resq    1  ; Memory to save RSP
endstruc

mksection .text

;; =============================================================================
;; =============================================================================
;; Initializes POLY1305 context structure
;; =============================================================================
%macro POLY1305_INIT 6
%define %%KEY %1        ; [in] pointer to 32-byte key
%define %%A0  %2        ; [out] GPR with accumulator bits 63..0
%define %%A1  %3        ; [out] GPR with accumulator bits 127..64
%define %%A2  %4        ; [out] GPR with accumulator bits 195..128
%define %%R0  %5        ; [out] GPR with R constant bits 63..0
%define %%R1  %6        ; [out] GPR with R constant bits 127..64

        ;; R = KEY[0..15] & 0xffffffc0ffffffc0ffffffc0fffffff
        mov     %%R0, 0x0ffffffc0fffffff
        and     %%R0, [%%KEY + (0 * 8)]

        mov     %%R1, 0x0ffffffc0ffffffc
        and     %%R1, [%%KEY + (1 * 8)]

        ;; set accumulator to 0
        xor     %%A0, %%A0
        xor     %%A1, %%A1
        xor     %%A2, %%A2
%endmacro

;; =============================================================================
;; =============================================================================
;; Computes hash for message length being multiple of block size
;; =============================================================================
%macro POLY1305_MUL_REDUCE 11-12
%define %%A0      %1    ; [in/out] GPR with accumulator bits 63:0
%define %%A1      %2    ; [in/out] GPR with accumulator bits 127:64
%define %%A2      %3    ; [in/out] GPR with accumulator bits 195:128
%define %%R0      %4    ; [in] GPR with R constant bits 63:0
%define %%R1      %5    ; [in] GPR with R constant bits 127:64
%define %%C1      %6    ; [in] C1 = R1 + (R1 >> 2)
%define %%T1      %7    ; [clobbered] GPR register
%define %%T2      %8    ; [clobbered] GPR register
%define %%T3      %9    ; [clobbered] GPR register
%define %%GP_RAX  %10   ; [clobbered] RAX register
%define %%GP_RDX  %11   ; [clobbered] RDX register
%define %%ONLY128 %12   ; [in] Used if input A2 is 0

        ;; Combining 64-bit x 64-bit multiplication with reduction steps
        ;;
        ;; NOTES:
        ;;   1) A2 here is only two bits so anything above is subject of reduction.
        ;;      Constant C1 = R1 + (R1 >> 2) simplifies multiply with less operations
        ;;   2) Magic 5x comes from mod 2^130-5 property and incorporating
        ;;      reduction into multiply phase.
        ;;      See "Cheating at modular arithmetic" and "Poly1305's prime: 2^130 - 5"
        ;;      paragraphs at https://loup-vaillant.fr/tutorials/poly1305-design for more details.
        ;;
        ;; Flow of the code below is as follows:
        ;;
        ;;          A2        A1        A0
        ;;        x           R1        R0
        ;;   -----------------------------
        ;;       A2×R0     A1×R0     A0×R0
        ;;   +             A0×R1
        ;;   +           5xA2xR1   5xA1xR1
        ;;   -----------------------------
        ;;     [0|L2L] [L1H|L1L] [L0H|L0L]
        ;;
        ;;   Registers:  T3:T2     T1:A0
        ;;
        ;; Completing the multiply and adding (with carry) 3x128-bit limbs into
        ;; 192-bits again (3x64-bits):
        ;; A0 = L0L
        ;; A1 = L0H + L1L
        ;; T3 = L1H + L2L

        ;; T3:T2 = (A0 * R1)
        mov     %%GP_RAX, %%R1
        mul     %%A0
        mov     %%T2, %%GP_RAX
        mov     %%GP_RAX, %%R0
        mov     %%T3, %%GP_RDX

        ;; T1:A0 = (A0 * R0)
        mul     %%A0
        mov     %%A0, %%GP_RAX  ;; A0 not used in other operations
        mov     %%GP_RAX, %%R0
        mov     %%T1, %%GP_RDX

        ;; T3:T2 += (A1 * R0)
        mul     %%A1
        add     %%T2, %%GP_RAX
        mov     %%GP_RAX, %%C1
        adc     %%T3, %%GP_RDX

        ;; T1:A0 += (A1 * R1x5)
        mul     %%A1
%if %0 == 11
        mov     %%A1, %%A2      ;; use A1 for A2
%endif
        add     %%A0, %%GP_RAX
        adc     %%T1, %%GP_RDX

        ;; NOTE: A2 is clamped to 2-bits,
        ;;       R1/R0 is clamped to 60-bits,
        ;;       their product is less than 2^64.

%if %0 == 11
        ;; T3:T2 += (A2 * R1x5)
        imul    %%A1, %%C1
        add     %%T2, %%A1
        mov     %%A1, %%T1 ;; T1:A0 => A1:A0
        adc     %%T3, 0

        ;; T3:A1 += (A2 * R0)
        imul    %%A2, %%R0
        add     %%A1, %%T2
        adc     %%T3, %%A2
        ;; If A2 == 0, just move and add T1-T2 to A1
%else
        mov     %%A1, %%T1
        add     %%A1, %%T2
        adc     %%T3, 0
%endif

        ;; At this point, 3 64-bit limbs are in T3:A1:A0
        ;; T3 can span over more than 2 bits so final partial reduction step is needed.
        ;;
        ;; Partial reduction (just to fit into 130 bits)
        ;;    A2 = T3 & 3
        ;;    k = (T3 & ~3) + (T3 >> 2)
        ;;         Y    x4  +  Y    x1
        ;;    A2:A1:A0 += k
        ;;
        ;; Result will be in A2:A1:A0
        mov     %%T1, %%T3
        mov     DWORD(%%A2), DWORD(%%T3)
        and     %%T1, ~3
        shr     %%T3, 2
        and     DWORD(%%A2), 3
        add     %%T1, %%T3

        ;; A2:A1:A0 += k (kept in T1)
        add     %%A0, %%T1
        adc     %%A1, 0
        adc     DWORD(%%A2), 0
%endmacro

;; =============================================================================
;; =============================================================================
;; Computes hash for 8 16-byte message blocks,
;; and adds new message blocks to accumulator.
;;
;; It first multiplies all 8 blocks with powers of R:
;;
;;      a2      a1      a0
;; ×    b2      b1      b0
;; ---------------------------------------
;;     a2×b0   a1×b0   a0×b0
;; +   a1×b1   a0×b1 5×a2×b1
;; +   a0×b2 5×a2×b2 5×a1×b2
;; ---------------------------------------
;;        p2      p1      p0
;;
;; Then, it propagates the carry (higher bits after bit 43) from lower limbs into higher limbs,
;; multiplying by 5 in case of the carry of p2.
;;
%macro POLY1305_MUL_REDUCE_VEC 15
%define %%A0      %1  ; [in/out] ZMM register containing 1st 44-bit limb of the 8 blocks
%define %%A1      %2  ; [in/out] ZMM register containing 2nd 44-bit limb of the 8 blocks
%define %%A2      %3  ; [in/out] ZMM register containing 3rd 44-bit limb of the 8 blocks
%define %%R0      %4  ; [in] ZMM register (R0) to include the 1st limb of R
%define %%R1      %5  ; [in] ZMM register (R1) to include the 2nd limb of R
%define %%R2      %6  ; [in] ZMM register (R2) to include the 3rd limb of R
%define %%R1P     %7  ; [in] ZMM register (R1') to include the 2nd limb of R (multiplied by 5)
%define %%R2P     %8  ; [in] ZMM register (R2') to include the 3rd limb of R (multiplied by 5)
%define %%P0_L    %9  ; [clobbered] ZMM register to contain p[0] of the 8 blocks
%define %%P0_H    %10 ; [clobbered] ZMM register to contain p[0] of the 8 blocks
%define %%P1_L    %11 ; [clobbered] ZMM register to contain p[1] of the 8 blocks
%define %%P1_H    %12 ; [clobbered] ZMM register to contain p[1] of the 8 blocks
%define %%P2_L    %13 ; [clobbered] ZMM register to contain p[2] of the 8 blocks
%define %%P2_H    %14 ; [clobbered] ZMM register to contain p[2] of the 8 blocks
%define %%ZTMP1   %15 ; [clobbered] Temporary ZMM register

        ;; Reset accumulator
        vpxorq  %%P0_L, %%P0_L
        vpxorq  %%P0_H, %%P0_H
        vpxorq  %%P1_L, %%P1_L
        vpxorq  %%P1_H, %%P1_H
        vpxorq  %%P2_L, %%P2_L
        vpxorq  %%P2_H, %%P2_H

        ; Reset accumulator and calculate products
        vpmadd52luq %%P0_L, %%A2, %%R1P
        vpmadd52huq %%P0_H, %%A2, %%R1P
        vpmadd52luq %%P1_L, %%A2, %%R2P
        vpmadd52huq %%P1_H, %%A2, %%R2P
        vpmadd52luq %%P2_L, %%A2, %%R0
        vpmadd52huq %%P2_H, %%A2, %%R0

        vpmadd52luq %%P1_L, %%A0, %%R1
        vpmadd52huq %%P1_H, %%A0, %%R1
        vpmadd52luq %%P2_L, %%A0, %%R2
        vpmadd52huq %%P2_H, %%A0, %%R2
        vpmadd52luq %%P0_L, %%A0, %%R0
        vpmadd52huq %%P0_H, %%A0, %%R0

        vpmadd52luq %%P0_L, %%A1, %%R2P
        vpmadd52huq %%P0_H, %%A1, %%R2P
        vpmadd52luq %%P1_L, %%A1, %%R0
        vpmadd52huq %%P1_H, %%A1, %%R0
        vpmadd52luq %%P2_L, %%A1, %%R1
        vpmadd52huq %%P2_H, %%A1, %%R1

        ; Carry propagation (first pass)
        vpsrlq  %%ZTMP1, %%P0_L, 44
        vpandq  %%A0, %%P0_L, [rel mask_44] ; Clear top 20 bits
        vpsllq  %%P0_H, 8
        vpaddq  %%P0_H, %%ZTMP1
        vpaddq  %%P1_L, %%P0_H
        vpandq  %%A1, %%P1_L, [rel mask_44] ; Clear top 20 bits
        vpsrlq  %%ZTMP1, %%P1_L, 44
        vpsllq  %%P1_H, 8
        vpaddq  %%P1_H, %%ZTMP1
        vpaddq  %%P2_L, %%P1_H
        vpandq  %%A2, %%P2_L, [rel mask_42] ; Clear top 22 bits
        vpsrlq  %%ZTMP1, %%P2_L, 42
        vpsllq  %%P2_H, 10
        vpaddq  %%P2_H, %%ZTMP1

        ; Carry propagation (second pass)

        ; Multiply by 5 the highest bits (above 130 bits)
        vpaddq  %%A0, %%P2_H
        vpsllq  %%P2_H, 2
        vpaddq  %%A0, %%P2_H
        vpsrlq  %%ZTMP1, %%A0, 44
        vpandq  %%A0, [rel mask_44]
        vpaddq  %%A1, %%ZTMP1
%endmacro

;; =============================================================================
;; =============================================================================
;; Computes hash for 16 16-byte message blocks,
;; and adds new message blocks to accumulator,
;; interleaving this computation with the loading and splatting
;; of new data.
;;
;; It first multiplies all 16 blocks with powers of R (8 blocks from A0-A2
;; and 8 blocks from B0-B2, multiplied by R0-R2)
;;
;;      a2      a1      a0
;; ×    b2      b1      b0
;; ---------------------------------------
;;     a2×b0   a1×b0   a0×b0
;; +   a1×b1   a0×b1 5×a2×b1
;; +   a0×b2 5×a2×b2 5×a1×b2
;; ---------------------------------------
;;        p2      p1      p0
;;
;; Then, it propagates the carry (higher bits after bit 43)
;; from lower limbs into higher limbs,
;; multiplying by 5 in case of the carry of p2, and adds
;; the results to A0-A2 and B0-B2.
;;
;; =============================================================================
%macro POLY1305_MSG_MUL_REDUCE_VEC16 34
%define %%A0      %1  ; [in/out] ZMM register containing 1st 44-bit limb of blocks 1-8
%define %%A1      %2  ; [in/out] ZMM register containing 2nd 44-bit limb of blocks 1-8
%define %%A2      %3  ; [in/out] ZMM register containing 3rd 44-bit limb of blocks 1-8
%define %%B0      %4  ; [in/out] ZMM register containing 1st 44-bit limb of blocks 9-16
%define %%B1      %5  ; [in/out] ZMM register containing 2nd 44-bit limb of blocks 9-16
%define %%B2      %6  ; [in/out] ZMM register containing 3rd 44-bit limb of blocks 9-16
%define %%R0      %7  ; [in] ZMM register (R0) to include the 1st limb of R
%define %%R1      %8  ; [in] ZMM register (R1) to include the 2nd limb of R
%define %%R2      %9  ; [in] ZMM register (R2) to include the 3rd limb of R
%define %%R1P     %10 ; [in] ZMM register (R1') to include the 2nd limb of R (multiplied by 5)
%define %%R2P     %11 ; [in] ZMM register (R2') to include the 3rd limb of R (multiplied by 5)
%define %%P0_L    %12 ; [clobbered] ZMM register to contain p[0] of the 8 blocks 1-8
%define %%P0_H    %13 ; [clobbered] ZMM register to contain p[0] of the 8 blocks 1-8
%define %%P1_L    %14 ; [clobbered] ZMM register to contain p[1] of the 8 blocks 1-8
%define %%P1_H    %15 ; [clobbered] ZMM register to contain p[1] of the 8 blocks 1-8
%define %%P2_L    %16 ; [clobbered] ZMM register to contain p[2] of the 8 blocks 1-8
%define %%P2_H    %17 ; [clobbered] ZMM register to contain p[2] of the 8 blocks 1-8
%define %%Q0_L    %18 ; [clobbered] ZMM register to contain p[0] of the 8 blocks 9-16
%define %%Q0_H    %19 ; [clobbered] ZMM register to contain p[0] of the 8 blocks 9-16
%define %%Q1_L    %20 ; [clobbered] ZMM register to contain p[1] of the 8 blocks 9-16
%define %%Q1_H    %21 ; [clobbered] ZMM register to contain p[1] of the 8 blocks 9-16
%define %%Q2_L    %22 ; [clobbered] ZMM register to contain p[2] of the 8 blocks 9-16
%define %%Q2_H    %23 ; [clobbered] ZMM register to contain p[2] of the 8 blocks 9-16
%define %%ZTMP1   %24 ; [clobbered] Temporary ZMM register
%define %%ZTMP2   %25 ; [clobbered] Temporary ZMM register
%define %%ZTMP3   %26 ; [clobbered] Temporary ZMM register
%define %%ZTMP4   %27 ; [clobbered] Temporary ZMM register
%define %%ZTMP5   %28 ; [clobbered] Temporary ZMM register
%define %%ZTMP6   %29 ; [clobbered] Temporary ZMM register
%define %%ZTMP7   %30 ; [clobbered] Temporary ZMM register
%define %%ZTMP8   %31 ; [clobbered] Temporary ZMM register
%define %%ZTMP9   %32 ; [clobbered] Temporary ZMM register
%define %%MSG     %33 ; [in/out] Pointer to message
%define %%LEN     %34 ; [in/out] Length left of message

        ;; Reset accumulator
        vpxorq  %%P0_L, %%P0_L
        vpxorq  %%P0_H, %%P0_H
        vpxorq  %%P1_L, %%P1_L
        vpxorq  %%P1_H, %%P1_H
        vpxorq  %%P2_L, %%P2_L
        vpxorq  %%P2_H, %%P2_H
        vpxorq  %%Q0_L, %%Q0_L
        vpxorq  %%Q0_H, %%Q0_H
        vpxorq  %%Q1_L, %%Q1_L
        vpxorq  %%Q1_H, %%Q1_H
        vpxorq  %%Q2_L, %%Q2_L
        vpxorq  %%Q2_H, %%Q2_H

        ;; This code interleaves hash computation with input loading/splatting

                ; Calculate products
                vpmadd52luq %%P0_L, %%A2, %%R1P
                vpmadd52huq %%P0_H, %%A2, %%R1P
        ;; input loading of new blocks
        add     %%MSG, POLY1305_BLOCK_SIZE*16
        sub     %%LEN, POLY1305_BLOCK_SIZE*16

                vpmadd52luq %%Q0_L, %%B2, %%R1P
                vpmadd52huq %%Q0_H, %%B2, %%R1P

                vpmadd52luq %%P1_L, %%A2, %%R2P
                vpmadd52huq %%P1_H, %%A2, %%R2P
        ; Load next block of data (128 bytes)
        vmovdqu64 %%ZTMP5, [%%MSG]
        vmovdqu64 %%ZTMP2, [%%MSG + 64]

                vpmadd52luq %%Q1_L, %%B2, %%R2P
                vpmadd52huq %%Q1_H, %%B2, %%R2P

        ; Interleave new blocks of data
        vpunpckhqdq %%ZTMP3, %%ZTMP5, %%ZTMP2
        vpunpcklqdq %%ZTMP5, %%ZTMP5, %%ZTMP2

                vpmadd52luq %%P0_L, %%A0, %%R0
                vpmadd52huq %%P0_H, %%A0, %%R0
        ; Highest 42-bit limbs of new blocks
        vpsrlq  %%ZTMP6, %%ZTMP3, 24
        vporq   %%ZTMP6, [rel high_bit] ; Add 2^128 to all 8 final qwords of the message

                vpmadd52luq %%Q0_L, %%B0, %%R0
                vpmadd52huq %%Q0_H, %%B0, %%R0

        ; Middle 44-bit limbs of new blocks
        vpsrlq  %%ZTMP2, %%ZTMP5, 44
        vpsllq  %%ZTMP4, %%ZTMP3, 20

                vpmadd52luq %%P2_L, %%A2, %%R0
                vpmadd52huq %%P2_H, %%A2, %%R0
        vpternlogq %%ZTMP2, %%ZTMP4, [rel mask_44], 0xA8 ; (A OR B AND C)

        ; Lowest 44-bit limbs of new blocks
        vpandq  %%ZTMP5, [rel mask_44]

                vpmadd52luq %%Q2_L, %%B2, %%R0
                vpmadd52huq %%Q2_H, %%B2, %%R0

        ; Load next block of data (128 bytes)
        vmovdqu64 %%ZTMP8, [%%MSG + 64*2]
        vmovdqu64 %%ZTMP9, [%%MSG + 64*3]

                vpmadd52luq %%P1_L, %%A0, %%R1
                vpmadd52huq %%P1_H, %%A0, %%R1
        ; Interleave new blocks of data
        vpunpckhqdq %%ZTMP3, %%ZTMP8, %%ZTMP9
        vpunpcklqdq %%ZTMP8, %%ZTMP8, %%ZTMP9

                vpmadd52luq %%Q1_L, %%B0, %%R1
                vpmadd52huq %%Q1_H, %%B0, %%R1

        ; Highest 42-bit limbs of new blocks
        vpsrlq  %%ZTMP7, %%ZTMP3, 24
        vporq   %%ZTMP7, [rel high_bit] ; Add 2^128 to all 8 final qwords of the message

                vpmadd52luq %%P0_L, %%A1, %%R2P
                vpmadd52huq %%P0_H, %%A1, %%R2P

        ; Middle 44-bit limbs of new blocks
        vpsrlq  %%ZTMP9, %%ZTMP8, 44
        vpsllq  %%ZTMP4, %%ZTMP3, 20

                vpmadd52luq %%Q0_L, %%B1, %%R2P
                vpmadd52huq %%Q0_H, %%B1, %%R2P

        vpternlogq %%ZTMP9, %%ZTMP4, [rel mask_44], 0xA8 ; (A OR B AND C)

        ; Lowest 44-bit limbs of new blocks
        vpandq  %%ZTMP8, [rel mask_44]

                vpmadd52luq %%P2_L, %%A0, %%R2
                vpmadd52huq %%P2_H, %%A0, %%R2
        ; Carry propagation (first pass)
        vpsrlq  %%ZTMP1, %%P0_L, 44
        vpsllq  %%P0_H, 8
                vpmadd52luq %%Q2_L, %%B0, %%R2
                vpmadd52huq %%Q2_H, %%B0, %%R2

        vpsrlq  %%ZTMP3, %%Q0_L, 44
        vpsllq  %%Q0_H, 8

                vpmadd52luq %%P1_L, %%A1, %%R0
                vpmadd52huq %%P1_H, %%A1, %%R0
        ; Carry propagation (first pass) - continue
        vpandq  %%A0, %%P0_L, [rel mask_44] ; Clear top 20 bits
        vpaddq  %%P0_H, %%ZTMP1
                vpmadd52luq %%Q1_L, %%B1, %%R0
                vpmadd52huq %%Q1_H, %%B1, %%R0

        vpandq  %%B0, %%Q0_L, [rel mask_44] ; Clear top 20 bits
        vpaddq  %%Q0_H, %%ZTMP3

                vpmadd52luq %%P2_L, %%A1, %%R1
                vpmadd52huq %%P2_H, %%A1, %%R1
        ; Carry propagation (first pass) - continue
        vpaddq  %%P1_L, %%P0_H
        vpsllq  %%P1_H, 8
        vpsrlq  %%ZTMP1, %%P1_L, 44
                vpmadd52luq %%Q2_L, %%B1, %%R1
                vpmadd52huq %%Q2_H, %%B1, %%R1

        vpandq  %%A1, %%P1_L, [rel mask_44] ; Clear top 20 bits
        vpaddq  %%Q1_L, %%Q0_H
        vpsllq  %%Q1_H, 8
        vpsrlq  %%ZTMP3, %%Q1_L, 44
        vpandq  %%B1, %%Q1_L, [rel mask_44] ; Clear top 20 bits

        vpaddq  %%P2_L, %%P1_H          ; P2_L += P1_H + P1_L[63:44]
        vpaddq  %%P2_L, %%ZTMP1
        vpandq  %%A2, %%P2_L, [rel mask_42] ; Clear top 22 bits
        vpaddq  %%A2, %%ZTMP6 ; Add highest bits from new blocks to accumulator
        vpsrlq  %%ZTMP1, %%P2_L, 42
        vpsllq  %%P2_H, 10
        vpaddq  %%P2_H, %%ZTMP1

        vpaddq  %%Q2_L, %%Q1_H          ; Q2_L += P1_H + P1_L[63:44]
        vpaddq  %%Q2_L, %%ZTMP3
        vpandq  %%B2, %%Q2_L, [rel mask_42] ; Clear top 22 bits
        vpaddq  %%B2, %%ZTMP7 ; Add highest bits from new blocks to accumulator
        vpsrlq  %%ZTMP3, %%Q2_L, 42
        vpsllq  %%Q2_H, 10
        vpaddq  %%Q2_H, %%ZTMP3

        ; Carry propagation (second pass)
        ; Multiply by 5 the highest bits (above 130 bits)
        vpaddq  %%A0, %%P2_H
        vpsllq  %%P2_H, 2
        vpaddq  %%A0, %%P2_H
        vpaddq  %%B0, %%Q2_H
        vpsllq  %%Q2_H, 2
        vpaddq  %%B0, %%Q2_H

        vpsrlq  %%ZTMP1, %%A0, 44
        vpandq  %%A0, [rel mask_44]
        vpaddq  %%A0, %%ZTMP5 ; Add low 42-bit bits from new blocks to accumulator
        vpaddq  %%A1, %%ZTMP2 ; Add medium 42-bit bits from new blocks to accumulator
        vpaddq  %%A1, %%ZTMP1
        vpsrlq  %%ZTMP3, %%B0, 44
        vpandq  %%B0, [rel mask_44]
        vpaddq  %%B0, %%ZTMP8 ; Add low 42-bit bits from new blocks to accumulator
        vpaddq  %%B1, %%ZTMP9 ; Add medium 42-bit bits from new blocks to accumulator
        vpaddq  %%B1, %%ZTMP3
%endmacro

;; =============================================================================
;; =============================================================================
;; Computes hash for 16 16-byte message blocks.
;;
;; It first multiplies all 16 blocks with powers of R (8 blocks from A0-A2
;; and 8 blocks from B0-B2, multiplied by R0-R2 and S0-S2)
;;
;;
;;      a2      a1      a0
;; ×    b2      b1      b0
;; ---------------------------------------
;;     a2×b0   a1×b0   a0×b0
;; +   a1×b1   a0×b1 5×a2×b1
;; +   a0×b2 5×a2×b2 5×a1×b2
;; ---------------------------------------
;;        p2      p1      p0
;;
;; Then, it propagates the carry (higher bits after bit 43) from lower limbs into higher limbs,
;; multiplying by 5 in case of the carry of p2.
;;
;; =============================================================================
%macro POLY1305_MUL_REDUCE_VEC16 30
%define %%A0      %1  ; [in/out] ZMM register containing 1st 44-bit limb of the 8 blocks
%define %%A1      %2  ; [in/out] ZMM register containing 2nd 44-bit limb of the 8 blocks
%define %%A2      %3  ; [in/out] ZMM register containing 3rd 44-bit limb of the 8 blocks
%define %%B0      %4  ; [in/out] ZMM register containing 1st 44-bit limb of the 8 blocks
%define %%B1      %5  ; [in/out] ZMM register containing 2nd 44-bit limb of the 8 blocks
%define %%B2      %6  ; [in/out] ZMM register containing 3rd 44-bit limb of the 8 blocks
%define %%R0      %7  ; [in] ZMM register (R0) to include the 1st limb in IDX
%define %%R1      %8  ; [in] ZMM register (R1) to include the 2nd limb in IDX
%define %%R2      %9  ; [in] ZMM register (R2) to include the 3rd limb in IDX
%define %%R1P     %10 ; [in] ZMM register (R1') to include the 2nd limb (multiplied by 5) in IDX
%define %%R2P     %11 ; [in] ZMM register (R2') to include the 3rd limb (multiplied by 5) in IDX
%define %%S0      %12 ; [in] ZMM register (R0) to include the 1st limb in IDX
%define %%S1      %13 ; [in] ZMM register (R1) to include the 2nd limb in IDX
%define %%S2      %14 ; [in] ZMM register (R2) to include the 3rd limb in IDX
%define %%S1P     %15 ; [in] ZMM register (R1') to include the 2nd limb (multiplied by 5) in IDX
%define %%S2P     %16 ; [in] ZMM register (R2') to include the 3rd limb (multiplied by 5) in IDX
%define %%P0_L    %17 ; [clobbered] ZMM register to contain p[0] of the 8 blocks
%define %%P0_H    %18 ; [clobbered] ZMM register to contain p[0] of the 8 blocks
%define %%P1_L    %19 ; [clobbered] ZMM register to contain p[1] of the 8 blocks
%define %%P1_H    %20 ; [clobbered] ZMM register to contain p[1] of the 8 blocks
%define %%P2_L    %21 ; [clobbered] ZMM register to contain p[2] of the 8 blocks
%define %%P2_H    %22 ; [clobbered] ZMM register to contain p[2] of the 8 blocks
%define %%Q0_L    %23 ; [clobbered] ZMM register to contain p[0] of the 8 blocks
%define %%Q0_H    %24 ; [clobbered] ZMM register to contain p[0] of the 8 blocks
%define %%Q1_L    %25 ; [clobbered] ZMM register to contain p[1] of the 8 blocks
%define %%Q1_H    %26 ; [clobbered] ZMM register to contain p[1] of the 8 blocks
%define %%Q2_L    %27 ; [clobbered] ZMM register to contain p[2] of the 8 blocks
%define %%Q2_H    %28 ; [clobbered] ZMM register to contain p[2] of the 8 blocks
%define %%ZTMP1   %29 ; [clobbered] Temporary ZMM register
%define %%ZTMP2   %30 ; [clobbered] Temporary ZMM register

        ;; Reset accumulator
        vpxorq  %%P0_L, %%P0_L
        vpxorq  %%P0_H, %%P0_H
        vpxorq  %%P1_L, %%P1_L
        vpxorq  %%P1_H, %%P1_H
        vpxorq  %%P2_L, %%P2_L
        vpxorq  %%P2_H, %%P2_H
        vpxorq  %%Q0_L, %%Q0_L
        vpxorq  %%Q0_H, %%Q0_H
        vpxorq  %%Q1_L, %%Q1_L
        vpxorq  %%Q1_H, %%Q1_H
        vpxorq  %%Q2_L, %%Q2_L
        vpxorq  %%Q2_H, %%Q2_H

        ;; This code interleaves hash computation with input loading/splatting

        ; Calculate products
        vpmadd52luq %%P0_L, %%A2, %%R1P
        vpmadd52huq %%P0_H, %%A2, %%R1P

        vpmadd52luq %%Q0_L, %%B2, %%S1P
        vpmadd52huq %%Q0_H, %%B2, %%S1P

        vpmadd52luq %%P1_L, %%A2, %%R2P
        vpmadd52huq %%P1_H, %%A2, %%R2P

        vpmadd52luq %%Q1_L, %%B2, %%S2P
        vpmadd52huq %%Q1_H, %%B2, %%S2P

        vpmadd52luq %%P0_L, %%A0, %%R0
        vpmadd52huq %%P0_H, %%A0, %%R0

        vpmadd52luq %%Q0_L, %%B0, %%S0
        vpmadd52huq %%Q0_H, %%B0, %%S0

        vpmadd52luq %%P2_L, %%A2, %%R0
        vpmadd52huq %%P2_H, %%A2, %%R0
        vpmadd52luq %%Q2_L, %%B2, %%S0
        vpmadd52huq %%Q2_H, %%B2, %%S0

        vpmadd52luq %%P1_L, %%A0, %%R1
        vpmadd52huq %%P1_H, %%A0, %%R1
        vpmadd52luq %%Q1_L, %%B0, %%S1
        vpmadd52huq %%Q1_H, %%B0, %%S1

        vpmadd52luq %%P0_L, %%A1, %%R2P
        vpmadd52huq %%P0_H, %%A1, %%R2P

        vpmadd52luq %%Q0_L, %%B1, %%S2P
        vpmadd52huq %%Q0_H, %%B1, %%S2P

        vpmadd52luq %%P2_L, %%A0, %%R2
        vpmadd52huq %%P2_H, %%A0, %%R2

        vpmadd52luq %%Q2_L, %%B0, %%S2
        vpmadd52huq %%Q2_H, %%B0, %%S2

        ; Carry propagation (first pass)
        vpsrlq  %%ZTMP1, %%P0_L, 44
        vpsllq  %%P0_H, 8
        vpsrlq  %%ZTMP2, %%Q0_L, 44
        vpsllq  %%Q0_H, 8

        vpmadd52luq %%P1_L, %%A1, %%R0
        vpmadd52huq %%P1_H, %%A1, %%R0
        vpmadd52luq %%Q1_L, %%B1, %%S0
        vpmadd52huq %%Q1_H, %%B1, %%S0

        ; Carry propagation (first pass) - continue
        vpandq  %%A0, %%P0_L, [rel mask_44] ; Clear top 20 bits
        vpaddq  %%P0_H, %%ZTMP1
        vpandq  %%B0, %%Q0_L, [rel mask_44] ; Clear top 20 bits
        vpaddq  %%Q0_H, %%ZTMP2

        vpmadd52luq %%P2_L, %%A1, %%R1
        vpmadd52huq %%P2_H, %%A1, %%R1
        vpmadd52luq %%Q2_L, %%B1, %%S1
        vpmadd52huq %%Q2_H, %%B1, %%S1

        ; Carry propagation (first pass) - continue
        vpaddq  %%P1_L, %%P0_H
        vpsllq  %%P1_H, 8
        vpsrlq  %%ZTMP1, %%P1_L, 44
        vpandq  %%A1, %%P1_L, [rel mask_44] ; Clear top 20 bits
        vpaddq  %%Q1_L, %%Q0_H
        vpsllq  %%Q1_H, 8
        vpsrlq  %%ZTMP2, %%Q1_L, 44
        vpandq  %%B1, %%Q1_L, [rel mask_44] ; Clear top 20 bits

        vpaddq  %%P2_L, %%P1_H          ; P2_L += P1_H + P1_L[63:44]
        vpaddq  %%P2_L, %%ZTMP1
        vpandq  %%A2, %%P2_L, [rel mask_42] ; Clear top 22 bits
        vpsrlq  %%ZTMP1, %%P2_L, 42
        vpsllq  %%P2_H, 10
        vpaddq  %%P2_H, %%ZTMP1

        vpaddq  %%Q2_L, %%Q1_H          ; Q2_L += P1_H + P1_L[63:44]
        vpaddq  %%Q2_L, %%ZTMP2
        vpandq  %%B2, %%Q2_L, [rel mask_42] ; Clear top 22 bits
        vpsrlq  %%ZTMP2, %%Q2_L, 42
        vpsllq  %%Q2_H, 10
        vpaddq  %%Q2_H, %%ZTMP2

        ; Carry propagation (second pass)
        ; Multiply by 5 the highest bits (above 130 bits)
        vpaddq  %%A0, %%P2_H
        vpsllq  %%P2_H, 2
        vpaddq  %%A0, %%P2_H
        vpaddq  %%B0, %%Q2_H
        vpsllq  %%Q2_H, 2
        vpaddq  %%B0, %%Q2_H

        vpsrlq  %%ZTMP1, %%A0, 44
        vpandq  %%A0, [rel mask_44]
        vpaddq  %%A1, %%ZTMP1
        vpsrlq  %%ZTMP2, %%B0, 44
        vpandq  %%B0, [rel mask_44]
        vpaddq  %%B1, %%ZTMP2
%endmacro

;; =============================================================================
;; =============================================================================
;; Shuffle data blocks, so they match the right power of R.
;; Powers of R are in this order: R^8 R^4 R^7 R^3 R^6 R^2 R^5 R
;; Data blocks are coming in this order: A0 A4 A1 A5 A2 A6 A3 A7
;; Generally the computation is: A0*R^8 + A1*R^7 + A2*R^6 + A3*R^5 +
;;                               A4*R^4 + A5*R^3 + A6*R^2 + A7*R
;; When there are less data blocks, less powers of R are used, so data needs to
;; be shuffled. Example: if 4 blocks are left, only A0-A3 are available and only
;; R-R^4 are used (A0*R^4 + A1*R^3 + A2*R^2 + A3*R), so A0-A3 need to be shifted
;; =============================================================================
%macro SHUFFLE_DATA_BLOCKS 5
%define %%A_L           %1 ; [in/out] 0-43 bits of input data
%define %%A_M           %2 ; [in/out] 44-87 bits of input data
%define %%A_H           %3 ; [in/out] 88-129 bits of input data
%define %%TMP           %4 ; [clobbered] Temporary GP register
%define %%N_BLOCKS      %5 ; [in] Number of remaining input blocks

%if %%N_BLOCKS == 1
%define %%SHUF_MASK 0x39
%define %%KMASK 0xffff
%elif %%N_BLOCKS == 2
%define %%SHUF_MASK 0x4E
%define %%KMASK 0xffff
%elif %%N_BLOCKS == 3
%define %%SHUF_MASK 0x93
%define %%KMASK 0xffff
%elif %%N_BLOCKS == 4
%define %%KMASK 0xffff
%elif %%N_BLOCKS == 5
%define %%SHUF_MASK 0x39
%define %%KMASK 0xfff0
%elif %%N_BLOCKS == 6
%define %%SHUF_MASK 0x4E
%define %%KMASK 0xff00
%elif %%N_BLOCKS == 7
%define %%SHUF_MASK 0x93
%define %%KMASK 0xf000
%endif

        mov     %%TMP, %%KMASK
        kmovq   k1, %%TMP
        vpshufd %%A_L{k1}, %%A_L, 0x4E
        vpshufd %%A_M{k1}, %%A_M, 0x4E
        vpshufd %%A_H{k1}, %%A_H, 0x4E
%if %%N_BLOCKS != 4
        vshufi64x2 %%A_L, %%A_L, %%SHUF_MASK
        vshufi64x2 %%A_M, %%A_M, %%SHUF_MASK
        vshufi64x2 %%A_H, %%A_H, %%SHUF_MASK
%endif
%endmacro

;; =============================================================================
;; =============================================================================
;; Computes hash for message length being multiple of block size
;; =============================================================================
%macro POLY1305_BLOCKS 14
%define %%MSG     %1    ; [in/out] GPR pointer to input message (updated)
%define %%LEN     %2    ; [in/out] GPR in: length in bytes / out: length mod 16
%define %%A0      %3    ; [in/out] accumulator bits 63..0
%define %%A1      %4    ; [in/out] accumulator bits 127..64
%define %%A2      %5    ; [in/out] accumulator bits 195..128
%define %%R0      %6    ; [in] R constant bits 63..0
%define %%R1      %7    ; [in] R constant bits 127..64
%define %%T0      %8    ; [clobbered] GPR register
%define %%T1      %9    ; [clobbered] GPR register
%define %%T2      %10   ; [clobbered] GPR register
%define %%T3      %11   ; [clobbered] GPR register
%define %%GP_RAX  %12   ; [clobbered] RAX register
%define %%GP_RDX  %13   ; [clobbered] RDX register
%define %%PAD_16  %14   ; [in] text "pad_to_16" or "no_padding"

        ; Minimum of 256 bytes to run vectorized code
        cmp     %%LEN, POLY1305_BLOCK_SIZE*16
        jb      %%_final_loop

        ; Spread accumulator into 44-bit limbs in quadwords
        mov     %%T0, %%A0
        and     %%T0, [rel mask_44] ;; First limb (A[43:0])
        vmovq   xmm5, %%T0

        mov     %%T0, %%A1
        shrd    %%A0, %%T0, 44
        and     %%A0, [rel mask_44] ;; Second limb (A[77:52])
        vmovq   xmm6, %%A0

        shrd    %%A1, %%A2, 24
        and     %%A1, [rel mask_42] ;; Third limb (A[129:88])
        vmovq   xmm7, %%A1

        ; Load first block of data (128 bytes)
        vmovdqu64 zmm0, [%%MSG]
        vmovdqu64 zmm1, [%%MSG + 64]

        ; Interleave the data to form 44-bit limbs
        ;
        ; zmm13 to have bits 0-43 of all 8 blocks in 8 qwords
        ; zmm14 to have bits 87-44 of all 8 blocks in 8 qwords
        ; zmm15 to have bits 127-88 of all 8 blocks in 8 qwords
        vpunpckhqdq zmm15, zmm0, zmm1
        vpunpcklqdq zmm13, zmm0, zmm1

        vpsrlq  zmm14, zmm13, 44
        vpsllq  zmm18, zmm15, 20
        vpternlogq zmm14, zmm18, [rel mask_44], 0xA8  ; (A OR B AND C)

        vpandq  zmm13, [rel mask_44]
        vpsrlq  zmm15, 24

        ; Add 2^128 to all 8 final qwords of the message
        vporq   zmm15, [rel high_bit]

        vpaddq  zmm13, zmm5
        vpaddq  zmm14, zmm6
        vpaddq  zmm15, zmm7

        ; Load next blocks of data (128 bytes)
        vmovdqu64 zmm0, [%%MSG + 64*2]
        vmovdqu64 zmm1, [%%MSG + 64*3]

        ; Interleave the data to form 44-bit limbs
        ;
        ; zmm13 to have bits 0-43 of all 8 blocks in 8 qwords
        ; zmm14 to have bits 87-44 of all 8 blocks in 8 qwords
        ; zmm15 to have bits 127-88 of all 8 blocks in 8 qwords
        vpunpckhqdq zmm18, zmm0, zmm1
        vpunpcklqdq zmm16, zmm0, zmm1

        vpsrlq  zmm17, zmm16, 44
        vpsllq  zmm19, zmm18, 20
        vpternlogq zmm17, zmm19, [rel mask_44], 0xA8  ; (A OR B AND C)

        vpandq  zmm16, [rel mask_44]
        vpsrlq  zmm18, 24

        ; Add 2^128 to all 8 final qwords of the message
        vporq   zmm18, [rel high_bit]

        ; Use memory in stack to save powers of R, before loading them into ZMM registers
        ; The first 16*8 bytes will contain the 16 bytes of the 8 powers of R
        ; The last 64 bytes will contain the last 2 bits of powers of R, spread in 8 qwords,
        ; to be OR'd with the highest qwords (in zmm26)
        vmovq   xmm3, %%R0
        vpinsrq xmm3, %%R1, 1
        vinserti32x4 zmm1, xmm3, 3

        vpxorq  zmm0, zmm0
        vpxorq  zmm2, zmm2

        ; Calculate R^2
        mov     %%T0, %%R1
        shr     %%T0, 2
        add     %%T0, %%R1      ;; T0 = R1 + (R1 >> 2)

        mov     %%A0, %%R0
        mov     %%A1, %%R1

        POLY1305_MUL_REDUCE %%A0, %%A1, %%A2, %%R0, %%R1, %%T0, %%T1, %%T2, %%T3, %%GP_RAX, %%GP_RDX, no_A2

        vmovq   xmm3, %%A0
        vpinsrq xmm3, %%A1, 1
        vinserti32x4 zmm1, xmm3, 2

        vmovq   xmm4, %%A2
        vinserti32x4 zmm2, xmm4, 2

        ; Calculate R^3
        POLY1305_MUL_REDUCE %%A0, %%A1, %%A2, %%R0, %%R1, %%T0, %%T1, %%T2, %%T3, %%GP_RAX, %%GP_RDX

        vmovq   xmm3, %%A0
        vpinsrq xmm3, %%A1, 1
        vinserti32x4 zmm1, xmm3, 1

        vmovq   xmm4, %%A2
        vinserti32x4 zmm2, xmm4, 1

        ; Calculate R^4
        POLY1305_MUL_REDUCE %%A0, %%A1, %%A2, %%R0, %%R1, %%T0, %%T1, %%T2, %%T3, %%GP_RAX, %%GP_RDX

        vmovq   xmm3, %%A0
        vpinsrq xmm3, %%A1, 1
        vinserti32x4 zmm1, xmm3, 0

        vmovq   xmm4, %%A2
        vinserti32x4 zmm2, xmm4, 0

        ; Move 2 MSbits to top 24 bits, to be OR'ed later
        vpsllq  zmm2, 40

        vpunpckhqdq zmm21, zmm1, zmm0
        vpunpcklqdq zmm19, zmm1, zmm0

        vpsrlq  zmm20, zmm19, 44
        vpsllq  zmm4, zmm21, 20
        vpternlogq zmm20, zmm4, [rel mask_44], 0xA8  ; (A OR B AND C)

        vpandq  zmm19, [rel mask_44]
        vpsrlq  zmm21, 24

        ; zmm2 contains the 2 highest bits of the powers of R
        vporq   zmm21, zmm2

        ; Broadcast 44-bit limbs of R^4
        mov     %%T0, %%A0
        and     %%T0, [rel mask_44] ;; First limb (R^4[43:0])
        vpbroadcastq zmm22, %%T0

        mov     %%T0, %%A1
        shrd    %%A0, %%T0, 44
        and     %%A0, [rel mask_44] ;; Second limb (R^4[87:44])
        vpbroadcastq zmm23, %%A0

        shrd    %%A1, %%A2, 24
        and     %%A1, [rel mask_42] ;; Third limb (R^4[129:88])
        vpbroadcastq zmm24, %%A1

        ; Generate 4*5*R^4
        vpsllq  zmm25, zmm23, 2
        vpsllq  zmm26, zmm24, 2

        ; 5*R^4
        vpaddq  zmm25, zmm23
        vpaddq  zmm26, zmm24

        ; 4*5*R^4
        vpsllq  zmm25, 2
        vpsllq  zmm26, 2

        vpslldq zmm29, zmm19, 8
        vpslldq zmm30, zmm20, 8
        vpslldq zmm31, zmm21, 8

        ; Calculate R^8-R^5
        POLY1305_MUL_REDUCE_VEC zmm19, zmm20, zmm21, \
                                zmm22, zmm23, zmm24, \
                                zmm25, zmm26, \
                                zmm5, zmm6, zmm7, zmm8, zmm9, zmm10, \
                                zmm11

        ; Interleave powers of R: R^8 R^4 R^7 R^3 R^6 R^2 R^5 R
        vporq   zmm19, zmm29
        vporq   zmm20, zmm30
        vporq   zmm21, zmm31

        ; Broadcast R^8
        vpbroadcastq zmm22, xmm19
        vpbroadcastq zmm23, xmm20
        vpbroadcastq zmm24, xmm21

        ; Generate 4*5*R^8
        vpsllq  zmm25, zmm23, 2
        vpsllq  zmm26, zmm24, 2

        ; 5*R^8
        vpaddq  zmm25, zmm23
        vpaddq  zmm26, zmm24

        ; 4*5*R^8
        vpsllq  zmm25, 2
        vpsllq  zmm26, 2

        cmp     %%LEN, POLY1305_BLOCK_SIZE*32
        jb      %%_len_256_511

        ; Store R^8-R for later use
        vmovdqa64 [rsp + _r_save], zmm19
        vmovdqa64 [rsp + _r_save + 64], zmm20
        vmovdqa64 [rsp + _r_save + 64*2], zmm21

        ; Calculate R^16-R^9
        POLY1305_MUL_REDUCE_VEC zmm19, zmm20, zmm21, \
                                zmm22, zmm23, zmm24, \
                                zmm25, zmm26, \
                                zmm5, zmm6, zmm7, zmm8, zmm9, zmm10, \
                                zmm11

        ; Store R^16-R^9 for later use
        vmovdqa64 [rsp + _r_save + 64*3], zmm19
        vmovdqa64 [rsp + _r_save + 64*4], zmm20
        vmovdqa64 [rsp + _r_save + 64*5], zmm21

        ; Broadcast R^16
        vpbroadcastq zmm22, xmm19
        vpbroadcastq zmm23, xmm20
        vpbroadcastq zmm24, xmm21

        ; Generate 4*5*R^16
        vpsllq  zmm25, zmm23, 2
        vpsllq  zmm26, zmm24, 2

        ; 5*R^16
        vpaddq  zmm25, zmm23
        vpaddq  zmm26, zmm24

        ; 4*5*R^16
        vpsllq  zmm25, 2
        vpsllq  zmm26, 2

        mov     %%T0, %%LEN
        and     %%T0, 0xffffffffffffff00 ; multiple of 256 bytes

%%_poly1305_blocks_loop:
        endbranch64
        cmp     %%T0, POLY1305_BLOCK_SIZE*16
        jbe     %%_poly1305_blocks_loop_end

        ; zmm13-zmm18 contain the 16 blocks of message plus the previous accumulator
        ; zmm22-24 contain the 5x44-bit limbs of the powers of R
        ; zmm25-26 contain the 5x44-bit limbs of the powers of R' (5*4*R)
        POLY1305_MSG_MUL_REDUCE_VEC16 zmm13, zmm14, zmm15, zmm16, zmm17, zmm18, \
                                      zmm22, zmm23, zmm24, zmm25, zmm26, \
                                      zmm5, zmm6, zmm7, zmm8, zmm9, zmm10, \
                                      zmm19, zmm20, zmm21, zmm27, zmm28, zmm29, \
                                      zmm30, zmm31, zmm11, zmm0, zmm1, \
                                      zmm2, zmm3, zmm4, zmm12, %%MSG, %%T0

        jmp     %%_poly1305_blocks_loop

%%_poly1305_blocks_loop_end:

        ;; Need to multiply by r^16, r^15, r^14... r

        ; First multiply by r^16-r^9

        ; Read R^16-R^9
        vmovdqa64 zmm19, [rsp + _r_save + 64*3]
        vmovdqa64 zmm20, [rsp + _r_save + 64*4]
        vmovdqa64 zmm21, [rsp + _r_save + 64*5]
        ; Read R^8-R
        vmovdqa64 zmm22, [rsp + _r_save]
        vmovdqa64 zmm23, [rsp + _r_save + 64]
        vmovdqa64 zmm24, [rsp + _r_save + 64*2]

        ; zmm27 to have bits 87-44 of all 9-16th powers of R' in 8 qwords
        ; zmm28 to have bits 129-88 of all 9-16th powers of R' in 8 qwords
        vpsllq  zmm0, zmm20, 2
        vpaddq  zmm27, zmm20, zmm0 ; R1' (R1*5)
        vpsllq  zmm1, zmm21, 2
        vpaddq  zmm28, zmm21, zmm1 ; R2' (R2*5)

        ; 4*5*R
        vpsllq  zmm27, 2
        vpsllq  zmm28, 2

        ; Then multiply by r^8-r

        ; zmm25 to have bits 87-44 of all 1-8th powers of R' in 8 qwords
        ; zmm26 to have bits 129-88 of all 1-8th powers of R' in 8 qwords
        vpsllq  zmm2, zmm23, 2
        vpaddq  zmm25, zmm23, zmm2 ; R1' (R1*5)
        vpsllq  zmm3, zmm24, 2
        vpaddq  zmm26, zmm24, zmm3 ; R2' (R2*5)

        ; 4*5*R
        vpsllq  zmm25, 2
        vpsllq  zmm26, 2

        POLY1305_MUL_REDUCE_VEC16 zmm13, zmm14, zmm15, zmm16, zmm17, zmm18, \
                                  zmm19, zmm20, zmm21, zmm27, zmm28, \
                                  zmm22, zmm23, zmm24, zmm25, zmm26, \
                                  zmm0, zmm1, zmm2, zmm3, zmm4, zmm5, zmm6, \
                                  zmm7, zmm8, zmm9, zmm10, zmm11, zmm12, zmm29

        ;; Add all blocks (horizontally)
        vpaddq  zmm13, zmm16
        vpaddq  zmm14, zmm17
        vpaddq  zmm15, zmm18

        vextracti64x4   ymm0, zmm13, 1
        vextracti64x4   ymm1, zmm14, 1
        vextracti64x4   ymm2, zmm15, 1

        vpaddq  ymm13, ymm0
        vpaddq  ymm14, ymm1
        vpaddq  ymm15, ymm2

        vextracti32x4   xmm10, ymm13, 1
        vextracti32x4   xmm11, ymm14, 1
        vextracti32x4   xmm12, ymm15, 1

        vpaddq  xmm13, xmm10
        vpaddq  xmm14, xmm11
        vpaddq  xmm15, xmm12

        vpsrldq xmm10, xmm13, 8
        vpsrldq xmm11, xmm14, 8
        vpsrldq xmm12, xmm15, 8

        ; Finish folding and clear second qword
        mov     %%T0, 0xfd
        kmovq   k1, %%T0
        vpaddq  xmm13{k1}{z}, xmm10
        vpaddq  xmm14{k1}{z}, xmm11
        vpaddq  xmm15{k1}{z}, xmm12

        add     %%MSG, POLY1305_BLOCK_SIZE*16

        and     %%LEN, (POLY1305_BLOCK_SIZE*16 - 1) ; Get remaining lengths (LEN < 256 bytes)

%%_less_than_256:
        endbranch64

        cmp     %%LEN, POLY1305_BLOCK_SIZE*8
        jb      %%_less_than_128

        ; Read next 128 bytes
        ; Load first block of data (128 bytes)
        vmovdqu64 zmm0, [%%MSG]
        vmovdqu64 zmm1, [%%MSG + 64]

        ; Interleave the data to form 44-bit limbs
        ;
        ; zmm13 to have bits 0-43 of all 8 blocks in 8 qwords
        ; zmm14 to have bits 87-44 of all 8 blocks in 8 qwords
        ; zmm15 to have bits 127-88 of all 8 blocks in 8 qwords
        vpunpckhqdq zmm5, zmm0, zmm1
        vpunpcklqdq zmm3, zmm0, zmm1

        vpsrlq  zmm4, zmm3, 44
        vpsllq  zmm8, zmm5, 20
        vpternlogq zmm4, zmm8, [rel mask_44], 0xA8  ; (A OR B AND C)

        vpandq  zmm3, [rel mask_44]
        vpsrlq  zmm5, 24

        ; Add 2^128 to all 8 final qwords of the message
        vporq   zmm5, [rel high_bit]

        vpaddq  zmm13, zmm3
        vpaddq  zmm14, zmm4
        vpaddq  zmm15, zmm5

        add     %%MSG, POLY1305_BLOCK_SIZE*8
        sub     %%LEN, POLY1305_BLOCK_SIZE*8

        POLY1305_MUL_REDUCE_VEC zmm13, zmm14, zmm15, \
                                zmm22, zmm23, zmm24, \
                                zmm25, zmm26, \
                                zmm5, zmm6, zmm7, zmm8, zmm9, zmm10, \
                                zmm11

        ;; Add all blocks (horizontally)
        vextracti64x4   ymm0, zmm13, 1
        vextracti64x4   ymm1, zmm14, 1
        vextracti64x4   ymm2, zmm15, 1

        vpaddq  ymm13, ymm0
        vpaddq  ymm14, ymm1
        vpaddq  ymm15, ymm2

        vextracti32x4   xmm10, ymm13, 1
        vextracti32x4   xmm11, ymm14, 1
        vextracti32x4   xmm12, ymm15, 1

        vpaddq  xmm13, xmm10
        vpaddq  xmm14, xmm11
        vpaddq  xmm15, xmm12

        vpsrldq xmm10, xmm13, 8
        vpsrldq xmm11, xmm14, 8
        vpsrldq xmm12, xmm15, 8

        ; Finish folding and clear second qword
        mov     %%T0, 0xfd
        kmovq   k1, %%T0
        vpaddq  xmm13{k1}{z}, xmm10
        vpaddq  xmm14{k1}{z}, xmm11
        vpaddq  xmm15{k1}{z}, xmm12

%%_less_than_128:
        cmp     %%LEN, 32 ; If remaining bytes is <= 32, perform last blocks in scalar
        jbe     %%_simd_to_gp

        mov     %%T0, %%LEN
        and     %%T0, 0x3f
        lea     %%T1, [rel byte64_len_to_mask_table]
        mov     %%T1, [%%T1 + 8*%%T0]

        ; Load default byte masks
        mov     %%T2, 0xffffffffffffffff
        xor     %%T3, %%T3

        cmp     %%LEN, 64
        cmovb   %%T2, %%T1 ; Load mask for first 64 bytes
        cmovg   %%T3, %%T1 ; Load mask for second 64 bytes

        kmovq   k1, %%T2
        kmovq   k2, %%T3
        vmovdqu8 zmm0{k1}{z}, [%%MSG]
        vmovdqu8 zmm1{k2}{z}, [%%MSG + 64]

        ; Pad last block message, if partial
%ifnidn %%PAD_16,pad_to_16
        kshiftlq k3, k1, 1
        kandnq  k3, k1, k3
        kshiftlq k4, k2, 1
        kandnq  k4, k2, k4
        vmovdqu8 zmm30{k3}{z}, [rel pad64_bit]
        vmovdqu8 zmm31{k4}{z}, [rel pad64_bit]
        vporq   zmm0, zmm30
        vporq   zmm1, zmm31
%endif
        mov     %%T0, %%LEN
        and     %%T0, 0x70 ; Multiple of 16 bytes
        ; Load last block of data (up to 112 bytes)
        shr     %%T0, 3 ; Get number of full qwords

        ; Interleave the data to form 44-bit limbs
        ;
        ; zmm13 to have bits 0-43 of all 8 blocks in 8 qwords
        ; zmm14 to have bits 87-44 of all 8 blocks in 8 qwords
        ; zmm15 to have bits 127-88 of all 8 blocks in 8 qwords
        vpunpckhqdq zmm4, zmm0, zmm1
        vpunpcklqdq zmm2, zmm0, zmm1

        vpsrlq  zmm3, zmm2, 44
        vpsllq  zmm28, zmm4, 20
        vpternlogq zmm3, zmm28, [rel mask_44], 0xA8  ; (A OR B AND C)

        vpandq  zmm2, [rel mask_44]
        vpsrlq  zmm4, 24

        lea     %%T1, [rel qword_high_bit_mask]
%ifidn %%PAD_16,pad_to_16
        mov     %%T3, %%LEN
        mov     %%T2, %%T0
        add     %%T2, 2
        and     %%T3, 0xf
        cmovnz  %%T0, %%T2
%endif
        kmovb   k1, [%%T1 + %%T0]
        ; Add 2^128 to final qwords of the message (all full blocks and partial block,
        ; if "pad_to_16" is selected)
        vporq   zmm4{k1}, [rel high_bit]

        vpaddq  zmm13, zmm2
        vpaddq  zmm14, zmm3
        vpaddq  zmm15, zmm4

        mov     %%T0, %%LEN
        add     %%T0, 15
        shr     %%T0, 4         ; Get number of 16-byte blocks (including partial blocks)
        xor     %%LEN, %%LEN    ; All length will be consumed

        ; No need to shuffle data blocks (data is in the right order)
        cmp     %%T0, 8
        je      %%_end_shuffle

        cmp     %%T0, 4
        je      %%_shuffle_blocks_4
        jb      %%_shuffle_blocks_3

        ; Number of 16-byte blocks > 4
        cmp     %%T0, 6
        je      %%_shuffle_blocks_6
        ja      %%_shuffle_blocks_7
        jmp     %%_shuffle_blocks_5

%assign i 3
%rep 5
APPEND(%%_shuffle_blocks_, i):
        SHUFFLE_DATA_BLOCKS zmm13, zmm14, zmm15, %%T1, i
        jmp     %%_end_shuffle
%assign i (i + 1)
%endrep

%%_end_shuffle:
        endbranch64

        ; zmm13-zmm15 contain the 8 blocks of message plus the previous accumulator
        ; zmm22-24 contain the 3x44-bit limbs of the powers of R
        ; zmm25-26 contain the 3x44-bit limbs of the powers of R' (5*4*R)
        POLY1305_MUL_REDUCE_VEC zmm13, zmm14, zmm15, \
                                zmm22, zmm23, zmm24, \
                                zmm25, zmm26, \
                                zmm5, zmm6, zmm7, zmm8, zmm9, zmm10, \
                                zmm11

        ;; Add all blocks (horizontally)
        vextracti64x4   ymm0, zmm13, 1
        vextracti64x4   ymm1, zmm14, 1
        vextracti64x4   ymm2, zmm15, 1

        vpaddq  ymm13, ymm0
        vpaddq  ymm14, ymm1
        vpaddq  ymm15, ymm2

        vextracti32x4   xmm10, ymm13, 1
        vextracti32x4   xmm11, ymm14, 1
        vextracti32x4   xmm12, ymm15, 1

        vpaddq  xmm13, xmm10
        vpaddq  xmm14, xmm11
        vpaddq  xmm15, xmm12

        vpsrldq xmm10, xmm13, 8
        vpsrldq xmm11, xmm14, 8
        vpsrldq xmm12, xmm15, 8

        vpaddq  xmm13, xmm10
        vpaddq  xmm14, xmm11
        vpaddq  xmm15, xmm12

%%_simd_to_gp:
        ; Carry propagation
        vpsrlq  xmm0, xmm13, 44
        vpandq  xmm13, [rel mask_44] ; Clear top 20 bits
        vpaddq  xmm14, xmm0
        vpsrlq  xmm0, xmm14, 44
        vpandq  xmm14, [rel mask_44] ; Clear top 20 bits
        vpaddq  xmm15, xmm0
        vpsrlq  xmm0, xmm15, 42
        vpandq  xmm15, [rel mask_42] ; Clear top 22 bits
        vpsllq  xmm1, xmm0, 2
        vpaddq  xmm0, xmm1
        vpaddq  xmm13, xmm0

        ; Put together A
        vmovq   %%A0, xmm13

        vmovq   %%T0, xmm14
        mov     %%T1, %%T0
        shl     %%T1, 44
        or      %%A0, %%T1

        shr     %%T0, 20
        vmovq   %%A2, xmm15
        mov     %%A1, %%A2
        shl     %%A1, 24
        or      %%A1, %%T0
        shr     %%A2, 40

        ; Clear powers of R
%ifdef SAFE_DATA
        vpxorq  zmm0, zmm0
        vmovdqa64 [rsp + _r_save], zmm0
        vmovdqa64 [rsp + _r_save + 64], zmm0
        vmovdqa64 [rsp + _r_save + 64*2], zmm0
        vmovdqa64 [rsp + _r_save + 64*3], zmm0
        vmovdqa64 [rsp + _r_save + 64*4], zmm0
        vmovdqa64 [rsp + _r_save + 64*5], zmm0
%endif

        vzeroupper

%%_final_loop:
        cmp     %%LEN, POLY1305_BLOCK_SIZE
        jb      %%_poly1305_blocks_partial

        ;; A += MSG[i]
        add     %%A0, [%%MSG + 0]
        adc     %%A1, [%%MSG + 8]
        adc     %%A2, 1                 ;; no padding bit

        mov     %%T0, %%R1
        shr     %%T0, 2
        add     %%T0, %%R1      ;; T0 = R1 + (R1 >> 2)

        POLY1305_MUL_REDUCE %%A0, %%A1, %%A2, %%R0, %%R1, \
                            %%T0, %%T1, %%T2, %%T3, %%GP_RAX, %%GP_RDX

        add     %%MSG, POLY1305_BLOCK_SIZE
        sub     %%LEN, POLY1305_BLOCK_SIZE

        jmp     %%_final_loop

%%_len_256_511:

        ; zmm13-zmm15 contain the 8 blocks of message plus the previous accumulator
        ; zmm22-24 contain the 3x44-bit limbs of the powers of R
        ; zmm25-26 contain the 3x44-bit limbs of the powers of R' (5*4*R)
        POLY1305_MUL_REDUCE_VEC zmm13, zmm14, zmm15, \
                                zmm22, zmm23, zmm24, \
                                zmm25, zmm26, \
                                zmm5, zmm6, zmm7, zmm8, zmm9, zmm10, \
                                zmm11

        ; Then multiply by r^8-r

        ; zmm19-zmm21 contains R^8-R, need to move it to zmm22-24,
        ; as it might be used in other part of the code
        vmovdqa64 zmm22, zmm19
        vmovdqa64 zmm23, zmm20
        vmovdqa64 zmm24, zmm21

        ; zmm25 to have bits 87-44 of all 8 powers of R' in 8 qwords
        ; zmm26 to have bits 129-88 of all 8 powers of R' in 8 qwords
        vpsllq  zmm0, zmm23, 2
        vpaddq  zmm25, zmm23, zmm0 ; R1' (R1*5)
        vpsllq  zmm1, zmm24, 2
        vpaddq  zmm26, zmm24, zmm1 ; R2' (R2*5)

        ; 4*5*R^8
        vpsllq  zmm25, 2
        vpsllq  zmm26, 2

        vpaddq  zmm13, zmm16
        vpaddq  zmm14, zmm17
        vpaddq  zmm15, zmm18

        ; zmm13-zmm15 contain the 8 blocks of message plus the previous accumulator
        ; zmm22-24 contain the 3x44-bit limbs of the powers of R
        ; zmm25-26 contain the 3x44-bit limbs of the powers of R' (5*4*R)
        POLY1305_MUL_REDUCE_VEC zmm13, zmm14, zmm15, \
                                zmm22, zmm23, zmm24, \
                                zmm25, zmm26, \
                                zmm5, zmm6, zmm7, zmm8, zmm9, zmm10, \
                                zmm11

        ;; Add all blocks (horizontally)
        vextracti64x4   ymm0, zmm13, 1
        vextracti64x4   ymm1, zmm14, 1
        vextracti64x4   ymm2, zmm15, 1

        vpaddq  ymm13, ymm0
        vpaddq  ymm14, ymm1
        vpaddq  ymm15, ymm2

        vextracti32x4   xmm10, ymm13, 1
        vextracti32x4   xmm11, ymm14, 1
        vextracti32x4   xmm12, ymm15, 1

        vpaddq  xmm13, xmm10
        vpaddq  xmm14, xmm11
        vpaddq  xmm15, xmm12

        vpsrldq xmm10, xmm13, 8
        vpsrldq xmm11, xmm14, 8
        vpsrldq xmm12, xmm15, 8

        ; Finish folding and clear second qword
        mov     %%T0, 0xfd
        kmovq   k1, %%T0
        vpaddq  xmm13{k1}{z}, xmm10
        vpaddq  xmm14{k1}{z}, xmm11
        vpaddq  xmm15{k1}{z}, xmm12

        add     %%MSG, POLY1305_BLOCK_SIZE*16
        sub     %%LEN, POLY1305_BLOCK_SIZE*16

        jmp     %%_less_than_256
%%_poly1305_blocks_partial:

        or      %%LEN, %%LEN
        jz      %%_poly1305_blocks_exit

        lea     %%T1, [rel byte_len_to_mask_table]
        kmovw   k1, [%%T1 + %%LEN*2]
        vmovdqu8 xmm0{k1}{z}, [%%MSG]

%ifnidn %%PAD_16,pad_to_16
        ;; pad the message
        lea     %%T2, [rel pad16_bit]
        shl     %%LEN, 4
        vporq   xmm0, [%%T2 + %%LEN]
%endif
        vmovq   %%T0, xmm0
        vpextrq %%T1, xmm0, 1
        ;; A += MSG[i]
        add     %%A0, %%T0
        adc     %%A1, %%T1
%ifnidn %%PAD_16,pad_to_16
        adc     %%A2, 0                 ;; no padding bit
%else
        adc     %%A2, 1                 ;; padding bit please
%endif

        mov     %%T0, %%R1
        shr     %%T0, 2
        add     %%T0, %%R1      ;; T0 = R1 + (R1 >> 2)

        POLY1305_MUL_REDUCE %%A0, %%A1, %%A2, %%R0, %%R1, \
                            %%T0, %%T1, %%T2, %%T3, %%GP_RAX, %%GP_RDX

%%_poly1305_blocks_exit:
%endmacro

;; =============================================================================
;; =============================================================================
;; Finalizes Poly1305 hash calculation on a message
;; =============================================================================
%macro POLY1305_FINALIZE 8
%define %%KEY     %1    ; [in] pointer to 32 byte key
%define %%MAC     %2    ; [in/out] pointer to store MAC value into (16 bytes)
%define %%A0      %3    ; [in/out] accumulator bits 63..0
%define %%A1      %4    ; [in/out] accumulator bits 127..64
%define %%A2      %5    ; [in/out] accumulator bits 195..128
%define %%T0      %6    ; [clobbered] GPR register
%define %%T1      %7    ; [clobbered] GPR register
%define %%T2      %8    ; [clobbered] GPR register

        ;; T = A - P, where P = 2^130 - 5
        ;;     P[63..0]    = 0xFFFFFFFFFFFFFFFB
        ;;     P[127..64]  = 0xFFFFFFFFFFFFFFFF
        ;;     P[195..128] = 0x0000000000000003
        mov     %%T0, %%A0
        mov     %%T1, %%A1
        mov     %%T2, %%A2

        sub     %%T0, -5        ;; 0xFFFFFFFFFFFFFFFB
        sbb     %%T1, -1        ;; 0xFFFFFFFFFFFFFFFF
        sbb     %%T2, 0x3

        ;; if A > (2^130 - 5) then A = T
        ;;     - here, if borrow/CF == false then A = T
        cmovnc  %%A0, %%T0
        cmovnc  %%A1, %%T1

        ;; MAC = (A + S) mod 2^128 (S = key[16..31])
        add     %%A0, [%%KEY + (2 * 8)]
        adc     %%A1, [%%KEY + (3 * 8)]

        ;; store MAC
        mov     [%%MAC + (0 * 8)], %%A0
        mov     [%%MAC + (1 * 8)], %%A1
%endmacro

;; =============================================================================
;; =============================================================================
;; Creates stack frame and saves registers
;; =============================================================================
%macro FUNC_ENTRY 0
        mov     rax, rsp
        sub     rsp, STACKFRAME_size
	and	rsp, -64

        mov     [rsp + _gpr_save + 8*0], rbx
        mov     [rsp + _gpr_save + 8*1], rbp
        mov     [rsp + _gpr_save + 8*2], r12
        mov     [rsp + _gpr_save + 8*3], r13
        mov     [rsp + _gpr_save + 8*4], r14
        mov     [rsp + _gpr_save + 8*5], r15
%ifndef LINUX
        mov     [rsp + _gpr_save + 8*6], rsi
        mov     [rsp + _gpr_save + 8*7], rdi
%endif
        mov     [rsp + _rsp_save], rax

%endmacro       ; FUNC_ENTRY

;; =============================================================================
;; =============================================================================
;; Restores registers and removes the stack frame
;; =============================================================================
%macro FUNC_EXIT 0
        mov     rbx, [rsp + _gpr_save + 8*0]
        mov     rbp, [rsp + _gpr_save + 8*1]
        mov     r12, [rsp + _gpr_save + 8*2]
        mov     r13, [rsp + _gpr_save + 8*3]
        mov     r14, [rsp + _gpr_save + 8*4]
        mov     r15, [rsp + _gpr_save + 8*5]
%ifndef LINUX
        mov     rsi, [rsp + _gpr_save + 8*6]
        mov     rdi, [rsp + _gpr_save + 8*7]
%endif
        mov     rsp, [rsp + _rsp_save]

%ifdef SAFE_DATA
       clear_scratch_gps_asm
%endif ;; SAFE_DATA

%endmacro

;; =============================================================================
;; =============================================================================
;; void poly1305_aead_update_fma_avx512(const void *msg, const uint64_t msg_len,
;;                                      void *hash, const void *key)
;; arg1 - Input message
;; arg2 - Message length
;; arg3 - Input/output hash
;; arg4 - Poly1305 key
align 32
MKGLOBAL(poly1305_aead_update_fma_avx512,function,internal)
poly1305_aead_update_fma_avx512:

%ifdef SAFE_PARAM
        or      arg1, arg1
        jz      .poly1305_update_exit

        or      arg3, arg3
        jz      .poly1305_update_exit

        or      arg4, arg4
        jz      .poly1305_update_exit
%endif

        FUNC_ENTRY

%ifdef LINUX
%xdefine _a0 gp3
%xdefine _a1 gp4
%xdefine _a2 gp5
%xdefine _r0 gp6
%xdefine _r1 gp7
%xdefine _len arg2
%xdefine _arg3 arg4             ; use rcx, arg3 = rdx
%else
%xdefine _a0 gp3
%xdefine _a1 rdi
%xdefine _a2 gp5                ; = arg4 / r9
%xdefine _r0 gp6
%xdefine _r1 gp7
%xdefine _len gp2               ; rsi
%xdefine _arg3 arg3             ; arg
%endif

        ;; load R
        mov     _r0, [arg4 + 0 * 8]
        mov     _r1, [arg4 + 1 * 8]

        ;; load accumulator / current hash value
        ;; note: arg4 can't be used beyond this point
%ifdef LINUX
        mov     _arg3, arg3             ; note: _arg3 = arg4 (linux)
%endif
        mov     _a0, [_arg3 + 0 * 8]
        mov     _a1, [_arg3 + 1 * 8]
        mov     _a2, [_arg3 + 2 * 8]    ; note: _a2 = arg4 (win)

%ifndef LINUX
        mov     _len, arg2      ;; arg2 = rdx on Windows
%endif
        POLY1305_BLOCKS arg1, _len, _a0, _a1, _a2, _r0, _r1, \
                        gp10, gp11, gp8, gp9, rax, rdx, pad_to_16

        ;; save accumulator back
        mov     [_arg3 + 0 * 8], _a0
        mov     [_arg3 + 1 * 8], _a1
        mov     [_arg3 + 2 * 8], _a2

        FUNC_EXIT
.poly1305_update_exit:
        ret

;; =============================================================================
;; =============================================================================
;; void poly1305_aead_complete_fma_avx512(const void *hash, const void *key,
;;                                        void *tag)
;; arg1 - Input hash
;; arg2 - Poly1305 key
;; arg3 - Output tag
align 32
MKGLOBAL(poly1305_aead_complete_fma_avx512,function,internal)
poly1305_aead_complete_fma_avx512:

%ifdef SAFE_PARAM
        or      arg1, arg1
        jz      .poly1305_complete_exit

        or      arg2, arg2
        jz      .poly1305_complete_exit

        or      arg3, arg3
        jz      .poly1305_complete_exit
%endif

        FUNC_ENTRY

%xdefine _a0 gp6
%xdefine _a1 gp7
%xdefine _a2 gp8

        ;; load accumulator / current hash value
        mov     _a0, [arg1 + 0 * 8]
        mov     _a1, [arg1 + 1 * 8]
        mov     _a2, [arg1 + 2 * 8]

        POLY1305_FINALIZE arg2, arg3, _a0, _a1, _a2, gp9, gp10, gp11

        ;; clear Poly key
%ifdef SAFE_DATA
        vpxorq  ymm0, ymm0
        vmovdqu64 [arg2], ymm0
%endif

        FUNC_EXIT
.poly1305_complete_exit:
        ret

;; =============================================================================
;; =============================================================================
;; void poly1305_mac_fma_avx512(IMB_JOB *job)
;; arg1 - job structure
align 32
MKGLOBAL(poly1305_mac_fma_avx512,function,internal)
poly1305_mac_fma_avx512:
        endbranch64
        FUNC_ENTRY

%ifndef LINUX
        mov     job, arg1
%endif

%ifdef SAFE_PARAM
        or      job, job
        jz      .poly1305_mac_exit
%endif

%xdefine _a0 gp1
%xdefine _a1 gp2
%xdefine _a2 gp3
%xdefine _r0 gp4
%xdefine _r1 gp5

        mov     gp6, [job + _poly1305_key]
        POLY1305_INIT   gp6, _a0, _a1, _a2, _r0, _r1

        mov     msg, [job + _src]
        add     msg, [job + _hash_start_src_offset_in_bytes]
        mov     len, [job + _msg_len_to_hash]
        POLY1305_BLOCKS msg, len, _a0, _a1, _a2, _r0, _r1, \
                        gp6, gp7, gp8, gp9, rax, rdx, no_padding

        mov     rax, [job + _poly1305_key]
        mov     rdx, [job + _auth_tag_output]
        POLY1305_FINALIZE rax, rdx, _a0, _a1, _a2, gp6, gp7, gp8

.poly1305_mac_exit:
        FUNC_EXIT
        ret

mksection stack-noexec
