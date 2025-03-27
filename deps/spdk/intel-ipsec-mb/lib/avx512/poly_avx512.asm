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

[bits 64]
default rel

align 64
mask_26:
dq      0x3ffffff, 0x3ffffff, 0x3ffffff, 0x3ffffff, 0x3ffffff, 0x3ffffff, 0x3ffffff, 0x3ffffff

align 64
high_bit:
dq      0x1000000, 0x1000000, 0x1000000, 0x1000000, 0x1000000, 0x1000000, 0x1000000, 0x1000000

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
dq      0, 0, 0, 0, 0, 0, 0, 0
dq      0x0100, 0, 0, 0, 0, 0, 0, 0
dq      0x010000, 0, 0, 0, 0, 0, 0, 0
dq      0x01000000, 0, 0, 0, 0, 0, 0, 0
dq      0x0100000000, 0, 0, 0, 0, 0, 0, 0
dq      0x010000000000, 0, 0, 0, 0, 0, 0, 0
dq      0x01000000000000, 0, 0, 0, 0, 0, 0, 0
dq      0x0100000000000000, 0, 0, 0, 0, 0, 0, 0
dq      0, 0x01, 0, 0, 0, 0, 0, 0
dq      0, 0x0100, 0, 0, 0, 0, 0, 0
dq      0, 0x010000, 0, 0, 0, 0, 0, 0
dq      0, 0x01000000, 0, 0, 0, 0, 0, 0
dq      0, 0x0100000000, 0, 0, 0, 0, 0, 0
dq      0, 0x010000000000, 0, 0, 0, 0, 0, 0
dq      0, 0x01000000000000, 0, 0, 0, 0, 0, 0
dq      0, 0x0100000000000000, 0, 0, 0, 0, 0, 0
dq      0, 0, 0, 0, 0, 0, 0, 0
dq      0, 0, 0x0100, 0, 0, 0, 0, 0
dq      0, 0, 0x010000, 0, 0, 0, 0, 0
dq      0, 0, 0x01000000, 0, 0, 0, 0, 0
dq      0, 0, 0x0100000000, 0, 0, 0, 0, 0
dq      0, 0, 0x010000000000, 0, 0, 0, 0, 0
dq      0, 0, 0x01000000000000, 0, 0, 0, 0, 0
dq      0, 0, 0x0100000000000000, 0, 0, 0, 0, 0
dq      0, 0, 0, 0x01, 0, 0, 0, 0
dq      0, 0, 0, 0x0100, 0, 0, 0, 0
dq      0, 0, 0, 0x010000, 0, 0, 0, 0
dq      0, 0, 0, 0x01000000, 0, 0, 0, 0
dq      0, 0, 0, 0x0100000000, 0, 0, 0, 0
dq      0, 0, 0, 0x010000000000, 0, 0, 0, 0
dq      0, 0, 0, 0x01000000000000, 0, 0, 0, 0
dq      0, 0, 0, 0x0100000000000000, 0, 0, 0, 0
dq      0, 0, 0, 0, 0, 0, 0, 0
dq      0, 0, 0, 0, 0x0100, 0, 0, 0
dq      0, 0, 0, 0, 0x010000, 0, 0, 0
dq      0, 0, 0, 0, 0x01000000, 0, 0, 0
dq      0, 0, 0, 0, 0x0100000000, 0, 0, 0
dq      0, 0, 0, 0, 0x010000000000, 0, 0, 0
dq      0, 0, 0, 0, 0x01000000000000, 0, 0, 0
dq      0, 0, 0, 0, 0x0100000000000000, 0, 0, 0
dq      0, 0, 0, 0, 0, 0x01, 0, 0
dq      0, 0, 0, 0, 0, 0x0100, 0, 0
dq      0, 0, 0, 0, 0, 0x010000, 0, 0
dq      0, 0, 0, 0, 0, 0x01000000, 0, 0
dq      0, 0, 0, 0, 0, 0x0100000000, 0, 0
dq      0, 0, 0, 0, 0, 0x010000000000, 0, 0
dq      0, 0, 0, 0, 0, 0x01000000000000, 0, 0
dq      0, 0, 0, 0, 0, 0x0100000000000000, 0, 0
dq      0, 0, 0, 0, 0, 0, 0, 0
dq      0, 0, 0, 0, 0, 0, 0x0100, 0
dq      0, 0, 0, 0, 0, 0, 0x010000, 0
dq      0, 0, 0, 0, 0, 0, 0x01000000, 0
dq      0, 0, 0, 0, 0, 0, 0x0100000000, 0
dq      0, 0, 0, 0, 0, 0, 0x010000000000, 0
dq      0, 0, 0, 0, 0, 0, 0x01000000000000, 0
dq      0, 0, 0, 0, 0, 0, 0x0100000000000000, 0
dq      0, 0, 0, 0, 0, 0, 0, 0x01
dq      0, 0, 0, 0, 0, 0, 0, 0x0100
dq      0, 0, 0, 0, 0, 0, 0, 0x010000
dq      0, 0, 0, 0, 0, 0, 0, 0x01000000
dq      0, 0, 0, 0, 0, 0, 0, 0x0100000000
dq      0, 0, 0, 0, 0, 0, 0, 0x010000000000
dq      0, 0, 0, 0, 0, 0, 0, 0x01000000000000
dq      0, 0, 0, 0, 0, 0, 0, 0x0100000000000000

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
_r_save:        resq    16 ; Memory to save limbs of powers of R
_rp_save:       resq    8  ; Memory to save limbs of powers of R'
_gpr_save:      resq    8  ; Memory to save GP registers
_rsp_save:      resq    1  ; Memory to save RSP pointer
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
;; Computes hash for 8 16-byte message blocks.
;; It computes H8 = ((H0 + M1) * R^8 + M2 * R^7 + M3 * R^6 + M4 * R^5 +
;;                   M5 * R^4 + M6 * R^3 + M7 * R^2 + M8 * R)
;;
;; It first multiplies all 8 blocks with powers of R:
;;
;;      a4      a3      a2      a1      a0
;; ×    b4      b3      b2      b1      b0
;; ---------------------------------------
;;   a4×b0   a3×b0   a2×b0   a1×b0   a0×b0
;; + a3×b1   a2×b1   a1×b1   a0×b1 5×a4×b1
;; + a2×b2   a1×b2   a0×b2 5×a4×b2 5×a3×b2
;; + a1×b3   a0×b3 5×a4×b3 5×a3×b3 5×a2×b3
;; + a0×b4 5×a4×b4 5×a3×b4 5×a2×b4 5×a1×b4
;; ---------------------------------------
;;      p4      p3      p2      p1      p0
;;
;; Then, it propagates the carry (higher bits after bit 25) from lower limbs into higher limbs,
;; multiplying by 5 in case of the carry of p4.
;;
;; =============================================================================
%macro POLY1305_MUL_REDUCE_VEC 28-29
%define %%A0      %1  ; [in/out] ZMM register containing 1st 26-bit limb of the 8 blocks
%define %%A1      %2  ; [in/out] ZMM register containing 2nd 26-bit limb of the 8 blocks
%define %%A2      %3  ; [in/out] ZMM register containing 3rd 26-bit limb of the 8 blocks
%define %%A3      %4  ; [in/out] ZMM register containing 4th 26-bit limb of the 8 blocks
%define %%A4      %5  ; [in/out] ZMM register containing 5th 26-bit limb of the 8 blocks
%define %%R0      %6  ; [in] ZMM register (R0) to include the 1st limb in IDX
%define %%R1      %7  ; [in] ZMM register (R1) to include the 2nd limb in IDX
%define %%R2      %8  ; [in] ZMM register (R2) to include the 3rd limb in IDX
%define %%R3      %9  ; [in] ZMM register (R3) to include the 4th limb in IDX
%define %%R4      %10 ; [in] ZMM register (R4) to include the 5th limb in IDX
%define %%R1P     %11 ; [in] ZMM register (R1') to include the 2nd limb (multiplied by 5) in IDX
%define %%R2P     %12 ; [in] ZMM register (R2') to include the 3rd limb (multiplied by 5) in IDX
%define %%R3P     %13 ; [in] ZMM register (R3') to include the 4th limb (multiplied by 5) in IDX
%define %%R4P     %14 ; [in] ZMM register (R4') to include the 5th limb (multiplied by 5) in IDX
%define %%P0      %15 ; [clobbered] ZMM register to contain p[0] of the 8 blocks
%define %%P1      %16 ; [clobbered] ZMM register to contain p[1] of the 8 blocks
%define %%P2      %17 ; [clobbered] ZMM register to contain p[2] of the 8 blocks
%define %%P3      %18 ; [clobbered] ZMM register to contain p[3] of the 8 blocks
%define %%P4      %19 ; [clobbered] ZMM register to contain p[4] of the 8 blocks
%define %%MASK_26 %20 ; [in] ZMM register containing 26-bit mask
%define %%ZTMP1   %21 ; [clobbered] Temporary ZMM register
%define %%ZTMP2   %22 ; [clobbered] Temporary ZMM register
%define %%ZTMP3   %23 ; [clobbered] Temporary ZMM register
%define %%ZTMP4   %24 ; [clobbered] Temporary ZMM register
%define %%ZTMP5   %25 ; [clobbered] Temporary ZMM register
%define %%ZTMP6   %26 ; [clobbered] Temporary ZMM register
%define %%ZTMP7   %27 ; [clobbered] Temporary ZMM register
%define %%ZTMP8   %28 ; [clobbered] Temporary ZMM register
%define %%MSG     %29 ; [in] Pointer to message

        ; Calculate p[0] addends
        vpmuludq        %%P0, %%A0, %%R0
        vpmuludq        %%P1, %%A0, %%R1
        vpmuludq        %%P2, %%A0, %%R2
        vpmuludq        %%ZTMP2, %%A1, %%R4P
        vpmuludq        %%ZTMP3, %%A2, %%R3P
        vpmuludq        %%ZTMP4, %%A3, %%R2P
        vpmuludq        %%ZTMP5, %%A4, %%R1P

        ; Calculate p[0]
        vpaddq          %%P0, %%ZTMP2
        vpaddq          %%P0, %%ZTMP3
        vpaddq          %%P0, %%ZTMP4
        vpaddq          %%P0, %%ZTMP5

        ; Calculate p[1] addends
        vpmuludq        %%ZTMP2, %%A1, %%R0
        vpmuludq        %%ZTMP3, %%A2, %%R4P
        vpmuludq        %%ZTMP4, %%A3, %%R3P
        vpmuludq        %%ZTMP5, %%A4, %%R2P

        ; Calculate p[1]
        vpaddq          %%P1, %%ZTMP2
        vpaddq          %%P1, %%ZTMP3
        vpaddq          %%P1, %%ZTMP4
        vpaddq          %%P1, %%ZTMP5

        ; Calculate p[2] addends
        vpmuludq        %%ZTMP2, %%A1, %%R1
        vpmuludq        %%ZTMP3, %%A2, %%R0
        vpmuludq        %%ZTMP4, %%A3, %%R4P
        vpmuludq        %%ZTMP5, %%A4, %%R3P

        ; Calculate p[2]
        vpaddq          %%P2, %%ZTMP2
        vpaddq          %%P2, %%ZTMP3
        vpaddq          %%P2, %%ZTMP4
        vpaddq          %%P2, %%ZTMP5

        ; Calculate p[3] addends
        vpmuludq        %%P3, %%A0, %%R3
        vpmuludq        %%ZTMP2, %%A1, %%R2
        vpmuludq        %%ZTMP3, %%A2, %%R1
        vpmuludq        %%ZTMP4, %%A3, %%R0
        vpmuludq        %%ZTMP5, %%A4, %%R4P

        ; Calculate p[3]
        vpaddq          %%P3, %%ZTMP2
        vpaddq          %%P3, %%ZTMP3
        vpaddq          %%P3, %%ZTMP4
        vpaddq          %%P3, %%ZTMP5

        ; Calculate p[4] addends
        vpmuludq        %%P4, %%A0, %%R4
        vpmuludq        %%ZTMP2, %%A1, %%R3
        vpmuludq        %%ZTMP3, %%A2, %%R2
        vpmuludq        %%ZTMP4, %%A3, %%R1
        vpmuludq        %%ZTMP5, %%A4, %%R0

        ; Calculate p[4]
        vpaddq          %%P4, %%ZTMP2
        vpaddq          %%P4, %%ZTMP3
        vpaddq          %%P4, %%ZTMP4
        vpaddq          %%P4, %%ZTMP5

%if %0 == 28
        ; First pass of carry propagation
        vpsrlq          %%ZTMP1, %%P0, 26
        vpaddq          %%P1, %%ZTMP1
        vpsrlq          %%ZTMP1, %%P1, 26
        vpaddq          %%P2, %%ZTMP1
        vpsrlq          %%ZTMP1, %%P2, 26
        vpaddq          %%P3, %%ZTMP1
        vpsrlq          %%ZTMP1, %%P3, 26
        vpaddq          %%P4, %%ZTMP1

        vpsrlq          %%ZTMP1, %%P4, 26
        vpsllq          %%ZTMP2, %%ZTMP1, 2
        vpaddq          %%ZTMP1, %%ZTMP2 ; (*5)
        vpandq          %%P0, %%MASK_26 ; Clear top 32+6 bits
        vpandq          %%P1, %%MASK_26 ; Clear top 32+6 bits
        vpandq          %%P2, %%MASK_26 ; Clear top 32+6 bits
        vpandq          %%P3, %%MASK_26 ; Clear top 32+6 bits
        vpandq          %%P4, %%MASK_26 ; Clear top 32+6 bits
        vpaddq          %%P0, %%ZTMP1

        ; Second pass of carry propagation
        vpsrlq          %%ZTMP1, %%P0, 26
        vpaddq          %%P1, %%ZTMP1
        vpsrlq          %%ZTMP1, %%P1, 26
        vpaddq          %%P2, %%ZTMP1
        vpsrlq          %%ZTMP1, %%P2, 26
        vpaddq          %%P3, %%ZTMP1
        vpsrlq          %%ZTMP1, %%P3, 26
        vpaddq          %%P4, %%ZTMP1

        vpandq          %%A0, %%P0, %%MASK_26 ; Clear top 32+6 bits
        vpandq          %%A1, %%P1, %%MASK_26 ; Clear top 32+6 bits
        vpandq          %%A2, %%P2, %%MASK_26 ; Clear top 32+6 bits
        vpandq          %%A3, %%P3, %%MASK_26 ; Clear top 32+6 bits
        vpandq          %%A4, %%P4, %%MASK_26 ; Clear top 32+6 bits
%else ;; %0 == 29
        ; Load next block of data (128 bytes)
        vmovdqu64 %%ZTMP3, [%%MSG]
        vmovdqu64 %%ZTMP4, [%%MSG + 64]

        ; First pass of carry propagation
        vpsrlq          %%ZTMP1, %%P0, 26
        vpaddq          %%P1, %%ZTMP1
        vpsrlq          %%ZTMP1, %%P1, 26
        vpaddq          %%P2, %%ZTMP1
        vpsrlq          %%ZTMP1, %%P2, 26
        vpaddq          %%P3, %%ZTMP1
        vpsrlq          %%ZTMP1, %%P3, 26
        vpaddq          %%P4, %%ZTMP1

        ; Interleave the data to form 26-bit limbs
        ;
        ; %%ZTMP3 to have bits 0-25 of all 8 blocks in 8 qwords
        ; %%ZTMP4 to have bits 51-26 of all 8 blocks in 8 qwords
        ; %%ZTMP5 to have bits 77-52 of all 8 blocks in 8 qwords
        ; %%ZTMP8 to have bits 103-78 of all 8 blocks in 8 qwords
        ; %%ZTMP7 to have bits 127-104 of all 8 blocks in 8 qwords

        ; Add 2^128 to all 8 final qwords of the message
        vpunpckhqdq     %%ZTMP7, %%ZTMP3, %%ZTMP4
        vpunpcklqdq     %%ZTMP3, %%ZTMP3, %%ZTMP4
        vpsrlq          %%ZTMP5, %%ZTMP3, 52
        vpsllq          %%ZTMP6, %%ZTMP7, 12

        vpsrlq          %%ZTMP1, %%P4, 26
        vpsllq          %%ZTMP2, %%ZTMP1, 2
        vpaddq          %%ZTMP1, %%ZTMP2 ; (*5)
        vpandq          %%P0, %%MASK_26 ; Clear top 32+6 bits
        vpandq          %%P1, %%MASK_26 ; Clear top 32+6 bits
        vpandq          %%P2, %%MASK_26 ; Clear top 32+6 bits
        vpandq          %%P3, %%MASK_26 ; Clear top 32+6 bits
        vpandq          %%P4, %%MASK_26 ; Clear top 32+6 bits
        vpaddq          %%P0, %%ZTMP1

        ; Second pass of carry propagation
        vpsrlq          %%ZTMP1, %%P0, 26
        vpaddq          %%P1, %%ZTMP1
        vpsrlq          %%ZTMP1, %%P1, 26

        vpsrlq          %%ZTMP4, %%ZTMP3, 26
        vpsrlq          %%ZTMP8, %%ZTMP7, 14
        vpsrlq          %%ZTMP7, 40

        vpaddq          %%P2, %%ZTMP1
        vpsrlq          %%ZTMP1, %%P2, 26
        vpaddq          %%P3, %%ZTMP1
        vpsrlq          %%ZTMP1, %%P3, 26

        vpandq          %%ZTMP3, %%MASK_26
        vpandq          %%ZTMP4, %%MASK_26
        vpternlogq      %%ZTMP5, %%ZTMP6, %%MASK_26, 0xA8 ; (A OR B AND C)
        vpandq          %%ZTMP8, %%MASK_26

        vpaddq          %%P4, %%ZTMP1

        vpandq          %%A0, %%P0, %%MASK_26 ; Clear top 32+6 bits
        vpandq          %%A1, %%P1, %%MASK_26 ; Clear top 32+6 bits
        vporq           %%ZTMP7, [rel high_bit]

        ; Add new message blocks to previous accumulator
        vpaddq          %%A0, %%ZTMP3
        vpaddq          %%A1, %%ZTMP4

        vpandq          %%A2, %%P2, %%MASK_26 ; Clear top 32+6 bits
        vpandq          %%A3, %%P3, %%MASK_26 ; Clear top 32+6 bits
        vpandq          %%A4, %%P4, %%MASK_26 ; Clear top 32+6 bits

        vpaddq          %%A2, %%ZTMP5
        vpaddq          %%A3, %%ZTMP8
        vpaddq          %%A4, %%ZTMP7
%endif
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
%macro SHUFFLE_DATA_BLOCKS 7
%define %%A_0_25        %1 ; [in/out] 0-25 bits of input data
%define %%A_26_51       %2 ; [in/out] 26-51 bits of input data
%define %%A_52_77       %3 ; [in/out] 52-77 bits of input data
%define %%A_78_103      %4 ; [in/out] 78-103 bits of input data
%define %%A_104_129     %5 ; [in/out] 104-129 bits of input data
%define %%TMP           %6 ; [clobbered] Temporary GP register
%define %%N_BLOCKS      %7 ; [in] Number of remaining input blocks

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
        vpshufd %%A_0_25{k1}, %%A_0_25, 0x4E
        vpshufd %%A_26_51{k1}, %%A_26_51, 0x4E
        vpshufd %%A_52_77{k1}, %%A_52_77, 0x4E
        vpshufd %%A_78_103{k1}, %%A_78_103, 0x4E
        vpshufd %%A_104_129{k1}, %%A_104_129, 0x4E
%if %%N_BLOCKS != 4
        vshufi64x2 %%A_0_25, %%A_0_25, %%SHUF_MASK
        vshufi64x2 %%A_26_51, %%A_26_51, %%SHUF_MASK
        vshufi64x2 %%A_52_77, %%A_52_77, %%SHUF_MASK
        vshufi64x2 %%A_78_103, %%A_78_103, %%SHUF_MASK
        vshufi64x2 %%A_104_129, %%A_104_129, %%SHUF_MASK
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

%define %%MASK_26 zmm31

        ; Minimum of 256 bytes to run vectorized code
        cmp     %%LEN, POLY1305_BLOCK_SIZE*16
        jb      %%_final_loop

        vmovdqa64 %%MASK_26, [rel mask_26]

        ; Spread accumulator into 26-bit limbs in quadwords
        mov     %%T0, %%A0
        and     %%T0, 0x3ffffff ;; First limb (A[25:0])
        vmovq   xmm5, %%T0

        mov     %%T0, %%A0
        shr     %%T0, 26
        and     %%T0, 0x3ffffff ;; Second limb (A[51:26])
        vmovq   xmm6, %%T0

        mov     %%T0, %%A1
        shrd    %%A0, %%T0, 52
        and     %%A0, 0x3ffffff ;; Third limb (A[77:52])
        vmovq   xmm7, %%A0

        mov     %%T0, %%A1
        shr     %%T0, 14
        and     %%T0, 0x3ffffff ; Fourth limb (A[103:78])
        vmovq   xmm8, %%T0

        shrd    %%A1, %%A2, 40
        vmovq   xmm9, %%A1

        ; Load first block of data (128 bytes)
        vmovdqu64 zmm0, [%%MSG]
        vmovdqu64 zmm1, [%%MSG + 64]

        ; Interleave the data to form 26-bit limbs
        ;
        ; zmm15 to have bits 0-25 of all 8 blocks in 8 qwords
        ; zmm16 to have bits 51-26 of all 8 blocks in 8 qwords
        ; zmm17 to have bits 77-52 of all 8 blocks in 8 qwords
        ; zmm18 to have bits 103-78 of all 8 blocks in 8 qwords
        ; zmm19 to have bits 127-104 of all 8 blocks in 8 qwords
        vpunpckhqdq zmm19, zmm0, zmm1
        vpunpcklqdq zmm15, zmm0, zmm1

        vpsrlq  zmm17, zmm15, 52
        vpsllq  zmm18, zmm19, 12

        vpsrlq  zmm16, zmm15, 26
        vpsrlq  zmm20, zmm19, 14
        vpsrlq  zmm19, 40
        vpandq  zmm15, %%MASK_26
        vpandq  zmm16, %%MASK_26
        vpternlogq zmm17, zmm18, %%MASK_26, 0xA8 ; (A OR B AND C)
        vpandq  zmm18, zmm20, %%MASK_26

        ; Add 2^128 to all 8 final qwords of the message
        vporq   zmm19, [rel high_bit]

        vpaddq  zmm5, zmm15
        vpaddq  zmm6, zmm16
        vpaddq  zmm7, zmm17
        vpaddq  zmm8, zmm18
        vpaddq  zmm9, zmm19

        ; Use memory in stack to save powers of R, before loading them into ZMM registers
        ; The first 16*8 bytes will contain the 16 bytes of the 8 powers of R
        ; The last 64 bytes will contain the last 2 bits of powers of R, spread in 8 qwords,
        ; to be OR'd with the highest qwords (in zmm26)
        mov     [rsp + _r_save + 16*7], %%R0
        mov     [rsp + _r_save + 16*7 + 8], %%R1

        xor     %%T0, %%T0
        mov     [rsp + _rp_save + 8*7], %%T0

        ; Calculate R^2
        mov     %%T0, %%R1
        shr     %%T0, 2
        add     %%T0, %%R1      ;; T0 = R1 + (R1 >> 2)

        mov     %%A0, %%R0
        mov     %%A1, %%R1

        POLY1305_MUL_REDUCE %%A0, %%A1, %%A2, %%R0, %%R1, %%T0, %%T1, %%T2, %%T3, %%GP_RAX, %%GP_RDX, no_A2

        mov     [rsp + _r_save + 16*6], %%A0
        mov     [rsp + _r_save + 16*6 + 8], %%A1
        mov     %%T3, %%A2
        shl     %%T3, 24
        mov     [rsp + _rp_save + 8*5], %%T3

        ; Calculate R^3
        POLY1305_MUL_REDUCE %%A0, %%A1, %%A2, %%R0, %%R1, %%T0, %%T1, %%T2, %%T3, %%GP_RAX, %%GP_RDX

        mov     [rsp + _r_save + 16*5], %%A0
        mov     [rsp + _r_save + 16*5 + 8], %%A1
        mov     %%T3, %%A2
        shl     %%T3, 24
        mov     [rsp + _rp_save + 8*3], %%T3

        ; Calculate R^4
        POLY1305_MUL_REDUCE %%A0, %%A1, %%A2, %%R0, %%R1, %%T0, %%T1, %%T2, %%T3, %%GP_RAX, %%GP_RDX

        mov     [rsp + _r_save + 16*4], %%A0
        mov     [rsp + _r_save + 16*4 + 8], %%A1
        mov     %%T3, %%A2
        shl     %%T3, 24
        mov     [rsp + _rp_save + 8], %%T3

        ; Calculate R^5
        POLY1305_MUL_REDUCE %%A0, %%A1, %%A2, %%R0, %%R1, %%T0, %%T1, %%T2, %%T3, %%GP_RAX, %%GP_RDX

        mov     [rsp + _r_save + 16*3], %%A0
        mov     [rsp + _r_save + 16*3 + 8], %%A1
        mov     %%T3, %%A2
        shl     %%T3, 24
        mov     [rsp + _rp_save + 8*6], %%T3

        ; Calculate R^6
        POLY1305_MUL_REDUCE %%A0, %%A1, %%A2, %%R0, %%R1, %%T0, %%T1, %%T2, %%T3, %%GP_RAX, %%GP_RDX

        mov     [rsp + _r_save + 16*2], %%A0
        mov     [rsp + _r_save + 16*2 + 8], %%A1
        mov     %%T3, %%A2
        shl     %%T3, 24
        mov     [rsp + _rp_save + 8*4], %%T3

        ; Calculate R^7
        POLY1305_MUL_REDUCE %%A0, %%A1, %%A2, %%R0, %%R1, %%T0, %%T1, %%T2, %%T3, %%GP_RAX, %%GP_RDX

        mov     [rsp + _r_save + 16], %%A0
        mov     [rsp + _r_save + 16 + 8], %%A1
        mov     %%T3, %%A2
        shl     %%T3, 24
        mov     [rsp + _rp_save + 8*2], %%T3

        ; Calculate R^8
        POLY1305_MUL_REDUCE %%A0, %%A1, %%A2, %%R0, %%R1, %%T0, %%T1, %%T2, %%T3, %%GP_RAX, %%GP_RDX

        mov     [rsp + _r_save], %%A0
        mov     [rsp + _r_save + 8], %%A1
        mov     %%T3, %%A2
        shl     %%T3, 24
        mov     [rsp + _rp_save], %%T3

        ; Broadcast 26-bit limbs of R^8
        mov     %%T0, %%A0
        and     %%T0, 0x3ffffff ;; First limb (R^8[25:0])
        vpbroadcastq zmm22, %%T0

        mov     %%T0, %%A0
        shr     %%T0, 26
        and     %%T0, 0x3ffffff ;; Second limb (R^8[51:26])
        vpbroadcastq zmm23, %%T0

        mov     %%T0, %%A1
        shrd    %%A0, %%T0, 52
        and     %%A0, 0x3ffffff ;; Third limb (R^8[77:52])
        vpbroadcastq zmm24, %%A0

        mov     %%T0, %%A1
        shr     %%T0, 14
        and     %%T0, 0x3ffffff ; Fourth limb (R^8[103:78])
        vmovq   xmm0, %%T0
        vpbroadcastq zmm25, %%T0

        shrd    %%A1, %%A2, 40 ;; Fifth limb (R^8[129:104])
        vpbroadcastq zmm26, %%A1

        ; Generate 5*R^8
        vpsllq  zmm27, zmm23, 2
        vpsllq  zmm28, zmm24, 2
        vpsllq  zmm29, zmm25, 2
        vpsllq  zmm30, zmm26, 2

        vpaddq  zmm27, zmm23
        vpaddq  zmm28, zmm24
        vpaddq  zmm29, zmm25
        vpaddq  zmm30, zmm26

        mov     %%T0, %%LEN
        and     %%T0, 0xffffffffffffff80 ; multiple of 128 bytes

%%_poly1305_blocks_loop:
        cmp     %%T0, POLY1305_BLOCK_SIZE*8
        jbe     %%_poly1305_blocks_loop_end

        add     %%MSG, POLY1305_BLOCK_SIZE*8
        sub     %%T0, POLY1305_BLOCK_SIZE*8

        ; zmm5-zmm9 contain the 8 blocks of message plus the previous accumulator
        ; zmm22-26 contain the 5x26-bit limbs of the powers of R
        ; zmm27-30 contain the 5x26-bit limbs of the powers of R' (5*R)
        POLY1305_MUL_REDUCE_VEC zmm5, zmm6, zmm7, zmm8, zmm9, \
                                zmm22, zmm23, zmm24, zmm25, zmm26, \
                                zmm27, zmm28, zmm29, zmm30, \
                                zmm15, zmm16, zmm17, zmm18, zmm19, zmm31, \
                                zmm10, zmm11, zmm12, zmm13, zmm14, zmm0, zmm1, zmm2, \
                                %%MSG

        jmp     %%_poly1305_blocks_loop

%%_poly1305_blocks_loop_end:

        ;; Need to multiply by r^8,r^7... r

        ; Interleave the powers of R to form 26-bit limbs
        ;
        ; zmm22 to have bits 0-25 of all 8 powers of R in 8 qwords
        ; zmm23 to have bits 51-26 of all 8 powers of R in 8 qwords
        ; zmm24 to have bits 77-52 of all 8 powers of R in 8 qwords
        ; zmm25 to have bits 103-78 of all 8 powers of R in 8 qwords
        ; zmm26 to have bits 127-104 of all 8 powers of R in 8 qwords
        vmovdqa64 zmm0, [rsp + _r_save]
        vmovdqa64 zmm1, [rsp + _r_save + 64]

        vpunpckhqdq zmm26, zmm0, zmm1
        vpunpcklqdq zmm22, zmm0, zmm1

        vpsrlq  zmm24, zmm22, 52
        vpsllq  zmm25, zmm26, 12
        vpsrlq  zmm23, zmm22, 26
        vpsrlq  zmm30, zmm26, 14
        vpsrlq  zmm26, 40

        vpandq  zmm22, %%MASK_26                 ; R0
        vpandq  zmm23, %%MASK_26                 ; R1
        vpternlogq zmm24, zmm25, %%MASK_26, 0xA8 ; R2 (A OR B AND C)
        vpandq  zmm25, zmm30, %%MASK_26          ; R3

        ; rsp + _rp_save contains the 2 highest bits of the powers of R
        vporq   zmm26, [rsp + _rp_save]   ; R4

        ; zmm27 to have bits 51-26 of all 8 powers of R' in 8 qwords
        ; zmm28 to have bits 77-52 of all 8 powers of R' in 8 qwords
        ; zmm29 to have bits 103-78 of all 8 powers of R' in 8 qwords
        ; zmm30 to have bits 127-104 of all 8 powers of R' in 8 qwords
        vpsllq  zmm0, zmm23, 2
        vpaddq  zmm27, zmm23, zmm0 ; R1' (R1*5)

        vpsllq  zmm1, zmm24, 2
        vpaddq  zmm28, zmm24, zmm1 ; R2' (R2*5)

        vpsllq  zmm2, zmm25, 2
        vpaddq  zmm29, zmm25, zmm2 ; R3' (R3*5)

        vpsllq  zmm3, zmm26, 2
        vpaddq  zmm30, zmm26, zmm3 ; R4' (R4*5)

        ; zmm5-zmm9 contain the 8 blocks of message plus the previous accumulator
        ; zmm22-26 contain the 5x26-bit limbs of the powers of R
        ; zmm27-30 contain the 5x26-bit limbs of the powers of R' (5*R)
        POLY1305_MUL_REDUCE_VEC zmm5, zmm6, zmm7, zmm8, zmm9, \
                                zmm22, zmm23, zmm24, zmm25, zmm26, \
                                zmm27, zmm28, zmm29, zmm30, \
                                zmm15, zmm16, zmm17, zmm18, zmm19, zmm31, \
                                zmm10, zmm11, zmm12, zmm13, zmm14, zmm0, zmm1, zmm2

        ;; Add all blocks (horizontally)
        vextracti64x4   ymm0, zmm5, 1
        vextracti64x4   ymm1, zmm6, 1
        vextracti64x4   ymm2, zmm7, 1
        vextracti64x4   ymm3, zmm8, 1
        vextracti64x4   ymm4, zmm9, 1

        vpaddq  ymm0, ymm5
        vpaddq  ymm1, ymm6
        vpaddq  ymm2, ymm7
        vpaddq  ymm3, ymm8
        vpaddq  ymm4, ymm9

        vextracti32x4   xmm10, ymm0, 1
        vextracti32x4   xmm11, ymm1, 1
        vextracti32x4   xmm12, ymm2, 1
        vextracti32x4   xmm13, ymm3, 1
        vextracti32x4   xmm14, ymm4, 1

        vpaddq  xmm0, xmm10
        vpaddq  xmm1, xmm11
        vpaddq  xmm2, xmm12
        vpaddq  xmm3, xmm13
        vpaddq  xmm4, xmm14

        vpsrldq xmm10, xmm0, 8
        vpsrldq xmm11, xmm1, 8
        vpsrldq xmm12, xmm2, 8
        vpsrldq xmm13, xmm3, 8
        vpsrldq xmm14, xmm4, 8

        ; Finish folding and clear second qword
        mov     %%T0, 0xfd
        kmovq   k1, %%T0
        vpaddq  xmm0{k1}{z}, xmm10
        vpaddq  xmm1{k1}{z}, xmm11
        vpaddq  xmm2{k1}{z}, xmm12
        vpaddq  xmm3{k1}{z}, xmm13
        vpaddq  xmm4{k1}{z}, xmm14

        add     %%MSG, POLY1305_BLOCK_SIZE*8

        and     %%LEN, (POLY1305_BLOCK_SIZE*8 - 1) ; Get remaining lengths (LEN < 128 bytes)
        cmp     %%LEN, 64 ; If remaining bytes is <= 64, perform last blocks in scalar
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
        vmovdqu8 zmm5{k1}{z}, [%%MSG]
        vmovdqu8 zmm6{k2}{z}, [%%MSG + 64]

        ; Pad last block message, if partial
%ifnidn %%PAD_16,pad_to_16
        lea     %%T1, [rel pad64_bit]
        shl     %%T0, 6

        cmp     %%LEN, 64
        ja      %%_pad_second_64b

        vporq   zmm5, [%%T1 + %%T0]
        jmp     %%_end_pad

%%_pad_second_64b:
        vporq   zmm6, [%%T1 + %%T0]

%%_end_pad:
%endif
        mov     %%T0, %%LEN
        and     %%T0, 0x70 ; Multiple of 16 bytes
        ; Load last block of data (up to 112 bytes)
        shr     %%T0, 3 ; Get number of full qwords

        ; Interleave the data to form 26-bit limbs
        ;
        ; zmm15 to have bits 0-25 of all 8 blocks in 8 qwords
        ; zmm16 to have bits 51-26 of all 8 blocks in 8 qwords
        ; zmm17 to have bits 77-52 of all 8 blocks in 8 qwords
        ; zmm18 to have bits 103-78 of all 8 blocks in 8 qwords
        ; zmm19 to have bits 127-104 of all 8 blocks in 8 qwords
        vpunpckhqdq zmm19, zmm5, zmm6
        vpunpcklqdq zmm15, zmm5, zmm6

        vpsrlq  zmm17, zmm15, 52
        vpsllq  zmm18, zmm19, 12

        vpsrlq  zmm16, zmm15, 26
        vpsrlq  zmm20, zmm19, 14
        vpsrlq  zmm19, 40
        vpandq  zmm15, %%MASK_26
        vpandq  zmm16, %%MASK_26
        vpternlogq zmm17, zmm18, %%MASK_26, 0xA8 ; (A OR B AND C)
        vpandq  zmm18, zmm20, %%MASK_26

        lea     %%T1, [rel qword_high_bit_mask]
%ifnidn %%PAD_16,pad_to_16
        kmovb   k1, [%%T1 + %%T0]
%else
        mov     %%T3, %%LEN
        mov     %%T2, %%T0
        add     %%T2, 2
        and     %%T3, 0xf
        cmovnz  %%T0, %%T2
        kmovb   k1, [%%T1 + %%T0]
%endif
        ; Add 2^128 to final qwords of the message (all full blocks and partial block,
        ; if "pad_to_16" is selected)
        vporq   zmm19{k1}, [rel high_bit]

        vpaddq  zmm5, zmm15, zmm0
        vpaddq  zmm6, zmm16, zmm1
        vpaddq  zmm7, zmm17, zmm2
        vpaddq  zmm8, zmm18, zmm3
        vpaddq  zmm9, zmm19, zmm4

        mov     %%T0, %%LEN
        add     %%T0, 15
        shr     %%T0, 4         ; Get number of 16-byte blocks (including partial blocks)
        xor     %%LEN, %%LEN    ; All length will be consumed

        ; No need to shuffle data blocks (data is in the right order)
        cmp     %%T0, 8
        je      %%_end_shuffle

        ; Number of 16-byte blocks > 4
        cmp     %%T0, 6
        je      %%_shuffle_blocks_6
        ja      %%_shuffle_blocks_7

        ; 5 blocks remaining
%assign i 5
%rep 3
APPEND(%%_shuffle_blocks_, i):
        SHUFFLE_DATA_BLOCKS zmm5, zmm6, zmm7, zmm8, zmm9, %%T1, i
        jmp     %%_end_shuffle
%assign i (i + 1)
%endrep

%%_end_shuffle:

        ; zmm5-zmm9 contain the 8 blocks of message plus the previous accumulator
        ; zmm22-26 contain the 5x26-bit limbs of the powers of R
        ; zmm27-30 contain the 5x26-bit limbs of the powers of R' (5*R)
        POLY1305_MUL_REDUCE_VEC zmm5, zmm6, zmm7, zmm8, zmm9, \
                                zmm22, zmm23, zmm24, zmm25, zmm26, \
                                zmm27, zmm28, zmm29, zmm30, \
                                zmm15, zmm16, zmm17, zmm18, zmm19, zmm31, \
                                zmm10, zmm11, zmm12, zmm13, zmm14, zmm0, zmm1, zmm2

        ;; Add all blocks (horizontally)
        vextracti64x4   ymm0, zmm5, 1
        vextracti64x4   ymm1, zmm6, 1
        vextracti64x4   ymm2, zmm7, 1
        vextracti64x4   ymm3, zmm8, 1
        vextracti64x4   ymm4, zmm9, 1

        vpaddq  ymm0, ymm5
        vpaddq  ymm1, ymm6
        vpaddq  ymm2, ymm7
        vpaddq  ymm3, ymm8
        vpaddq  ymm4, ymm9

        vextracti32x4   xmm10, ymm0, 1
        vextracti32x4   xmm11, ymm1, 1
        vextracti32x4   xmm12, ymm2, 1
        vextracti32x4   xmm13, ymm3, 1
        vextracti32x4   xmm14, ymm4, 1

        vpaddq  xmm0, xmm10
        vpaddq  xmm1, xmm11
        vpaddq  xmm2, xmm12
        vpaddq  xmm3, xmm13
        vpaddq  xmm4, xmm14

        vpsrldq xmm10, xmm0, 8
        vpsrldq xmm11, xmm1, 8
        vpsrldq xmm12, xmm2, 8
        vpsrldq xmm13, xmm3, 8
        vpsrldq xmm14, xmm4, 8

        vpaddq  xmm0, xmm10
        vpaddq  xmm1, xmm11
        vpaddq  xmm2, xmm12
        vpaddq  xmm3, xmm13
        vpaddq  xmm4, xmm14

%%_simd_to_gp:
        ; Carry propagation
        vpsrlq  xmm6, xmm0, 26
        vpandq  xmm0, XWORD(%%MASK_26) ; Clear top 32+6 bits
        vpaddq  xmm1, xmm6
        vpsrlq  xmm6, xmm1, 26
        vpandq  xmm1, XWORD(%%MASK_26) ; Clear top 32+6 bits
        vpaddq  xmm2, xmm6
        vpsrlq  xmm6, xmm2, 26
        vpandq  xmm2, XWORD(%%MASK_26) ; Clear top 32+6 bits
        vpaddq  xmm3, xmm6
        vpsrlq  xmm6, xmm3, 26
        vpandq  xmm3, XWORD(%%MASK_26) ; Clear top 32+6 bits
        vpaddq  xmm4, xmm6
        vpsrlq  xmm6, xmm4, 26
        vpandq  xmm4, XWORD(%%MASK_26) ; Clear top 32+6 bits
        vpsllq  xmm5, xmm6, 2
        vpaddq  xmm6, xmm5
        vpaddq  xmm0, xmm6

        ; Put together A
        vmovq   %%A0, xmm0
        vmovq   %%T0, xmm1
        shl     %%T0, 26
        or      %%A0, %%T0
        vmovq   %%T0, xmm2
        mov     %%T1, %%T0
        shl     %%T1, 52
        or      %%A0, %%T1
        shr     %%T0, 12
        mov     %%A1, %%T0
        vmovq   %%T1, xmm3
        shl     %%T1, 14
        or      %%A1, %%T1
        vmovq   %%T0, xmm4
        mov     %%T1, %%T0
        shl     %%T1, 40
        or      %%A1, %%T1
        shr     %%T0, 24
        mov     %%A2, %%T0

        ; Clear powers of R
%ifdef SAFE_DATA
        vpxorq  zmm0, zmm0
        vmovdqa64 [rsp + _r_save], zmm0
        vmovdqa64 [rsp + _r_save + 64], zmm0
        vmovdqa64 [rsp + _rp_save], zmm0
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

%%_poly1305_blocks_partial:

        or      %%LEN, %%LEN
        jz      %%_poly1305_blocks_exit

        lea     %%T1, [rel byte_len_to_mask_table]
        kmovq   k1, [%%T1 + %%LEN*2]
        vmovdqu8 xmm0{k1}{z}, [%%MSG]

%ifnidn %%PAD_16,pad_to_16
        ;; pad the message in the scratch buffer
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
        and     rsp, -64

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
;; void poly1305_aead_update_avx512(const void *msg, const uint64_t msg_len,
;;                                  void *hash, const void *key)
;; arg1 - Input message
;; arg2 - Message length
;; arg3 - Input/output hash
;; arg4 - Poly1305 key
align 32
MKGLOBAL(poly1305_aead_update_avx512,function,internal)
poly1305_aead_update_avx512:

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
;; void poly1305_aead_complete_avx512(const void *hash, const void *key,
;;                                    void *tag)
;; arg1 - Input hash
;; arg2 - Poly1305 key
;; arg3 - Output tag
align 32
MKGLOBAL(poly1305_aead_complete_avx512,function,internal)
poly1305_aead_complete_avx512:

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
;; void poly1305_mac_plain_avx512(IMB_JOB *job)
;; arg1 - job structure
align 32
MKGLOBAL(poly1305_mac_plain_avx512,function,internal)
poly1305_mac_plain_avx512:
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
