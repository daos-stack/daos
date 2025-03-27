/*******************************************************************************
  Copyright (c) 2012-2021, Intel Corporation

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
      * Neither the name of Intel Corporation nor the names of its contributors
        may be used to endorse or promote products derived from this software
        without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#ifndef IMB_IPSEC_MB_H
#define IMB_IPSEC_MB_H

#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 128-bit data type that is not in sdtint.h */
typedef struct {
        uint64_t low;
        uint64_t high;
} imb_uint128_t;

/**
 * Macros for aligning data structures and function inlines
 */
#if defined __linux__ || defined __FreeBSD__
/**< Linux/FreeBSD */
#define DECLARE_ALIGNED(decl, alignval) \
        decl __attribute__((aligned(alignval)))
#define __forceinline \
        static inline __attribute__((always_inline))

#if __GNUC__ >= 4
#define IMB_DLL_EXPORT __attribute__((visibility("default")))
#define IMB_DLL_LOCAL  __attribute__((visibility("hidden")))
#else /* GNU C 4.0 and later */
#define IMB_DLL_EXPORT
#define IMB_DLL_LOCAL
#endif /**< different C compiler */

#else
/* Windows */

#ifdef __MINGW32__
/* MinGW-w64 */
#define DECLARE_ALIGNED(decl, alignval) \
        decl __attribute__((aligned(alignval)))
#undef __forceinline
#define __forceinline \
        static inline __attribute__((always_inline))

#else
/* MSVS */
#define DECLARE_ALIGNED(decl, alignval)         \
        __declspec(align(alignval)) decl
#define __forceinline \
        static __forceinline

#endif /* __MINGW__ */

/**
 * Windows DLL export is done via DEF file
 */
#define IMB_DLL_EXPORT
#define IMB_DLL_LOCAL

#endif /* defined __linux__ || defined __FreeBSD__ */

/**
 * Library version
 */
#define IMB_VERSION_STR "1.1.0"
#define IMB_VERSION_NUM 0x10100

/**
 * Macro to translate version number
 */
#define IMB_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))

/**
 * Custom ASSERT and DIM macros
 */
#ifdef DEBUG
#include <assert.h>
#define IMB_ASSERT(x) assert(x)
#else
#define IMB_ASSERT(x)
#endif

#ifndef IMB_DIM
#define IMB_DIM(x) (sizeof(x) / sizeof(x[0]))
#endif

/**
 * Architecture definitions
 */
typedef enum {
        IMB_ARCH_NONE = 0,
        IMB_ARCH_NOAESNI,
        IMB_ARCH_SSE,
        IMB_ARCH_AVX,
        IMB_ARCH_AVX2,
        IMB_ARCH_AVX512,
        IMB_ARCH_NUM,
} IMB_ARCH;

/**
 * Algorithm constants
 */
#define IMB_DES_KEY_SCHED_SIZE (16 * 8) /**< 16 rounds x 8 bytes */
#define IMB_DES_BLOCK_SIZE 8

#define IMB_AES_BLOCK_SIZE 16

#define IMB_SHA1_DIGEST_SIZE_IN_BYTES   20
#define IMB_SHA224_DIGEST_SIZE_IN_BYTES 28
#define IMB_SHA256_DIGEST_SIZE_IN_BYTES 32
#define IMB_SHA384_DIGEST_SIZE_IN_BYTES 48
#define IMB_SHA512_DIGEST_SIZE_IN_BYTES 64

#define IMB_SHA1_BLOCK_SIZE 64    /**< 512 bits is 64 byte blocks */
#define IMB_SHA_256_BLOCK_SIZE 64 /**< 512 bits is 64 byte blocks */
#define IMB_SHA_384_BLOCK_SIZE 128
#define IMB_SHA_512_BLOCK_SIZE 128

#define IMB_KASUMI_KEY_SIZE         16
#define IMB_KASUMI_IV_SIZE          8
#define IMB_KASUMI_BLOCK_SIZE       8
#define IMB_KASUMI_DIGEST_SIZE      4

/**
 * Minimum Ethernet frame size to calculate CRC32
 * Source Address (6 bytes) + Destination Address (6 bytes) + Type/Len (2 bytes)
 */
#define IMB_DOCSIS_CRC32_MIN_ETH_PDU_SIZE 14
#define IMB_DOCSIS_CRC32_TAG_SIZE         4

/**
 * Job structure definitions
 */

typedef enum {
        IMB_STATUS_BEING_PROCESSED  = 0,
        IMB_STATUS_COMPLETED_CIPHER = 1,
        IMB_STATUS_COMPLETED_AUTH   = 2,
        IMB_STATUS_COMPLETED        = 3, /**< COMPLETED_CIPHER |
					   COMPLETED_AUTH */
        IMB_STATUS_INVALID_ARGS     = 4,
        IMB_STATUS_INTERNAL_ERROR,
        IMB_STATUS_ERROR
} IMB_STATUS;

/**
 * Library error types
 */
typedef enum {
        IMB_ERR_MIN = 2000,
        /* job api */
        IMB_ERR_NULL_MBMGR,
        IMB_ERR_JOB_NULL_SRC,
        IMB_ERR_JOB_NULL_DST,
        IMB_ERR_JOB_NULL_KEY,
        IMB_ERR_JOB_NULL_IV,
        IMB_ERR_JOB_NULL_AUTH,
        IMB_ERR_JOB_NULL_AAD,
        IMB_ERR_JOB_CIPH_LEN,
        IMB_ERR_JOB_AUTH_LEN,
        IMB_ERR_JOB_IV_LEN,
        IMB_ERR_JOB_KEY_LEN,
        IMB_ERR_JOB_AUTH_TAG_LEN,
        IMB_ERR_JOB_AAD_LEN,
        IMB_ERR_JOB_SRC_OFFSET,
        IMB_ERR_JOB_CHAIN_ORDER,
        IMB_ERR_CIPH_MODE,
        IMB_ERR_HASH_ALGO,
        IMB_ERR_JOB_NULL_AUTH_KEY,
        IMB_ERR_JOB_NULL_SGL_CTX,
        IMB_ERR_JOB_NULL_NEXT_IV,
        IMB_ERR_JOB_PON_PLI,
        /* direct api */
        IMB_ERR_NULL_SRC,
        IMB_ERR_NULL_DST,
        IMB_ERR_NULL_KEY,
        IMB_ERR_NULL_EXP_KEY,
        IMB_ERR_NULL_IV,
        IMB_ERR_NULL_AUTH,
        IMB_ERR_NULL_AAD,
        IMB_ERR_CIPH_LEN,
        IMB_ERR_AUTH_LEN,
        IMB_ERR_IV_LEN,
        IMB_ERR_KEY_LEN,
        IMB_ERR_AUTH_TAG_LEN,
        IMB_ERR_AAD_LEN,
        IMB_ERR_SRC_OFFSET,
        IMB_ERR_NULL_AUTH_KEY,
        IMB_ERR_NULL_CTX,
        IMB_ERR_MAX       /* don't move this one */
} IMB_ERR;

/**
 * IMB_ERR_MIN should be higher than __ELASTERROR
 * to avoid overlap with standard error values
 */
#ifdef __ELASTERROR
#if __ELASTERROR > 2000
#error "Library error codes conflict with errno.h - please update IMB_ERR_MIN!"
#endif
#endif

/**
 * Define enums from API v0.53, so applications that were using this version
 * will still be compiled successfully.
 * Note: this list has been extended with new names after version 0.55.
 * This list does not need to be extended for new enums.
 */
#ifndef NO_COMPAT_IMB_API_053
/* Previous cipher mode enums */
#define CBC                     IMB_CIPHER_CBC
#define CNTR                    IMB_CIPHER_CNTR
#define NULL_CIPHER             IMB_CIPHER_NULL
#define DOCSIS_SEC_BPI          IMB_CIPHER_DOCSIS_SEC_BPI
#define GCM                     IMB_CIPHER_GCM
#define CUSTOM_CIPHER           IMB_CIPHER_CUSTOM
#define DES                     IMB_CIPHER_DES
#define DOCSIS_DES              IMB_CIPHER_DOCSIS_DES
#define CCM                     IMB_CIPHER_CCM
#define DES3                    IMB_CIPHER_DES3
#define PON_AES_CNTR            IMB_CIPHER_PON_AES_CNTR
#define ECB                     IMB_CIPHER_ECB
#define CNTR_BITLEN             IMB_CIPHER_CNTR_BITLEN

/* Previous hash algo enums */
#define SHA1                    IMB_AUTH_HMAC_SHA_1
#define SHA_224                 IMB_AUTH_HMAC_SHA_224
#define SHA_256                 IMB_AUTH_HMAC_SHA_256
#define SHA_384                 IMB_AUTH_HMAC_SHA_384
#define SHA_512                 IMB_AUTH_HMAC_SHA_512
#define AES_XCBC                IMB_AUTH_AES_XCBC
#define MD5                     IMB_AUTH_MD5
#define NULL_HASH               IMB_AUTH_NULL
#define AES_GMAC                IMB_AUTH_AES_GMAC
#define CUSTOM_HASH             IMB_AUTH_CUSTOM
#define AES_CCM                 IMB_AUTH_AES_CCM
#define AES_CMAC                IMB_AUTH_AES_CMAC
#define PLAIN_SHA1              IMB_AUTH_SHA_1
#define PLAIN_SHA_224           IMB_AUTH_SHA_224
#define PLAIN_SHA_256           IMB_AUTH_SHA_256
#define PLAIN_SHA_384           IMB_AUTH_SHA_384
#define PLAIN_SHA_512           IMB_AUTH_SHA_512
#define AES_CMAC_BITLEN         IMB_AUTH_AES_CMAC_BITLEN
#define PON_CRC_BIP             IMB_AUTH_PON_CRC_BIP

/* Previous cipher direction enums */
#define ENCRYPT                 IMB_DIR_ENCRYPT
#define DECRYPT                 IMB_DIR_DECRYPT

/* Previous chain order enums */
#define HASH_CIPHER             IMB_ORDER_HASH_CIPHER
#define CIPHER_HASH             IMB_ORDER_CIPHER_HASH

/* Previous key size enums */
#define AES_128_BYTES           IMB_KEY_128_BYTES
#define AES_192_BYTES           IMB_KEY_192_BYTES
#define AES_256_BYTES           IMB_KEY_256_BYTES
#define IMB_KEY_AES_128_BYTES   IMB_KEY_128_BYTES
#define IMB_KEY_AES_192_BYTES   IMB_KEY_192_BYTES
#define IMB_KEY_AES_256_BYTES   IMB_KEY_256_BYTES
#define AES_KEY_SIZE_BYTES      IMB_KEY_SIZE_BYTES

#define MB_MGR                  IMB_MGR
#define JOB_AES_HMAC            IMB_JOB
#define JOB_STS                 IMB_STATUS
#define IMB_JOB_STS             IMB_STATUS
#define JOB_CIPHER_MODE         IMB_CIPHER_MODE
#define JOB_CIPHER_DIRECTION    IMB_CIPHER_DIRECTION
#define JOB_HASH_ALG            IMB_HASH_ALG
#define JOB_CHAIN_ORDER         IMB_CHAIN_ORDER
#define MAX_JOBS                IMB_MAX_JOBS

#define STS_BEING_PROCESSED     IMB_STATUS_BEING_PROCESSED
#define STS_COMPLETED_AES       IMB_STATUS_COMPLETED_CIPHER
#define STS_COMPLETED_HMAC      IMB_STATUS_COMPLETED_AUTH
#define STS_COMPLETED           IMB_STATUS_COMPLETED
#define STS_INVALID_ARGS        IMB_STATUS_INVALID_ARGS
#define STS_INTERNAL_ERROR      IMB_STATUS_INTERNAL_ERROR
#define STS_ERROR               IMB_STATUS_ERROR

#define MAX_TAG_LEN             IMB_MAX_TAG_LEN
#define GCM_IV_DATA_LEN         IMB_GCM_IV_DATA_LEN
#define GCM_128_KEY_LEN         IMB_GCM_128_KEY_LEN
#define GCM_192_KEY_LEN         IMB_GCM_192_KEY_LEN
#define GCM_256_KEY_LEN         IMB_GCM_256_KEY_LEN

#define DES_KEY_SCHED_SIZE      IMB_DES_KEY_SCHED_SIZE
#define DES_BLOCK_SIZE          IMB_DES_BLOCK_SIZE

#define AES_BLOCK_SIZE          IMB_AES_BLOCK_SIZE

#define SHA1_DIGEST_SIZE_IN_BYTES   IMB_SHA1_DIGEST_SIZE_IN_BYTES
#define SHA224_DIGEST_SIZE_IN_BYTES IMB_SHA224_DIGEST_SIZE_IN_BYTES
#define SHA256_DIGEST_SIZE_IN_BYTES IMB_SHA256_DIGEST_SIZE_IN_BYTES
#define SHA384_DIGEST_SIZE_IN_BYTES IMB_SHA384_DIGEST_SIZE_IN_BYTES
#define SHA512_DIGEST_SIZE_IN_BYTES IMB_SHA512_DIGEST_SIZE_IN_BYTES

#define SHA1_BLOCK_SIZE    IMB_SHA1_BLOCK_SIZE
#define SHA_256_BLOCK_SIZE IMB_SHA_256_BLOCK_SIZE
#define SHA_384_BLOCK_SIZE IMB_SHA_384_BLOCK_SIZE
#define SHA_512_BLOCK_SIZE IMB_SHA_512_BLOCK_SIZE

#define KASUMI_KEY_SIZE    IMB_KASUMI_KEY_SIZE
#define KASUMI_IV_SIZE     IMB_KASUMI_IV_SIZE
#define KASUMI_BLOCK_SIZE  IMB_KASUMI_BLOCK_SIZE
#define KASUMI_DIGEST_SIZE IMB_KASUMI_DIGEST_SIZE

#define DOCSIS_CRC32_MIN_ETH_PDU_SIZE IMB_DOCSIS_CRC32_MIN_ETH_PDU_SIZE
#define DOCSIS_CRC32_TAG_SIZE         IMB_DOCSIS_CRC32_TAG_SIZE

/* Previous fields in IMB_JOB/JOB_AES_HMAC */
#define aes_enc_key_expanded enc_keys
#define aes_dec_key_expanded dec_keys
#define aes_key_len_in_bytes key_len_in_bytes
#endif /* !NO_COMPAT_IMB_API_053 */

typedef enum {
        IMB_CIPHER_CBC = 1,
        IMB_CIPHER_CNTR,
        IMB_CIPHER_NULL,
        IMB_CIPHER_DOCSIS_SEC_BPI,
        IMB_CIPHER_GCM,
        IMB_CIPHER_CUSTOM,
        IMB_CIPHER_DES,
        IMB_CIPHER_DOCSIS_DES,
        IMB_CIPHER_CCM,
        IMB_CIPHER_DES3,
        IMB_CIPHER_PON_AES_CNTR,
        IMB_CIPHER_ECB,
        IMB_CIPHER_CNTR_BITLEN,       /**< 128-EEA2/NEA2 (3GPP) */
        IMB_CIPHER_ZUC_EEA3,          /**< 128-EEA3/NEA3 (3GPP) */
        IMB_CIPHER_SNOW3G_UEA2_BITLEN,/**< 128-UEA2 (3GPP) */
        IMB_CIPHER_KASUMI_UEA1_BITLEN,/**< 128-UEA1 (3GPP) */
        IMB_CIPHER_CBCS_1_9,          /**< MPEG CENC (ISO 23001-7) */
        IMB_CIPHER_CHACHA20,
        IMB_CIPHER_CHACHA20_POLY1305, /**< AEAD CHACHA20 */
        IMB_CIPHER_CHACHA20_POLY1305_SGL, /**< AEAD CHACHA20 with SGL support*/
        IMB_CIPHER_SNOW_V,
        IMB_CIPHER_SNOW_V_AEAD,
        IMB_CIPHER_GCM_SGL,
        IMB_CIPHER_NUM
} IMB_CIPHER_MODE;

typedef enum {
        IMB_DIR_ENCRYPT = 1,
        IMB_DIR_DECRYPT
} IMB_CIPHER_DIRECTION;

