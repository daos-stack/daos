#
# Copyright (c) 2012-2021, Intel Corporation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#     * Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of Intel Corporation nor the names of its contributors
#       may be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

LIB = libIPSec_MB
SHARED ?= y
IMB_HDR = intel-ipsec-mb.h

# Detect library version
IMB_VERSION = $(shell grep -e "IMB_VERSION_STR" $(IMB_HDR) | cut -d'"' -f2)
ifeq ($(IMB_VERSION),)
$(error "Failed to detect library version!")
endif

VERSION = $(shell echo $(IMB_VERSION) | cut -d. -f1-3)
SO_VERSION = $(shell echo $(VERSION) | cut -d. -f1)

PREFIX ?= /usr
LIB_INSTALL_DIR ?= $(PREFIX)/lib
HDR_DIR ?= $(PREFIX)/include
MAN_DIR ?= $(PREFIX)/man/man7
MAN1 = libipsec-mb.7
MAN2 = libipsec-mb-dev.7
NOLDCONFIG ?= n

USE_YASM ?= n
YASM ?= yasm
NASM ?= nasm

# Detect NASM version (minimum version required: 2.14)
NASM_VERSION = $(shell $(NASM) -v | cut -d " " -f 3)

NASM_MAJOR_REQ = 2
NASM_MINOR_REQ = 14

ifeq ($(NASM_VERSION),)
$(error "NASM is not installed! Minimum required version: $(NASM_MAJOR_REQ).$(NASM_MINOR_REQ)")
else
NASM_MAJOR_VER = $(shell echo $(NASM_VERSION) | cut -d "." -f 1)
NASM_MINOR_VER = $(shell echo $(NASM_VERSION) | cut -d "." -f 2 | cut -c 1-2)
NASM_GE_MAJOR = $(shell [ $(NASM_MAJOR_VER) -ge $(NASM_MAJOR_REQ) ] && echo true)
NASM_GE_MINOR = $(shell [ $(NASM_MINOR_VER) -ge $(NASM_MINOR_REQ) ] && echo true)
ifneq ($(NASM_GE_MAJOR),true)
$(warning "NASM version found: $(NASM_VERSION)")
$(error "Minimum required: $(NASM_MAJOR_REQ).$(NASM_MINOR_REQ)")
endif
ifneq ($(NASM_GE_MINOR),true)
$(warning "NASM version found: $(NASM_VERSION)")
$(error "Minimum required: $(NASM_MAJOR_REQ).$(NASM_MINOR_REQ)")
endif
endif

OBJ_DIR ?= obj
LIB_DIR ?= .

INCLUDE_DIRS := include . no-aesni
INCLUDES := $(foreach i,$(INCLUDE_DIRS),-I $i)

CC ?= gcc
# MINGW should be non-zero value if detected
MINGW ?= $(shell $(CC) -dM -E - < /dev/null | grep -i mingw | wc -l)

# if "-z ibt" is supported then assume "-z shstk, -z cet-report=error" are also supported
# "-fcf-protection" needs to be checked separately
ifeq ($(MINGW),0)
CC_HAS_CET = $(and $(shell $(CC) --target-help 2> /dev/null | grep -m1 -e "-z ibt" | wc -l), \
	$(shell $(CC) --help=common 2> /dev/null | grep -m1 -e "-fcf-protection" | wc -l))
CET_LDFLAGS=-r -z ibt -z shstk
endif
CFLAGS := -DNO_COMPAT_IMB_API_053 $(EXTRA_CFLAGS) $(INCLUDES) \
	-W -Wall -Wextra -Wmissing-declarations -Wpointer-arith \
	-Wcast-qual -Wundef -Wwrite-strings  \
	-Wformat -Wformat-security \
	-Wunreachable-code -Wmissing-noreturn -Wsign-compare -Wno-endif-labels \
	-Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition \
	-fno-strict-overflow -fno-delete-null-pointer-checks -fwrapv

ifeq ($(MINGW),0)
CFLAGS += -DLINUX
endif

ifeq ($(CC_HAS_CET),1)
CFLAGS += -fcf-protection=full
endif

ASM_INCLUDE_DIRS := .

YASM_INCLUDES := $(foreach i,$(ASM_INCLUDE_DIRS),-I $i)
NASM_INCLUDES := $(foreach i,$(ASM_INCLUDE_DIRS),-I$i/)
ifneq ($(MINGW),0)
YASM_FLAGS := -f x64 -f win64 -X gnu -g dwarf2 -DWIN_ABI $(YASM_INCLUDES)
NASM_FLAGS := -Werror -fwin64 -Xvc -gcv8 -DWIN_ABI $(NASM_INCLUDES)
else
YASM_FLAGS := -f x64 -f elf64 -X gnu -g dwarf2 -DLINUX -D__linux__ $(YASM_INCLUDES)
NASM_FLAGS := -Werror -felf64 -Xgnu -gdwarf -DLINUX -D__linux__ $(NASM_INCLUDES)
endif

DEBUG_OPT ?= -O0
ifeq ($(DEBUG),y)
CFLAGS += -g -DDEBUG
OPT = $(DEBUG_OPT)
LDFLAGS += -g
else
ifeq ($(MINGW),0)
OPT = -O3
CFLAGS += -fstack-protector -D_FORTIFY_SOURCE=2
else
OPT = -O2
LDFLAGS += -s
endif
endif

