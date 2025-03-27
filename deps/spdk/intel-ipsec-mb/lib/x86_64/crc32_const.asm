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

%include "include/os.asm"

[bits 64]
default rel

mksection .rodata

;; SCTP CRC32 https://www.ietf.org/rfc/rfc3309.txt
;; 0x1edc6f41 (Castagnoli93)
align 64
MKGLOBAL(crc32_sctp_const,data,internal)
crc32_sctp_const:
        dq 0x000000004ef6a711, 0x00000000fa374b2e   ; 2048-b fold
        dq 0x00000000e78dbf1d, 0x000000005a47b20d   ; 1024-b fold
        dq 0x0000000079d09793, 0x00000000da9c52d0   ; 896-b fold
        dq 0x00000000ac594d98, 0x000000007def8667   ; 768-b fold
        dq 0x0000000038f8236c, 0x000000009a6aeb31   ; 640-b fold
        dq 0x00000000aa97d41d, 0x00000000a6955f31   ; 512-b fold
        dq 0x00000000e6957b4d, 0x00000000aa5eec4a   ; 384-b fold
        dq 0x0000000059a3508a, 0x000000007bba6798   ; 256-b fold
        dq 0x0000000018571d18, 0x000000006503ea99   ; 128-b fold
        dq 0x0000000000000000, 0x0000000000000000   ; padding (zero)
        dq 0xd7a0166500000000, 0x3aab457600000000   ; 128->64 reduction
        dq 0x000000011f91caf6, 0x000000011edc6f41   ; 64->32 reduction

;; 3GPP TS 36.212-880-Multiplexing and channel coding
;; LTE CRC24A polynomial 0x864CFB
align 64
MKGLOBAL(crc32_lte24_a_const,data,internal)
crc32_lte24_a_const:
        dq 0x00000000a79dfd00, 0x0000000009e45400   ; 2048-b fold
        dq 0x000000002e6a9100, 0x000000008a322000   ; 1024-b fold
        dq 0x0000000054e2ed00, 0x00000000fd99d400   ; 896-b fold
        dq 0x000000009f23d400, 0x000000006d688300   ; 768-b fold
        dq 0x00000000ae2a4900, 0x00000000e84f6300   ; 640-b fold
        dq 0x00000000467d2400, 0x000000001f428700   ; 512-b fold
        dq 0x000000005b703800, 0x000000006c1c3500   ; 384-b fold
        dq 0x000000009d89a200, 0x0000000066dd1f00   ; 256-b fold
        dq 0x0000000064e4d700, 0x000000002c8c9d00   ; 128-b fold
        dq 0x0000000000000000, 0x0000000000000000   ; padding
        dq 0xfd7e0c0000000000, 0xd9fe8c0000000000   ; 128->64 reduction
        dq 0x00000001f845fe24, 0x00000001864cfb00   ; 64->32 reduction

;; 3GPP TS 36.212-880-Multiplexing and channel coding
;; LTE CRC24B polynomial 0x800063
align 64
MKGLOBAL(crc32_lte24_b_const,data,internal)
crc32_lte24_b_const:
        dq 0x00000000427ce200, 0x00000000f4390500   ; 2048-b fold
        dq 0x00000000016d3800, 0x0000000078202200   ; 1024-b fold
        dq 0x00000000777d9800, 0x000000008d622b00   ; 896-b fold
        dq 0x0000000042004300, 0x000000000d562200   ; 768-b fold
        dq 0x00000000f95b0f00, 0x0000000005356d00   ; 640-b fold
        dq 0x00000000a0660100, 0x00000000b5015b00   ; 512-b fold
        dq 0x0000000089012300, 0x00000000a5686300   ; 384-b fold
        dq 0x0000000084560100, 0x000000005634d200   ; 256-b fold
        dq 0x0000000080140500, 0x0000000042000100   ; 128-b fold
        dq 0x0000000000000000, 0x0000000000000000   ; padding
        dq 0x0900020000000000, 0x9004210000000000   ; 128->64 reduction
        dq 0x00000001ffff83ff, 0x0000000180006300   ; 64->32 reduction