typedef enum {
        IMB_AUTH_HMAC_SHA_1 = 1,    /**< HMAC-SHA1 */
        IMB_AUTH_HMAC_SHA_224,      /**< HMAC-SHA224 */
        IMB_AUTH_HMAC_SHA_256,      /**< HMAC-SHA256 */
        IMB_AUTH_HMAC_SHA_384,      /**< HMAC-SHA384 */
        IMB_AUTH_HMAC_SHA_512,      /**< HMAC-SHA512 */
        IMB_AUTH_AES_XCBC,
        IMB_AUTH_MD5,               /**< HMAC-MD5 */
        IMB_AUTH_NULL,
        IMB_AUTH_AES_GMAC,
        IMB_AUTH_CUSTOM,
        IMB_AUTH_AES_CCM,            /**< AES128-CCM */
        IMB_AUTH_AES_CMAC,           /**< AES128-CMAC */
        IMB_AUTH_SHA_1,              /**< SHA1 */
        IMB_AUTH_SHA_224,            /**< SHA224 */
        IMB_AUTH_SHA_256,            /**< SHA256 */
        IMB_AUTH_SHA_384,            /**< SHA384 */
        IMB_AUTH_SHA_512,            /**< SHA512 */
        IMB_AUTH_AES_CMAC_BITLEN,    /**< 128-EIA2/NIA2 (3GPP) */
        IMB_AUTH_PON_CRC_BIP,
        IMB_AUTH_ZUC_EIA3_BITLEN,    /**< 128-EIA3/NIA3 (3GPP) */
        IMB_AUTH_DOCSIS_CRC32,       /**< with DOCSIS_SEC_BPI only */
        IMB_AUTH_SNOW3G_UIA2_BITLEN, /**< 128-UIA2 (3GPP) */
        IMB_AUTH_KASUMI_UIA1,        /**< 128-UIA1 (3GPP) */
        IMB_AUTH_AES_GMAC_128,       /**< AES-GMAC (128-bit key) */
        IMB_AUTH_AES_GMAC_192,       /**< AES-GMAC (192-bit key) */
        IMB_AUTH_AES_GMAC_256,       /**< AES-GMAC (256-bit key) */
        IMB_AUTH_AES_CMAC_256,       /**< AES256-CMAC */
        IMB_AUTH_POLY1305,           /**< POLY1305 */
        IMB_AUTH_CHACHA20_POLY1305,  /**< AEAD POLY1305 */
        IMB_AUTH_CHACHA20_POLY1305_SGL, /**< AEAD CHACHA20 with SGL support */
        IMB_AUTH_ZUC256_EIA3_BITLEN,    /**< 256-EIA3/NIA3 (3GPP) */
        IMB_AUTH_SNOW_V_AEAD,           /**< SNOW-V-AEAD */
        IMB_AUTH_GCM_SGL,               /**< AES-GCM with SGL support */
        IMB_AUTH_CRC32_ETHERNET_FCS,    /**< CRC32-ETHERNET-FCS */
        IMB_AUTH_CRC32_SCTP,            /**< CRC32-SCTP */
        IMB_AUTH_CRC32_WIMAX_OFDMA_DATA,/**< CRC32-WIMAX-OFDMA-DATA */
        IMB_AUTH_CRC24_LTE_A,           /**< CRC32-LTE-A */
        IMB_AUTH_CRC24_LTE_B,           /**< CRC32-LTE-B */
        IMB_AUTH_CRC16_X25,             /**< CRC16-X25 */
        IMB_AUTH_CRC16_FP_DATA,         /**< CRC16-FP-DATA */
        IMB_AUTH_CRC11_FP_HEADER,       /**< CRC11-FP-HEADER */
        IMB_AUTH_CRC10_IUUP_DATA,       /**< CRC10-IUUP-DATA */
        IMB_AUTH_CRC8_WIMAX_OFDMA_HCS,  /**< CRC8-WIMAX-OFDMA-HCS */
        IMB_AUTH_CRC7_FP_HEADER,        /**< CRC7-FP-HEADER */
        IMB_AUTH_CRC6_IUUP_HEADER,      /**< CRC6-IUUP-HEADER */
        IMB_AUTH_NUM
} IMB_HASH_ALG;

typedef enum {
        IMB_ORDER_CIPHER_HASH = 1,
        IMB_ORDER_HASH_CIPHER
} IMB_CHAIN_ORDER;

typedef enum {
        IMB_KEY_128_BYTES = 16,
        IMB_KEY_192_BYTES = 24,
        IMB_KEY_256_BYTES = 32
} IMB_KEY_SIZE_BYTES;

typedef enum {
        IMB_SGL_INIT = 0,
        IMB_SGL_UPDATE,
        IMB_SGL_COMPLETE
} IMB_SGL_STATE;

/**
 * For AES, enc_keys and dec_keys are
 * expected to point to expanded keys structure.
 * - AES-CTR, AES-ECB and AES-CCM, only enc_keys is used
 * - DOCSIS (AES-CBC + AES-CFB), both pointers are used
 *   enc_keys has to be set always for the partial block
 *
 * For DES, enc_keys and dec_keys are
 * expected to point to DES key schedule.
 * - same key schedule used for enc and dec operations
 *
 * For 3DES, enc_keys and dec_keys are
 * expected to point to an array of 3 pointers for
 * the corresponding 3 key schedules.
 * - same key schedule used for enc and dec operations
 */

typedef struct IMB_JOB {
        const void *enc_keys;  /**< 16-byte aligned pointer. */
        const void *dec_keys;
        uint64_t key_len_in_bytes;
        const uint8_t *src; /**< Input. May be cipher text or plaintext.
			      In-place ciphering allowed. */
        uint8_t *dst; /**<Output. May be cipher text or plaintext.
			In-place ciphering allowed, i.e. dst = src. */
        union {
                uint64_t cipher_start_src_offset_in_bytes;
                uint64_t cipher_start_src_offset_in_bits;
                uint64_t cipher_start_offset_in_bits;
        };
        /**
	 * Max len = 65472 bytes.
         * IPSec case, the maximum cipher
         * length would be:
         * 65535 -
         * 20 (outer IP header) -
         * 24 (ESP header + IV) -
         * 12 (supported ICV length)
	 */
        union {
                uint64_t msg_len_to_cipher_in_bytes;
                uint64_t msg_len_to_cipher_in_bits;
        };
        uint64_t hash_start_src_offset_in_bytes;
        /**
	 * Max len = 65496 bytes.
         * (Max cipher len +
         * 24 bytes ESP header)
	 */
        union {
                uint64_t msg_len_to_hash_in_bytes;
                uint64_t msg_len_to_hash_in_bits;
        };
        const uint8_t *iv;	/**< Initialization Vector (IV) */
        uint64_t iv_len_in_bytes; /**< IV length in bytes. */
        uint8_t *auth_tag_output; /**< Tag output. This may point to a location
				    in the src buffer (for in place)*/
        uint64_t auth_tag_output_len_in_bytes; /**< Authentication (i.e. HMAC)
						 tag output length in bytes
						 (may be a truncated value) */

        /* Start algorithm-specific fields */
        union {
                struct _HMAC_specific_fields {
                        /**< Hashed result of HMAC key xor'd
			 * with ipad (0x36). */
                        const uint8_t *_hashed_auth_key_xor_ipad;
                        /**< Hashed result of HMAC key xor'd
			 * with opad (0x5c). */
                        const uint8_t *_hashed_auth_key_xor_opad;
                } HMAC;
                struct _AES_XCBC_specific_fields {
                        /**< 16-byte aligned pointers */
                        const uint32_t *_k1_expanded;
                        const uint8_t *_k2;
                        const uint8_t *_k3;
                } XCBC;
                struct _AES_CCM_specific_fields {
                        /**< Additional Authentication Data (AAD) */
                        const void *aad;
                        uint64_t aad_len_in_bytes; /**< Length of AAD */
                } CCM;
                struct _AES_CMAC_specific_fields {
                        const void *_key_expanded; /**< 16-byte aligned */
                        const void *_skey1;
                        const void *_skey2;
                } CMAC;
                struct _AES_GCM_specific_fields {
                        /**< Additional Authentication Data (AAD) */
                        const void *aad;
                        uint64_t aad_len_in_bytes;    /**< Length of AAD */
                        /**< AES-GCM context (for SGL only) */
                        struct gcm_context_data *ctx;
                } GCM;
                struct _ZUC_EIA3_specific_fields {
                        /**< 16-byte aligned pointers */
                        const uint8_t *_key;
                        const uint8_t *_iv;
                        const uint8_t *_iv23;
                } ZUC_EIA3;
                struct _SNOW3G_UIA2_specific_fields {
                        /**< 16-byte aligned pointers */
                        const void *_key;
                        const void *_iv;
                } SNOW3G_UIA2;
                struct _KASUMI_UIA1_specific_fields {
                        /**< 16-byte aligned pointers */
                        const void *_key;
                } KASUMI_UIA1;
                struct _AES_GMAC_specific_fields {
                        const struct gcm_key_data *_key;
                        const void *_iv;
                        uint64_t iv_len_in_bytes;
                } GMAC; /**< Used with AES_GMAC_128/192/256 */
                struct _POLY1305_specific_fields {
                        const void *_key; /**< pointer to 32 byte key */
                } POLY1305;
                struct _CHACHA20_POLY1305_specific_fields {
                        /**< Additional Authentication Data (AAD) */
                        const void *aad;
                        uint64_t aad_len_in_bytes;    /**< Length of AAD */
                        /**< Chacha20-Poly1305 context */
                        struct chacha20_poly1305_context_data *ctx;
                } CHACHA20_POLY1305;
                struct _SNOW_V_AEAD_specific_fields {
                        const void *aad;
                        uint64_t aad_len_in_bytes;
                        void *reserved;
                } SNOW_V_AEAD;
        } u;

        IMB_STATUS status;
        IMB_CIPHER_MODE cipher_mode; /**< IMB_CIPHER_CBC, IMB_CIPHER_CNTR,
				       IMB_CIPHER_GCM, etc. */
        IMB_CIPHER_DIRECTION cipher_direction;/**< IMB_DIR_ENCRYPT/
						IMB_DIR_DECRYPT */
        IMB_HASH_ALG hash_alg; /**< IMB_AUTH_SHA_1 or others... */
        /**
	 * IMB_ORDER_CIPHER_HASH or IMB_ORDER_HASH_CIPHER.
         * For AES-CCM, when encrypting, IMB_ORDER_HASH_CIPHER
         * must be selected, and when decrypting,
         * IMB_ORDER_CIPHER_HASH must be selected.
	 */
        IMB_CHAIN_ORDER chain_order;

        void *user_data;
        void *user_data2;

        /**
         * stateless custom cipher and hash
         *   Return:
         *     success: 0
         *     fail:    other
         */
        int (*cipher_func)(struct IMB_JOB *);
        int (*hash_func)(struct IMB_JOB *);

        IMB_SGL_STATE sgl_state;

        union {
                struct _CBCS_specific_fields {
                        void *next_iv;
                } CBCS;
        } cipher_fields;
} IMB_JOB;


/* KASUMI */

/* 64 precomputed words for key schedule */
#define KASUMI_KEY_SCHEDULE_SIZE  64

/**
 * Structure to maintain internal key scheduling
 */
typedef struct kasumi_key_sched_s {
    /**< Kasumi internal scheduling */
    uint16_t sk16[KASUMI_KEY_SCHEDULE_SIZE];      /**< key schedule */
    uint16_t msk16[KASUMI_KEY_SCHEDULE_SIZE];     /**< modified key schedule */
} kasumi_key_sched_t;

/* GCM data structures */
#define IMB_GCM_BLOCK_LEN   16

/**
 * @brief holds GCM operation context
 *
 * init, update and finalize context data
 */
struct gcm_context_data {
        uint8_t  aad_hash[IMB_GCM_BLOCK_LEN];
        uint64_t aad_length;
        uint64_t in_length;
        uint8_t  partial_block_enc_key[IMB_GCM_BLOCK_LEN];
        uint8_t  orig_IV[IMB_GCM_BLOCK_LEN];
        uint8_t  current_counter[IMB_GCM_BLOCK_LEN];
        uint64_t partial_block_length;
};
#undef IMB_GCM_BLOCK_LEN

/**
 * @brief holds Chacha20-Poly1305 operation context
 */
struct chacha20_poly1305_context_data {
        uint64_t hash[3]; /**< Intermediate computation of hash value */
        uint64_t aad_len; /**< Total AAD length */
        uint64_t hash_len; /**< Total length to digest (excluding AAD) */
        uint8_t last_ks[64]; /**< Last 64 bytes of KS */
        uint8_t poly_key[32]; /**< Poly key */
        uint8_t poly_scratch[16]; /**< Scratchpad to compute Poly on 16 bytes */
        uint64_t last_block_count; /**< Last block count used in last segment */
        uint64_t remain_ks_bytes;/**< Amount of bytes still to use of keystream
				   (up to 63 bytes) */
        uint64_t remain_ct_bytes; /**< Amount of ciphertext bytes still to use
				    of previous segment to authenticate
				    (up to 16 bytes) */
        uint8_t IV[12]; /**< IV (12 bytes) */
};

/**
 * Authenticated Tag Length in bytes.
 * Valid values are 16 (most likely), 12 or 8.
 */
#define IMB_MAX_TAG_LEN (16)

/**
 * IV data is limited to 16 bytes as follows:
 * 12 bytes is provided by an application -
 *    pre-counter block j0: 4 byte salt (from Security Association)
 *    concatenated with 8 byte Initialization Vector (from IPSec ESP
 *    Payload).
 * 4 byte value 0x00000001 is padded automatically by the library -
 *    there is no need to add these 4 bytes on application side anymore.
 */
#define IMB_GCM_IV_DATA_LEN (12)

#define IMB_GCM_128_KEY_LEN (16)
#define IMB_GCM_192_KEY_LEN (24)
#define IMB_GCM_256_KEY_LEN (32)

#define IMB_GCM_ENC_KEY_LEN 16
#define IMB_GCM_KEY_SETS    (15) /**< exp key + 14 exp round keys*/

/**
 * @brief holds intermediate key data needed to improve performance
 *
 * gcm_key_data hold internal key information used by gcm128, gcm192 and gcm256.
 */
#ifdef __WIN32
__declspec(align(64))
#endif /* WIN32 */
struct gcm_key_data {
        uint8_t expanded_keys[IMB_GCM_ENC_KEY_LEN * IMB_GCM_KEY_SETS];
        union {
                /**< Storage for precomputed hash keys */
                struct {
                        /**
                         * This is needed for schoolbook multiply purposes.
                         * (HashKey<<1 mod poly), (HashKey^2<<1 mod poly), ...,
                         * (Hashkey^48<<1 mod poly)
                         */
                        uint8_t shifted_hkey[IMB_GCM_ENC_KEY_LEN * 8];
                        /**
                         * This is needed for Karatsuba multiply purposes.
                         * Storage for XOR of High 64 bits and low 64 bits
                         * of HashKey mod poly.
                         *
                         * (HashKey<<1 mod poly), (HashKey^2<<1 mod poly), ...,
                         * (Hashkey^128<<1 mod poly)
                         */
                        uint8_t shifted_hkey_k[IMB_GCM_ENC_KEY_LEN * 8];
                } sse_avx;
                struct {
                        /**
                         * This is needed for schoolbook multiply purposes.
                         * (HashKey<<1 mod poly), (HashKey^2<<1 mod poly), ...,
                         * (Hashkey^48<<1 mod poly)
                         */
                        uint8_t shifted_hkey[IMB_GCM_ENC_KEY_LEN * 8];
                } avx2_avx512;
                struct {
                        /**
                         * (HashKey<<1 mod poly), (HashKey^2<<1 mod poly), ...,
                         * (Hashkey^48<<1 mod poly)
                         */
                        uint8_t shifted_hkey[IMB_GCM_ENC_KEY_LEN * 48];
                } vaes_avx512;
        } ghash_keys;
}
#ifdef LINUX
__attribute__((aligned(64)));
#else
;
#endif

#undef IMB_GCM_ENC_KEY_LEN
#undef IMB_GCM_KEY_SETS

/* API data type definitions */
struct IMB_MGR;