ifneq ($(SAFE_DATA),n)
CFLAGS += -DSAFE_DATA
NASM_FLAGS += -DSAFE_DATA
YASM_FLAGS += -DSAFE_DATA
endif

ifneq ($(SAFE_PARAM),n)
CFLAGS += -DSAFE_PARAM
NASM_FLAGS += -DSAFE_PARAM
YASM_FLAGS += -DSAFE_PARAM
endif

ifneq ($(SAFE_LOOKUP),n)
CFLAGS += -DSAFE_LOOKUP
NASM_FLAGS += -DSAFE_LOOKUP
YASM_FLAGS += -DSAFE_LOOKUP
endif

# prevent SIMD optimizations for non-aesni modules
CFLAGS_NO_SIMD = $(CFLAGS) -O1
CFLAGS += $(OPT)

# Set generic architectural optimizations
OPT_X86 := -msse4.2
OPT_SSE := -msse4.2 -maes -mpclmul
OPT_AVX := -mavx -maes -mpclmul
OPT_AVX2 := -mavx2 -maes -mpclmul
OPT_AVX512 := -mavx2 -maes -mpclmul # -mavx512f is not available until gcc 4.9
OPT_NOAESNI := -msse4.2 -mno-aes -mno-pclmul

# Set architectural optimizations for GCC/CC
ifeq ($(CC),$(filter $(CC),gcc cc))
GCC_VERSION = $(shell $(CC) -dumpversion | cut -d. -f1)
GCC_GE_V5 = $(shell [ $(GCC_VERSION) -ge 5 ] && echo true)
ifeq ($(GCC_GE_V5),true)
OPT_SSE := -march=nehalem -maes -mpclmul
OPT_AVX := -march=sandybridge -maes -mpclmul
OPT_AVX2 := -march=haswell -maes -mpclmul
OPT_AVX512 := -march=broadwell -maes -mpclmul
OPT_NOAESNI := -march=nehalem -mno-pclmul
endif
endif

# Set architectural optimizations for clang
ifeq ($(CC),$(filter $(CC),clang))
CLANG_VERSION = $(shell $(CC) --version | head -n 1 | cut -d ' ' -f 3)
CLANG_GE_V381 = $(shell test "$(CLANG_VERSION)" \> "3.8.0" && echo true)
ifeq ($(CLANG_GE_V381),true)
OPT_SSE := -march=nehalem -maes -mpclmul
OPT_AVX := -march=sandybridge -maes -mpclmul
OPT_AVX2 := -march=haswell -maes -mpclmul
OPT_AVX512 := -march=broadwell -maes -mpclmul
endif
# remove CFLAGS that clang warns about
CFLAGS := $(subst -fno-delete-null-pointer-checks,,$(CFLAGS))
CFLAGS := $(subst -fno-strict-overflow,,$(CFLAGS))
endif

# so or static build
ifeq ($(SHARED),y)
CFLAGS += -fPIC
ifneq ($(MINGW),0)
LIBNAME = $(LIB).dll
else
LIBNAME = $(LIB).so.$(VERSION)
LDFLAGS += -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now
endif
LIBPERM = 0755
ifeq ($(CC_HAS_CET),1)
LDFLAGS += -fcf-protection=full -Wl,-z,ibt -Wl,-z,shstk -Wl,-z,cet-report=error
endif
else
# SPDK crypto needs shared but -fPIC so to make it clear what
# we are doing, we just comment out the CFLAGS below and
# explicitly set them in our relevant makefile.
#CFLAGS += -fPIE
LIBNAME = $(LIB).a
LIBPERM = 0644
LDFLAGS += -g
ifeq ($(CC_HAS_CET),1)
LDFLAGS += -fcf-protection=full
endif
endif

# warning messages
SAFE_PARAM_MSG1="SAFE_PARAM option not set."
SAFE_PARAM_MSG2="Input parameters will not be checked."
SAFE_DATA_MSG1="SAFE_DATA option not set."
SAFE_DATA_MSG2="Stack and registers containing sensitive information, \
		such keys or IV will not be cleared \
		at the end of function calls."
SAFE_LOOKUP_MSG1="SAFE_LOOKUP option not set."
SAFE_LOOKUP_MSG2="Lookups which depend on sensitive information \
		are not guaranteed to be done in constant time."

#
# List of C modules (any origin)
#
c_lib_objs := \
	mb_mgr_avx.o \
	mb_mgr_avx2.o \
	mb_mgr_avx512.o \
	mb_mgr_sse.o \
	mb_mgr_sse_no_aesni.o \
	alloc.o \
	aes_xcbc_expand_key.o \
	md5_one_block.o \
	sha_sse.o \
	sha_avx.o \
	des_key.o \
	des_basic.o \
	version.o \
	cpu_feature.o \
	aesni_emu.o \
	kasumi_avx.o \
	kasumi_iv.o \
	kasumi_sse.o \
	zuc_sse_top.o \
	zuc_sse_no_aesni_top.o \
	zuc_avx_top.o \
	zuc_avx2_top.o \
	zuc_avx512_top.o \
	zuc_iv.o \
	snow3g_sse.o \
	snow3g_sse_no_aesni.o \
	snow3g_avx.o \
	snow3g_avx2.o \
	snow3g_avx512.o \
	snow3g_tables.o \
	snow3g_iv.o \
	snow_v_sse.o \
	snow_v_sse_noaesni.o \
	snow_v_avx.o \
	mb_mgr_auto.o \
	error.o