;; 3GPP TS 25.435, 3GPP TS 25.427
;; Framing Protocol CRC polynomial
;; CRC16 0x8005 for data
align 64
MKGLOBAL(crc32_fp_data_crc16_const,data,internal)
crc32_fp_data_crc16_const:
        dq 0x000000007f870000, 0x00000000fe630000   ; 2048-b fold
        dq 0x00000000fffb0000, 0x0000000086930000   ; 1024-b fold
        dq 0x000000000e5a0000, 0x00000000bf840000   ; 896-b fold
        dq 0x00000000871f0000, 0x000000006dd20000   ; 768-b fold
        dq 0x00000000ff070000, 0x0000000075530000   ; 640-b fold
        dq 0x00000000807d0000, 0x00000000f9e30000   ; 512-b fold
        dq 0x0000000007120000, 0x0000000063320000   ; 384-b fold
        dq 0x0000000000070000, 0x0000000087730000   ; 256-b fold
        dq 0x00000000ff830000, 0x00000000f9130000   ; 128-b fold
        dq 0x0000000000000000, 0x0000000000000000   ; padding
        dq 0x8663000000000000, 0x807b000000000000   ; 128->64 reduction
        dq 0x00000001fffbffe7, 0x0000000180050000   ; 64->32 reduction

;; 3GPP TS 25.435, 3GPP TS 25.427
;; Framing Protocol CRC polynomial
;; CRC11 0x307 for EDCH header
align 64
MKGLOBAL(crc32_fp_header_crc11_const,data,internal)
crc32_fp_header_crc11_const:
        dq 0x00000000cda00000, 0x00000000e4e00000   ; 2048-b fold
        dq 0x00000000d6a00000, 0x00000000c2000000   ; 1024-b fold
        dq 0x0000000010c00000, 0x00000000e8200000   ; 896-b fold
        dq 0x000000008c000000, 0x0000000097600000   ; 768-b fold
        dq 0x0000000018800000, 0x0000000093200000   ; 640-b fold
        dq 0x000000007c000000, 0x0000000051c00000   ; 512-b fold
        dq 0x000000005c200000, 0x000000001ac00000   ; 384-b fold
        dq 0x00000000b8800000, 0x0000000017e00000   ; 256-b fold
        dq 0x0000000004c00000, 0x00000000a0800000   ; 128-b fold
        dq 0x0000000000000000, 0x0000000000000000   ; padding
        dq 0xe140000000000000, 0x6100000000000000   ; 128->64 reduction
        dq 0x000000017208e3d8, 0x0000000160e00000   ; 64->32 reduction

;; 3GPP TS 25.435, 3GPP TS 25.427
;; Framing Protocol CRC polynomial
;; CRC7 0x45 for header
align 64
MKGLOBAL(crc32_fp_header_crc7_const,data,internal)
crc32_fp_header_crc7_const:
        dq 0x000000008a000000, 0x000000009e000000   ; 2048-b fold
        dq 0x00000000a8000000, 0x00000000da000000   ; 1024-b fold
        dq 0x000000002a000000, 0x0000000054000000   ; 896-b fold
        dq 0x0000000068000000, 0x00000000d0000000   ; 768-b fold
        dq 0x000000001a000000, 0x0000000034000000   ; 640-b fold
        dq 0x0000000064000000, 0x00000000c8000000   ; 512-b fold
        dq 0x00000000dc000000, 0x0000000032000000   ; 384-b fold
        dq 0x00000000f2000000, 0x000000006e000000   ; 256-b fold
        dq 0x000000005e000000, 0x00000000bc000000   ; 128-b fold
        dq 0x0000000000000000, 0x0000000000000000   ; padding
        dq 0x9e00000000000000, 0xea00000000000000   ; 128->64 reduction
        dq 0x00000001f79d6171, 0x000000018a000000   ; 64->32 reduction

;; 3GPP TS 25.415
;; IuUP CRC polynomial
;; CRC10 0x233 for data
align 64
MKGLOBAL(crc32_iuup_data_crc10_const,data,internal)
crc32_iuup_data_crc10_const:
        dq 0x00000000e5000000, 0x00000000fdc00000   ; 2048-b fold
        dq 0x0000000039400000, 0x000000005c400000   ; 1024-b fold
        dq 0x0000000020c00000, 0x0000000028800000   ; 896-b fold
        dq 0x0000000002c00000, 0x0000000099800000   ; 768-b fold
        dq 0x000000007ac00000, 0x00000000a9400000   ; 640-b fold
        dq 0x00000000dac00000, 0x00000000e8400000   ; 512-b fold
        dq 0x00000000d6000000, 0x0000000014400000   ; 384-b fold
        dq 0x00000000c7000000, 0x000000004cc00000   ; 256-b fold
        dq 0x00000000fb000000, 0x0000000092c00000   ; 128-b fold
        dq 0x0000000000000000, 0x0000000000000000   ; padding
        dq 0xa800000000000000, 0xb240000000000000   ; 128->64 reduction
        dq 0x00000001f083a337, 0x000000018cc00000   ; 64->32 reduction