typedef void (*init_mb_mgr_t)(struct IMB_MGR *);
typedef IMB_JOB *(*get_next_job_t)(struct IMB_MGR *);
typedef IMB_JOB *(*submit_job_t)(struct IMB_MGR *);
typedef IMB_JOB *(*get_completed_job_t)(struct IMB_MGR *);
typedef IMB_JOB *(*flush_job_t)(struct IMB_MGR *);
typedef uint32_t (*queue_size_t)(struct IMB_MGR *);
typedef void (*keyexp_t)(const void *, void *, void *);
typedef void (*cmac_subkey_gen_t)(const void *, void *, void *);
typedef void (*hash_one_block_t)(const void *, void *);
typedef void (*hash_fn_t)(const void *, const uint64_t, void *);
typedef void (*xcbc_keyexp_t)(const void *, void *, void *, void *);
typedef int (*des_keysched_t)(uint64_t *, const void *);
typedef void (*aes_cfb_t)(void *, const void *, const void *, const void *,
                          uint64_t);
typedef void (*aes_gcm_enc_dec_t)(const struct gcm_key_data *,
                                  struct gcm_context_data *,
                                  uint8_t *, uint8_t const *, uint64_t,
                                  const uint8_t *, uint8_t const *, uint64_t,
                                  uint8_t *, uint64_t);
typedef void (*aes_gcm_enc_dec_iv_t)(const struct gcm_key_data *,
                                     struct gcm_context_data *, uint8_t *,
                                     uint8_t const *, const uint64_t,
                                     const uint8_t *, uint8_t const *,
                                     const uint64_t, uint8_t *,
                                     const uint64_t, const uint64_t);
typedef void (*aes_gcm_init_t)(const struct gcm_key_data *,
                               struct gcm_context_data *,
                               const uint8_t *, uint8_t const *, uint64_t);
typedef void (*aes_gcm_init_var_iv_t)(const struct gcm_key_data *,
                                      struct gcm_context_data *,
                                      const uint8_t *, const uint64_t,
                                      const uint8_t *, const uint64_t);
typedef void (*aes_gcm_enc_dec_update_t)(const struct gcm_key_data *,
                                         struct gcm_context_data *,
                                         uint8_t *, const uint8_t *, uint64_t);
typedef void (*aes_gcm_enc_dec_finalize_t)(const struct gcm_key_data *,
                                           struct gcm_context_data *,
                                           uint8_t *, uint64_t);
typedef void (*aes_gcm_precomp_t)(struct gcm_key_data *);
typedef void (*aes_gcm_pre_t)(const void *, struct gcm_key_data *);

typedef void (*aes_gmac_init_t)(const struct gcm_key_data *,
                                struct gcm_context_data *,
                                const uint8_t *, const uint64_t);
typedef void (*aes_gmac_update_t)(const struct gcm_key_data *,
                                  struct gcm_context_data *,
                                  const uint8_t *, const uint64_t);
typedef void (*aes_gmac_finalize_t)(const struct gcm_key_data *,
                                  struct gcm_context_data *,
                                  uint8_t *, const uint64_t);

typedef void (*chacha_poly_init_t)(const void *,
                                   struct chacha20_poly1305_context_data *,
                                   const void *, const void *, const uint64_t);
typedef void (*chacha_poly_enc_dec_update_t)(const void *,
                                     struct chacha20_poly1305_context_data *,
                                     void *, const void *, const uint64_t);
typedef void (*chacha_poly_finalize_t)(struct chacha20_poly1305_context_data *,
                                    void *, const uint64_t);
typedef void (*ghash_t)(struct gcm_key_data *, const void *,
                        const uint64_t, void *, const uint64_t);

typedef void (*zuc_eea3_1_buffer_t)(const void *, const void *, const void *,
                                    void *, const uint32_t);

typedef void (*zuc_eea3_4_buffer_t)(const void * const *, const void * const *,
                                    const void * const *, void **,
                                    const uint32_t *);

typedef void (*zuc_eea3_n_buffer_t)(const void * const *, const void * const *,
                                    const void * const *, void **,
                                    const uint32_t *, const uint32_t);

typedef void (*zuc_eia3_1_buffer_t)(const void *, const void *, const void *,
                                    const uint32_t, uint32_t *);

typedef void (*zuc_eia3_n_buffer_t)(const void * const *, const void * const *,
                                    const void * const *,
                                    const uint32_t *, uint32_t **,
                                    const uint32_t);


typedef void (*kasumi_f8_1_buffer_t)(const kasumi_key_sched_t *,
                                     const uint64_t, const void *, void *,
                                     const uint32_t);
typedef void (*kasumi_f8_1_buffer_bit_t)(const kasumi_key_sched_t *,
                                         const uint64_t, const void *,
                                         void *,
                                         const uint32_t, const uint32_t);
typedef void (*kasumi_f8_2_buffer_t)(const kasumi_key_sched_t *,
                                     const uint64_t,  const uint64_t,
                                     const void *, void *,
                                     const uint32_t,
                                     const void *, void *,
                                     const uint32_t);
typedef void (*kasumi_f8_3_buffer_t)(const kasumi_key_sched_t *,
                                     const uint64_t,  const uint64_t,
                                     const uint64_t,
                                     const void *, void *,
                                     const void *, void *,
                                     const void *, void *,
                                     const uint32_t);
typedef void (*kasumi_f8_4_buffer_t)(const kasumi_key_sched_t *,
                                     const uint64_t,  const uint64_t,
                                     const uint64_t,  const uint64_t,
                                     const void *, void *,
                                     const void *, void *,
                                     const void *, void *,
                                     const void *, void *,
                                     const uint32_t);
typedef void (*kasumi_f8_n_buffer_t)(const kasumi_key_sched_t *,
                                     const uint64_t *, const void * const *,
                                     void **, const uint32_t *,
                                     const uint32_t);
typedef void (*kasumi_f9_1_buffer_user_t)(const kasumi_key_sched_t *,
                                          const uint64_t, const void *,
                                          const uint32_t, void *,
                                          const uint32_t);
typedef void (*kasumi_f9_1_buffer_t)(const kasumi_key_sched_t *,
                                     const void *,
                                     const uint32_t, void *);
typedef int (*kasumi_init_f8_key_sched_t)(const void *,
                                          kasumi_key_sched_t *);
typedef int (*kasumi_init_f9_key_sched_t)(const void *,
                                          kasumi_key_sched_t *);
typedef size_t (*kasumi_key_sched_size_t)(void);


/**
 * Snow3G key scheduling structure
 */
typedef struct snow3g_key_schedule_s {
        /* KEY */
        uint32_t k[4];
} snow3g_key_schedule_t;

typedef void (*snow3g_f8_1_buffer_t)(const snow3g_key_schedule_t *,
                                     const void *, const void *,
                                     void *, const uint32_t);

typedef void (*snow3g_f8_1_buffer_bit_t)(const snow3g_key_schedule_t *,
                                         const void *, const void *, void *,
                                         const uint32_t, const uint32_t);

typedef void (*snow3g_f8_2_buffer_t)(const snow3g_key_schedule_t *,
                                     const void *, const void *,
                                     const void *, void *, const uint32_t,
                                     const void *, void *,const uint32_t);

typedef void (*snow3g_f8_4_buffer_t)(const snow3g_key_schedule_t *,
                                     const void *, const void *,const void *,
                                     const void *, const void *, void *,
                                     const uint32_t, const void *, void *,
                                     const uint32_t, const void *, void *,
                                     const uint32_t, const void *, void *,
                                     const uint32_t);

typedef void (*snow3g_f8_8_buffer_t)(const snow3g_key_schedule_t *,
                                     const void *, const void *,const void *,
                                     const void *, const void *, const void *,
                                     const void *, const void *, const void *,
                                     void *, const uint32_t, const void *,
                                     void *, const uint32_t, const void *,
                                     void *, const uint32_t, const void *,
                                     void *, const uint32_t, const void *,
                                     void *, const uint32_t, const void *,
                                     void *, const uint32_t, const void *,
                                     void *, const uint32_t, const void *,
                                     void *, const uint32_t);

typedef void
(*snow3g_f8_8_buffer_multikey_t)(const snow3g_key_schedule_t * const [],
                                 const void * const [], const void * const [],
                                 void *[], const uint32_t[]);

typedef void (*snow3g_f8_n_buffer_t)(const snow3g_key_schedule_t *,
                                     const void * const [],
                                     const void * const [],
                                     void *[], const uint32_t[],
                                     const uint32_t);

typedef void
(*snow3g_f8_n_buffer_multikey_t)(const snow3g_key_schedule_t * const [],
                                 const void * const [],
                                 const void * const [],
                                 void *[], const uint32_t[],
                                 const uint32_t);

typedef void (*snow3g_f9_1_buffer_t)(const snow3g_key_schedule_t *,
                                     const void *, const void *,
                                     const uint64_t, void *);

typedef int (*snow3g_init_key_sched_t)(const void *,
                                       snow3g_key_schedule_t *);

typedef size_t (*snow3g_key_sched_size_t)(void);

typedef uint32_t (*hec_32_t)(const uint8_t *);
typedef uint64_t (*hec_64_t)(const uint8_t *);

typedef uint32_t (*crc32_fn_t)(const void *, const uint64_t);
/* Multi-buffer manager flags passed to alloc_mb_mgr() */

#define IMB_FLAG_SHANI_OFF (1ULL << 0) /**< disable use of SHANI extension */
#define IMB_FLAG_AESNI_OFF (1ULL << 1) /**< disable use of AESNI extension */

/**
 * Multi-buffer manager detected features
 * - if bit is set then hardware supports given extension
 * - valid after call to init_mb_mgr() or alloc_mb_mgr()
 * - some HW supported features can be disabled via IMB_FLAG_xxx (see above)
 */

#define IMB_FEATURE_SHANI      (1ULL << 0)
#define IMB_FEATURE_AESNI      (1ULL << 1)
#define IMB_FEATURE_PCLMULQDQ  (1ULL << 2)
#define IMB_FEATURE_CMOV       (1ULL << 3)
#define IMB_FEATURE_SSE4_2     (1ULL << 4)
#define IMB_FEATURE_AVX        (1ULL << 5)
#define IMB_FEATURE_AVX2       (1ULL << 6)
#define IMB_FEATURE_AVX512F    (1ULL << 7)
#define IMB_FEATURE_AVX512DQ   (1ULL << 8)
#define IMB_FEATURE_AVX512CD   (1ULL << 9)
#define IMB_FEATURE_AVX512BW   (1ULL << 10)
#define IMB_FEATURE_AVX512VL   (1ULL << 11)
#define IMB_FEATURE_AVX512_SKX (IMB_FEATURE_AVX512F | IMB_FEATURE_AVX512DQ | \
                                IMB_FEATURE_AVX512CD | IMB_FEATURE_AVX512BW | \
                                IMB_FEATURE_AVX512VL)
#define IMB_FEATURE_VAES       (1ULL << 12)
#define IMB_FEATURE_VPCLMULQDQ (1ULL << 13)
#define IMB_FEATURE_SAFE_DATA  (1ULL << 14)
#define IMB_FEATURE_SAFE_PARAM (1ULL << 15)
#define IMB_FEATURE_GFNI       (1ULL << 16)
#define IMB_FEATURE_AVX512_IFMA (1ULL << 17)
#define IMB_FEATURE_BMI2       (1ULL << 18)

/* TOP LEVEL (IMB_MGR) Data structure fields */

#define IMB_MAX_JOBS 128

typedef struct IMB_MGR {

        uint64_t flags;	  /**< passed to alloc_mb_mgr() */
        uint64_t features; /**< reflects features of multi-buffer instance */

        uint64_t reserved[5]; /**< reserved for the future */
        uint32_t used_arch; /**< Architecture being used */

	int imb_errno; /**< per mb_mgr error status */

        /**
         * ARCH handlers / API
         * Careful as changes here can break ABI compatibility
         * (always include function pointers at the end of the list,
         * before "earliest_job")
         */
        get_next_job_t          get_next_job;
        submit_job_t            submit_job;
        submit_job_t            submit_job_nocheck;
        get_completed_job_t     get_completed_job;
        flush_job_t             flush_job;
        queue_size_t            queue_size;
        keyexp_t                keyexp_128;
        keyexp_t                keyexp_192;
        keyexp_t                keyexp_256;
        cmac_subkey_gen_t       cmac_subkey_gen_128;
        xcbc_keyexp_t           xcbc_keyexp;
        des_keysched_t          des_key_sched;
        hash_one_block_t        sha1_one_block;
        hash_one_block_t        sha224_one_block;
        hash_one_block_t        sha256_one_block;
        hash_one_block_t        sha384_one_block;
        hash_one_block_t        sha512_one_block;
        hash_one_block_t        md5_one_block;
        hash_fn_t               sha1;
        hash_fn_t               sha224;
        hash_fn_t               sha256;
        hash_fn_t               sha384;
        hash_fn_t               sha512;
        aes_cfb_t               aes128_cfb_one;

        aes_gcm_enc_dec_t       gcm128_enc;
        aes_gcm_enc_dec_t       gcm192_enc;
        aes_gcm_enc_dec_t       gcm256_enc;
        aes_gcm_enc_dec_t       gcm128_dec;
        aes_gcm_enc_dec_t       gcm192_dec;
        aes_gcm_enc_dec_t       gcm256_dec;
        aes_gcm_init_t          gcm128_init;
        aes_gcm_init_t          gcm192_init;
        aes_gcm_init_t          gcm256_init;
        aes_gcm_enc_dec_update_t gcm128_enc_update;
        aes_gcm_enc_dec_update_t gcm192_enc_update;
        aes_gcm_enc_dec_update_t gcm256_enc_update;
        aes_gcm_enc_dec_update_t gcm128_dec_update;
        aes_gcm_enc_dec_update_t gcm192_dec_update;
        aes_gcm_enc_dec_update_t gcm256_dec_update;
        aes_gcm_enc_dec_finalize_t gcm128_enc_finalize;
        aes_gcm_enc_dec_finalize_t gcm192_enc_finalize;
        aes_gcm_enc_dec_finalize_t gcm256_enc_finalize;
        aes_gcm_enc_dec_finalize_t gcm128_dec_finalize;
        aes_gcm_enc_dec_finalize_t gcm192_dec_finalize;
        aes_gcm_enc_dec_finalize_t gcm256_dec_finalize;
        aes_gcm_precomp_t       gcm128_precomp;
        aes_gcm_precomp_t       gcm192_precomp;
        aes_gcm_precomp_t       gcm256_precomp;
        aes_gcm_pre_t           gcm128_pre;
        aes_gcm_pre_t           gcm192_pre;
        aes_gcm_pre_t           gcm256_pre;

        zuc_eea3_1_buffer_t eea3_1_buffer;
        zuc_eea3_4_buffer_t eea3_4_buffer;
        zuc_eea3_n_buffer_t eea3_n_buffer;
        zuc_eia3_1_buffer_t eia3_1_buffer;

        kasumi_f8_1_buffer_t      f8_1_buffer;
        kasumi_f8_1_buffer_bit_t  f8_1_buffer_bit;
        kasumi_f8_2_buffer_t      f8_2_buffer;
        kasumi_f8_3_buffer_t      f8_3_buffer;
        kasumi_f8_4_buffer_t      f8_4_buffer;
        kasumi_f8_n_buffer_t      f8_n_buffer;
        kasumi_f9_1_buffer_t      f9_1_buffer;
        kasumi_f9_1_buffer_user_t f9_1_buffer_user;
        kasumi_init_f8_key_sched_t kasumi_init_f8_key_sched;
        kasumi_init_f9_key_sched_t kasumi_init_f9_key_sched;
        kasumi_key_sched_size_t    kasumi_key_sched_size;

        snow3g_f8_1_buffer_bit_t snow3g_f8_1_buffer_bit;
        snow3g_f8_1_buffer_t snow3g_f8_1_buffer;
        snow3g_f8_2_buffer_t snow3g_f8_2_buffer;
        snow3g_f8_4_buffer_t snow3g_f8_4_buffer;
        snow3g_f8_8_buffer_t snow3g_f8_8_buffer;
        snow3g_f8_n_buffer_t snow3g_f8_n_buffer;
        snow3g_f8_8_buffer_multikey_t snow3g_f8_8_buffer_multikey;
        snow3g_f8_n_buffer_multikey_t snow3g_f8_n_buffer_multikey;
        snow3g_f9_1_buffer_t snow3g_f9_1_buffer;
        snow3g_init_key_sched_t snow3g_init_key_sched;
        snow3g_key_sched_size_t snow3g_key_sched_size;

        ghash_t                 ghash;
        zuc_eia3_n_buffer_t     eia3_n_buffer;
        aes_gcm_init_var_iv_t   gcm128_init_var_iv;
        aes_gcm_init_var_iv_t   gcm192_init_var_iv;
        aes_gcm_init_var_iv_t   gcm256_init_var_iv;

        aes_gmac_init_t         gmac128_init;
        aes_gmac_init_t         gmac192_init;
        aes_gmac_init_t         gmac256_init;
        aes_gmac_update_t       gmac128_update;
        aes_gmac_update_t       gmac192_update;
        aes_gmac_update_t       gmac256_update;
        aes_gmac_finalize_t     gmac128_finalize;
        aes_gmac_finalize_t     gmac192_finalize;
        aes_gmac_finalize_t     gmac256_finalize;
        hec_32_t                hec_32;
        hec_64_t                hec_64;
        cmac_subkey_gen_t       cmac_subkey_gen_256;
        aes_gcm_pre_t           ghash_pre;
        crc32_fn_t              crc32_ethernet_fcs;
        crc32_fn_t              crc16_x25;
        crc32_fn_t              crc32_sctp;
        crc32_fn_t              crc24_lte_a;
        crc32_fn_t              crc24_lte_b;
        crc32_fn_t              crc16_fp_data;
        crc32_fn_t              crc11_fp_header;
        crc32_fn_t              crc7_fp_header;
        crc32_fn_t              crc10_iuup_data;
        crc32_fn_t              crc6_iuup_header;
        crc32_fn_t              crc32_wimax_ofdma_data;
        crc32_fn_t              crc8_wimax_ofdma_hcs;

        chacha_poly_init_t           chacha20_poly1305_init;
        chacha_poly_enc_dec_update_t chacha20_poly1305_enc_update;
        chacha_poly_enc_dec_update_t chacha20_poly1305_dec_update;
        chacha_poly_finalize_t       chacha20_poly1305_finalize;

        /* in-order scheduler fields */
        int              earliest_job; /**< byte offset, -1 if none */
        int              next_job;     /**< byte offset */
        IMB_JOB     jobs[IMB_MAX_JOBS];

        /* out of order managers */
        void *aes128_ooo;
        void *aes192_ooo;
        void *aes256_ooo;
        void *docsis128_sec_ooo;
        void *docsis128_crc32_sec_ooo;
        void *docsis256_sec_ooo;
        void *docsis256_crc32_sec_ooo;
        void *des_enc_ooo;
        void *des_dec_ooo;
        void *des3_enc_ooo;
        void *des3_dec_ooo;
        void *docsis_des_enc_ooo;
        void *docsis_des_dec_ooo;

        void *hmac_sha_1_ooo;
        void *hmac_sha_224_ooo;
        void *hmac_sha_256_ooo;
        void *hmac_sha_384_ooo;
        void *hmac_sha_512_ooo;
        void *hmac_md5_ooo;
        void *aes_xcbc_ooo;
        void *aes_ccm_ooo;
        void *aes_cmac_ooo;
        void *zuc_eea3_ooo;
        void *zuc_eia3_ooo;
        void *aes128_cbcs_ooo;
        void *zuc256_eea3_ooo;
        void *zuc256_eia3_ooo;
        void *aes256_ccm_ooo;
        void *aes256_cmac_ooo;
        void *snow3g_uea2_ooo;
        void *snow3g_uia2_ooo;
} IMB_MGR;