#
# List of ASM modules (root directory/common)
#
asm_generic_lib_objs := \
	aes_keyexp_128.o \
	aes_keyexp_192.o \
	aes_keyexp_256.o \
	aes_cmac_subkey_gen.o \
	save_xmms.o \
	clear_regs_mem_fns.o \
	const.o \
	aes128_ecbenc_x3.o \
	zuc_common.o \
	wireless_common.o \
	constant_lookup_fns.o \
	crc32_refl_const.o \
	crc32_const.o \
	poly1305.o \
	chacha20_poly1305.o

#
# List of ASM modules (no-aesni directory)
#
asm_noaesni_lib_objs := \
	aes128_cbc_dec_by4_sse_no_aesni.o \
	aes192_cbc_dec_by4_sse_no_aesni.o \
	aes256_cbc_dec_by4_sse_no_aesni.o \
	aes_cbc_enc_128_x4_no_aesni.o \
	aes_cbc_enc_192_x4_no_aesni.o \
	aes_cbc_enc_256_x4_no_aesni.o \
	aes128_cntr_by8_sse_no_aesni.o \
	aes192_cntr_by8_sse_no_aesni.o \
	aes256_cntr_by8_sse_no_aesni.o \
	aes_ecb_by4_sse_no_aesni.o \
	aes128_cntr_ccm_by8_sse_no_aesni.o \
	aes256_cntr_ccm_by8_sse_no_aesni.o \
	pon_sse_no_aesni.o \
	zuc_sse_no_aesni.o \
	aes_cfb_sse_no_aesni.o \
	aes128_cbc_mac_x4_no_aesni.o \
	aes256_cbc_mac_x4_no_aesni.o \
	aes_xcbc_mac_128_x4_no_aesni.o \
	mb_mgr_aes_flush_sse_no_aesni.o \
	mb_mgr_aes_submit_sse_no_aesni.o \
	mb_mgr_aes192_flush_sse_no_aesni.o \
	mb_mgr_aes192_submit_sse_no_aesni.o \
	mb_mgr_aes256_flush_sse_no_aesni.o \
	mb_mgr_aes256_submit_sse_no_aesni.o \
	mb_mgr_aes_cmac_submit_flush_sse_no_aesni.o \
	mb_mgr_aes256_cmac_submit_flush_sse_no_aesni.o \
	mb_mgr_aes_ccm_auth_submit_flush_sse_no_aesni.o \
	mb_mgr_aes256_ccm_auth_submit_flush_sse_no_aesni.o \
	mb_mgr_aes_xcbc_flush_sse_no_aesni.o \
	mb_mgr_aes_xcbc_submit_sse_no_aesni.o \
	mb_mgr_zuc_submit_flush_sse_no_aesni.o \
	ethernet_fcs_sse_no_aesni.o \
	crc16_x25_sse_no_aesni.o \
	aes_cbcs_1_9_enc_128_x4_no_aesni.o \
	aes128_cbcs_1_9_dec_by4_sse_no_aesni.o \
	mb_mgr_aes128_cbcs_1_9_submit_sse.o \
	mb_mgr_aes128_cbcs_1_9_flush_sse.o \
	mb_mgr_aes128_cbcs_1_9_submit_sse_no_aesni.o \
	mb_mgr_aes128_cbcs_1_9_flush_sse_no_aesni.o \
	crc32_refl_by8_sse_no_aesni.o \
	crc32_by8_sse_no_aesni.o \
	crc32_sctp_sse_no_aesni.o \
	crc32_lte_sse_no_aesni.o \
	crc32_fp_sse_no_aesni.o \
	crc32_iuup_sse_no_aesni.o \
	crc32_wimax_sse_no_aesni.o \
	snow3g_uia2_sse_no_aesni.o