;; 3GPP TS 25.415
;; IuUP CRC polynomial
;; CRC6 0x2f for header
align 64
MKGLOBAL(crc32_iuup_header_crc6_const,data,internal)
crc32_iuup_header_crc6_const:
        dq 0x00000000c4000000, 0x0000000068000000   ; 2048-b fold
        dq 0x00000000bc000000, 0x0000000034000000   ; 1024-b fold
        dq 0x0000000010000000, 0x0000000040000000   ; 896-b fold
        dq 0x00000000b0000000, 0x0000000004000000   ; 768-b fold
        dq 0x0000000064000000, 0x000000002c000000   ; 640-b fold
        dq 0x00000000f4000000, 0x00000000a8000000   ; 512-b fold
        dq 0x000000004c000000, 0x000000008c000000   ; 384-b fold
        dq 0x0000000070000000, 0x000000007c000000   ; 256-b fold
        dq 0x0000000068000000, 0x000000001c000000   ; 128-b fold
        dq 0x0000000000000000, 0x0000000000000000   ; padding
        dq 0x3400000000000000, 0xc400000000000000   ; 128->64 reduction
        dq 0x00000001c8af6983, 0x00000001bc000000   ; 64->32 reduction

;; WIMAX OFDMA DATA CRC32 0x4c11db7 (IEEE 802.16)
align 64
MKGLOBAL(crc32_wimax_ofdma_data_const,data,internal)
crc32_wimax_ofdma_data_const:
        dq 0x0000000088fe2237, 0x00000000cbcf3bcb   ; 2048-b fold
        dq 0x00000000567fddeb, 0x0000000010bd4d7c   ; 1024-b fold
        dq 0x000000003a06a4c6, 0x000000002ecc3300   ; 896-b fold
        dq 0x000000001d49ada7, 0x000000007606eeeb   ; 768-b fold
        dq 0x00000000f91a84e2, 0x00000000e2ca9d03   ; 640-b fold
        dq 0x00000000e6228b11, 0x000000008833794c   ; 512-b fold
        dq 0x000000008c3828a8, 0x0000000064bf7a9b   ; 384-b fold
        dq 0x0000000075be46b7, 0x00000000569700e5   ; 256-b fold
        dq 0x00000000e8a45605, 0x00000000c5b9cd4c   ; 128-b fold
        dq 0x0000000000000000, 0x0000000000000000   ; padding
        dq 0xf200aa6600000000, 0x490d678d00000000   ; 128->64 reduction
        dq 0x0000000104d101df, 0x0000000104c11db7   ; 64->32 reduction

;; WIMAX OFDMA HCS CRC8 0x07 (IEEE 802.16)
align 64
MKGLOBAL(crc32_wimax_ofdma_hcs8_const,data,internal)
crc32_wimax_ofdma_hcs8_const:
        dq 0x00000000d9000000, 0x00000000df000000   ; 2048-b fold
        dq 0x00000000f1000000, 0x0000000029000000   ; 1024-b fold
        dq 0x00000000fb000000, 0x0000000097000000   ; 896-b fold
        dq 0x00000000fe000000, 0x00000000c8000000   ; 768-b fold
        dq 0x000000007f000000, 0x0000000064000000   ; 640-b fold
        dq 0x00000000bc000000, 0x0000000032000000   ; 512-b fold
        dq 0x000000005e000000, 0x0000000019000000   ; 384-b fold
        dq 0x000000002f000000, 0x000000008f000000   ; 256-b fold
        dq 0x0000000094000000, 0x00000000c4000000   ; 128-b fold
        dq 0x0000000000000000, 0x0000000000000000   ; padding
        dq 0x7900000000000000, 0x6200000000000000   ; 128->64 reduction
        dq 0x0000000107156a16, 0x0000000107000000   ; 64->32 reduction

mksection stack-noexec