/**
 * API definitions
 */


/**
 * @brief Get library version in string format
 *
 * @return library version string
 */
IMB_DLL_EXPORT const char *imb_get_version_str(void);

/**
 * @brief Get library version in numerical format
 *
 * Use IMB_VERSION() macro to compare this
 * numerical version against known library version.
 *
 * @return library version number
 */
IMB_DLL_EXPORT unsigned imb_get_version(void);


/**
 * @brief API to get error status
 *
 * @param mb_mgr Pointer to multi-buffer manager
 *
 * @retval Integer error type
 */
IMB_DLL_EXPORT int imb_get_errno(IMB_MGR *mb_mgr);

/**
 * @brief API to get description for \a errnum
 *
 * @param errnum error type
 *
 * @retval String description of \a errnum
 */
IMB_DLL_EXPORT const char *imb_get_strerror(int errnum);

/**
 * get_next_job returns a job object. This must be filled in and returned
 * via submit_job before get_next_job is called again.
 * After submit_job is called, one should call get_completed_job() at least
 * once (and preferably until it returns NULL).
 * get_completed_job and flush_job returns a job object. This job object ceases
 * to be usable at the next call to get_next_job
 */

/**
 * @brief Allocates memory for multi-buffer manager instance
 *
 * For binary compatibility between library versions
 * it is recommended to use this API.
 *
 * @param flags multi-buffer manager flags
 *     IMB_FLAG_SHANI_OFF - disable use (and detection) of SHA extensions,
 *                          currently SHANI is only available for SSE
 *     IMB_FLAG_AESNI_OFF - disable use (and detection) of AES extensions.
 *
 * @return Pointer to allocated memory for MB_MGR structure
 * @retval NULL on allocation error
 */
IMB_DLL_EXPORT IMB_MGR *alloc_mb_mgr(uint64_t flags);

/**
 * @brief Frees memory allocated previously by alloc_mb_mgr()
 *
 * @param ptr a pointer to allocated MB_MGR structure
 *
 */
IMB_DLL_EXPORT void free_mb_mgr(IMB_MGR *state);

/**
 * @brief Calculates necessary memory size for IMB_MGR.
 *
 * @return Size for IMB_MGR (aligned to 64 bytes)
 */
IMB_DLL_EXPORT size_t imb_get_mb_mgr_size(void);

/**
 * @brief Initializes IMB_MGR pointers to out-of-order managers with
 *        use of externally allocated memory.
 *
 * imb_get_mb_mgr_size() should be called to know how much memory
 * should be allocated externally.
 *
 * init_mb_mgr_XXX() must be called after this function call,
 * whereas XXX is the desired architecture.
 *
 * @param ptr Pointer to allocated memory
 * @param flags multi-buffer manager flags
 *     IMB_FLAG_SHANI_OFF - disable use (and detection) of SHA extensions,
 *                          currently SHANI is only available for SSE
 *     IMB_FLAG_AESNI_OFF - disable use (and detection) of AES extensions.
 *
 * @param reset_mgr if 0, IMB_MGR structure is not cleared, else it is.
 *
 * @return Pointer to IMB_MGR structure
 */
IMB_DLL_EXPORT IMB_MGR *imb_set_pointers_mb_mgr(void *ptr, const uint64_t flags,
                                                const unsigned reset_mgr);