#
# List of ASM modules (sse directory)
#
asm_sse_lib_objs := \
	aes128_cbc_dec_by4_sse.o \
	aes128_cbc_dec_by8_sse.o \
	aes192_cbc_dec_by4_sse.o \
	aes192_cbc_dec_by8_sse.o \
	aes256_cbc_dec_by4_sse.o \
	aes256_cbc_dec_by8_sse.o \
	aes_cbc_enc_128_x4.o \
	aes_cbc_enc_192_x4.o \
	aes_cbc_enc_256_x4.o \
	aes_cbc_enc_128_x8_sse.o \
	aes_cbc_enc_192_x8_sse.o \
	aes_cbc_enc_256_x8_sse.o \
	pon_by8_sse.o \
	aes128_cntr_by8_sse.o \
	aes192_cntr_by8_sse.o \
	aes256_cntr_by8_sse.o \
	aes128_ecb_by4_sse.o \
	aes192_ecb_by4_sse.o \
	aes256_ecb_by4_sse.o \
	aes128_cntr_ccm_by8_sse.o \
	aes256_cntr_ccm_by8_sse.o \
	aes_cfb_sse.o \
	aes128_cbc_mac_x4.o \
	aes256_cbc_mac_x4.o \
	aes128_cbc_mac_x8_sse.o \
	aes256_cbc_mac_x8_sse.o \
	aes128_xcbc_mac_x4_sse.o \
	md5_x4x2_sse.o \
	sha1_x4_sse.o \
	sha1_one_block_sse.o \
	sha224_one_block_sse.o \
	sha256_one_block_sse.o \
	sha384_one_block_sse.o \
	sha512_one_block_sse.o \
	sha512_x2_sse.o \
	sha_256_mult_sse.o \
	sha1_ni_x2_sse.o \
	sha256_ni_x2_sse.o \
	zuc_x4_sse.o \
	zuc_sse_gfni.o \
	mb_mgr_aes_flush_sse.o \
	mb_mgr_aes_submit_sse.o \
	mb_mgr_aes192_flush_sse.o \
	mb_mgr_aes192_submit_sse.o \
	mb_mgr_aes256_flush_sse.o \
	mb_mgr_aes256_submit_sse.o \
	mb_mgr_aes_flush_sse_x8.o \
	mb_mgr_aes_submit_sse_x8.o \
	mb_mgr_aes192_flush_sse_x8.o \
	mb_mgr_aes192_submit_sse_x8.o \
	mb_mgr_aes256_flush_sse_x8.o \
	mb_mgr_aes256_submit_sse_x8.o \
	mb_mgr_aes128_cmac_x4_submit_flush_sse.o \
	mb_mgr_aes256_cmac_x4_submit_flush_sse.o \
	mb_mgr_aes_cmac_submit_flush_sse_x8.o \
	mb_mgr_aes256_cmac_submit_flush_sse_x8.o \
	mb_mgr_aes128_ccm_by8_auth_submit_flush_sse.o \
	mb_mgr_aes_ccm_auth_submit_flush_sse_x8.o \
	mb_mgr_aes256_ccm_by8_auth_submit_flush_sse.o \
	mb_mgr_aes256_ccm_auth_submit_flush_sse_x8.o \
	mb_mgr_aes128_xcbc_x4_flush_sse.o \
	mb_mgr_aes128_xcbc_x4_submit_sse.o \
	mb_mgr_hmac_md5_flush_sse.o \
	mb_mgr_hmac_md5_submit_sse.o \
	mb_mgr_hmac_flush_sse.o \
	mb_mgr_hmac_submit_sse.o \
	mb_mgr_hmac_sha_224_flush_sse.o \
	mb_mgr_hmac_sha_224_submit_sse.o \
	mb_mgr_hmac_sha_256_flush_sse.o \
	mb_mgr_hmac_sha_256_submit_sse.o \
	mb_mgr_hmac_sha_384_flush_sse.o \
	mb_mgr_hmac_sha_384_submit_sse.o \
	mb_mgr_hmac_sha_512_flush_sse.o \
	mb_mgr_hmac_sha_512_submit_sse.o \
	mb_mgr_hmac_flush_ni_sse.o \
	mb_mgr_hmac_submit_ni_sse.o \
	mb_mgr_hmac_sha_224_flush_ni_sse.o \
	mb_mgr_hmac_sha_224_submit_ni_sse.o \
	mb_mgr_hmac_sha_256_flush_ni_sse.o \
	mb_mgr_hmac_sha_256_submit_ni_sse.o \
	mb_mgr_zuc_submit_flush_sse.o \
	mb_mgr_zuc_submit_flush_gfni_sse.o \
	ethernet_fcs_sse.o \
	crc16_x25_sse.o \
	crc32_sctp_sse.o \
	aes_cbcs_1_9_enc_128_x4.o \
	aes128_cbcs_1_9_dec_by4_sse.o \
	crc32_refl_by8_sse.o \
	crc32_by8_sse.o \
	crc32_lte_sse.o \
	crc32_fp_sse.o \
	crc32_iuup_sse.o \
	crc32_wimax_sse.o \
	chacha20_sse.o \
        memcpy_sse.o \
	snow3g_uia2_by4_sse.o

