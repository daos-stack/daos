;;
;; Copyright (c) 2020-2021, Intel Corporation
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

;; Useful links for understanding Poly1305:
;; "ChaCha20 and Poly1305 for IETF Protocols"
;;     https://tools.ietf.org/html/rfc7539
;; "A GO IMPLEMENTATION OF POLY1305 THAT MAKES SENSE"
;;     https://blog.filippo.io/a-literate-go-implementation-of-poly1305/
;; "The design of Poly1305"
;;     http://loup-vaillant.fr/tutorials/poly1305-design

%include "include/os.asm"
%include "include/reg_sizes.asm"
%include "include/memcpy.asm"
%include "include/imb_job.asm"
%include "include/clear_regs.asm"

[bits 64]
default rel

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

struc STACKFRAME
_gpr_save:      resq    8
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
%macro POLY1305_MUL_REDUCE 11
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
        mov     %%A1, %%A2      ;; use A1 for A2
        add     %%A0, %%GP_RAX
        adc     %%T1, %%GP_RDX

        ;; NOTE: A2 is clamped to 2-bits,
        ;;       R1/R0 is clamped to 60-bits,
        ;;       their product is less than 2^64.

        ;; T3:T2 += (A2 * R1x5)
        imul    %%A1, %%C1
        add     %%T2, %%A1
        mov     %%A1, %%T1 ;; T1:A0 => A1:A0
        adc     %%T3, 0

        ;; T3:A1 += (A2 * R0)
        imul    %%A2, %%R0
        add     %%A1, %%T2
        adc     %%T3, %%A2

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
;; Computes hash for message length being multiple of block size
;; =============================================================================
%macro POLY1305_BLOCKS 13
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

        mov     %%T0, %%R1
        shr     %%T0, 2
        add     %%T0, %%R1      ;; T0 = R1 + (R1 >> 2)

%%_poly1305_blocks_loop:
        cmp     %%LEN, POLY1305_BLOCK_SIZE
        jb      %%_poly1305_blocks_loop_end

        ;; A += MSG[i]
        add     %%A0, [%%MSG + (0 * 8)]
        adc     %%A1, [%%MSG + (1 * 8)]
        adc     %%A2, 1                 ;; padding bit

        POLY1305_MUL_REDUCE %%A0, %%A1, %%A2, %%R0, %%R1, \
                            %%T0, %%T1, %%T2, %%T3, %%GP_RAX, %%GP_RDX

        add     %%MSG, POLY1305_BLOCK_SIZE
        sub     %%LEN, POLY1305_BLOCK_SIZE
        jmp     %%_poly1305_blocks_loop

%%_poly1305_blocks_loop_end:
%endmacro

;; =============================================================================
;; =============================================================================
;; Computes hash for the final partial block
;; =============================================================================
%macro POLY1305_PARTIAL_BLOCK 15
%define %%BUF     %1    ; [in/clobbered] pointer to 16 byte scratch buffer
%define %%MSG     %2    ; [in] GPR pointer to input message
%define %%LEN     %3    ; [in] GPR message length
%define %%A0      %4    ; [in/out] accumulator bits 63..0
%define %%A1      %5    ; [in/out] accumulator bits 127..64
%define %%A2      %6    ; [in/out] accumulator bits 195..128
%define %%R0      %7    ; [in] R constant bits 63..0
%define %%R1      %8    ; [in] R constant bits 127..64
%define %%T0      %9    ; [clobbered] GPR register
%define %%T1      %10   ; [clobbered] GPR register
%define %%T2      %11   ; [clobbered] GPR register
%define %%T3      %12   ; [clobbered] GPR register
%define %%GP_RAX  %13   ; [clobbered] RAX register
%define %%GP_RDX  %14   ; [clobbered] RDX register
%define %%PAD_16  %15   ; [in] text "pad_to_16" or "no_padding"

        ;; clear the scratch buffer
        xor     %%T1, %%T1
        mov     [%%BUF + 0], %%T1
        mov     [%%BUF + 8], %%T1

        ;; copy message bytes into the scratch buffer
        memcpy_sse_16_1 %%BUF, %%MSG, %%LEN, %%T1, %%T2