IMB_DLL_EXPORT void init_mb_mgr_avx(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *submit_job_avx(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *submit_job_nocheck_avx(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *flush_job_avx(IMB_MGR *state);
IMB_DLL_EXPORT uint32_t queue_size_avx(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *get_completed_job_avx(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *get_next_job_avx(IMB_MGR *state);

IMB_DLL_EXPORT void init_mb_mgr_avx2(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *submit_job_avx2(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *submit_job_nocheck_avx2(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *flush_job_avx2(IMB_MGR *state);
IMB_DLL_EXPORT uint32_t queue_size_avx2(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *get_completed_job_avx2(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *get_next_job_avx2(IMB_MGR *state);

IMB_DLL_EXPORT void init_mb_mgr_avx512(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *submit_job_avx512(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *submit_job_nocheck_avx512(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *flush_job_avx512(IMB_MGR *state);
IMB_DLL_EXPORT uint32_t queue_size_avx512(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *get_completed_job_avx512(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *get_next_job_avx512(IMB_MGR *state);

IMB_DLL_EXPORT void init_mb_mgr_sse(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *submit_job_sse(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *submit_job_nocheck_sse(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *flush_job_sse(IMB_MGR *state);
IMB_DLL_EXPORT uint32_t queue_size_sse(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *get_completed_job_sse(IMB_MGR *state);
IMB_DLL_EXPORT IMB_JOB *get_next_job_sse(IMB_MGR *state);

/**
 * @brief Automatically initialize most performant
 *        Multi-buffer manager based on CPU features
 *
 * @param [in]  state Pointer to MB_MGR struct
 * @param [out] arch Pointer to arch enum to be set (can be NULL)
 *
 */
IMB_DLL_EXPORT void init_mb_mgr_auto(IMB_MGR *state, IMB_ARCH *arch);

/**
 * Wrapper macros to call arch API's set up
 * at init phase of multi-buffer manager.
 *
 * For example, after calling init_mb_mgr_sse(&mgr)
 * The 'mgr' structure be set up so that:
 *   mgr.get_next_job will point to get_next_job_sse(),
 *   mgr.submit_job will point to submit_job_sse(),
 *   mgr.submit_job_nocheck will point to submit_job_nocheck_sse(),
 *   mgr.get_completed_job will point to get_completed_job_sse(),
 *   mgr.flush_job will point to flush_job_sse(),
 *   mgr.queue_size will point to queue_size_sse()
 *   mgr.keyexp_128 will point to aes_keyexp_128_sse()
 *   mgr.keyexp_192 will point to aes_keyexp_192_sse()
 *   mgr.keyexp_256 will point to aes_keyexp_256_sse()
 *   etc.
 *
 * Direct use of arch API's may result in better performance.
 * Using below indirect interface may produce slightly worse performance but
 * it can simplify application implementation.
 * The test app provides example of using the indirect interface.
 */
#define IMB_GET_NEXT_JOB(_mgr)       ((_mgr)->get_next_job((_mgr)))
#define IMB_SUBMIT_JOB(_mgr)         ((_mgr)->submit_job((_mgr)))
#define IMB_SUBMIT_JOB_NOCHECK(_mgr) ((_mgr)->submit_job_nocheck((_mgr)))
#define IMB_GET_COMPLETED_JOB(_mgr)  ((_mgr)->get_completed_job((_mgr)))
#define IMB_FLUSH_JOB(_mgr)          ((_mgr)->flush_job((_mgr)))
#define IMB_QUEUE_SIZE(_mgr)         ((_mgr)->queue_size((_mgr)))

/* Key expansion and generation API's */
#define IMB_AES_KEYEXP_128(_mgr, _raw, _enc, _dec)      \
        ((_mgr)->keyexp_128((_raw), (_enc), (_dec)))
#define IMB_AES_KEYEXP_192(_mgr, _raw, _enc, _dec)      \
        ((_mgr)->keyexp_192((_raw), (_enc), (_dec)))
#define IMB_AES_KEYEXP_256(_mgr, _raw, _enc, _dec)      \
        ((_mgr)->keyexp_256((_raw), (_enc), (_dec)))

#define IMB_AES_CMAC_SUBKEY_GEN_128(_mgr, _key_exp, _k1, _k2)   \
        ((_mgr)->cmac_subkey_gen_128((_key_exp), (_k1), (_k2)))

#define IMB_AES_CMAC_SUBKEY_GEN_256(_mgr, _key_exp, _k1, _k2)   \
        ((_mgr)->cmac_subkey_gen_256((_key_exp), (_k1), (_k2)))

#define IMB_AES_XCBC_KEYEXP(_mgr, _key, _k1_exp, _k2, _k3)      \
        ((_mgr)->xcbc_keyexp((_key), (_k1_exp), (_k2), (_k3)))

#define IMB_DES_KEYSCHED(_mgr, _ks, _key)       \
        ((_mgr)->des_key_sched((_ks), (_key)))

/* Hash API's */
#define IMB_SHA1_ONE_BLOCK(_mgr, _data, _digest)        \
        ((_mgr)->sha1_one_block((_data), (_digest)))
#define IMB_SHA1(_mgr, _data, _length, _digest)         \
        ((_mgr)->sha1((_data), (_length), (_digest)))
#define IMB_SHA224_ONE_BLOCK(_mgr, _data, _digest)      \
        ((_mgr)->sha224_one_block((_data), (_digest)))
#define IMB_SHA224(_mgr, _data, _length, _digest)       \
        ((_mgr)->sha224((_data), (_length), (_digest)))
#define IMB_SHA256_ONE_BLOCK(_mgr, _data, _digest)      \
        ((_mgr)->sha256_one_block((_data), (_digest)))
#define IMB_SHA256(_mgr, _data, _length, _digest)       \
        ((_mgr)->sha256((_data), (_length), (_digest)))
#define IMB_SHA384_ONE_BLOCK(_mgr, _data, _digest)      \
        ((_mgr)->sha384_one_block((_data), (_digest)))
#define IMB_SHA384(_mgr, _data, _length, _digest)       \
        ((_mgr)->sha384((_data), (_length), (_digest)))
#define IMB_SHA512_ONE_BLOCK(_mgr, _data, _digest)      \
        ((_mgr)->sha512_one_block((_data), (_digest)))
#define IMB_SHA512(_mgr, _data, _length, _digest)       \
        ((_mgr)->sha512((_data), (_length), (_digest)))
#define IMB_MD5_ONE_BLOCK(_mgr, _data, _digest)         \
        ((_mgr)->md5_one_block((_data), (_digest)))

/* AES-CFB API */
#define IMB_AES128_CFB_ONE(_mgr, _out, _in, _iv, _enc, _len)            \
        ((_mgr)->aes128_cfb_one((_out), (_in), (_iv), (_enc), (_len)))

/* AES-GCM API's */
#define IMB_AES128_GCM_ENC(_mgr, _key, _ctx, _out, _in, _len, _iv, _aad, _aadl,\
                           _tag, _tagl)                                 \
        ((_mgr)->gcm128_enc((_key), (_ctx), (_out), (_in), (_len), (_iv), \
                            (_aad), (_aadl), (_tag), (_tagl)))
#define IMB_AES192_GCM_ENC(_mgr, _key, _ctx, _out, _in, _len, _iv, _aad, _aadl,\
                           _tag, _tagl)                                 \
        ((_mgr)->gcm192_enc((_key), (_ctx), (_out), (_in), (_len), (_iv), \
                            (_aad), (_aadl), (_tag), (_tagl)))
#define IMB_AES256_GCM_ENC(_mgr, _key, _ctx, _out, _in, _len, _iv, _aad, _aadl,\
                           _tag, _tagl)                                 \
        ((_mgr)->gcm256_enc((_key), (_ctx), (_out), (_in), (_len), (_iv), \
                            (_aad), (_aadl), (_tag), (_tagl)))

#define IMB_AES128_GCM_DEC(_mgr, _key, _ctx, _out, _in, _len, _iv, _aad, _aadl,\
                           _tag, _tagl)                                 \
        ((_mgr)->gcm128_dec((_key), (_ctx), (_out), (_in), (_len), (_iv), \
                            (_aad), (_aadl), (_tag), (_tagl)))
#define IMB_AES192_GCM_DEC(_mgr, _key, _ctx, _out, _in, _len, _iv, _aad, _aadl,\
                           _tag, _tagl)                                 \
        ((_mgr)->gcm192_dec((_key), (_ctx), (_out), (_in), (_len), (_iv), \
                            (_aad), (_aadl), (_tag), (_tagl)))
#define IMB_AES256_GCM_DEC(_mgr, _key, _ctx, _out, _in, _len, _iv, _aad, _aadl,\
                           _tag, _tagl)                                 \
        ((_mgr)->gcm256_dec((_key), (_ctx), (_out), (_in), (_len), (_iv), \
                            (_aad), (_aadl), (_tag), (_tagl)))

#define IMB_AES128_GCM_INIT(_mgr, _key, _ctx, _iv, _aad, _aadl)        \
        ((_mgr)->gcm128_init((_key), (_ctx), (_iv), (_aad), (_aadl)))
#define IMB_AES192_GCM_INIT(_mgr, _key, _ctx, _iv, _aad, _aadl)        \
        ((_mgr)->gcm192_init((_key), (_ctx), (_iv), (_aad), (_aadl)))
#define IMB_AES256_GCM_INIT(_mgr, _key, _ctx, _iv, _aad, _aadl)        \
        ((_mgr)->gcm256_init((_key), (_ctx), (_iv), (_aad), (_aadl)))

#define IMB_AES128_GCM_INIT_VAR_IV(_mgr, _key, _ctx, _iv, _ivl, _aad, _aadl) \
        ((_mgr)->gcm128_init_var_iv((_key), (_ctx), (_iv), (_ivl), \
                                    (_aad), (_aadl)))
#define IMB_AES192_GCM_INIT_VAR_IV(_mgr, _key, _ctx, _iv, _ivl, _aad, _aadl) \
        ((_mgr)->gcm192_init_var_iv((_key), (_ctx), (_iv), (_ivl), \
                                    (_aad), (_aadl)))
#define IMB_AES256_GCM_INIT_VAR_IV(_mgr, _key, _ctx, _iv, _ivl, _aad, _aadl) \
        ((_mgr)->gcm256_init_var_iv((_key), (_ctx), (_iv), (_ivl), \
                                    (_aad), (_aadl)))

#define IMB_AES128_GCM_ENC_UPDATE(_mgr, _key, _ctx, _out, _in, _len)    \
        ((_mgr)->gcm128_enc_update((_key), (_ctx), (_out), (_in), (_len)))
#define IMB_AES192_GCM_ENC_UPDATE(_mgr, _key, _ctx, _out, _in, _len)    \
        ((_mgr)->gcm192_enc_update((_key), (_ctx), (_out), (_in), (_len)))
#define IMB_AES256_GCM_ENC_UPDATE(_mgr, _key, _ctx, _out, _in, _len)    \
        ((_mgr)->gcm256_enc_update((_key), (_ctx), (_out), (_in), (_len)))

#define IMB_AES128_GCM_DEC_UPDATE(_mgr, _key, _ctx, _out, _in, _len)    \
        ((_mgr)->gcm128_dec_update((_key), (_ctx), (_out), (_in), (_len)))
#define IMB_AES192_GCM_DEC_UPDATE(_mgr, _key, _ctx, _out, _in, _len)    \
        ((_mgr)->gcm192_dec_update((_key), (_ctx), (_out), (_in), (_len)))
#define IMB_AES256_GCM_DEC_UPDATE(_mgr, _key, _ctx, _out, _in, _len)    \
        ((_mgr)->gcm256_dec_update((_key), (_ctx), (_out), (_in), (_len)))

#define IMB_AES128_GCM_ENC_FINALIZE(_mgr, _key, _ctx, _tag, _tagl)      \
        ((_mgr)->gcm128_enc_finalize((_key), (_ctx), (_tag), (_tagl)))
#define IMB_AES192_GCM_ENC_FINALIZE(_mgr, _key, _ctx, _tag, _tagl)      \
        ((_mgr)->gcm192_enc_finalize((_key), (_ctx), (_tag), (_tagl)))
#define IMB_AES256_GCM_ENC_FINALIZE(_mgr, _key, _ctx, _tag, _tagl)      \
        ((_mgr)->gcm256_enc_finalize((_key), (_ctx), (_tag), (_tagl)))

#define IMB_AES128_GCM_DEC_FINALIZE(_mgr, _key, _ctx, _tag, _tagl)      \
        ((_mgr)->gcm128_dec_finalize((_key), (_ctx), (_tag), (_tagl)))
#define IMB_AES192_GCM_DEC_FINALIZE(_mgr, _key, _ctx, _tag, _tagl)      \
        ((_mgr)->gcm192_dec_finalize((_key), (_ctx), (_tag), (_tagl)))
#define IMB_AES256_GCM_DEC_FINALIZE(_mgr, _key, _ctx, _tag, _tagl)      \
        ((_mgr)->gcm256_dec_finalize((_key), (_ctx), (_tag), (_tagl)))

#define IMB_AES128_GMAC_INIT(_mgr, _key, _ctx, _iv, _ivl) \
        ((_mgr)->gmac128_init((_key), (_ctx), (_iv), (_ivl)))
#define IMB_AES192_GMAC_INIT(_mgr, _key, _ctx, _iv, _ivl) \
        ((_mgr)->gmac192_init((_key), (_ctx), (_iv), (_ivl)))
#define IMB_AES256_GMAC_INIT(_mgr, _key, _ctx, _iv, _ivl) \
        ((_mgr)->gmac256_init((_key), (_ctx), (_iv), (_ivl)))

#define IMB_AES128_GMAC_UPDATE(_mgr, _key, _ctx, _in, _len) \
        ((_mgr)->gmac128_update((_key), (_ctx), (_in), (_len)))
#define IMB_AES192_GMAC_UPDATE(_mgr, _key, _ctx, _in, _len) \
        ((_mgr)->gmac192_update((_key), (_ctx), (_in), (_len)))
#define IMB_AES256_GMAC_UPDATE(_mgr, _key, _ctx, _in, _len) \
        ((_mgr)->gmac256_update((_key), (_ctx), (_in), (_len)))

#define IMB_AES128_GMAC_FINALIZE(_mgr, _key, _ctx, _tag, _tagl)      \
        ((_mgr)->gmac128_finalize((_key), (_ctx), (_tag), (_tagl)))
#define IMB_AES192_GMAC_FINALIZE(_mgr, _key, _ctx, _tag, _tagl)      \
        ((_mgr)->gmac192_finalize((_key), (_ctx), (_tag), (_tagl)))
#define IMB_AES256_GMAC_FINALIZE(_mgr, _key, _ctx, _tag, _tagl)      \
        ((_mgr)->gmac256_finalize((_key), (_ctx), (_tag), (_tagl)))

#define IMB_AES128_GCM_PRECOMP(_mgr, _key) \
        ((_mgr)->gcm128_precomp((_key)))
#define IMB_AES192_GCM_PRECOMP(_mgr, _key) \
        ((_mgr)->gcm192_precomp((_key)))
#define IMB_AES256_GCM_PRECOMP(_mgr, _key) \
        ((_mgr)->gcm256_precomp((_key)))

#define IMB_AES128_GCM_PRE(_mgr, _key_in, _key_exp)     \
        ((_mgr)->gcm128_pre((_key_in), (_key_exp)))
#define IMB_AES192_GCM_PRE(_mgr, _key_in, _key_exp)     \
        ((_mgr)->gcm192_pre((_key_in), (_key_exp)))
#define IMB_AES256_GCM_PRE(_mgr, _key_in, _key_exp)     \
        ((_mgr)->gcm256_pre((_key_in), (_key_exp)))

#define IMB_GHASH_PRE(_mgr, _key_in, _key_exp)          \
        ((_mgr)->ghash_pre((_key_in), (_key_exp)))
#define IMB_GHASH(_mgr, _key, _in, _in_len, _io_auth, _out_len) \
        ((_mgr)->ghash((_key), (_in), (_in_len), (_io_auth), (_out_len)))

/* Chacha20-Poly1305 direct API's */
#define IMB_CHACHA20_POLY1305_INIT(_mgr, _key, _ctx, _iv, _aad, _aadl)        \
        ((_mgr)->chacha20_poly1305_init((_key), (_ctx), (_iv), (_aad), (_aadl)))

#define IMB_CHACHA20_POLY1305_ENC_UPDATE(_mgr, _key, _ctx, _out, _in, _len)    \
        ((_mgr)->chacha20_poly1305_enc_update((_key), (_ctx), (_out), (_in), \
                                              (_len)))

#define IMB_CHACHA20_POLY1305_DEC_UPDATE(_mgr, _key, _ctx, _out, _in, _len)    \
        ((_mgr)->chacha20_poly1305_dec_update((_key), (_ctx), (_out), (_in), \
                                              (_len)))

#define IMB_CHACHA20_POLY1305_ENC_FINALIZE(_mgr, _ctx, _tag, _tagl)      \
        ((_mgr)->chacha20_poly1305_finalize((_ctx), (_tag), (_tagl)))

#define IMB_CHACHA20_POLY1305_DEC_FINALIZE(_mgr, _ctx, _tag, _tagl)      \
        ((_mgr)->chacha20_poly1305_finalize((_ctx), (_tag), (_tagl)))

/* ZUC EEA3/EIA3 functions */

/**
 * @brief ZUC EEA3 Confidentiality functions
 *
 * @param _mgr   Pointer to multi-buffer structure
 * @param _key   Pointer to key
 * @param _iv    Pointer to 16-byte IV
 * @param _in    Pointer to Plaintext/Ciphertext input.
 * @param _out   Pointer to Ciphertext/Plaintext output.
 * @param _len   Length of input data in bytes.
 */
#define IMB_ZUC_EEA3_1_BUFFER(_mgr, _key, _iv, _in, _out, _len) \
        ((_mgr)->eea3_1_buffer((_key), (_iv), (_in), (_out), (_len)))
#define IMB_ZUC_EEA3_4_BUFFER(_mgr, _key, _iv, _in, _out, _len) \
        ((_mgr)->eea3_4_buffer((_key), (_iv), (_in), (_out), (_len)))
#define IMB_ZUC_EEA3_N_BUFFER(_mgr, _key, _iv, _in, _out, _len, _num) \
        ((_mgr)->eea3_n_buffer((_key), (_iv), (_in), (_out), (_len), (_num)))


/**
 * @brief ZUC EIA3 Integrity function
 *
 * @param _mgr   Pointer to multi-buffer structure
 * @param _key   Pointer to key
 * @param _iv    Pointer to 16-byte IV
 * @param _in    Pointer to Plaintext/Ciphertext input.
 * @param _len   Length of input data in bits.
 * @param _tag   Pointer to Authenticated Tag output (4 bytes)
 */
#define IMB_ZUC_EIA3_1_BUFFER(_mgr, _key, _iv, _in, _len, _tag) \
        ((_mgr)->eia3_1_buffer((_key), (_iv), (_in), (_len), (_tag)))
#define IMB_ZUC_EIA3_N_BUFFER(_mgr, _key, _iv, _in, _len, _tag, _num) \
        ((_mgr)->eia3_n_buffer((_key), (_iv), (_in), (_len), (_tag), (_num)))


/* KASUMI F8/F9 functions */

/**
 * @brief Kasumi byte-level f8 operation on a single buffer
 *
 * This function performs kasumi f8 operation on a single buffer. The key has
 * already been scheduled with kasumi_init_f8_key_sched().
 * No extra bits are modified.
 *
 * @param [in]  _mgr      Pointer to multi-buffer structure
 * @param [in]  _ctx     Context where the scheduled keys are stored
 * @param [in]  _iv      Initialization vector
 * @param [in]  _in      Input buffer
 * @param [out] _out     Output buffer
 * @param [in]  _len     Length in BYTES
 *
 ******************************************************************************/
#define IMB_KASUMI_F8_1_BUFFER(_mgr, _ctx, _iv, _in, _out, _len) \
        ((_mgr)->f8_1_buffer((_ctx), (_iv), (_in), (_out), (_len)))

/**
 * @brief Kasumi bit-level f8 operation on a single buffer
 *
 * This function performs kasumi f8 operation on a single buffer. The key has
 * already been scheduled with kasumi_init_f8_key_sched().
 * No extra bits are modified.
 *
 * @param [in]  _mgr      Pointer to multi-buffer structure
 * @param [in]  _ctx     Context where the scheduled keys are stored
 * @param [in]  _iv      Initialization vector
 * @param [in]  _in      Input buffer
 * @param [out] _out     Output buffer
 * @param [in]  _len     Length in BITS
 * @param [in]  _offset  Offset in BITS from begin of input buffer
 *
 ******************************************************************************/
#define IMB_KASUMI_F8_1_BUFFER_BIT(_mgr, _ctx, _iv, _in, _out, _len, _offset) \
        ((_mgr)->f8_1_buffer_bit((_ctx), (_iv), (_in), (_out), (_len), \
                                 (_offset)))

/**
 * @brief Kasumi byte-level f8 operation in parallel on two buffers
 *
 * This function performs kasumi f8 operation on a two buffers.
 * They will be processed with the same key, which has already been scheduled
 * with kasumi_init_f8_key_sched().
 *
 * @param [in]  _mgr      Pointer to multi-buffer structure
 * @param [in]  _ctx     Context where the scheduled keys are stored
 * @param [in]  _iv1     Initialization vector for buffer in1
 * @param [in]  _iv2     Initialization vector for buffer in2
 * @param [in]  _in1     Input buffer 1
 * @param [out] _out1    Output buffer 1
 * @param [in]  _len1    Length in BYTES of input buffer 1
 * @param [in]  _in2     Input buffer 2
 * @param [out] _out2    Output buffer 2
 * @param [in]  _len2    Length in BYTES of input buffer 2
 *
 ******************************************************************************/
#define IMB_KASUMI_F8_2_BUFFER(_mgr, _ctx, _iv1, _iv2, _in1, _out1, _len1, \
                               _in2, _out2, _len2) \
        ((_mgr)->f8_2_buffer((_ctx), (_iv1), (_iv2), (_in1), (_out1), (_len1), \
                             (_in2), (_out2), (_len2)))
/**
 * @brief kasumi byte-level f8 operation in parallel on three buffers
 *
 * This function performs kasumi f8 operation on a three buffers.
 * They must all have the same length and they will be processed with the same
 * key, which has already been scheduled with kasumi_init_f8_key_sched().
 *
 * @param [in]  _mgr      Pointer to multi-buffer structure
 * @param [in]  _ctx     Context where the scheduled keys are stored
 * @param [in]  _iv1     Initialization vector for buffer in1
 * @param [in]  _iv2     Initialization vector for buffer in2
 * @param [in]  _iv3     Initialization vector for buffer in3
 * @param [in]  _in1     Input buffer 1
 * @param [out] _out1    Output buffer 1
 * @param [in]  _in2     Input buffer 2
 * @param [out] _out2    Output buffer 2
 * @param [in]  _in3     Input buffer 3
 * @param [out] _out3    Output buffer 3
 * @param [in]  _len     Common length in bytes for all buffers
 *
 ******************************************************************************/
#define IMB_KASUMI_F8_3_BUFFER(_mgr, _ctx, _iv1, _iv2, _iv3, _in1, _out1, \
                               _in2, _out2, _in3, _out3, _len) \
        ((_mgr)->f8_3_buffer((_ctx), (_iv1), (_iv2), (_iv3), (_in1), (_out1), \
                             (_in2), (_out2), (_in3), (_out3), (_len)))
/**
 * @brief kasumi byte-level f8 operation in parallel on four buffers
 *
 * This function performs kasumi f8 operation on four buffers.
 * They must all have the same length and they will be processed with the same
 * key, which has already been scheduled with kasumi_init_f8_key_sched().
 *
 * @param [in]  _mgr      Pointer to multi-buffer structure
 * @param [in]  _ctx     Context where the scheduled keys are stored
 * @param [in]  _iv1     Initialization vector for buffer in1
 * @param [in]  _iv2     Initialization vector for buffer in2
 * @param [in]  _iv3     Initialization vector for buffer in3
 * @param [in]  _iv4     Initialization vector for buffer in4
 * @param [in]  _in1     Input buffer 1
 * @param [out] _out1    Output buffer 1
 * @param [in]  _in2     Input buffer 2
 * @param [out] _out2    Output buffer 2
 * @param [in]  _in3     Input buffer 3
 * @param [out] _out3    Output buffer 3
 * @param [in]  _in4     Input buffer 4
 * @param [out] _out4    Output buffer 4
 * @param [in]  _len     Common length in bytes for all buffers
 *
 ******************************************************************************/
#define IMB_KASUMI_F8_4_BUFFER(_mgr, _ctx, _iv1, _iv2, _iv3, _iv4, \
                               _in1, _out1, _in2, _out2, _in3, _out3, \
                               _in4, _out4, _len) \
        ((_mgr)->f8_4_buffer((_ctx), (_iv1), (_iv2), (_iv3), (_iv4), \
                             (_in1), (_out1), (_in2), (_out2), \
                             (_in3), (_out3), (_in4), (_out4), (_len)))
/**
 * @brief Kasumi f8 operation on N buffers
 *
 * All input buffers can have different lengths and they will be processed
 * with the same key, which has already been scheduled
 * with kasumi_init_f8_key_sched().
 *
 * @param [in]  _mgr     Pointer to multi-buffer structure
 * @param [in]  _ctx     Context where the scheduled keys are stored
 * @param [in]  _iv      Array of IV values
 * @param [in]  _in      Array of input buffers
 * @param [out] _out     Array of output buffers
 * @param [in]  _len     Array of corresponding input buffer lengths in BITS
 * @param [in]  _count   Number of input buffers
 */
#define IMB_KASUMI_F8_N_BUFFER(_mgr, _ctx, _iv, _in, _out, _len, _count) \
        ((_mgr)->f8_n_buffer((_ctx), (_iv), (_in), (_out), (_len), \
                             (_count)))
/**
 * @brief Kasumi bit-level f9 operation on a single buffer.
 *
 * The first QWORD of in represents the COUNT and FRESH, the last QWORD
 * represents the DIRECTION and PADDING. (See 3GPP TS 35.201 v10.0 section 4)
 *
 * The key has already been scheduled with kasumi_init_f9_key_sched().
 *
 * @param [in]  _mgr     Pointer to multi-buffer structure
 * @param [in]  _ctx     Context where the scheduled keys are stored
 * @param [in]  _in      Input buffer
 * @param [in]  _len     Length in BYTES of the data to be hashed
 * @param [out] _tag     Computed digest
 *
 */
#define IMB_KASUMI_F9_1_BUFFER(_mgr, _ctx,  _in, _len, _tag) \
        ((_mgr)->f9_1_buffer((_ctx), (_in), (_len), (_tag)))

/**
 * @brief Kasumi bit-level f9 operation on a single buffer.
 *
 * The key has already been scheduled with kasumi_init_f9_key_sched().
 *
 * @param [in]  _mgr     Pointer to multi-buffer structure
 * @param [in]  _ctx     Context where the scheduled keys are stored
 * @param [in]  _iv      Initialization vector
 * @param [in]  _in      Input buffer
 * @param [in]  _len     Length in BITS of the data to be hashed
 * @param [out] _tag     Computed digest
 * @param [in]  _dir     Direction bit
 *
 */
#define IMB_KASUMI_F9_1_BUFFER_USER(_mgr, _ctx, _iv, _in, _len, _tag, _dir) \
        ((_mgr)->f9_1_buffer_user((_ctx), (_iv), (_in), (_len), \
                                  (_tag), (_dir)))

/**
 * KASUMI F8 key schedule init function.
 *
 * @param[in]  _mgr      Pointer to multi-buffer structure
 * @param[in]  _key      Confidentiality key (expected in LE format)
 * @param[out] _ctx      Key schedule context to be initialised
 * @return 0 on success, -1 on failure
 *
 ******************************************************************************/
#define IMB_KASUMI_INIT_F8_KEY_SCHED(_mgr, _key, _ctx)     \
        ((_mgr)->kasumi_init_f8_key_sched((_key), (_ctx)))

/**
 * KASUMI F9 key schedule init function.
 *
 * @param[in]  _mgr      Pointer to multi-buffer structure
 * @param[in]  _key      Integrity key (expected in LE format)
 * @param[out] _ctx      Key schedule context to be initialised
 * @return 0 on success, -1 on failure
 *
 ******************************************************************************/
#define IMB_KASUMI_INIT_F9_KEY_SCHED(_mgr, _key, _ctx)     \
        ((_mgr)->kasumi_init_f9_key_sched((_key), (_ctx)))

/**
 *******************************************************************************
 * This function returns the size of the kasumi_key_sched_t, used
 * to store the key schedule.
 *
 * @param[in]  _mgr      Pointer to multi-buffer structure
 * @return size of kasumi_key_sched_t type success
 *
 ******************************************************************************/
#define IMB_KASUMI_KEY_SCHED_SIZE(_mgr)((_mgr)->kasumi_key_sched_size())


/* SNOW3G F8/F9 functions */

/**
 * This function performs snow3g f8 operation on a single buffer. The key has
 * already been scheduled with snow3g_init_key_sched().
 *
 * @param[in]  _mgr           Pointer to multi-buffer structure
 * @param[in]  _ctx           Context where the scheduled keys are stored
 * @param[in]  _iv            iv[3] = count
 *                           iv[2] = (bearer << 27) | ((dir & 0x1) << 26)
 *                           iv[1] = pIV[3]
 *                           iv[0] = pIV[2]
 * @param[in]  _in            Input buffer
 * @param[out] _out           Output buffer
 * @param[in]  _len           Length in bits of input buffer
 * @param[in]  _offset        Offset in input/output buffer (in bits)
 */
#define IMB_SNOW3G_F8_1_BUFFER_BIT(_mgr, _ctx, _iv, _in, _out, _len, _offset) \
        ((_mgr)->snow3g_f8_1_buffer_bit((_ctx), (_iv), (_in),           \
                                        (_out), (_len), (_offset)))

/**
 * This function performs snow3g f8 operation on a single buffer. The key has
 * already been scheduled with snow3g_init_key_sched().
 *
 * @param[in]  _mgr           Pointer to multi-buffer structure
 * @param[in]  _ctx           Context where the scheduled keys are stored
 * @param[in]  _iv            iv[3] = count
 *                           iv[2] = (bearer << 27) | ((dir & 0x1) << 26)
 *                           iv[1] = pIV[3]
 *                           iv[0] = pIV[2]
 * @param[in]  _in            Input buffer
 * @param[out] _out           Output buffer
 * @param[in]  _len           Length in bits of input buffer
 */
#define IMB_SNOW3G_F8_1_BUFFER(_mgr, _ctx, _iv, _in, _out, _len)        \
        ((_mgr)->snow3g_f8_1_buffer((_ctx), (_iv), (_in), (_out), (_len)))

/**
 * This function performs snow3g f8 operation on two buffers. They will
 * be processed with the same key, which has already been scheduled with
 * snow3g_init_key_sched().
 *
 * @param[in]  _mgr           Pointer to multi-buffer structure
 * @param[in]  _ctx           Context where the scheduled keys are stored
 * @param[in]  _iv1           IV to use for buffer pBufferIn1
 * @param[in]  _iv2           IV to use for buffer pBufferIn2
 * @param[in]  _in1           Input buffer 1
 * @param[out] _out1          Output buffer 1
 * @param[in]  _len1          Length in bytes of input buffer 1
 * @param[in]  _in2           Input buffer 2
 * @param[out] _out2          Output buffer 2
 * @param[in]  _len2          Length in bytes of input buffer 2
 */
#define IMB_SNOW3G_F8_2_BUFFER(_mgr, _ctx, _iv1, _iv2,        \
                               _in1, _out1, _len1,            \
                               _in2, _out2, _len2)            \
        ((_mgr)->snow3g_f8_2_buffer((_ctx), (_iv1), (_iv2),   \
                                    (_in1), (_out1), (_len1), \
                                    (_in2), (_out2), (_len2)))

/**
 *******************************************************************************
 * This function performs snow3g f8 operation on four buffers. They will
 * be processed with the same key, which has already been scheduled with
 * snow3g_init_key_sched().
 *
 * @param[in]  _mgr           Pointer to multi-buffer structure
 * @param[in]  _ctx           Context where the scheduled keys are stored
 * @param[in]  _iv1           IV to use for buffer pBufferIn1
 * @param[in]  _iv2           IV to use for buffer pBufferIn2
 * @param[in]  _iv3           IV to use for buffer pBufferIn3
 * @param[in]  _iv4           IV to use for buffer pBufferIn4
 * @param[in]  _in1           Input buffer 1
 * @param[out] _out1          Output buffer 1
 * @param[in]  _len1          Length in bytes of input buffer 1
 * @param[in]  _in2           Input buffer 2
 * @param[out] _out2          Output buffer 2
 * @param[in]  _len2          Length in bytes of input buffer 2
 * @param[in]  _in3           Input buffer 3
 * @param[out] _out3          Output buffer 3
 * @param[in]  _len3          Length in bytes of input buffer 3
 * @param[in]  _in4           Input buffer 4
 * @param[out] _out4          Output buffer 4
 * @param[in]  _len4          Length in bytes of input buffer 4
 */
#define IMB_SNOW3G_F8_4_BUFFER(_mgr, _ctx, _iv1, _iv2, _iv3, _iv4,      \
                               _in1, _out1, _len1,                      \
                               _in2, _out2, _len2,                      \
                               _in3, _out3, _len3,                      \
                               _in4, _out4, _len4)                      \
        ((_mgr)->snow3g_f8_4_buffer((_ctx), (_iv1), (_iv2), (_iv3), (_iv4), \
                                    (_in1), (_out1), (_len1), \
                                    (_in2), (_out2), (_len2), \
                                    (_in3), (_out3), (_len3), \
                                    (_in4), (_out4), (_len4)))

/**
 *******************************************************************************
 * This function performs snow3g f8 operation on eight buffers. They will
 * be processed with the same key, which has already been scheduled with
 * snow3g_init_key_sched().
 *
 * @param[in]  _mgr           Pointer to multi-buffer structure
 * @param[in]  _ctx           Context where the scheduled keys are stored
 * @param[in]  _iv1           IV to use for buffer pBufferIn1
 * @param[in]  _iv2           IV to use for buffer pBufferIn2
 * @param[in]  _iv3           IV to use for buffer pBufferIn3
 * @param[in]  _iv4           IV to use for buffer pBufferIn4
 * @param[in]  _iv5           IV to use for buffer pBufferIn5
 * @param[in]  _iv6           IV to use for buffer pBufferIn6
 * @param[in]  _iv7           IV to use for buffer pBufferIn7
 * @param[in]  _iv8           IV to use for buffer pBufferIn8
 * @param[in]  _in1           Input buffer 1
 * @param[out] _out1          Output buffer 1
 * @param[in]  _len1          Length in bytes of input buffer 1
 * @param[in]  _in2           Input buffer 2
 * @param[out] _out2          Output buffer 2
 * @param[in]  _len2          Length in bytes of input buffer 2
 * @param[in]  _in3           Input buffer 3
 * @param[out] _out3          Output buffer 3
 * @param[in]  _len3          Length in bytes of input buffer 3
 * @param[in]  _in4           Input buffer 4
 * @param[out] _out4          Output buffer 4
 * @param[in]  _len4          Length in bytes of input buffer 4
 * @param[in]  _in5           Input buffer 5
 * @param[out] _out5          Output buffer 5
 * @param[in]  _len5          Length in bytes of input buffer 5
 * @param[in]  _in6           Input buffer 6
 * @param[out] _out6          Output buffer 6
 * @param[in]  _len6          Length in bytes of input buffer 6
 * @param[in]  _in7           Input buffer 7
 * @param[out] _out7          Output buffer 7
 * @param[in]  _len7          Length in bytes of input buffer 7
 * @param[in]  _in8           Input buffer 8
 * @param[out] _out8          Output buffer 8
 * @param[in]  _len8          Length in bytes of input buffer 8
 */
#define IMB_SNOW3G_F8_8_BUFFER(_mgr, _ctx, _iv1, _iv2, _iv3, _iv4,      \
                               _iv5, _iv6, _iv7, _iv8,                  \
                               _in1, _out1, _len1,                      \
                               _in2, _out2, _len2,                      \
                               _in3, _out3, _len3,                      \
                               _in4, _out4, _len4,                      \
                               _in5, _out5, _len5,                      \
                               _in6, _out6, _len6,                      \
                               _in7, _out7, _len7,                      \
                               _in8, _out8, _len8)                      \
        ((_mgr)->snow3g_f8_8_buffer((_ctx), (_iv1), (_iv2), (_iv3), (_iv4), \
                                    (_iv5), (_iv6), (_iv7), (_iv8),     \
                                    (_in1), (_out1), (_len1),           \
                                    (_in2), (_out2), (_len2),           \
                                    (_in3), (_out3), (_len3),           \
                                    (_in4), (_out4), (_len4),           \
                                    (_in5), (_out5), (_len5),           \
                                    (_in6), (_out6), (_len6),           \
                                    (_in7), (_out7), (_len7),           \
                                    (_in8), (_out8), (_len8)))
/**
 * This function performs snow3g f8 operation on eight buffers. They will
 * be processed with individual keys, which have already been scheduled
 * with snow3g_init_key_sched().
 *
 * @param[in]  _mgr      Pointer to multi-buffer structure
 * @param[in]  _ctx      Array of 8 Contexts, where the scheduled keys
 * are stored
 * @param[in]  _iv       Array of 8 IV values
 * @param[in]  _in       Array of 8 input buffers
 * @param[out] _out      Array of 8 output buffers
 * @param[in]  _len     Array of 8 corresponding input buffer lengths
 */
#define IMB_SNOW3G_F8_8_BUFFER_MULTIKEY(_mgr, _ctx, _iv, _in, _out, _len) \
        ((_mgr)->snow3g_f8_8_buffer_multikey((_ctx), (_iv), (_in), (_out),\
                                             (_len)))

/**
 * This function performs snow3g f8 operation in parallel on N buffers. All
 * input buffers can have different lengths and they will be processed with the
 * same key, which has already been scheduled with snow3g_init_key_sched().
 *
 * @param[in]  _mgr      Pointer to multi-buffer structure
 * @param[in]  _ctx      Context where the scheduled keys are stored
 * @param[in]  _iv       Array of IV values
 * @param[in]  _in       Array of input buffers
 * @param[out] _out      Array of output buffers - out[0] set to NULL on failure
 * @param[in]  _len      Array of corresponding input buffer lengths
 * @param[in]  _count    Number of input buffers
 *
 ******************************************************************************/
#define IMB_SNOW3G_F8_N_BUFFER(_mgr, _ctx, _iv, _in, _out, _len, _count) \
        ((_mgr)->snow3g_f8_n_buffer((_ctx), (_iv), (_in), \
                                    (_out), (_len), (_count)))

/**
 * This function performs snow3g f8 operation in parallel on N buffers. All
 * input buffers can have different lengths. Confidentiallity keys can vary,
 * schedules with snow3g_init_key_sched_multi().
 *
 * @param[in]  _mgr      Pointer to multi-buffer structure
 * @param[in]  _ctx      Array of Contexts, where the scheduled keys are stored
 * @param[in]  _iv       Array of IV values
 * @param[in]  _in       Array of input buffers
 * @param[out] _out      Array of output buffers
 *                      - out[0] set to NULL on failure
 * @param[in]  _len      Array of corresponding input buffer lengths
 * @param[in]  _count    Number of input buffers
 */
#define IMB_SNOW3G_F8_N_BUFFER_MULTIKEY(_mgr, _ctx, _iv, _in,           \
                                        _out, _len, _count)             \
        ((_mgr)->snow3g_f8_n_buffer_multikey((_ctx), (_iv), (_in),      \
                                             (_out), (_len), (_count)))

/**
 * This function performs a snow3g f9 operation on a single block of data. The
 * key has already been scheduled with snow3g_init_f8_key_sched().
 *
 * @param[in]  _mgr      Pointer to multi-buffer structure
 * @param[in]  _ctx      Context where the scheduled keys are stored
 * @param[in]  _iv       iv[3] = _BSWAP32(fresh^(dir<<15))
 *                      iv[2] = _BSWAP32(count^(dir<<31))
 *                      iv[1] = _BSWAP32(fresh)
 *                      iv[0] = _BSWAP32(count)
 *
 * @param[in]  _in       Input buffer
 * @param[in]  _len      Length in bits of the data to be hashed
 * @param[out] _digest   Computed digest
 */
#define IMB_SNOW3G_F9_1_BUFFER(_mgr, _ctx, _iv, _in, _len, _digest)     \
        ((_mgr)->snow3g_f9_1_buffer((_ctx), (_iv), (_in), (_len), (_digest)))

/**
 * Snow3g key schedule init function.
 *
 * @param[in]  _mgr      Pointer to multi-buffer structure
 * @param[in]  _key      Confidentiality/Integrity key (expected in LE format)
 * @param[out] _ctx      Key schedule context to be initialised
 * @return 0 on success
 * @return -1 on error
 *
 ******************************************************************************/
#define IMB_SNOW3G_INIT_KEY_SCHED(_mgr, _key, _ctx)     \
        ((_mgr)->snow3g_init_key_sched((_key), (_ctx)))

/**
 *******************************************************************************
 * This function returns the size of the snow3g_key_schedule_t, used
 * to store the key schedule.
 *
 * @param[in]  _mgr      Pointer to multi-buffer structure
 * @return size of snow3g_key_schedule_t type
 *
 ******************************************************************************/
#define IMB_SNOW3G_KEY_SCHED_SIZE(_mgr)((_mgr)->snow3g_key_sched_size())

/**
 *  HEC compute functions
 */
#define IMB_HEC_32(_mgr, _in)((_mgr)->hec_32(_in))
#define IMB_HEC_64(_mgr, _in)((_mgr)->hec_64(_in))

/**
 * CRC32 Ethernet FCS function
 */
#define IMB_CRC32_ETHERNET_FCS(_mgr,_in,_len) \
        (_mgr)->crc32_ethernet_fcs(_in,_len)

/**
 *  CRC16 X25 function
 */
#define IMB_CRC16_X25(_mgr,_in,_len) \
        (_mgr)->crc16_x25(_in,_len)

/**
 *  CRC32 SCTP function
 */
#define IMB_CRC32_SCTP(_mgr,_in,_len) \
        (_mgr)->crc32_sctp(_in,_len)

/**
 *  LTE CRC24A function
 */
#define IMB_CRC24_LTE_A(_mgr,_in,_len) \
        (_mgr)->crc24_lte_a(_in,_len)

/**
 *  LTE CRC24B function
 */
#define IMB_CRC24_LTE_B(_mgr,_in,_len) \
        (_mgr)->crc24_lte_b(_in,_len)

/**
 *  Framing Protocol CRC16 function (3GPP TS 25.435, 3GPP TS 25.427)
 */
#define IMB_CRC16_FP_DATA(_mgr,_in,_len) \
        (_mgr)->crc16_fp_data(_in,_len)

/**
 *  Framing Protocol CRC11 function (3GPP TS 25.435, 3GPP TS 25.427)
 */
#define IMB_CRC11_FP_HEADER(_mgr,_in,_len) \
        (_mgr)->crc11_fp_header(_in,_len)

/**
 * Framing Protocol CRC7 function (3GPP TS 25.435, 3GPP TS 25.427)
 */
#define IMB_CRC7_FP_HEADER(_mgr,_in,_len) \
        (_mgr)->crc7_fp_header(_in,_len)

/**
 *  IUUP CRC10 function (3GPP TS 25.415)
 */
#define IMB_CRC10_IUUP_DATA(_mgr,_in,_len) \
        (_mgr)->crc10_iuup_data(_in,_len)

/**
 *  IUUP CRC6 function (3GPP TS 25.415)
 */
#define IMB_CRC6_IUUP_HEADER(_mgr,_in,_len) \
        (_mgr)->crc6_iuup_header(_in,_len)

/**
 *  WIMAX OFDMA DATA CRC32 function (IEEE 802.16)
 */
#define IMB_CRC32_WIMAX_OFDMA_DATA(_mgr,_in,_len) \
        (_mgr)->crc32_wimax_ofdma_data(_in,_len)

/**
 *  WIMAX OFDMA HCS CRC8 function (IEEE 802.16)
 */
#define IMB_CRC8_WIMAX_OFDMA_HCS(_mgr,_in,_len) \
        (_mgr)->crc8_wimax_ofdma_hcs(_in,_len)

/* Auxiliary functions */

/**
 * @brief DES key schedule set up
 *
 * \a ks buffer needs to accommodate \a DES_KEY_SCHED_SIZE (128) bytes of data.
 *
 * @param ks destination buffer to accommodate DES key schedule
 * @param key a pointer to an 8 byte DES key
 *
 * @return Operation status
 * @retval 0 success
 * @retval !0 error
 */
IMB_DLL_EXPORT int
des_key_schedule(uint64_t *ks, const void *key);

/* SSE */
IMB_DLL_EXPORT void sha1_sse(const void *data, const uint64_t length,
                             void *digest);
IMB_DLL_EXPORT void sha1_one_block_sse(const void *data, void *digest);
IMB_DLL_EXPORT void sha224_sse(const void *data, const uint64_t length,
                               void *digest);
IMB_DLL_EXPORT void sha224_one_block_sse(const void *data, void *digest);
IMB_DLL_EXPORT void sha256_sse(const void *data, const uint64_t length,
                               void *digest);
IMB_DLL_EXPORT void sha256_one_block_sse(const void *data, void *digest);
IMB_DLL_EXPORT void sha384_sse(const void *data, const uint64_t length,
                               void *digest);
IMB_DLL_EXPORT void sha384_one_block_sse(const void *data, void *digest);
IMB_DLL_EXPORT void sha512_sse(const void *data, const uint64_t length,
                               void *digest);
IMB_DLL_EXPORT void sha512_one_block_sse(const void *data, void *digest);
IMB_DLL_EXPORT void md5_one_block_sse(const void *data, void *digest);
IMB_DLL_EXPORT void aes_keyexp_128_sse(const void *key, void *enc_exp_keys,
                                       void *dec_exp_keys);
IMB_DLL_EXPORT void aes_keyexp_192_sse(const void *key, void *enc_exp_keys,
                                       void *dec_exp_keys);
IMB_DLL_EXPORT void aes_keyexp_256_sse(const void *key, void *enc_exp_keys,
                                       void *dec_exp_keys);
IMB_DLL_EXPORT void aes_xcbc_expand_key_sse(const void *key, void *k1_exp,
                                            void *k2, void *k3);
IMB_DLL_EXPORT void aes_keyexp_128_enc_sse(const void *key,
                                           void *enc_exp_keys);
IMB_DLL_EXPORT void aes_keyexp_192_enc_sse(const void *key,
                                           void *enc_exp_keys);
IMB_DLL_EXPORT void aes_keyexp_256_enc_sse(const void *key,
                                           void *enc_exp_keys);
IMB_DLL_EXPORT void aes_cmac_subkey_gen_sse(const void *key_exp, void *key1,
                                            void *key2);
IMB_DLL_EXPORT void aes_cfb_128_one_sse(void *out, const void *in,
                                        const void *iv, const void *keys,
                                        uint64_t len);
/* AVX */
IMB_DLL_EXPORT void sha1_avx(const void *data, const uint64_t length,
                             void *digest);
IMB_DLL_EXPORT void sha1_one_block_avx(const void *data, void *digest);
IMB_DLL_EXPORT void sha224_avx(const void *data, const uint64_t length,
                               void *digest);
IMB_DLL_EXPORT void sha224_one_block_avx(const void *data, void *digest);
IMB_DLL_EXPORT void sha256_avx(const void *data, const uint64_t length,
                               void *digest);
IMB_DLL_EXPORT void sha256_one_block_avx(const void *data, void *digest);
IMB_DLL_EXPORT void sha384_avx(const void *data, const uint64_t length,
                               void *digest);
IMB_DLL_EXPORT void sha384_one_block_avx(const void *data, void *digest);
IMB_DLL_EXPORT void sha512_avx(const void *data, const uint64_t length,
                               void *digest);
IMB_DLL_EXPORT void sha512_one_block_avx(const void *data, void *digest);
IMB_DLL_EXPORT void md5_one_block_avx(const void *data, void *digest);
IMB_DLL_EXPORT void aes_keyexp_128_avx(const void *key, void *enc_exp_keys,
                                       void *dec_exp_keys);
IMB_DLL_EXPORT void aes_keyexp_192_avx(const void *key, void *enc_exp_keys,
                                       void *dec_exp_keys);
IMB_DLL_EXPORT void aes_keyexp_256_avx(const void *key, void *enc_exp_keys,
                                       void *dec_exp_keys);
IMB_DLL_EXPORT void aes_xcbc_expand_key_avx(const void *key, void *k1_exp,
                                            void *k2, void *k3);
IMB_DLL_EXPORT void aes_keyexp_128_enc_avx(const void *key,
                                           void *enc_exp_keys);
IMB_DLL_EXPORT void aes_keyexp_192_enc_avx(const void *key,
                                           void *enc_exp_keys);
IMB_DLL_EXPORT void aes_keyexp_256_enc_avx(const void *key,
                                           void *enc_exp_keys);
IMB_DLL_EXPORT void aes_cmac_subkey_gen_avx(const void *key_exp, void *key1,
                                            void *key2);
IMB_DLL_EXPORT void aes_cfb_128_one_avx(void *out, const void *in,
                                        const void *iv, const void *keys,
                                        uint64_t len);
/* AVX2 */
IMB_DLL_EXPORT void sha1_avx2(const void *data, const uint64_t length,
                              void *digest);
IMB_DLL_EXPORT void sha1_one_block_avx2(const void *data, void *digest);
IMB_DLL_EXPORT void sha224_avx2(const void *data, const uint64_t length,
                                void *digest);
IMB_DLL_EXPORT void sha224_one_block_avx2(const void *data, void *digest);
IMB_DLL_EXPORT void sha256_avx2(const void *data, const uint64_t length,
                                void *digest);
IMB_DLL_EXPORT void sha256_one_block_avx2(const void *data, void *digest);
IMB_DLL_EXPORT void sha384_avx2(const void *data, const uint64_t length,
                                void *digest);
IMB_DLL_EXPORT void sha384_one_block_avx2(const void *data, void *digest);
IMB_DLL_EXPORT void sha512_avx2(const void *data, const uint64_t length,
                                void *digest);
IMB_DLL_EXPORT void sha512_one_block_avx2(const void *data, void *digest);
IMB_DLL_EXPORT void md5_one_block_avx2(const void *data, void *digest);
IMB_DLL_EXPORT void aes_keyexp_128_avx2(const void *key, void *enc_exp_keys,
                                        void *dec_exp_keys);
IMB_DLL_EXPORT void aes_keyexp_192_avx2(const void *key, void *enc_exp_keys,
                                        void *dec_exp_keys);
IMB_DLL_EXPORT void aes_keyexp_256_avx2(const void *key, void *enc_exp_keys,
                                        void *dec_exp_keys);
IMB_DLL_EXPORT void aes_xcbc_expand_key_avx2(const void *key, void *k1_exp,
                                             void *k2, void *k3);
IMB_DLL_EXPORT void aes_keyexp_128_enc_avx2(const void *key,
                                            void *enc_exp_keys);
IMB_DLL_EXPORT void aes_keyexp_192_enc_avx2(const void *key,
                                            void *enc_exp_keys);
IMB_DLL_EXPORT void aes_keyexp_256_enc_avx2(const void *key,
                                            void *enc_exp_keys);
IMB_DLL_EXPORT void aes_cmac_subkey_gen_avx2(const void *key_exp, void *key1,
                                             void *key2);
IMB_DLL_EXPORT void aes_cfb_128_one_avx2(void *out, const void *in,
                                         const void *iv, const void *keys,
                                         uint64_t len);

/* AVX512 */
IMB_DLL_EXPORT void sha1_avx512(const void *data, const uint64_t length,
                                 void *digest);
IMB_DLL_EXPORT void sha1_one_block_avx512(const void *data, void *digest);
IMB_DLL_EXPORT void sha224_avx512(const void *data, const uint64_t length,
                                  void *digest);
IMB_DLL_EXPORT void sha224_one_block_avx512(const void *data, void *digest);
IMB_DLL_EXPORT void sha256_avx512(const void *data, const uint64_t length,
                                  void *digest);
IMB_DLL_EXPORT void sha256_one_block_avx512(const void *data, void *digest);
IMB_DLL_EXPORT void sha384_avx512(const void *data, const uint64_t length,
                                  void *digest);
IMB_DLL_EXPORT void sha384_one_block_avx512(const void *data, void *digest);
IMB_DLL_EXPORT void sha512_avx512(const void *data, const uint64_t length,
                                  void *digest);
IMB_DLL_EXPORT void sha512_one_block_avx512(const void *data, void *digest);
IMB_DLL_EXPORT void md5_one_block_avx512(const void *data, void *digest);
IMB_DLL_EXPORT void aes_keyexp_128_avx512(const void *key, void *enc_exp_keys,
                                          void *dec_exp_keys);
IMB_DLL_EXPORT void aes_keyexp_192_avx512(const void *key, void *enc_exp_keys,
                                          void *dec_exp_keys);
IMB_DLL_EXPORT void aes_keyexp_256_avx512(const void *key, void *enc_exp_keys,
                                          void *dec_exp_keys);
IMB_DLL_EXPORT void aes_xcbc_expand_key_avx512(const void *key, void *k1_exp,
                                               void *k2, void *k3);
IMB_DLL_EXPORT void aes_keyexp_128_enc_avx512(const void *key,
                                              void *enc_exp_keys);
IMB_DLL_EXPORT void aes_keyexp_192_enc_avx512(const void *key,
                                              void *enc_exp_keys);
IMB_DLL_EXPORT void aes_keyexp_256_enc_avx512(const void *key,
                                              void *enc_exp_keys);
IMB_DLL_EXPORT void aes_cmac_subkey_gen_avx512(const void *key_exp, void *key1,
                                               void *key2);
IMB_DLL_EXPORT void aes_cfb_128_one_avx512(void *out, const void *in,
                                           const void *iv, const void *keys,
                                           uint64_t len);

/**
 * Direct GCM API.
 * Note that GCM is also available through job API.
 */

/**
 * @brief GCM-AES Encryption
 *
 * @param key_data GCM expanded key data
 * @param context_data GCM operation context data
 * @param out Ciphertext output. Encrypt in-place is allowed.
 * @param in Plaintext input.
 * @param len Length of data in Bytes for encryption.
 * @param iv pointer to 12 byte IV structure. Internally, library
 *        concates 0x00000001 value to it.
 * @param aad Additional Authentication Data (AAD).
 * @param aad_len Length of AAD.
 * @param auth_tag Authenticated Tag output.
 * @param auth_tag_len Authenticated Tag Length in bytes (must be
 *                     a multiple of 4 bytes). Valid values are
 *                     16 (most likely), 12 or 8.
 */

IMB_DLL_EXPORT void
aes_gcm_enc_128_sse(const struct gcm_key_data *key_data,
                    struct gcm_context_data *context_data,
                    uint8_t *out, uint8_t const *in, uint64_t len,
                    const uint8_t *iv, uint8_t const *aad, uint64_t aad_len,
                    uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_128_avx_gen2(const struct gcm_key_data *key_data,
                         struct gcm_context_data *context_data,
                         uint8_t *out, uint8_t const *in, uint64_t len,
                         const uint8_t *iv,
                         uint8_t const *aad, uint64_t aad_len,
                         uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_128_avx_gen4(const struct gcm_key_data *key_data,
                         struct gcm_context_data *context_data,
                         uint8_t *out, uint8_t const *in, uint64_t len,
                         const uint8_t *iv,
                         uint8_t const *aad, uint64_t aad_len,
                         uint8_t *auth_tag, uint64_t auth_tag_len);

IMB_DLL_EXPORT void
aes_gcm_enc_192_sse(const struct gcm_key_data *key_data,
                    struct gcm_context_data *context_data,
                    uint8_t *out, uint8_t const *in, uint64_t len,
                    const uint8_t *iv, uint8_t const *aad, uint64_t aad_len,
                    uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_192_avx_gen2(const struct gcm_key_data *key_data,
                         struct gcm_context_data *context_data,
                         uint8_t *out, uint8_t const *in, uint64_t len,
                         const uint8_t *iv,
                         uint8_t const *aad, uint64_t aad_len,
                         uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_192_avx_gen4(const struct gcm_key_data *key_data,
                         struct gcm_context_data *context_data,
                         uint8_t *out, uint8_t const *in, uint64_t len,
                         const uint8_t *iv,
                         uint8_t const *aad, uint64_t aad_len,
                         uint8_t *auth_tag, uint64_t auth_tag_len);

IMB_DLL_EXPORT void
aes_gcm_enc_256_sse(const struct gcm_key_data *key_data,
                    struct gcm_context_data *context_data,
                    uint8_t *out, uint8_t const *in, uint64_t len,
                    const uint8_t *iv,
                    uint8_t const *aad, uint64_t aad_len,
                    uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_256_avx_gen2(const struct gcm_key_data *key_data,
                         struct gcm_context_data *context_data,
                         uint8_t *out, uint8_t const *in, uint64_t len,
                         const uint8_t *iv,
                         uint8_t const *aad, uint64_t aad_len,
                         uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_256_avx_gen4(const struct gcm_key_data *key_data,
                         struct gcm_context_data *context_data,
                         uint8_t *out, uint8_t const *in, uint64_t len,
                         const uint8_t *iv,
                         uint8_t const *aad, uint64_t aad_len,
                         uint8_t *auth_tag, uint64_t auth_tag_len);

/**
 * @brief GCM-AES Decryption
 *
 * @param key_data GCM expanded keys data
 * @param context_data GCM operation context data
 * @param out Plaintext output. Decrypt in-place is allowed.
 * @param in Ciphertext input.
 * @param len Length of data in Bytes for decryption.
 * @param iv pointer to 12 byte IV structure. Internally, library
 *        concates 0x00000001 value to it.
 * @param aad Additional Authentication Data (AAD).
 * @param aad_len Length of AAD.
 * @param auth_tag Authenticated Tag output.
 * @param auth_tag_len Authenticated Tag Length in bytes (must be
 *                     a multiple of 4 bytes). Valid values are
 *                     16 (most likely), 12 or 8.
 */
IMB_DLL_EXPORT void
aes_gcm_dec_128_sse(const struct gcm_key_data *key_data,
                    struct gcm_context_data *context_data,
                    uint8_t *out, uint8_t const *in, uint64_t len,
                    const uint8_t *iv, uint8_t const *aad, uint64_t aad_len,
                    uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_128_avx_gen2(const struct gcm_key_data *key_data,
                         struct gcm_context_data *context_data,
                         uint8_t *out, uint8_t const *in, uint64_t len,
                         const uint8_t *iv,
                         uint8_t const *aad, uint64_t aad_len,
                         uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_128_avx_gen4(const struct gcm_key_data *key_data,
                         struct gcm_context_data *context_data,
                         uint8_t *out, uint8_t const *in, uint64_t len,
                         const uint8_t *iv,
                         uint8_t const *aad, uint64_t aad_len,
                         uint8_t *auth_tag, uint64_t auth_tag_len);

IMB_DLL_EXPORT void
aes_gcm_dec_192_sse(const struct gcm_key_data *key_data,
                    struct gcm_context_data *context_data,
                    uint8_t *out, uint8_t const *in, uint64_t len,
                    const uint8_t *iv, uint8_t const *aad, uint64_t aad_len,
                    uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_192_avx_gen2(const struct gcm_key_data *key_data,
                         struct gcm_context_data *context_data,
                         uint8_t *out, uint8_t const *in, uint64_t len,
                         const uint8_t *iv,
                         uint8_t const *aad, uint64_t aad_len,
                         uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_192_avx_gen4(const struct gcm_key_data *key_data,
                         struct gcm_context_data *context_data,
                         uint8_t *out, uint8_t const *in, uint64_t len,
                         const uint8_t *iv,
                         uint8_t const *aad, uint64_t aad_len,
                         uint8_t *auth_tag, uint64_t auth_tag_len);

IMB_DLL_EXPORT void
aes_gcm_dec_256_sse(const struct gcm_key_data *key_data,
                    struct gcm_context_data *context_data,
                    uint8_t *out, uint8_t const *in, uint64_t len,
                    const uint8_t *iv, uint8_t const *aad, uint64_t aad_len,
                    uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_256_avx_gen2(const struct gcm_key_data *key_data,
                         struct gcm_context_data *context_data,
                         uint8_t *out, uint8_t const *in, uint64_t len,
                         const uint8_t *iv,
                         uint8_t const *aad, uint64_t aad_len,
                         uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_256_avx_gen4(const struct gcm_key_data *key_data,
                         struct gcm_context_data *context_data,
                         uint8_t *out, uint8_t const *in, uint64_t len,
                         const uint8_t *iv,
                         uint8_t const *aad, uint64_t aad_len,
                         uint8_t *auth_tag, uint64_t auth_tag_len);

/**
 * @brief Start a AES-GCM Encryption message
 *
 * @param key_data GCM expanded key data
 * @param context_data GCM operation context data
 * @param iv pointer to 12 byte IV structure. Internally, library
 *        concates 0x00000001 value to it.
 * @param aad Additional Authentication Data (AAD).
 * @param aad_len Length of AAD.
 *
 */
IMB_DLL_EXPORT void
aes_gcm_init_128_sse(const struct gcm_key_data *key_data,
                     struct gcm_context_data *context_data,
                     const uint8_t *iv, uint8_t const *aad, uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_128_avx_gen2(const struct gcm_key_data *key_data,
                          struct gcm_context_data *context_data,
                          const uint8_t *iv,
                          uint8_t const *aad, uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_128_avx_gen4(const struct gcm_key_data *key_data,
                          struct gcm_context_data *context_data,
                          const uint8_t *iv,
                          uint8_t const *aad, uint64_t aad_len);

IMB_DLL_EXPORT void
aes_gcm_init_192_sse(const struct gcm_key_data *key_data,
                     struct gcm_context_data *context_data,
                     const uint8_t *iv, uint8_t const *aad, uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_192_avx_gen2(const struct gcm_key_data *key_data,
                          struct gcm_context_data *context_data,
                          const uint8_t *iv,
                          uint8_t const *aad, uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_192_avx_gen4(const struct gcm_key_data *key_data,
                          struct gcm_context_data *context_data,
                          const uint8_t *iv,
                          uint8_t const *aad, uint64_t aad_len);

IMB_DLL_EXPORT void
aes_gcm_init_256_sse(const struct gcm_key_data *key_data,
                     struct gcm_context_data *context_data,
                     const uint8_t *iv, uint8_t const *aad, uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_256_avx_gen2(const struct gcm_key_data *key_data,
                          struct gcm_context_data *context_data,
                          const uint8_t *iv,
                          uint8_t const *aad, uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_256_avx_gen4(const struct gcm_key_data *key_data,
                          struct gcm_context_data *context_data,
                          const uint8_t *iv,
                          uint8_t const *aad, uint64_t aad_len);

/**
 * @brief encrypt a block of a AES-GCM Encryption message
 *
 * @param key_data GCM expanded key data
 * @param context_data GCM operation context data
 * @param out Ciphertext output. Encrypt in-place is allowed.
 * @param in Plaintext input.
 * @param len Length of data in Bytes for decryption.
 */
IMB_DLL_EXPORT void
aes_gcm_enc_128_update_sse(const struct gcm_key_data *key_data,
                           struct gcm_context_data *context_data,
                           uint8_t *out, const uint8_t *in, uint64_t len);
IMB_DLL_EXPORT void
aes_gcm_enc_128_update_avx_gen2(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in, uint64_t len);
IMB_DLL_EXPORT void
aes_gcm_enc_128_update_avx_gen4(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in, uint64_t len);

IMB_DLL_EXPORT void
aes_gcm_enc_192_update_sse(const struct gcm_key_data *key_data,
                           struct gcm_context_data *context_data,
                           uint8_t *out, const uint8_t *in, uint64_t len);
IMB_DLL_EXPORT void
aes_gcm_enc_192_update_avx_gen2(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in, uint64_t len);
IMB_DLL_EXPORT void
aes_gcm_enc_192_update_avx_gen4(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in, uint64_t len);

IMB_DLL_EXPORT void
aes_gcm_enc_256_update_sse(const struct gcm_key_data *key_data,
                           struct gcm_context_data *context_data,
                           uint8_t *out, const uint8_t *in, uint64_t len);
IMB_DLL_EXPORT void
aes_gcm_enc_256_update_avx_gen2(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in, uint64_t len);
IMB_DLL_EXPORT void
aes_gcm_enc_256_update_avx_gen4(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in, uint64_t len);

/**
 * @brief decrypt a block of a AES-GCM Encryption message
 *
 * @param key_data GCM expanded key data
 * @param context_data GCM operation context data
 * @param out Plaintext output. Decrypt in-place is allowed.
 * @param in Ciphertext input.
 * @param len Length of data in Bytes for decryption.
 */
IMB_DLL_EXPORT void
aes_gcm_dec_128_update_sse(const struct gcm_key_data *key_data,
                           struct gcm_context_data *context_data,
                           uint8_t *out, const uint8_t *in, uint64_t len);
IMB_DLL_EXPORT void
aes_gcm_dec_128_update_avx_gen2(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in, uint64_t len);
IMB_DLL_EXPORT void
aes_gcm_dec_128_update_avx_gen4(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in, uint64_t len);

IMB_DLL_EXPORT void
aes_gcm_dec_192_update_sse(const struct gcm_key_data *key_data,
                           struct gcm_context_data *context_data,
                           uint8_t *out, const uint8_t *in, uint64_t len);
IMB_DLL_EXPORT void
aes_gcm_dec_192_update_avx_gen2(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in, uint64_t len);
IMB_DLL_EXPORT void
aes_gcm_dec_192_update_avx_gen4(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in, uint64_t len);

IMB_DLL_EXPORT void
aes_gcm_dec_256_update_sse(const struct gcm_key_data *key_data,
                           struct gcm_context_data *context_data,
                           uint8_t *out, const uint8_t *in, uint64_t len);
IMB_DLL_EXPORT void
aes_gcm_dec_256_update_avx_gen2(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in, uint64_t len);
IMB_DLL_EXPORT void
aes_gcm_dec_256_update_avx_gen4(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in, uint64_t len);

/**
 * @brief End encryption of a AES-GCM Encryption message
 *
 * @param key_data GCM expanded key data
 * @param context_data GCM operation context data
 * @param auth_tag Authenticated Tag output.
 * @param auth_tag_len Authenticated Tag Length in bytes (must be
 *                     a multiple of 4 bytes). Valid values are
 *                     16 (most likely), 12 or 8.
 */
IMB_DLL_EXPORT void
aes_gcm_enc_128_finalize_sse(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_128_finalize_avx_gen2(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_128_finalize_avx_gen4(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  uint8_t *auth_tag, uint64_t auth_tag_len);

IMB_DLL_EXPORT void
aes_gcm_enc_192_finalize_sse(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_192_finalize_avx_gen2(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_192_finalize_avx_gen4(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  uint8_t *auth_tag, uint64_t auth_tag_len);

IMB_DLL_EXPORT void
aes_gcm_enc_256_finalize_sse(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_256_finalize_avx_gen2(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_256_finalize_avx_gen4(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  uint8_t *auth_tag, uint64_t auth_tag_len);

/**
 * @brief End decryption of a AES-GCM Encryption message
 *
 * @param key_data GCM expanded key data
 * @param context_data GCM operation context data
 * @param auth_tag Authenticated Tag output.
 * @param auth_tag_len Authenticated Tag Length in bytes (must be
 *                     a multiple of 4 bytes). Valid values are
 *                     16 (most likely), 12 or 8.
 */
IMB_DLL_EXPORT void
aes_gcm_dec_128_finalize_sse(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_128_finalize_avx_gen2(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_128_finalize_avx_gen4(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  uint8_t *auth_tag, uint64_t auth_tag_len);

IMB_DLL_EXPORT void
aes_gcm_dec_192_finalize_sse(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_192_finalize_avx_gen2(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_192_finalize_avx_gen4(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  uint8_t *auth_tag, uint64_t auth_tag_len);

IMB_DLL_EXPORT void
aes_gcm_dec_256_finalize_sse(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_256_finalize_avx_gen2(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_256_finalize_avx_gen4(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  uint8_t *auth_tag, uint64_t auth_tag_len);

/**
 * @brief Precomputation of HashKey constants
 *
 * Precomputation of HashKey<<1 mod poly constants (shifted_hkey_X and
 * shifted_hkey_X_k).
 *
 * @param gdata GCM context data
 */
IMB_DLL_EXPORT void aes_gcm_precomp_128_sse(struct gcm_key_data *key_data);
IMB_DLL_EXPORT void aes_gcm_precomp_128_avx_gen2(struct gcm_key_data *key_data);
IMB_DLL_EXPORT void aes_gcm_precomp_128_avx_gen4(struct gcm_key_data *key_data);

IMB_DLL_EXPORT void aes_gcm_precomp_192_sse(struct gcm_key_data *key_data);
IMB_DLL_EXPORT void aes_gcm_precomp_192_avx_gen2(struct gcm_key_data *key_data);
IMB_DLL_EXPORT void aes_gcm_precomp_192_avx_gen4(struct gcm_key_data *key_data);

IMB_DLL_EXPORT void aes_gcm_precomp_256_sse(struct gcm_key_data *key_data);
IMB_DLL_EXPORT void aes_gcm_precomp_256_avx_gen2(struct gcm_key_data *key_data);
IMB_DLL_EXPORT void aes_gcm_precomp_256_avx_gen4(struct gcm_key_data *key_data);

/**
 * @brief Pre-processes GCM key data
 *
 * Prefills the gcm key data with key values for each round and
 * the initial sub hash key for tag encoding
 *
 * @param key pointer to key data
 * @param key_data GCM expanded key data
 *
 */
IMB_DLL_EXPORT void aes_gcm_pre_128_sse(const void *key,
                                        struct gcm_key_data *key_data);
IMB_DLL_EXPORT void aes_gcm_pre_128_avx_gen2(const void *key,
                                             struct gcm_key_data *key_data);
IMB_DLL_EXPORT void aes_gcm_pre_128_avx_gen4(const void *key,
                                             struct gcm_key_data *key_data);
IMB_DLL_EXPORT void aes_gcm_pre_192_sse(const void *key,
                                        struct gcm_key_data *key_data);
IMB_DLL_EXPORT void aes_gcm_pre_192_avx_gen2(const void *key,
                                             struct gcm_key_data *key_data);
IMB_DLL_EXPORT void aes_gcm_pre_192_avx_gen4(const void *key,
                                             struct gcm_key_data *key_data);
IMB_DLL_EXPORT void aes_gcm_pre_256_sse(const void *key,
                                        struct gcm_key_data *key_data);
IMB_DLL_EXPORT void aes_gcm_pre_256_avx_gen2(const void *key,
                                             struct gcm_key_data *key_data);
IMB_DLL_EXPORT void aes_gcm_pre_256_avx_gen4(const void *key,
                                             struct gcm_key_data *key_data);

/**
 * @brief Generation of ZUC Initialization Vectors (for EEA3 and EIA3)
 *
 * @param [in]  count  COUNT (4 bytes in Little Endian)
 * @param [in]  bearer BEARER (5 bits)
 * @param [in]  dir    DIRECTION (1 bit)
 * @param [out] iv_ptr Pointer to generated IV (16 bytes)
 *
 * @return
 *      - 0 if success
 *      - 1 if one or more parameters are wrong
 */
IMB_DLL_EXPORT int zuc_eea3_iv_gen(const uint32_t count,
                                   const uint8_t bearer,
                                   const uint8_t dir,
                                   void *iv_ptr);
IMB_DLL_EXPORT int zuc_eia3_iv_gen(const uint32_t count,
                                   const uint8_t bearer,
                                   const uint8_t dir,
                                   void *iv_ptr);

/**
 * @brief Generation of KASUMI F8 Initialization Vector
 *
 * @param [in]  count  COUNT (4 bytes in Little Endian)
 * @param [in]  bearer BEARER (5 bits)
 * @param [in]  dir    DIRECTION (1 bit)
 * @param [out] iv_ptr Pointer to generated IV (16 bytes)
 *
 * @return
 *      - 0 if success
 *      - 1 if one or more parameters are wrong
 */
IMB_DLL_EXPORT int kasumi_f8_iv_gen(const uint32_t count,
                                    const uint8_t bearer,
                                    const uint8_t dir,
                                    void *iv_ptr);
/**
 * @brief Generation of KASUMI F9 Initialization Vector
 *
 * @param [in]  count  COUNT (4 bytes in Little Endian)
 * @param [in]  fresh  FRESH (4 bytes in Little Endian)
 * @param [out] iv_ptr Pointer to generated IV (16 bytes)
 *
 * @return
 *      - 0 if success
 *      - 1 if one or more parameters are wrong
 */
IMB_DLL_EXPORT int kasumi_f9_iv_gen(const uint32_t count,
                                    const uint32_t fresh,
                                    void *iv_ptr);

/**
 * @brief Generation of SNOW3G F8 Initialization Vector
 *
 * Parameters are passed in Little Endian format and
 * used to generate the IV in Big Endian format
 *
 * @param [in]  count  COUNT (4 bytes in Little Endian)
 * @param [in]  bearer BEARER (5 bits)
 * @param [in]  dir    DIRECTION (1 bit)
 * @param [out] iv_ptr Pointer to generated IV (16 bytes) in Big Endian format
 *
 * @return
 *      - 0 if success
 *      - 1 if one or more parameters are wrong
 */
IMB_DLL_EXPORT int snow3g_f8_iv_gen(const uint32_t count,
                                    const uint8_t bearer,
                                    const uint8_t dir,
                                    void *iv_ptr);
/**
 * @brief Generation of SNOW3G F9 Initialization Vector
 *
 * Parameters are passed in Little Endian format and
 * used to generate the IV in Big Endian format
 *
 * @param [in]  count  COUNT (4 bytes in Little Endian)
 * @param [in]  fresh  FRESH (4 bytes in Little Endian)
 * @param [in]  dir    DIRECTION (1 bit)
 * @param [out] iv_ptr Pointer to generated IV (16 bytes) in Big Endian format
 *
 * @return
 *      - 0 if success
 *      - 1 if one or more parameters are wrong
 */
IMB_DLL_EXPORT int snow3g_f9_iv_gen(const uint32_t count,
                                    const uint32_t fresh,
                                    const uint8_t dir,
                                    void *iv_ptr);
/**
 * @brief Force clearing/zeroing of memory
 *
 * @param [in] mem   Pointer to memory address to clear
 * @param [in] size  Size of memory to clear (in bytes)
 */
IMB_DLL_EXPORT void imb_clear_mem(void *mem, const size_t size);

#ifdef __cplusplus
}
#endif

#endif /* IMB_IPSEC_MB_H */