#
# List of ASM modules (avx directory)
#
asm_avx_lib_objs := \
	aes_cbc_enc_128_x8.o \
	aes_cbc_enc_192_x8.o \
	aes_cbc_enc_256_x8.o \
	aes128_cbc_dec_by8_avx.o \
	aes192_cbc_dec_by8_avx.o \
	aes256_cbc_dec_by8_avx.o \
	pon_by8_avx.o \
	aes128_cntr_by8_avx.o \
	aes192_cntr_by8_avx.o \
	aes256_cntr_by8_avx.o \
	aes128_cntr_ccm_by8_avx.o \
	aes256_cntr_ccm_by8_avx.o \
	aes128_ecb_by4_avx.o \
	aes192_ecb_by4_avx.o \
	aes256_ecb_by4_avx.o \
	aes_cfb_avx.o \
	aes128_cbc_mac_x8.o \
	aes256_cbc_mac_x8.o \
	aes128_xcbc_mac_x8_avx.o \
	md5_x4x2_avx.o \
	sha1_x4_avx.o \
	sha1_one_block_avx.o \
	sha224_one_block_avx.o \
	sha256_one_block_avx.o \
	sha_256_mult_avx.o \
	sha384_one_block_avx.o \
	sha512_one_block_avx.o \
	sha512_x2_avx.o \
	zuc_x4_avx.o \
	mb_mgr_aes_flush_avx.o \
	mb_mgr_aes_submit_avx.o \
	mb_mgr_aes192_flush_avx.o \
	mb_mgr_aes192_submit_avx.o \
	mb_mgr_aes256_flush_avx.o \
	mb_mgr_aes256_submit_avx.o \
	mb_mgr_aes128_cmac_x8_submit_flush_avx.o \
	mb_mgr_aes256_cmac_x8_submit_flush_avx.o \
	mb_mgr_aes128_ccm_by8_auth_submit_flush_avx.o \
	mb_mgr_aes256_ccm_by8_auth_submit_flush_avx.o \
	mb_mgr_aes128_xcbc_x8_flush_avx.o \
	mb_mgr_aes128_xcbc_x8_submit_avx.o \
	mb_mgr_hmac_md5_flush_avx.o \
	mb_mgr_hmac_md5_submit_avx.o \
	mb_mgr_hmac_flush_avx.o \
	mb_mgr_hmac_submit_avx.o \
	mb_mgr_hmac_sha_224_flush_avx.o \
	mb_mgr_hmac_sha_224_submit_avx.o \
	mb_mgr_hmac_sha_256_flush_avx.o \
	mb_mgr_hmac_sha_256_submit_avx.o \
	mb_mgr_hmac_sha_384_flush_avx.o \
	mb_mgr_hmac_sha_384_submit_avx.o \
	mb_mgr_hmac_sha_512_flush_avx.o \
	mb_mgr_hmac_sha_512_submit_avx.o \
	mb_mgr_zuc_submit_flush_avx.o \
	ethernet_fcs_avx.o \
	crc16_x25_avx.o \
	aes_cbcs_1_9_enc_128_x8.o \
	aes128_cbcs_1_9_dec_by8_avx.o \
	mb_mgr_aes128_cbcs_1_9_submit_avx.o \
	mb_mgr_aes128_cbcs_1_9_flush_avx.o \
	crc32_refl_by8_avx.o \
	crc32_by8_avx.o \
	crc32_sctp_avx.o \
	crc32_lte_avx.o \
	crc32_fp_avx.o \
	crc32_iuup_avx.o \
	crc32_wimax_avx.o \
	chacha20_avx.o \
        memcpy_avx.o \
	snow3g_uia2_by4_avx.o

#
# List of ASM modules (avx2 directory)
#
asm_avx2_lib_objs := \
	md5_x8x2_avx2.o \
	sha1_x8_avx2.o \
	sha256_oct_avx2.o \
	sha512_x4_avx2.o \
	zuc_x8_avx2.o \
	mb_mgr_hmac_md5_flush_avx2.o \
	mb_mgr_hmac_md5_submit_avx2.o \
	mb_mgr_hmac_flush_avx2.o \
	mb_mgr_hmac_submit_avx2.o \
	mb_mgr_hmac_sha_224_flush_avx2.o \
	mb_mgr_hmac_sha_224_submit_avx2.o \
	mb_mgr_hmac_sha_256_flush_avx2.o \
	mb_mgr_hmac_sha_256_submit_avx2.o \
	mb_mgr_hmac_sha_384_flush_avx2.o \
	mb_mgr_hmac_sha_384_submit_avx2.o \
	mb_mgr_hmac_sha_512_flush_avx2.o \
	mb_mgr_hmac_sha_512_submit_avx2.o \
	mb_mgr_zuc_submit_flush_avx2.o \
	chacha20_avx2.o

#
# List of ASM modules (avx512 directory)
#
asm_avx512_lib_objs := \
	sha1_x16_avx512.o \
	sha256_x16_avx512.o \
	sha512_x8_avx512.o \
	des_x16_avx512.o \
	aes_cntr_by16_vaes_avx512.o \
	pon_vaes_avx512.o \
	aes_cbc_dec_by16_vaes_avx512.o \
	aes_cbc_enc_vaes_avx512.o \
	aes_cbcs_enc_vaes_avx512.o \
	aes_cbcs_dec_by16_vaes_avx512.o \
	aes_docsis_dec_avx512.o \
	aes_docsis_enc_avx512.o \
	aes_docsis_dec_vaes_avx512.o \
	aes_docsis_enc_vaes_avx512.o \
	zuc_x16_avx512.o \
	zuc_x16_vaes_avx512.o \
	mb_mgr_aes_submit_avx512.o \
	mb_mgr_aes_flush_avx512.o \
	mb_mgr_aes192_submit_avx512.o \
	mb_mgr_aes192_flush_avx512.o \
	mb_mgr_aes256_submit_avx512.o \
	mb_mgr_aes256_flush_avx512.o \
	mb_mgr_hmac_flush_avx512.o \
	mb_mgr_hmac_submit_avx512.o \
	mb_mgr_hmac_sha_224_flush_avx512.o \
	mb_mgr_hmac_sha_224_submit_avx512.o \
	mb_mgr_hmac_sha_256_flush_avx512.o \
	mb_mgr_hmac_sha_256_submit_avx512.o \
	mb_mgr_hmac_sha_384_flush_avx512.o \
	mb_mgr_hmac_sha_384_submit_avx512.o \
	mb_mgr_hmac_sha_512_flush_avx512.o \
	mb_mgr_hmac_sha_512_submit_avx512.o \
	mb_mgr_des_avx512.o \
	mb_mgr_aes128_cmac_x16_submit_flush_vaes_avx512.o \
	mb_mgr_aes256_cmac_x16_submit_flush_vaes_avx512.o \
	mb_mgr_aes128_ccm_by16_auth_submit_flush_vaes_avx512.o \
	mb_mgr_aes256_ccm_by16_auth_submit_flush_vaes_avx512.o \
	mb_mgr_aes128_xcbc_x16_submit_flush_vaes_avx512.o \
	mb_mgr_zuc_submit_flush_avx512.o \
	mb_mgr_zuc_submit_flush_gfni_avx512.o \
	chacha20_avx512.o \
        poly_avx512.o \
        poly_fma_avx512.o \
	ethernet_fcs_avx512.o \
	crc16_x25_avx512.o \
	crc32_refl_by16_vclmul_avx512.o \
	crc32_by16_vclmul_avx512.o \
	mb_mgr_aes_cbcs_1_9_submit_avx512.o \
	mb_mgr_aes_cbcs_1_9_flush_avx512.o \
	crc32_sctp_avx512.o \
	crc32_lte_avx512.o \
	crc32_fp_avx512.o \
	crc32_iuup_avx512.o \
	crc32_wimax_avx512.o \
	snow3g_uia2_by32_vaes_avx512.o \
	mb_mgr_snow3g_uea2_submit_flush_vaes_avx512.o \
	mb_mgr_snow3g_uia2_submit_flush_vaes_avx512.o

