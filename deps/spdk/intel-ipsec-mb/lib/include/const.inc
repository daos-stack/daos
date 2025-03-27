;;
;; Copyright (c) 2018-2021, Intel Corporation
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

%ifndef _CONST_INC_
%define _CONST_INC_

align 16
len_shift_dword_tab:
        db 0x00, 0x01, 0x02, 0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        db 0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0x02, 0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        db 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0x02, 0x03, 0xff, 0xff, 0xff, 0xff,
        db 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0x02, 0x03,

;;; Table used to zero index
align 16
len_mask_dword_tab:
        dw 0x0000, 0x0000, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
        dw 0xffff, 0xffff, 0x0000, 0x0000, 0xffff, 0xffff, 0xffff, 0xffff,
        dw 0xffff, 0xffff, 0xffff, 0xffff, 0x0000, 0x0000, 0xffff, 0xffff,
        dw 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0x0000, 0x0000,

%define len_tab_dword_diff 64

;;; Tables used to insert word into a SIMD register
extern len_shift_tab
extern len_mask_tab
extern shift_tab_16

;;; Table to do 0x80 byte shift for padding prefix
extern padding_0x80_tab16

;;; Size of len_shift_tab defined in const.asm module
%define len_tab_diff 128

; PINSRW_COMMON insert word into 128 bit SIMD register
%macro PINSRW_COMMON 7

%define %%type          %1 ; instruction type - sse or avx
%define %%dest          %2 ; dest XMM reg to insert word
%define %%tmp_simd      %3 ; XMM reg to clobber
%define %%tmp_gp        %4 ; GP reg to clobber
%define %%idx           %5 ; word index to insert value into XMM
%define %%val           %6 ; word value to insert into idx
%define %%scale_idx     %7 ; flag to set if index is to be scaled x16

%ifidn  %%scale_idx, scale_x16
        shl     %%idx, 4     ; scale idx up x16
%endif
%ifnum  %%val
        ;; immediate value passed on
        mov     DWORD(%%tmp_gp), %%val
%ifidn  %%type, sse
        movd    %%tmp_simd, DWORD(%%tmp_gp)
%else
        vmovd   %%tmp_simd, DWORD(%%tmp_gp)
%endif
%else
        ;; register name passed on
%ifidn  %%type, sse
        movd    %%tmp_simd, DWORD(%%val)
%else
        vmovd   %%tmp_simd, DWORD(%%val)
%endif
%endif
        lea     %%tmp_gp, [rel len_shift_tab]
        ;; check type - SSE or AVX
%ifidn  %%type, sse
        pshufb  %%tmp_simd, [%%tmp_gp + %%idx]
        pand    %%dest, [%%tmp_gp + len_tab_diff + %%idx]
        por     %%dest, %%tmp_simd
%else
        vpshufb %%tmp_simd, [%%tmp_gp + %%idx]
        vpand   %%dest, [%%tmp_gp + len_tab_diff + %%idx]
        vpor    %%dest, %%tmp_simd
%endif
%ifidn  %%scale_idx, scale_x16
        shr     %%idx, 4     ; reset idx
%endif
%endmacro

;;; Call SSE macro
%define XPINSRW PINSRW_COMMON sse,

;;; Call AVX macro
%define XVPINSRW PINSRW_COMMON avx,

;;; VPINSRW_M256 insert word into 32 byte memory range
%macro VPINSRW_M256 7

%define %%mem_addr      %1 ; 16 byte aligned memory address to insert word
%define %%tmp_simd1     %2 ; XMM reg to clobber
%define %%tmp_simd2     %3 ; XMM reg to clobber
%define %%tmp_gp        %4 ; GP reg to clobber
%define %%idx           %5 ; word index to insert value
%define %%val           %6 ; word value to insert into idx
%define %%scale_idx     %7 ; flag to set if index is to be scaled x16

        cmp     %%idx, 8
        jl      %%lower_128
        sub     %%idx, 8
        vmovdqa %%tmp_simd1, [%%mem_addr + 2*8]
        XVPINSRW %%tmp_simd1, %%tmp_simd2, %%tmp_gp, %%idx, %%val, %%scale_idx
        vmovdqa [%%mem_addr + 2*8], %%tmp_simd1
        add     %%idx, 8
        jmp     %%exit
%%lower_128:
        vmovdqa %%tmp_simd1, [%%mem_addr]
        XVPINSRW %%tmp_simd1, %%tmp_simd2, %%tmp_gp, %%idx, %%val, %%scale_idx
        vmovdqa [%%mem_addr], %%tmp_simd1
%%exit:
%endmacro

; PINSRD_COMMON insert dword into 128 bit SIMD register
%macro PINSRD_COMMON 7

%define %%type          %1 ; instruction type - sse or avx
%define %%dest          %2 ; dest XMM reg to insert word
%define %%tmp_simd      %3 ; XMM reg to clobber
%define %%tmp_gp        %4 ; GP reg to clobber
%define %%idx           %5 ; dword index to insert value into XMM
%define %%val           %6 ; dword value to insert into idx
%define %%scale_idx     %7 ; flag to set if index is to be scaled x16

%ifidn  %%scale_idx, scale_x16
        shl     %%idx, 4     ; scale idx up x16
%endif
%ifnum  %%val
        ;; immediate value passed on
        mov     DWORD(%%tmp_gp), %%val