%ifnidn %%PAD_16,pad_to_16
        ;; pad the message in the scratch buffer
        mov     byte [%%BUF + %%LEN], 0x01
%endif
        ;; A += MSG[i]
        add     %%A0, [%%BUF + 0]
        adc     %%A1, [%%BUF + 8]
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

%ifdef SAFE_DATA
        ;; clear the scratch buffer
        xor     %%T1, %%T1
        mov     [%%BUF + 0], %%T1
        mov     [%%BUF + 8], %%T1
%endif

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
        sub     rsp, STACKFRAME_size

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
        add     rsp, STACKFRAME_size

%ifdef SAFE_DATA
       clear_scratch_gps_asm
%endif ;; SAFE_DATA

%endmacro

;; =============================================================================
;; =============================================================================
;; void poly1305_mac_scalar(IMB_JOB *job)
;; arg1 - job structure
align 32
MKGLOBAL(poly1305_mac_scalar,function,internal)
poly1305_mac_scalar:
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
                        gp6, gp7, gp8, gp9, rax, rdx

        or      len, len
        jz      .poly1305_no_partial_block

        ;; create stack frame for the partial block scratch buffer
        sub     rsp, 16

        POLY1305_PARTIAL_BLOCK rsp, msg, len, _a0, _a1, _a2, _r0, _r1, \
                               gp6, gp7, gp8, gp9, rax, rdx, no_padding

        ;; remove the stack frame (memory is cleared as part of the macro)
        add     rsp, 16

.poly1305_no_partial_block:
        mov     rax, [job + _poly1305_key]
        mov     rdx, [job + _auth_tag_output]
        POLY1305_FINALIZE rax, rdx, _a0, _a1, _a2, gp6, gp7, gp8

.poly1305_mac_exit:
        FUNC_EXIT
        ret

;; =============================================================================
;; =============================================================================
;; void poly1305_aead_update_scalar(const void *msg, const uint64_t msg_len,
;;                                  void *hash, const void *key)
;; arg1 - message pointer
;; arg2 - message length in bytes
;; arg3 - pointer to current hash value (size 24 bytes)
;; arg4 - key pointer (size 32 bytes)
align 32
MKGLOBAL(poly1305_aead_update_scalar,function,internal)
poly1305_aead_update_scalar:

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
                        gp10, gp11, gp8, gp9, rax, rdx

        or      _len, _len
        jz      .poly1305_update_no_partial_block

        ;; create stack frame for the partial block scratch buffer
        sub     rsp, 16

        POLY1305_PARTIAL_BLOCK rsp, arg1, _len, _a0, _a1, _a2, _r0, _r1, \
                               gp10, gp11, gp8, gp9, rax, rdx, pad_to_16

        ;; remove the stack frame (memory is cleared as part of the macro)
        add     rsp, 16

.poly1305_update_no_partial_block:
        ;; save accumulator back
        mov     [_arg3 + 0 * 8], _a0
        mov     [_arg3 + 1 * 8], _a1
        mov     [_arg3 + 2 * 8], _a2

        FUNC_EXIT
.poly1305_update_exit:
        ret

;; =============================================================================
;; =============================================================================
;; void poly1305_aead_complete_scalar(const void *hash, const void *key, void *tag)
;; arg1 - pointer to current hash value (size 24 bytes)
;; arg2 - key pointer (size 32 bytes)
;; arg3 - pointer to store computed authentication tag (16 bytes)
align 32
MKGLOBAL(poly1305_aead_complete_scalar,function,internal)
poly1305_aead_complete_scalar:
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
        xor     rax, rax
        mov     [arg2], rax
        mov     [arg2 + 8], rax
        mov     [arg2 + 16], rax
        mov     [arg2 + 24], rax
%endif

        FUNC_EXIT
.poly1305_complete_exit:
        ret

mksection stack-noexec