#
# GCM object file lists
#

c_gcm_objs := gcm.o

asm_noaesni_gcm_objs := \
	gcm128_sse_no_aesni.o gcm192_sse_no_aesni.o gcm256_sse_no_aesni.o

asm_sse_gcm_objs := \
	aes128_gcm_by8_sse.o aes192_gcm_by8_sse.o aes256_gcm_by8_sse.o

asm_avx_gcm_objs := \
	aes128_gcm_by8_avx.o aes192_gcm_by8_avx.o aes256_gcm_by8_avx.o

asm_avx2_gcm_objs := \
	aes128_gcm_by8_avx2.o aes192_gcm_by8_avx2.o aes256_gcm_by8_avx2.o

asm_avx512_gcm_objs := \
	aes128_gcm_by48_api_vaes_avx512.o aes192_gcm_by48_api_vaes_avx512.o aes256_gcm_by48_api_vaes_avx512.o \
	aes128_gcm_by48_sgl_api_vaes_avx512.o aes192_gcm_by48_sgl_api_vaes_avx512.o aes256_gcm_by48_sgl_api_vaes_avx512.o \
	aes128_gmac_by48_api_vaes_avx512.o aes192_gmac_by48_api_vaes_avx512.o aes256_gmac_by48_api_vaes_avx512.o \
	aes128_gcm_by8_avx512.o aes192_gcm_by8_avx512.o aes256_gcm_by8_avx512.o

#
# build object files lists
#
asm_obj_files := $(asm_generic_lib_objs) \
	$(asm_noaesni_lib_objs) $(asm_noaesni_gcm_objs) \
	$(asm_sse_lib_objs) $(asm_sse_gcm_objs) \
	$(asm_avx_lib_objs) $(asm_avx_gcm_objs) \
	$(asm_avx2_lib_objs) $(asm_avx2_gcm_objs) \
	$(asm_avx512_lib_objs) $(asm_avx512_gcm_objs)
c_obj_files := $(c_lib_objs) $(c_gcm_objs)

#
# aggregate all objects files together and prefix with OBJDIR
#
lib_obj_files := $(asm_obj_files) $(c_obj_files)
target_obj_files := $(lib_obj_files:%=$(OBJ_DIR)/%)

#
# create a list of dependency files for assembly modules
# create a list of dependency files for c modules then
# prefix these with OBJDIR
#
asm_dep_files := $(asm_obj_files:%.o=%.d)

c_dep_files := $(c_obj_files:%.o=%.d)
c_dep_target_files := $(c_dep_files:%=$(OBJ_DIR)/%)

#
# aggregate all dependency files together and prefix with OBJDIR
#
dep_files := $(asm_dep_files) $(c_dep_files)
dep_target_files := $(dep_files:%=$(OBJ_DIR)/%)

all: $(LIB_DIR)/$(LIBNAME)

$(LIB_DIR)/$(LIBNAME): $(target_obj_files)
ifeq ($(SHARED),y)
ifneq ($(MINGW),0)
	$(CC) -shared $(LDFLAGS) -o $(LIBNAME) $^ $(LIB).def
else
	$(CC) -shared $(LDFLAGS) -Wl,-soname,$(LIB).so.$(SO_VERSION) -o $@ $^ -lc
	ln -f -s $(LIBNAME) $(LIB_DIR)/$(LIB).so.$(SO_VERSION)
	ln -f -s $(LIB).so.$(SO_VERSION) $(LIB_DIR)/$(LIB).so
endif
else
	$(AR) -qcs $@ $^
endif
ifeq ($(SAFE_PARAM), n)
	@echo "NOTE:" $(SAFE_PARAM_MSG1) $(SAFE_PARAM_MSG2)