%ifidn  %%type, sse
        movd    %%tmp_simd, DWORD(%%tmp_gp)
%else
        vmovd   %%tmp_simd, DWORD(%%tmp_gp)
%endif
%else
        ;; register name passed on
%ifidn  %%type, sse
        movd    %%tmp_simd, DWORD(%%val)
%else
        vmovd   %%tmp_simd, DWORD(%%val)
%endif
%endif
        lea     %%tmp_gp, [rel len_shift_dword_tab]
        ;; check type - SSE or AVX
%ifidn  %%type, sse
        pshufb  %%tmp_simd, [%%tmp_gp + %%idx]
        pand    %%dest, [%%tmp_gp + len_tab_dword_diff + %%idx]
        por     %%dest, %%tmp_simd
%else
        vpshufb %%tmp_simd, [%%tmp_gp + %%idx]
        vpand   %%dest, [%%tmp_gp + len_tab_dword_diff + %%idx]
        vpor    %%dest, %%tmp_simd
%endif
%ifidn  %%scale_idx, scale_x16
        shr     %%idx, 4     ; reset idx
%endif
%endmacro

;;; Call SSE macro
%define XPINSRD PINSRD_COMMON sse,

;;; Call AVX macro
%define XVPINSRD PINSRD_COMMON avx,

;;; VPINSRD_M256 insert dword into 32 byte memory range
%macro VPINSRD_M256 7

%define %%mem_addr      %1 ; 16 byte aligned memory address to insert word
%define %%tmp_simd1     %2 ; XMM reg to clobber
%define %%tmp_simd2     %3 ; XMM reg to clobber
%define %%tmp_gp        %4 ; GP reg to clobber
%define %%idx           %5 ; dword index to insert value
%define %%val           %6 ; dword value to insert into idx
%define %%scale_idx     %7 ; flag to set if index is to be scaled x16

%ifidn  %%scale_idx, scale_x16
        cmp     %%idx, 4
        jl      %%lower_128
        sub     %%idx, 4
%else ;; idx is multiplied by 16
        cmp     %%idx, 64
        jl      %%lower_128
        sub     %%idx, 64
%endif
        vmovdqa %%tmp_simd1, [%%mem_addr + 2*8]
        XVPINSRD %%tmp_simd1, %%tmp_simd2, %%tmp_gp, %%idx, %%val, %%scale_idx
        vmovdqa [%%mem_addr + 2*8], %%tmp_simd1
%ifidn  %%scale_idx, scale_x16
        add     %%idx, 4
%else
        add     %%idx, 64
%endif
        jmp     %%exit
%%lower_128:
        vmovdqa %%tmp_simd1, [%%mem_addr]
        XVPINSRD %%tmp_simd1, %%tmp_simd2, %%tmp_gp, %%idx, %%val, %%scale_idx
        vmovdqa [%%mem_addr], %%tmp_simd1
%%exit:
%endmacro

;;; PSLB_COMMON shift bytes 128 bit SIMD register
%macro PSLB_COMMON 6

%define %%type          %1 ; [in] instruction type - sse or avx
%define %%dir           %2 ; [in] shift direction - left or right
%define %%reg           %3 ; [in/out] XMM reg to shift bytes
%define %%num           %4 ; [in] GP reg containing number of bytes to shift
%define %%shuf_tab      %5 ; [clobbered] XMM reg to store shuffle table
%define %%tmp_gp        %6 ; [clobbered] GP reg to clobber

        ;; load shift table into %%shuf_tab
        lea     %%tmp_gp, [rel shift_tab_16 + 16]
%ifidn %%dir, left
        sub     %%tmp_gp, %%num
%else
        add     %%tmp_gp, %%num
%endif

%ifidn  %%type, sse
        movdqu  %%shuf_tab, [%%tmp_gp]
        pshufb  %%reg, %%shuf_tab
%else
        vmovdqu %%shuf_tab, [%%tmp_gp]
        vpshufb %%reg, %%shuf_tab
%endif
%endmacro

;;; Call SSE left shift macro
%macro XPSLLB 4
        PSLB_COMMON sse, left, %1,%2,%3,%4
%endm

;;; Call SSE right shift macro
%macro XPSRLB 4
        PSLB_COMMON sse, right, %1,%2,%3,%4
%endm

;;; Call AVX left shift macro
%macro XVPSLLB 4
        PSLB_COMMON avx, left, %1,%2,%3,%4
%endm

;;; Call AVX right shift macro
%macro XVPSRLB 4
        PSLB_COMMON avx, right, %1,%2,%3,%4
%endm

;; Variable shift GP register
%macro SHIFT_GP 5
%define %%VAL           %1 ;; [in] GP reg or imm value to be shifted
%define %%NBITS         %2 ;; [in] GP reg with number of bits to be shifted
%define %%OUT           %3 ;; [out] GP containing shifted result
%define %%TMP           %4 ;; [clobbered] tmp GP to save rcx
%define %%DIR           %5 ;; [in] direction to shift "left" or "right"

        mov     %%TMP, rcx       ; save rcx

        mov     rcx, %%NBITS
        mov     %%OUT, %%VAL
%ifidn %%DIR, left
        shl     %%OUT, cl
%else
        shr     %%OUT, cl
%endif

        mov     rcx, %%TMP       ; restore rcx
%endmacro

%endif ; end ifndef _CONST_INC_