endif
ifeq ($(SAFE_DATA), n)
	@echo "NOTE:" $(SAFE_DATA_MSG1) $(SAFE_DATA_MSG2)
endif
ifeq ($(SAFE_LOOKUP), n)
	@echo "NOTE:" $(SAFE_LOOKUP_MSG1) $(SAFE_LOOKUP_MSG2)
endif

.PHONY: install
install: $(LIB_DIR)/$(LIBNAME)
	install -d $(HDR_DIR)
	install -m 0644 $(IMB_HDR) $(HDR_DIR)
	install -d $(MAN_DIR)
	install -m 0444 $(MAN1) $(MAN_DIR)
	install -m 0444 $(MAN2) $(MAN_DIR)
	install -d $(LIB_INSTALL_DIR)
ifeq ($(SHARED),y)
	install -s -m $(LIBPERM) $(LIB_DIR)/$(LIBNAME) $(LIB_INSTALL_DIR)
else
	# must not strip symbol table for static libs
	install -m $(LIBPERM) $(LIB_DIR)/$(LIBNAME) $(LIB_INSTALL_DIR)
endif
ifeq ($(SHARED),y)
	cd $(LIB_INSTALL_DIR); \
		ln -f -s $(LIB).so.$(VERSION) $(LIB).so.$(SO_VERSION); \
		ln -f -s $(LIB).so.$(SO_VERSION) $(LIB).so
ifneq ($(NOLDCONFIG),y)
	ldconfig
endif
endif

.PHONY: uninstall
uninstall:
	-rm -f $(HDR_DIR)/$(IMB_HDR)
	-rm -f $(LIB_INSTALL_DIR)/$(LIBNAME)
	-rm -f $(MAN_DIR)/$(MAN1)
	-rm -f $(MAN_DIR)/$(MAN2)
ifeq ($(SHARED),y)
	-rm -f $(LIB_INSTALL_DIR)/$(LIB).so.$(SO_VERSION)
	-rm -f $(LIB_INSTALL_DIR)/$(LIB).so
endif

.PHONY: build_c_dep_target_files
build_c_dep_target_files: $(c_dep_target_files)

$(target_obj_files): | $(OBJ_DIR) $(LIB_DIR) build_c_dep_target_files
$(dep_target_files): | $(OBJ_DIR)

#
# object file build recipes
# - dependency file construction is part of the compilation
#

$(OBJ_DIR)/%.o:x86_64/%.c
	$(CC) -MMD $(OPT_X86) -c $(CFLAGS) $< -o $@

$(OBJ_DIR)/%.o:x86_64/%.asm
ifeq ($(USE_YASM),y)
	$(YASM) $(YASM_FLAGS) $< -o $@
else
	$(NASM) -MD $(@:.o=.d) -MT $@ -o $@ $(NASM_FLAGS) $<
endif
ifeq ($(CC_HAS_CET),1)
	$(LD) $(CET_LDFLAGS) -o $@.tmp $@
	mv $@.tmp $@
endif

$(OBJ_DIR)/%.o:sse/%.c
	$(CC) -MMD $(OPT_SSE) -c $(CFLAGS) $< -o $@

$(OBJ_DIR)/%.o:sse/%.asm
ifeq ($(USE_YASM),y)
	$(YASM) $(YASM_FLAGS) $< -o $@
else
	$(NASM) -MD $(@:.o=.d) -MT $@ -o $@ $(NASM_FLAGS) $<
endif
ifeq ($(CC_HAS_CET),1)
	$(LD) $(CET_LDFLAGS) -o $@.tmp $@
	mv $@.tmp $@
endif

$(OBJ_DIR)/%.o:avx/%.c
	$(CC) -MMD $(OPT_AVX) -c $(CFLAGS) $< -o $@

$(OBJ_DIR)/%.o:avx/%.asm
ifeq ($(USE_YASM),y)
	$(YASM) $(YASM_FLAGS) $< -o $@
else
	$(NASM) -MD $(@:.o=.d) -MT $@ -o $@ $(NASM_FLAGS) $<
endif
ifeq ($(CC_HAS_CET),1)
	$(LD) $(CET_LDFLAGS) -o $@.tmp $@
	mv $@.tmp $@
endif

$(OBJ_DIR)/%.o:avx2/%.c
	$(CC) -MMD $(OPT_AVX2) -c $(CFLAGS) $< -o $@

$(OBJ_DIR)/%.o:avx2/%.asm
ifeq ($(USE_YASM),y)
	$(YASM) $(YASM_FLAGS) $< -o $@
else
	$(NASM) -MD $(@:.o=.d) -MT $@ -o $@ $(NASM_FLAGS) $<
endif
ifeq ($(CC_HAS_CET),1)
	$(LD) $(CET_LDFLAGS) -o $@.tmp $@
	mv $@.tmp $@
endif

$(OBJ_DIR)/%.o:avx512/%.c
	$(CC) -MMD $(OPT_AVX512) -c $(CFLAGS) $< -o $@

$(OBJ_DIR)/%.o:avx512/%.asm
ifeq ($(USE_YASM),y)
	$(YASM) $(YASM_FLAGS) $< -o $@
else
	$(NASM) -MD $(@:.o=.d) -MT $@ -o $@ $(NASM_FLAGS) $<
endif
ifeq ($(CC_HAS_CET),1)
	$(LD) $(CET_LDFLAGS) -o $@.tmp $@
	mv $@.tmp $@
endif

$(OBJ_DIR)/%.o:no-aesni/%.c
	$(CC) -MMD $(OPT_NOAESNI) -c $(CFLAGS_NO_SIMD) $< -o $@

$(OBJ_DIR)/%.o:no-aesni/%.asm
ifeq ($(USE_YASM),y)
	$(YASM) $(YASM_FLAGS) $< -o $@
else
	$(NASM) -MD $(@:.o=.d) -MT $@ -o $@ $(NASM_FLAGS) $<
endif
ifeq ($(CC_HAS_CET),1)
	$(LD) $(CET_LDFLAGS) -o $@.tmp $@
	mv $@.tmp $@
endif

$(OBJ_DIR):
	mkdir $(OBJ_DIR)

$(LIB_DIR):
	mkdir $(LIB_DIR)

.PHONY: TAGS
TAGS:
	find ./ -name '*.[ch]' | etags -
	find ./ -name '*.asm'  | etags -a -
	find ./ -name '*.inc'  | etags -a -

.PHONY: clean
clean:
	rm -Rf $(target_obj_files)
	rm -Rf $(dep_target_files)
	rm -f $(LIB_DIR)/$(LIB).a $(LIB_DIR)/$(LIB).so* $(LIB_DIR)/$(LIB).dll

.PHONY: doxy
doxy:
	doxygen api_doxygen.conf

.PHONY: help
help:
	@echo "Available build options:"
	@echo "DEBUG=n (default)"
	@echo "          - this option will produce library not fit for debugging"
	@echo "SHARED=y (default)"
	@echo "          - this option will produce shared library"
	@echo "DEBUG_OPT=<optimization level>"
	@echo "		- this option will modify the optimization level when DEBUG is used"
	@echo "DEBUG=y   - this option will produce library fit for debugging"
	@echo "SHARED=n  - this option will produce static library"
	@echo "OBJ_DIR=obj (default)"
	@echo "          - this option can be used to change build directory"
	@echo "LIB_DIR=. (default)"
	@echo "          - this option can be used to change the library directory"
	@echo "SAFE_DATA=n"
	@echo "          - Sensitive data not cleared from registers and memory"
	@echo "            at operation end"
	@echo "SAFE_DATA=y (default)"
	@echo "          - Sensitive data cleared from registers and memory"
	@echo "            at operation end"
	@echo "SAFE_PARAM=n"
	@echo "          - API input parameters not checked"
	@echo "SAFE_PARAM=y (default)"
	@echo "          - API input parameters checked"
	@echo "SAFE_LOOKUP=n"
	@echo "          - Lookups depending on sensitive data might not be constant time"
	@echo "SAFE_LOOKUP=y (default)"
	@echo "          - Lookups depending on sensitive data are constant time"


CHECKPATCH ?= checkpatch.pl
# checkpatch ignore settings:
#   SPACING - produces false positives with tyepdefs and *
#   CONSTANT_COMPARISON - forbids defensive programming technique
#   USE_FUNC - produces false positives for Windows target
#   INITIALISED_STATIC, LEADING_SPACE, SPLIT_STRING, CODE_INDENT,
#   PREFER_ALIGNED, UNSPECIFIED_INT, ARRAY_SIZE, GLOBAL_INITIALISERS,
#   NEW_TYPEDEFS, AVOID_EXTERNS, COMPLEX_MACRO, BLOCK_COMMENT_STYLE
#     - found obsolete in this project
#
# NOTE: these flags cannot be broken into multiple lines due to
#       spaces injected by make
CHECKPATCH_FLAGS = --no-tree --no-signoff --emacs --no-color --ignore CODE_INDENT,INITIALISED_STATIC,LEADING_SPACE,SPLIT_STRING,UNSPECIFIED_INT,ARRAY_SIZE,BLOCK_COMMENT_STYLE,GLOBAL_INITIALISERS,NEW_TYPEDEFS,AVOID_EXTERNS,COMPLEX_MACRO,PREFER_ALIGNED,USE_FUNC,CONSTANT_COMPARISON,SPACING,GCC_BINARY_CONSTANT

%.c_style_check : %.c
	$(CHECKPATCH) $(CHECKPATCH_FLAGS) -f $<

%.h_style_check : %.h
	$(CHECKPATCH) $(CHECKPATCH_FLAGS) -f $<

%.asm_style_check : %.asm
	$(CHECKPATCH) $(CHECKPATCH_FLAGS) -f $<

%.inc_style_check : %.inc
	$(CHECKPATCH) $(CHECKPATCH_FLAGS) -f $<

SOURCES_DIRS := . sse avx avx2 avx512 include no-aesni
SOURCES := $(foreach dir,$(SOURCES_DIRS),$(wildcard $(dir)/*.[ch]) $(wildcard $(dir)/*.asm) $(wildcard $(dir)/*.inc))
SOURCES_STYLE := $(foreach infile,$(SOURCES),$(infile)_style_check)

.PHONY: style
style: $(SOURCES_STYLE)

# if target not clean or rinse then make dependencies
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),style)
-include $(dep_target_files)
endif
endif
