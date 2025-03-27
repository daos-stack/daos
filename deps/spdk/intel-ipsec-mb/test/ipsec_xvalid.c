/**********************************************************************
  Copyright(c) 2019-2021, Intel Corporation All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#ifdef LINUX
#include <stdlib.h> /* posix_memalign() and free() */
#else
#include <malloc.h> /* _aligned_malloc() and aligned_free() */
#endif
#include "misc.h"
#include "utils.h"
#ifdef PIN_BASED_CEC
#include <pin_based_cec.h>
#endif

#ifdef _WIN32
#include <intrin.h>
#define strdup _strdup
#define BSWAP64 _byteswap_uint64
#define __func__ __FUNCTION__
#define strcasecmp _stricmp
#else
#include <x86intrin.h>
#define BSWAP64 __builtin_bswap64
#endif

#include <intel-ipsec-mb.h>

/* maximum size of a test buffer */
#define JOB_SIZE_TOP (16 * 1024)
/* min size of a buffer when testing range of buffers */
#define DEFAULT_JOB_SIZE_MIN 16
/* max size of a buffer when testing range of buffers */
#define DEFAULT_JOB_SIZE_MAX (2 * 1024)
/* number of bytes to increase buffer size when testing range of buffers */
#define DEFAULT_JOB_SIZE_STEP 16

#define DEFAULT_JOB_ITER 10

#define MAX_GCM_AAD_SIZE 1024
#define MAX_CCM_AAD_SIZE 46
#define MAX_AAD_SIZE 1024

#define MAX_IV_SIZE 25 /* IV size for ZUC-256 */

#define MAX_NUM_JOBS 32
#define IMIX_ITER 1000

/* Maximum key and digest size for SHA-512 */
#define MAX_KEY_SIZE    IMB_SHA_512_BLOCK_SIZE
#define MAX_DIGEST_SIZE IMB_SHA512_DIGEST_SIZE_IN_BYTES

#define SEED 0xdeadcafe
#define STACK_DEPTH 8192

static int pattern_auth_key;
static int pattern_cipher_key;
static int pattern_plain_text;
static uint64_t pattern8_auth_key;
static uint64_t pattern8_cipher_key;
static uint64_t pattern8_plain_text;

#define MAX_OOO_MGR_SIZE 8192
#define OOO_MGR_FIRST aes128_ooo
#define OOO_MGR_LAST  zuc_eia3_ooo

/* Struct storing cipher parameters */
struct params_s {
        IMB_CIPHER_MODE         cipher_mode; /* CBC, CNTR, DES, GCM etc. */
        IMB_HASH_ALG            hash_alg; /* SHA-1 or others... */
        uint32_t		key_size;
        uint32_t		buf_size;
        uint64_t		aad_size;
        uint32_t		num_sizes;
};

/* Struct storing all expanded keys */
struct cipher_auth_keys {
        uint8_t temp_buf[IMB_SHA_512_BLOCK_SIZE];
        DECLARE_ALIGNED(uint32_t dust[15 * 4], 16);
        uint8_t ipad[IMB_SHA512_DIGEST_SIZE_IN_BYTES];
        uint8_t opad[IMB_SHA512_DIGEST_SIZE_IN_BYTES];
        DECLARE_ALIGNED(uint32_t k1_expanded[15 * 4], 16);
        DECLARE_ALIGNED(uint8_t	k2[32], 16);
        DECLARE_ALIGNED(uint8_t	k3[16], 16);
        DECLARE_ALIGNED(uint32_t enc_keys[15 * 4], 16);
        DECLARE_ALIGNED(uint32_t dec_keys[15 * 4], 16);
        DECLARE_ALIGNED(struct gcm_key_data gdata_key, 64);
};

/* Struct storing all necessary data for crypto operations */
struct data {
        uint8_t test_buf[MAX_NUM_JOBS][JOB_SIZE_TOP];
        uint8_t src_dst_buf[MAX_NUM_JOBS][JOB_SIZE_TOP];
        uint8_t aad[MAX_AAD_SIZE];
        uint8_t in_digest[MAX_NUM_JOBS][MAX_DIGEST_SIZE];
        uint8_t out_digest[MAX_NUM_JOBS][MAX_DIGEST_SIZE];
        uint8_t cipher_iv[MAX_IV_SIZE];
        uint8_t auth_iv[MAX_IV_SIZE];
        uint8_t ciph_key[MAX_KEY_SIZE];
        uint8_t auth_key[MAX_KEY_SIZE];
        struct cipher_auth_keys enc_keys;
        struct cipher_auth_keys dec_keys;
};

struct custom_job_params {
        IMB_CIPHER_MODE         cipher_mode; /* CBC, CNTR, DES, GCM etc. */
        IMB_HASH_ALG            hash_alg; /* SHA-1 or others... */
        uint32_t                key_size;
};

union params {
        IMB_ARCH                 arch_type;
        struct custom_job_params job_params;
};

struct str_value_mapping {
        const char      *name;
        union params    values;
};

const struct str_value_mapping arch_str_map[] = {
        {.name = "NONE",        .values.arch_type = IMB_ARCH_NONE },
        {.name = "SSE",         .values.arch_type = IMB_ARCH_SSE },
        {.name = "NO-AESNI",    .values.arch_type = IMB_ARCH_NOAESNI },
        {.name = "AVX",         .values.arch_type = IMB_ARCH_AVX },
        {.name = "AVX2",        .values.arch_type = IMB_ARCH_AVX2 },
        {.name = "AVX512",      .values.arch_type = IMB_ARCH_AVX512 }
};

struct str_value_mapping cipher_algo_str_map[] = {
        {
                .name = "AES-CBC-128",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_CBC,
                        .key_size = IMB_KEY_128_BYTES
                }
        },
        {
                .name = "AES-CBC-192",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_CBC,
                        .key_size = IMB_KEY_192_BYTES
                }
        },
        {
                .name = "AES-CBC-256",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_CBC,
                        .key_size = IMB_KEY_256_BYTES
                }
        },
        {
                .name = "AES-CTR-128",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_CNTR,
                        .key_size = IMB_KEY_128_BYTES
                }
        },
        {
                .name = "AES-CTR-192",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_CNTR,
                        .key_size = IMB_KEY_192_BYTES
                }
        },
        {
                .name = "AES-CTR-256",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_CNTR,
                        .key_size = IMB_KEY_256_BYTES
                }
        },
        {
                .name = "AES-CTR-128-BIT-LENGTH",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_CNTR_BITLEN,
                        .key_size = IMB_KEY_128_BYTES
                }
        },
        {
                .name = "AES-CTR-192-BIT-LENGTH",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_CNTR_BITLEN,
                        .key_size = IMB_KEY_192_BYTES
                }
        },
        {
                .name = "AES-CTR-256-BIT-LENGTH",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_CNTR_BITLEN,
                        .key_size = IMB_KEY_256_BYTES
                }
        },
        {
                .name = "AES-ECB-128",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_ECB,
                        .key_size = IMB_KEY_128_BYTES
                }
        },
        {
                .name = "AES-ECB-192",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_ECB,
                        .key_size = IMB_KEY_192_BYTES
                }
        },
        {
                .name = "AES-ECB-256",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_ECB,
                        .key_size = IMB_KEY_256_BYTES
                }
        },
        {
                .name = "DOCSIS-SEC-128",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_DOCSIS_SEC_BPI,
                        .key_size = IMB_KEY_128_BYTES
                }
        },
        {
                .name = "DOCSIS-SEC-256",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_DOCSIS_SEC_BPI,
                        .key_size = IMB_KEY_256_BYTES
                }
        },
        {
                .name = "DOCSIS-DES-64",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_DOCSIS_DES,
                        .key_size = 8
                }
        },
        {
                .name = "DES-CBC-64",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_DES,
                        .key_size = 8
                }
        },
        {
                .name = "3DES-CBC-192",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_DES3,
                        .key_size = 24
                }
        },
        {
                .name = "ZUC-EEA3",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_ZUC_EEA3,
                        .key_size = 16
                }
        },
        {
                .name = "ZUC-EEA3-256",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_ZUC_EEA3,
                        .key_size = 32
                }
        },
        {
                .name = "SNOW3G-UEA2",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_SNOW3G_UEA2_BITLEN,
                        .key_size = 16
                }
        },
        {
                .name = "KASUMI-F8",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_KASUMI_UEA1_BITLEN,
                        .key_size = 16
                }
        },
        {
                .name = "AES-CBCS-128",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_CBCS_1_9,
                        .key_size = 16
                }
        },
        {
                .name = "CHACHA20-256",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_CHACHA20,
                        .key_size = 32
                }
        },
        {
                .name = "SNOW-V",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_SNOW_V,
                        .key_size = 32
                }
        },
        {
                .name = "NULL-CIPHER",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_NULL,
                        .key_size = 0
                }
        }
};

struct str_value_mapping hash_algo_str_map[] = {
        {
                .name = "HMAC-SHA1",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_HMAC_SHA_1
                }
        },
        {
                .name = "HMAC-SHA224",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_HMAC_SHA_224
                }
        },
        {
                .name = "HMAC-SHA256",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_HMAC_SHA_256
                }
        },
        {
                .name = "HMAC-SHA384",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_HMAC_SHA_384
                }
        },
        {
                .name = "HMAC-SHA512",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_HMAC_SHA_512
                }
        },
        {
                .name = "AES-XCBC-128",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_AES_XCBC
                }
        },
        {
                .name = "HMAC-MD5",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_MD5
                }
        },
        {
                .name = "AES-CMAC-128",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_AES_CMAC
                }
        },
        {
                .name = "NULL-HASH",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_NULL
                }
        },
        {
                .name = "AES-CMAC-128-BIT-LENGTH",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_AES_CMAC_BITLEN
                }
        },
        {
                .name = "SHA1",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_SHA_1
                }
        },
        {
                .name = "SHA224",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_SHA_224
                }
        },
        {
                .name = "SHA256",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_SHA_256
                }
        },
        {
                .name = "SHA384",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_SHA_384
                }
        },
        {
                .name = "SHA512",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_SHA_512
                }
        },
        {
                .name = "ZUC-EIA3",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_ZUC_EIA3_BITLEN,
                }
        },
        {
                .name = "SNOW3G-UIA2",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_SNOW3G_UIA2_BITLEN,
                }
        },
        {
                .name = "KASUMI-F9",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_KASUMI_UIA1,
                }
        },
        {
                .name = "DOCSIS-SEC-128-CRC32",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_DOCSIS_CRC32,
                }
        },
        {
                .name = "AES-GMAC-128",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_AES_GMAC_128,
                }
        },
        {
                .name = "AES-GMAC-192",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_AES_GMAC_192,
                }
        },
        {
                .name = "AES-GMAC-256",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_AES_GMAC_256,
                }
        },
        {
                .name = "AES-CMAC-256",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_AES_CMAC_256,
                }
        },
        {
                .name = "POLY1305",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_POLY1305,
                }
        },
        {
                .name = "ZUC-EIA3-256",
                .values.job_params = {
                        .hash_alg = IMB_AUTH_ZUC256_EIA3_BITLEN,
                }
        },
};

struct str_value_mapping aead_algo_str_map[] = {
        {
                .name = "AES-GCM-128",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_GCM,
                        .hash_alg = IMB_AUTH_AES_GMAC,
                        .key_size = IMB_KEY_128_BYTES
                }
        },
        {
                .name = "AES-GCM-192",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_GCM,
                        .hash_alg = IMB_AUTH_AES_GMAC,
                        .key_size = IMB_KEY_192_BYTES
                }
        },
        {
                .name = "AES-GCM-256",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_GCM,
                        .hash_alg = IMB_AUTH_AES_GMAC,
                        .key_size = IMB_KEY_256_BYTES
                }
        },
        {
                .name = "AES-CCM-128",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_CCM,
                        .hash_alg = IMB_AUTH_AES_CCM,
                        .key_size = IMB_KEY_128_BYTES
                }
        },
        {
                .name = "AES-CCM-256",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_CCM,
                        .hash_alg = IMB_AUTH_AES_CCM,
                        .key_size = IMB_KEY_256_BYTES
                }
        },
        {
                .name = "PON-128-BIP-CRC32",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_PON_AES_CNTR,
                        .hash_alg = IMB_AUTH_PON_CRC_BIP,
                        .key_size = IMB_KEY_128_BYTES
                }
        },
        {
                .name = "PON-128-NO-CTR",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_PON_AES_CNTR,
                        .hash_alg = IMB_AUTH_PON_CRC_BIP,
                        .key_size = 0
                }
        },
        {
                .name = "AEAD-CHACHA20-256-POLY1305",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_CHACHA20_POLY1305,
                        .hash_alg = IMB_AUTH_CHACHA20_POLY1305,
                        .key_size = 32
                }
        },
        {
                .name = "SNOW-V-AEAD",
                .values.job_params = {
                        .cipher_mode = IMB_CIPHER_SNOW_V_AEAD,
                        .hash_alg = IMB_AUTH_SNOW_V_AEAD,
                        .key_size = 32
                }
        },
};

/* This struct stores all information about performed test case */
struct variant_s {
        uint32_t arch;
        struct params_s params;
        uint64_t *avg_times;
};

const uint8_t auth_tag_length_bytes[] = {
                12, /* IMB_AUTH_HMAC_SHA_1 */
                14, /* IMB_AUTH_HMAC_SHA_224 */
                16, /* IMB_AUTH_HMAC_SHA_256 */
                24, /* IMB_AUTH_HMAC_SHA_384 */
                32, /* IMB_AUTH_HMAC_SHA_512 */
                12, /* IMB_AUTH_AES_XCBC */
                12, /* IMB_AUTH_MD5 */
                0,  /* IMB_AUTH_NULL */
                16, /* IMB_AUTH_AES_GMAC */
                0,  /* IMB_AUTH_CUSTOM HASH */
                16, /* IMB_AES_CCM */
                16, /* IMB_AES_CMAC */
                20, /* IMB_PLAIN_SHA1 */
                28, /* IMB_PLAIN_SHA_224 */
                32, /* IMB_PLAIN_SHA_256 */
                48, /* IMB_PLAIN_SHA_384 */
                64, /* IMB_PLAIN_SHA_512 */
                4,  /* IMB_AES_CMAC_BITLEN (3GPP) */
                8,  /* IMB_PON */
                4,  /* IMB_ZUC_EIA3_BITLEN */
                IMB_DOCSIS_CRC32_TAG_SIZE, /* IMB_AUTH_DOCSIS_CRC32 */
                4,  /* IMB_AUTH_SNOW3G_UIA2_BITLEN (3GPP) */
                4,  /* IMB_AUTH_KASUMI_UIA1 (3GPP) */
                16, /* IMB_AUTH_AES_GMAC_128 */
                16, /* IMB_AUTH_AES_GMAC_192 */
                16, /* IMB_AUTH_AES_GMAC_256 */
                16, /* IMB_AUTH_AES_CMAC_256 */
                16, /* IMB_AUTH_POLY1305 */
                16, /* IMB_AUTH_CHACHA20_POLY1305 */
                16, /* IMB_AUTH_CHACHA20_POLY1305_SGL */
                4,  /* IMB_AUTH_ZUC256_EIA3_BITLEN */
                16, /* IMB_AUTH_SNOW_V_AEAD */
                16, /* IMB_AUTH_CRC32_ETHERNET_FCS */
                4,  /* IMB_AUTH_CRC32_ETHERNET_FCS */
                4,  /* IMB_AUTH_CRC32_SCTP */
                4,  /* IMB_AUTH_CRC32_WIMAX_OFDMA_DATA */
                4,  /* IMB_AUTH_CRC24_LTE_A */
                4,  /* IMB_AUTH_CRC24_LTE_B */
                4,  /* IMB_AUTH_CRC16_X25 */
                4,  /* IMB_AUTH_CRC16_FP_DATA */
                4,  /* IMB_AUTH_CRC11_FP_HEADER */
                4,  /* IMB_AUTH_CRC10_IUUP_DATA */
                4,  /* IMB_AUTH_CRC8_WIMAX_OFDMA_HCS */
                4,  /* IMB_AUTH_CRC7_FP_HEADER */
                4,  /* IMB_AUTH_CRC6_IUUP_HEADER */
};

/* Minimum, maximum and step values of key sizes */
const uint8_t key_sizes[][3] = {
                {16, 32, 8}, /* IMB_CIPHER_CBC */
                {16, 32, 8}, /* IMB_CIPHER_CNTR */
                {0, 0, 1},   /* IMB_CIPHER_NULL */
                {16, 32, 16}, /* IMB_CIPHER_DOCSIS_SEC_BPI */
                {16, 32, 8}, /* IMB_CIPHER_GCM */
                {0, 0, 1},   /* IMB_CIPHER_CUSTOM */
                {8, 8, 1},   /* IMB_CIPHER_DES */
                {8, 8, 1},   /* IMB_CIPHER_DOCSIS_DES */
                {16, 32, 16},/* IMB_CIPHER_CCM */
                {24, 24, 1}, /* IMB_CIPHER_DES3 */
                {16, 16, 1}, /* IMB_CIPHER_PON_AES_CNTR */
                {16, 32, 8}, /* IMB_CIPHER_ECB */
                {16, 32, 8}, /* IMB_CIPHER_CNTR_BITLEN */
                {16, 32, 16}, /* IMB_CIPHER_ZUC_EEA3 */
                {16, 16, 1}, /* IMB_CIPHER_SNOW3G_UEA2 */
                {16, 16, 1}, /* IMB_CIPHER_KASUMI_UEA1_BITLEN */
                {16, 16, 1}, /* IMB_CIPHER_CBCS_1_9 */
                {32, 32, 1}, /* IMB_CIPHER_CHACHA20 */
                {32, 32, 1}, /* IMB_CIPHER_CHACHA20_POLY1305 */
                {32, 32, 1}, /* IMB_CIPHER_CHACHA20_POLY1305_SGL */
                {32, 32, 1}, /* IMB_CIPHER_SNOW_V */
                {32, 32, 1}, /* IMB_CIPHER_SNOW_V_AEAD */
};

uint8_t custom_test = 0;
uint8_t verbose = 0;

enum range {
        RANGE_MIN = 0,
        RANGE_STEP,
        RANGE_MAX,
        NUM_RANGE
};

uint32_t job_sizes[NUM_RANGE] = {DEFAULT_JOB_SIZE_MIN,
                                 DEFAULT_JOB_SIZE_STEP,
                                 DEFAULT_JOB_SIZE_MAX};
/* Max number of jobs to submit in IMIX testing */
uint32_t max_num_jobs = 16;
/* IMIX disabled by default */
unsigned int imix_enabled = 0;
/* cipher and authentication IV sizes */
uint32_t cipher_iv_size = 0;
uint32_t auth_iv_size = 0;

struct custom_job_params custom_job_params = {
        .cipher_mode  = IMB_CIPHER_NULL,
        .hash_alg     = IMB_AUTH_NULL,
        .key_size = 0
};

/* AESNI_EMU disabled by default */
uint8_t enc_archs[IMB_ARCH_NUM] = {0, 0, 1, 1, 1, 1};
uint8_t dec_archs[IMB_ARCH_NUM] = {0, 0, 1, 1, 1, 1};

uint64_t flags = 0; /* flags passed to alloc_mb_mgr() */

static void
clear_data(struct data *data)
{
        unsigned i;

        for (i = 0; i < MAX_NUM_JOBS; i++) {
                imb_clear_mem(data->test_buf[i], JOB_SIZE_TOP);
                imb_clear_mem(data->src_dst_buf[i], JOB_SIZE_TOP);
                imb_clear_mem(data->in_digest[i], MAX_DIGEST_SIZE);
                imb_clear_mem(data->out_digest[i], MAX_DIGEST_SIZE);
        }

        imb_clear_mem(data->aad, MAX_AAD_SIZE);
        imb_clear_mem(data->cipher_iv, MAX_IV_SIZE);
        imb_clear_mem(data->auth_iv, MAX_IV_SIZE);
        imb_clear_mem(data->ciph_key, MAX_KEY_SIZE);
        imb_clear_mem(data->auth_key, MAX_KEY_SIZE);
        imb_clear_mem(&data->enc_keys, sizeof(struct cipher_auth_keys));
        imb_clear_mem(&data->dec_keys, sizeof(struct cipher_auth_keys));
}

/** Generate random fill patterns */
static void generate_patterns(void)
{
        /* randomize fill values - make sure they are unique and non-zero */
        do {
                pattern_auth_key = rand() & 255;
                pattern_cipher_key = rand() & 255;
                pattern_plain_text = rand() & 255;
        } while (pattern_auth_key == pattern_cipher_key ||
                 pattern_auth_key == pattern_plain_text ||
                 pattern_cipher_key == pattern_plain_text ||
                 pattern_auth_key == 0 ||
                 pattern_cipher_key == 0 ||
                 pattern_plain_text == 0);

        memset(&pattern8_auth_key, pattern_auth_key,
               sizeof(pattern8_auth_key));
        memset(&pattern8_cipher_key, pattern_cipher_key,
               sizeof(pattern8_cipher_key));
        memset(&pattern8_plain_text, pattern_plain_text,
               sizeof(pattern8_plain_text));

        printf(">>> Patterns: AUTH_KEY = 0x%02x, CIPHER_KEY = 0x%02x, "
               "PLAIN_TEXT = 0x%02x\n",
               pattern_auth_key, pattern_cipher_key, pattern_plain_text);
}

/*
 * Searches across a block of memory if a pattern is present
 * (indicating there is some left over sensitive data)
 *
 * Returns 0 if pattern is present or -1 if not present
 */
static int
search_patterns(const void *ptr, const size_t mem_size)
{
        const uint8_t *ptr8 = (const uint8_t *) ptr;
        const size_t limit = mem_size - sizeof(uint64_t);
        int ret = -1;
        size_t i;

        if (mem_size < sizeof(uint64_t)) {
                fprintf(stderr, "Invalid mem_size arg!\n");
                return -1;
        }

        for (i = 0; i <= limit; i++) {
                const uint64_t string = *((const uint64_t *) &ptr8[i]);

                if (string == pattern8_cipher_key) {
                        fprintf(stderr, "Part of CIPHER_KEY is present\n");
                        ret = 0;
                } else if (string == pattern8_auth_key) {
                        fprintf(stderr, "Part of AUTH_KEY is present\n");
                        ret = 0;
                } else if (string == pattern8_plain_text) {
                        fprintf(stderr,
                                "Part of plain/ciphertext is present\n");
                        ret = 0;
                }

                if (ret != -1)
                        break;
        }

        if (ret != -1) {
                size_t len_to_print = mem_size - i;

                fprintf(stderr, "Offset = %zu bytes, Addr = %p, RSP = %p\n",
                        i, &ptr8[i], rdrsp());

                if (len_to_print > 64)
                        len_to_print = 64;

                hexdump_ex(stderr, NULL, &ptr8[i], len_to_print, &ptr8[i]);
                return 0;
        }

        return -1;
}

static size_t
calculate_ooo_mgr_size(const void *ptr)
{
        size_t i;

        for (i = 0; i <= (MAX_OOO_MGR_SIZE - sizeof(uint64_t)); i++) {
                const uint64_t end_of_ooo_pattern = 0xDEADCAFEDEADCAFE;
                const uint8_t *ptr8 = (const uint8_t *) ptr;
                const uint64_t string = *((const uint64_t *) &ptr8[i]);

                if (string == end_of_ooo_pattern)
                        return i + sizeof(uint64_t);
        }

        /* no marker found */
        return MAX_OOO_MGR_SIZE;
}

static size_t
get_ooo_mgr_size(const void *ptr, const unsigned index)
{
        static size_t mgr_sz_tab[64];

        if (index >= DIM(mgr_sz_tab)) {
                fprintf(stderr,
                        "get_ooo_mgr_size() internal table too small!\n");
                exit(EXIT_FAILURE);
        }

        if (mgr_sz_tab[index] == 0)
                mgr_sz_tab[index] = calculate_ooo_mgr_size(ptr);

        return mgr_sz_tab[index];
}

static void
print_algo_info(const struct params_s *params)
{
        struct custom_job_params *job_params;
        uint32_t i;

        for (i = 0; i < DIM(aead_algo_str_map); i++) {
                job_params = &aead_algo_str_map[i].values.job_params;
                if (job_params->cipher_mode == params->cipher_mode &&
                    job_params->hash_alg == params->hash_alg &&
                    job_params->key_size == params->key_size) {
                        printf("AEAD algo = %s ", aead_algo_str_map[i].name);
                        return;
                }
        }

        for (i = 0; i < DIM(cipher_algo_str_map); i++) {
                job_params = &cipher_algo_str_map[i].values.job_params;
                if (job_params->cipher_mode == params->cipher_mode &&
                    job_params->key_size == params->key_size) {
                        printf("Cipher algo = %s ",
                               cipher_algo_str_map[i].name);
                        break;
                }
        }
        for (i = 0; i < DIM(hash_algo_str_map); i++) {
                job_params = &hash_algo_str_map[i].values.job_params;
                if (job_params->hash_alg == params->hash_alg) {
                        printf("Hash algo = %s ", hash_algo_str_map[i].name);
                        break;
                }
        }
}

static int
fill_job(IMB_JOB *job, const struct params_s *params,
         uint8_t *buf, uint8_t *digest, const uint8_t *aad,
         const uint32_t buf_size, const uint8_t tag_size,
         IMB_CIPHER_DIRECTION cipher_dir,
         struct cipher_auth_keys *keys, uint8_t *cipher_iv,
         uint8_t *auth_iv, unsigned index, uint8_t *next_iv)
{
        static const void *ks_ptr[3];
        uint32_t *k1_expanded = keys->k1_expanded;
        uint8_t *k2 = keys->k2;
        uint8_t *k3 = keys->k3;
        uint32_t *enc_keys = keys->enc_keys;
        uint32_t *dec_keys = keys->dec_keys;
        uint8_t *ipad = keys->ipad;
        uint8_t *opad = keys->opad;
        struct gcm_key_data *gdata_key = &keys->gdata_key;

        /* Force partial byte, by subtracting 3 bits from the full length */
        if (params->cipher_mode == IMB_CIPHER_CNTR_BITLEN)
                job->msg_len_to_cipher_in_bits = buf_size * 8 - 3;
        else
                job->msg_len_to_cipher_in_bytes = buf_size;

        job->msg_len_to_hash_in_bytes = buf_size;
        job->hash_start_src_offset_in_bytes = 0;
        job->cipher_start_src_offset_in_bytes = 0;
        job->iv = cipher_iv;
        job->user_data = (void *)((uintptr_t) index);

        if (params->cipher_mode == IMB_CIPHER_PON_AES_CNTR) {
                /* Subtract XGEM header */
                job->msg_len_to_cipher_in_bytes -= 8;
                job->cipher_start_src_offset_in_bytes = 8;
                /* If no crypto needed, set msg_len_to_cipher to 0 */
                if (params->key_size == 0)
                        job->msg_len_to_cipher_in_bytes = 0;
        }

        if (params->hash_alg == IMB_AUTH_DOCSIS_CRC32 &&
            params->cipher_mode == IMB_CIPHER_DOCSIS_SEC_BPI) {
                if (buf_size >=
                    (IMB_DOCSIS_CRC32_MIN_ETH_PDU_SIZE +
                     IMB_DOCSIS_CRC32_TAG_SIZE)) {
                        const uint64_t cipher_adjust = /* SA + DA only */
                                IMB_DOCSIS_CRC32_MIN_ETH_PDU_SIZE - 2;

                        job->cipher_start_src_offset_in_bytes += cipher_adjust;
                        job->msg_len_to_cipher_in_bytes -= cipher_adjust;
                        job->msg_len_to_hash_in_bytes -=
                                IMB_DOCSIS_CRC32_TAG_SIZE;
                } else if (buf_size > IMB_DOCSIS_CRC32_TAG_SIZE) {
                        job->msg_len_to_cipher_in_bytes = 0;
                        job->msg_len_to_hash_in_bytes -=
                                IMB_DOCSIS_CRC32_TAG_SIZE;
                } else {
                        job->msg_len_to_cipher_in_bytes = 0;
                        job->msg_len_to_hash_in_bytes = 0;
                }
        }

        /* In-place operation */
        job->src = buf;
        job->dst = buf + job->cipher_start_src_offset_in_bytes;
        job->auth_tag_output = digest;

        job->hash_alg = params->hash_alg;
        switch (params->hash_alg) {
        case IMB_AUTH_AES_XCBC:
                job->u.XCBC._k1_expanded = k1_expanded;
                job->u.XCBC._k2 = k2;
                job->u.XCBC._k3 = k3;
                break;
        case IMB_AUTH_AES_CMAC:
                job->u.CMAC._key_expanded = k1_expanded;
                job->u.CMAC._skey1 = k2;
                job->u.CMAC._skey2 = k3;
                break;
        case IMB_AUTH_AES_CMAC_BITLEN:
                job->u.CMAC._key_expanded = k1_expanded;
                job->u.CMAC._skey1 = k2;
                job->u.CMAC._skey2 = k3;
                /*
                 * CMAC bit level version is done in bits (length is
                 * converted to bits and it is decreased by 4 bits,
                 * to force the CMAC bitlen path)
                 */
                job->msg_len_to_hash_in_bits =
                        (job->msg_len_to_hash_in_bytes * 8) - 4;
                break;
        case IMB_AUTH_AES_CMAC_256:
                job->u.CMAC._key_expanded = k1_expanded;
                job->u.CMAC._skey1 = k2;
                job->u.CMAC._skey2 = k3;
                break;
        case IMB_AUTH_HMAC_SHA_1:
        case IMB_AUTH_HMAC_SHA_224:
        case IMB_AUTH_HMAC_SHA_256:
        case IMB_AUTH_HMAC_SHA_384:
        case IMB_AUTH_HMAC_SHA_512:
        case IMB_AUTH_MD5:
                /* HMAC hash alg is SHA1 or MD5 */
                job->u.HMAC._hashed_auth_key_xor_ipad =
                        (uint8_t *) ipad;
                job->u.HMAC._hashed_auth_key_xor_opad =
                        (uint8_t *) opad;
                break;
        case IMB_AUTH_ZUC256_EIA3_BITLEN:
                job->u.ZUC_EIA3._key  = k2;
                if (auth_iv_size == 23) {
                        job->u.ZUC_EIA3._iv23 = auth_iv;
                        job->u.ZUC_EIA3._iv = NULL;
                } else {
                        job->u.ZUC_EIA3._iv  = auth_iv;
                        job->u.ZUC_EIA3._iv23 = NULL;
                }
                job->msg_len_to_hash_in_bits =
                        (job->msg_len_to_hash_in_bytes * 8);
                break;
        case IMB_AUTH_ZUC_EIA3_BITLEN:
                job->u.ZUC_EIA3._key  = k2;
                job->u.ZUC_EIA3._iv  = auth_iv;
                job->msg_len_to_hash_in_bits =
                        (job->msg_len_to_hash_in_bytes * 8);
                break;
        case IMB_AUTH_SNOW3G_UIA2_BITLEN:
                job->u.SNOW3G_UIA2._key = k2;
                job->u.SNOW3G_UIA2._iv = auth_iv;
                job->msg_len_to_hash_in_bits =
                        (job->msg_len_to_hash_in_bytes * 8);
                break;
        case IMB_AUTH_KASUMI_UIA1:
                job->u.KASUMI_UIA1._key = k2;
                break;
        case IMB_AUTH_AES_GMAC_128:
        case IMB_AUTH_AES_GMAC_192:
        case IMB_AUTH_AES_GMAC_256:
                job->u.GMAC._key = gdata_key;
                job->u.GMAC._iv = auth_iv;
                job->u.GMAC.iv_len_in_bytes = 12;
                break;
        case IMB_AUTH_PON_CRC_BIP:
        case IMB_AUTH_NULL:
        case IMB_AUTH_AES_GMAC:
        case IMB_AUTH_AES_CCM:
        case IMB_AUTH_SHA_1:
        case IMB_AUTH_SHA_224:
        case IMB_AUTH_SHA_256:
        case IMB_AUTH_SHA_384:
        case IMB_AUTH_SHA_512:
        case IMB_AUTH_GCM_SGL:
        case IMB_AUTH_CRC32_ETHERNET_FCS:
        case IMB_AUTH_CRC32_SCTP:
        case IMB_AUTH_CRC32_WIMAX_OFDMA_DATA:
        case IMB_AUTH_CRC24_LTE_A:
        case IMB_AUTH_CRC24_LTE_B:
        case IMB_AUTH_CRC16_X25:
        case IMB_AUTH_CRC16_FP_DATA:
        case IMB_AUTH_CRC11_FP_HEADER:
        case IMB_AUTH_CRC10_IUUP_DATA:
        case IMB_AUTH_CRC8_WIMAX_OFDMA_HCS:
        case IMB_AUTH_CRC7_FP_HEADER:
        case IMB_AUTH_CRC6_IUUP_HEADER:
                /* No operation needed */
                break;
        case IMB_AUTH_DOCSIS_CRC32:
                break;
        case IMB_AUTH_POLY1305:
                job->u.POLY1305._key = k1_expanded;
                break;
        case IMB_AUTH_CHACHA20_POLY1305:
        case IMB_AUTH_CHACHA20_POLY1305_SGL:
                job->u.CHACHA20_POLY1305.aad_len_in_bytes = params->aad_size;
                job->u.CHACHA20_POLY1305.aad = aad;
                break;
        case IMB_AUTH_SNOW_V_AEAD:
                job->u.SNOW_V_AEAD.aad_len_in_bytes = params->aad_size;
                job->u.SNOW_V_AEAD.aad = aad;
                break;
        default:
                printf("Unsupported hash algorithm %u, line %u\n",
                       (unsigned) params->hash_alg, __LINE__);
                return -1;
        }

        job->auth_tag_output_len_in_bytes = tag_size;

        job->cipher_direction = cipher_dir;

        if (params->cipher_mode == IMB_CIPHER_NULL) {
                job->chain_order = IMB_ORDER_HASH_CIPHER;
        } else if (params->cipher_mode == IMB_CIPHER_CCM ||
                   (params->cipher_mode == IMB_CIPHER_DOCSIS_SEC_BPI &&
                    params->hash_alg == IMB_AUTH_DOCSIS_CRC32)) {
                if (job->cipher_direction == IMB_DIR_ENCRYPT)
                        job->chain_order = IMB_ORDER_HASH_CIPHER;
                else
                        job->chain_order = IMB_ORDER_CIPHER_HASH;
        } else {
                if (job->cipher_direction == IMB_DIR_ENCRYPT)
                        job->chain_order = IMB_ORDER_CIPHER_HASH;
                else
                        job->chain_order = IMB_ORDER_HASH_CIPHER;
        }

        /* Translating enum to the API's one */
        job->cipher_mode = params->cipher_mode;
        job->key_len_in_bytes = params->key_size;

        switch (job->cipher_mode) {
        case IMB_CIPHER_CBC:
        case IMB_CIPHER_DOCSIS_SEC_BPI:
        case IMB_CIPHER_CBCS_1_9:
                job->enc_keys = enc_keys;
                job->dec_keys = dec_keys;
                job->iv_len_in_bytes = 16;
                job->cipher_fields.CBCS.next_iv = next_iv;
                break;
        case IMB_CIPHER_PON_AES_CNTR:
        case IMB_CIPHER_CNTR:
        case IMB_CIPHER_CNTR_BITLEN:
                job->enc_keys = enc_keys;
                job->dec_keys = enc_keys;
                job->iv_len_in_bytes = 16;
                break;
        case IMB_CIPHER_GCM:
                job->enc_keys = gdata_key;
                job->dec_keys = gdata_key;
                job->u.GCM.aad_len_in_bytes = params->aad_size;
                job->u.GCM.aad = aad;
                job->iv_len_in_bytes = 12;
                break;
        case IMB_CIPHER_CCM:
                job->msg_len_to_cipher_in_bytes = buf_size;
                job->msg_len_to_hash_in_bytes = buf_size;
                job->hash_start_src_offset_in_bytes = 0;
                job->cipher_start_src_offset_in_bytes = 0;
                job->u.CCM.aad_len_in_bytes = params->aad_size;
                job->u.CCM.aad = aad;
                job->enc_keys = enc_keys;
                job->dec_keys = enc_keys;
                job->iv_len_in_bytes = 13;
                break;
        case IMB_CIPHER_DES:
        case IMB_CIPHER_DOCSIS_DES:
                job->enc_keys = enc_keys;
                job->dec_keys = enc_keys;
                job->iv_len_in_bytes = 8;
                break;
        case IMB_CIPHER_DES3:
                ks_ptr[0] = ks_ptr[1] = ks_ptr[2] = enc_keys;
                job->enc_keys = ks_ptr;
                job->dec_keys = ks_ptr;
                job->iv_len_in_bytes = 8;
                break;
        case IMB_CIPHER_ECB:
                job->enc_keys = enc_keys;
                job->dec_keys = dec_keys;
                job->iv_len_in_bytes = 0;
                break;
        case IMB_CIPHER_ZUC_EEA3:
                job->enc_keys = k2;
                job->dec_keys = k2;
                if (job->key_len_in_bytes == 16)
                        job->iv_len_in_bytes = 16;
                else /* 32 */
                        job->iv_len_in_bytes = 25;
                break;
        case IMB_CIPHER_SNOW3G_UEA2_BITLEN:
                job->enc_keys = k2;
                job->dec_keys = k2;
                job->iv_len_in_bytes = 16;
                job->cipher_start_src_offset_in_bits = 0;
                job->msg_len_to_cipher_in_bits =
                        (job->msg_len_to_cipher_in_bytes * 8);
                break;
        case IMB_CIPHER_KASUMI_UEA1_BITLEN:
                job->enc_keys = k2;
                job->dec_keys = k2;
                job->iv_len_in_bytes = 8;
                job->cipher_start_src_offset_in_bits = 0;
                job->msg_len_to_cipher_in_bits =
                        (job->msg_len_to_cipher_in_bytes * 8);
                break;
        case IMB_CIPHER_CHACHA20:
        case IMB_CIPHER_CHACHA20_POLY1305:
        case IMB_CIPHER_CHACHA20_POLY1305_SGL:
                job->enc_keys = k2;
                job->dec_keys = k2;
                job->iv_len_in_bytes = 12;
                break;
        case IMB_CIPHER_SNOW_V:
        case IMB_CIPHER_SNOW_V_AEAD:
                job->enc_keys = k2;
                job->dec_keys = k2;
                job->iv_len_in_bytes = 16;
                break;
        case IMB_CIPHER_NULL:
                /* No operation needed */
                break;
        default:
                printf("Unsupported cipher mode\n");
                return -1;
        }

        /*
         * If cipher IV size is set from command line,
         * overwrite the value here.
         */
        if (cipher_iv_size != 0)
                job->iv_len_in_bytes = cipher_iv_size;

        return 0;
}

static int
prepare_keys(IMB_MGR *mb_mgr, struct cipher_auth_keys *keys,
             const uint8_t *ciph_key, const uint8_t *auth_key,
             const struct params_s *params,
             const unsigned int force_pattern)
{
        uint8_t *buf = keys->temp_buf;
        uint32_t *dust = keys->dust;
        uint32_t *k1_expanded = keys->k1_expanded;
        uint8_t *k2 = keys->k2;
        uint8_t *k3 = keys->k3;
        uint32_t *enc_keys = keys->enc_keys;
        uint32_t *dec_keys = keys->dec_keys;
        uint8_t *ipad = keys->ipad;
        uint8_t *opad = keys->opad;
        struct gcm_key_data *gdata_key = &keys->gdata_key;
        uint8_t i;

        /* Set all expanded keys to pattern_cipher_key/pattern_auth_key
         * if flag is set */
        if (force_pattern) {
                switch (params->hash_alg) {
                case IMB_AUTH_AES_XCBC:
                        memset(k1_expanded, pattern_auth_key,
                               sizeof(keys->k1_expanded));
                        break;
                case IMB_AUTH_AES_CMAC:
                case IMB_AUTH_AES_CMAC_BITLEN:
                case IMB_AUTH_AES_CMAC_256:
                        memset(k1_expanded, pattern_auth_key,
                               sizeof(keys->k1_expanded));
                        memset(k2, pattern_auth_key, sizeof(keys->k2));
                        memset(k3, pattern_auth_key, sizeof(keys->k3));
                        break;
                case IMB_AUTH_POLY1305:
                        memset(k1_expanded, pattern_auth_key,
                               sizeof(keys->k1_expanded));
                        break;
                case IMB_AUTH_HMAC_SHA_1:
                case IMB_AUTH_HMAC_SHA_224:
                case IMB_AUTH_HMAC_SHA_256:
                case IMB_AUTH_HMAC_SHA_384:
                case IMB_AUTH_HMAC_SHA_512:
                case IMB_AUTH_MD5:
                        memset(ipad, pattern_auth_key, sizeof(keys->ipad));
                        memset(opad, pattern_auth_key, sizeof(keys->opad));
                        break;
                case IMB_AUTH_ZUC_EIA3_BITLEN:
                case IMB_AUTH_ZUC256_EIA3_BITLEN:
                case IMB_AUTH_SNOW3G_UIA2_BITLEN:
                case IMB_AUTH_KASUMI_UIA1:
                        memset(k3, pattern_auth_key, sizeof(keys->k3));
                        break;
                case IMB_AUTH_AES_CCM:
                case IMB_AUTH_AES_GMAC:
                case IMB_AUTH_NULL:
                case IMB_AUTH_SHA_1:
                case IMB_AUTH_SHA_224:
                case IMB_AUTH_SHA_256:
                case IMB_AUTH_SHA_384:
                case IMB_AUTH_SHA_512:
                case IMB_AUTH_PON_CRC_BIP:
                case IMB_AUTH_DOCSIS_CRC32:
                case IMB_AUTH_CHACHA20_POLY1305:
                case IMB_AUTH_CHACHA20_POLY1305_SGL:
                case IMB_AUTH_SNOW_V_AEAD:
                case IMB_AUTH_GCM_SGL:
                case IMB_AUTH_CRC32_ETHERNET_FCS:
                case IMB_AUTH_CRC32_SCTP:
                case IMB_AUTH_CRC32_WIMAX_OFDMA_DATA:
                case IMB_AUTH_CRC24_LTE_A:
                case IMB_AUTH_CRC24_LTE_B:
                case IMB_AUTH_CRC16_X25:
                case IMB_AUTH_CRC16_FP_DATA:
                case IMB_AUTH_CRC11_FP_HEADER:
                case IMB_AUTH_CRC10_IUUP_DATA:
                case IMB_AUTH_CRC8_WIMAX_OFDMA_HCS:
                case IMB_AUTH_CRC7_FP_HEADER:
                case IMB_AUTH_CRC6_IUUP_HEADER:
                        /* No operation needed */
                        break;
                case IMB_AUTH_AES_GMAC_128:
                case IMB_AUTH_AES_GMAC_192:
                case IMB_AUTH_AES_GMAC_256:
                        memset(gdata_key, pattern_auth_key,
                               sizeof(keys->gdata_key));
                        break;
                default:
                        fprintf(stderr,
                                "Unsupported hash algorithm %u, line %u\n",
                                (unsigned) params->hash_alg, __LINE__);
                        return -1;
                }

                switch (params->cipher_mode) {
                case IMB_CIPHER_GCM:
                        memset(gdata_key, pattern_cipher_key,
                                sizeof(keys->gdata_key));
                        break;
                case IMB_CIPHER_PON_AES_CNTR:
                case IMB_CIPHER_CBC:
                case IMB_CIPHER_CCM:
                case IMB_CIPHER_CNTR:
                case IMB_CIPHER_CNTR_BITLEN:
                case IMB_CIPHER_DOCSIS_SEC_BPI:
                case IMB_CIPHER_ECB:
                case IMB_CIPHER_CBCS_1_9:
                        memset(enc_keys, pattern_cipher_key,
                               sizeof(keys->enc_keys));
                        memset(dec_keys, pattern_cipher_key,
                               sizeof(keys->dec_keys));
                        break;
                case IMB_CIPHER_DES:
                case IMB_CIPHER_DES3:
                case IMB_CIPHER_DOCSIS_DES:
                        memset(enc_keys, pattern_cipher_key,
                               sizeof(keys->enc_keys));
                        break;
                case IMB_CIPHER_SNOW3G_UEA2_BITLEN:
                case IMB_CIPHER_KASUMI_UEA1_BITLEN:
                        memset(k2, pattern_cipher_key, 16);
                        break;
                case IMB_CIPHER_ZUC_EEA3:
                case IMB_CIPHER_CHACHA20:
                case IMB_CIPHER_CHACHA20_POLY1305:
                case IMB_CIPHER_CHACHA20_POLY1305_SGL:
                case IMB_CIPHER_SNOW_V:
                case IMB_CIPHER_SNOW_V_AEAD:
                        memset(k2, pattern_cipher_key, 32);
                        break;
                case IMB_CIPHER_NULL:
                        /* No operation needed */
                        break;
                default:
                        fprintf(stderr, "Unsupported cipher mode\n");
                        return -1;
                }

                return 0;
        }

        switch (params->hash_alg) {
        case IMB_AUTH_AES_XCBC:
                IMB_AES_XCBC_KEYEXP(mb_mgr, auth_key, k1_expanded, k2, k3);
                break;
        case IMB_AUTH_AES_CMAC:
        case IMB_AUTH_AES_CMAC_BITLEN:
                IMB_AES_KEYEXP_128(mb_mgr, auth_key, k1_expanded, dust);
                IMB_AES_CMAC_SUBKEY_GEN_128(mb_mgr, k1_expanded, k2, k3);
                break;
        case IMB_AUTH_AES_CMAC_256:
                IMB_AES_KEYEXP_256(mb_mgr, auth_key, k1_expanded, dust);
                IMB_AES_CMAC_SUBKEY_GEN_256(mb_mgr, k1_expanded, k2, k3);
                break;
        case IMB_AUTH_HMAC_SHA_1:
                /* compute ipad hash */
                memset(buf, 0x36, IMB_SHA1_BLOCK_SIZE);
                for (i = 0; i < IMB_SHA1_BLOCK_SIZE; i++)
                        buf[i] ^= auth_key[i];
                IMB_SHA1_ONE_BLOCK(mb_mgr, buf, ipad);

                /* compute opad hash */
                memset(buf, 0x5c, IMB_SHA1_BLOCK_SIZE);
                for (i = 0; i < IMB_SHA1_BLOCK_SIZE; i++)
                        buf[i] ^= auth_key[i];
                IMB_SHA1_ONE_BLOCK(mb_mgr, buf, opad);

                break;
        case IMB_AUTH_HMAC_SHA_224:
                /* compute ipad hash */
                memset(buf, 0x36, IMB_SHA_256_BLOCK_SIZE);
                for (i = 0; i < IMB_SHA_256_BLOCK_SIZE; i++)
                        buf[i] ^= auth_key[i];
                IMB_SHA224_ONE_BLOCK(mb_mgr, buf, ipad);

                /* compute opad hash */
                memset(buf, 0x5c, IMB_SHA_256_BLOCK_SIZE);
                for (i = 0; i < IMB_SHA_256_BLOCK_SIZE; i++)
                        buf[i] ^= auth_key[i];
                IMB_SHA224_ONE_BLOCK(mb_mgr, buf, opad);

                break;
        case IMB_AUTH_HMAC_SHA_256:
                /* compute ipad hash */
                memset(buf, 0x36, IMB_SHA_256_BLOCK_SIZE);
                for (i = 0; i < IMB_SHA_256_BLOCK_SIZE; i++)
                        buf[i] ^= auth_key[i];
                IMB_SHA256_ONE_BLOCK(mb_mgr, buf, ipad);

                /* compute opad hash */
                memset(buf, 0x5c, IMB_SHA_256_BLOCK_SIZE);
                for (i = 0; i < IMB_SHA_256_BLOCK_SIZE; i++)
                        buf[i] ^= auth_key[i];
                IMB_SHA256_ONE_BLOCK(mb_mgr, buf, opad);

                break;
        case IMB_AUTH_HMAC_SHA_384:
                /* compute ipad hash */
                memset(buf, 0x36, IMB_SHA_384_BLOCK_SIZE);
                for (i = 0; i < IMB_SHA_384_BLOCK_SIZE; i++)
                        buf[i] ^= auth_key[i];
                IMB_SHA384_ONE_BLOCK(mb_mgr, buf, ipad);

                /* compute opad hash */
                memset(buf, 0x5c, IMB_SHA_384_BLOCK_SIZE);
                for (i = 0; i < IMB_SHA_384_BLOCK_SIZE; i++)
                        buf[i] ^= auth_key[i];
                IMB_SHA384_ONE_BLOCK(mb_mgr, buf, opad);

                break;
        case IMB_AUTH_HMAC_SHA_512:
                /* compute ipad hash */
                memset(buf, 0x36, IMB_SHA_512_BLOCK_SIZE);
                for (i = 0; i < IMB_SHA_512_BLOCK_SIZE; i++)
                        buf[i] ^= auth_key[i];
                IMB_SHA512_ONE_BLOCK(mb_mgr, buf, ipad);

                /* compute opad hash */
                memset(buf, 0x5c, IMB_SHA_512_BLOCK_SIZE);
                for (i = 0; i < IMB_SHA_512_BLOCK_SIZE; i++)
                        buf[i] ^= auth_key[i];
                IMB_SHA512_ONE_BLOCK(mb_mgr, buf, opad);

                break;
        case IMB_AUTH_MD5:
                /* compute ipad hash */
                memset(buf, 0x36, 64);
                for (i = 0; i < 64; i++)
                        buf[i] ^= auth_key[i];
                IMB_MD5_ONE_BLOCK(mb_mgr, buf, ipad);

                /* compute opad hash */
                memset(buf, 0x5c, 64);
                for (i = 0; i < 64; i++)
                        buf[i] ^= auth_key[i];
                IMB_MD5_ONE_BLOCK(mb_mgr, buf, opad);

                break;
        case IMB_AUTH_ZUC_EIA3_BITLEN:
        case IMB_AUTH_ZUC256_EIA3_BITLEN:
        case IMB_AUTH_SNOW3G_UIA2_BITLEN:
        case IMB_AUTH_KASUMI_UIA1:
                memcpy(k2, auth_key, sizeof(keys->k2));
                break;
        case IMB_AUTH_AES_GMAC_128:
                IMB_AES128_GCM_PRE(mb_mgr, auth_key, gdata_key);
                break;
        case IMB_AUTH_AES_GMAC_192:
                IMB_AES192_GCM_PRE(mb_mgr, auth_key, gdata_key);
                break;
        case IMB_AUTH_AES_GMAC_256:
                IMB_AES256_GCM_PRE(mb_mgr, auth_key, gdata_key);
                break;
        case IMB_AUTH_AES_CCM:
        case IMB_AUTH_AES_GMAC:
        case IMB_AUTH_NULL:
        case IMB_AUTH_SHA_1:
        case IMB_AUTH_SHA_224:
        case IMB_AUTH_SHA_256:
        case IMB_AUTH_SHA_384:
        case IMB_AUTH_SHA_512:
        case IMB_AUTH_PON_CRC_BIP:
        case IMB_AUTH_DOCSIS_CRC32:
        case IMB_AUTH_CHACHA20_POLY1305:
        case IMB_AUTH_CHACHA20_POLY1305_SGL:
        case IMB_AUTH_SNOW_V_AEAD:
        case IMB_AUTH_GCM_SGL:
        case IMB_AUTH_CRC32_ETHERNET_FCS:
        case IMB_AUTH_CRC32_SCTP:
        case IMB_AUTH_CRC32_WIMAX_OFDMA_DATA:
        case IMB_AUTH_CRC24_LTE_A:
        case IMB_AUTH_CRC24_LTE_B:
        case IMB_AUTH_CRC16_X25:
        case IMB_AUTH_CRC16_FP_DATA:
        case IMB_AUTH_CRC11_FP_HEADER:
        case IMB_AUTH_CRC10_IUUP_DATA:
        case IMB_AUTH_CRC8_WIMAX_OFDMA_HCS:
        case IMB_AUTH_CRC7_FP_HEADER:
        case IMB_AUTH_CRC6_IUUP_HEADER:
                /* No operation needed */
                break;
        case IMB_AUTH_POLY1305:
                memcpy(k1_expanded, auth_key, 32);
                break;
        default:
                fprintf(stderr, "Unsupported hash algorithm %u, line %u\n",
                        (unsigned) params->hash_alg, __LINE__);
                return -1;
        }

        switch (params->cipher_mode) {
        case IMB_CIPHER_GCM:
                switch (params->key_size) {
                case IMB_KEY_128_BYTES:
                        IMB_AES128_GCM_PRE(mb_mgr, ciph_key, gdata_key);
                        break;
                case IMB_KEY_192_BYTES:
                        IMB_AES192_GCM_PRE(mb_mgr, ciph_key, gdata_key);
                        break;
                case IMB_KEY_256_BYTES:
                        IMB_AES256_GCM_PRE(mb_mgr, ciph_key, gdata_key);
                        break;
                default:
                        fprintf(stderr, "Wrong key size\n");
                        return -1;
                }
                break;
        case IMB_CIPHER_PON_AES_CNTR:
                switch (params->key_size) {
                case 16:
                        IMB_AES_KEYEXP_128(mb_mgr, ciph_key, enc_keys,
                                           dec_keys);
                        break;
                case 0:
                        break;
                default:
                        fprintf(stderr, "Wrong key size\n");
                        return -1;
                }
                break;
        case IMB_CIPHER_CBC:
        case IMB_CIPHER_CCM:
        case IMB_CIPHER_CNTR:
        case IMB_CIPHER_CNTR_BITLEN:
        case IMB_CIPHER_DOCSIS_SEC_BPI:
        case IMB_CIPHER_ECB:
        case IMB_CIPHER_CBCS_1_9:
                switch (params->key_size) {
                case IMB_KEY_128_BYTES:
                        IMB_AES_KEYEXP_128(mb_mgr, ciph_key, enc_keys,
                                           dec_keys);
                        break;
                case IMB_KEY_192_BYTES:
                        IMB_AES_KEYEXP_192(mb_mgr, ciph_key, enc_keys,
                                          dec_keys);
                        break;
                case IMB_KEY_256_BYTES:
                        IMB_AES_KEYEXP_256(mb_mgr, ciph_key, enc_keys,
                                           dec_keys);
                        break;
                default:
                        fprintf(stderr, "Wrong key size\n");
                        return -1;
                }
                break;
        case IMB_CIPHER_DES:
        case IMB_CIPHER_DES3:
        case IMB_CIPHER_DOCSIS_DES:
                des_key_schedule((uint64_t *) enc_keys, ciph_key);
                break;
        case IMB_CIPHER_SNOW3G_UEA2_BITLEN:
        case IMB_CIPHER_KASUMI_UEA1_BITLEN:
                memcpy(k2, ciph_key, 16);
                break;
        case IMB_CIPHER_ZUC_EEA3:
        case IMB_CIPHER_CHACHA20:
        case IMB_CIPHER_CHACHA20_POLY1305:
        case IMB_CIPHER_CHACHA20_POLY1305_SGL:
        case IMB_CIPHER_SNOW_V:
        case IMB_CIPHER_SNOW_V_AEAD:
                /* Use of:
                 *     memcpy(k2, ciph_key, 32);
                 * leaves sensitive data on the stack.
                 * Copying data in 16 byte chunks instead.
                 */
                memcpy(k2, ciph_key, 16);
                memcpy(k2 + 16, ciph_key + 16, 16);
                break;
        case IMB_CIPHER_NULL:
                /* No operation needed */
                break;
        default:
                fprintf(stderr, "Unsupported cipher mode\n");
                return -1;
        }

        return 0;
}

/* Modify the test buffer to set the HEC value and CRC, so the final
 * decrypted message can be compared against the test buffer */
static int
modify_pon_test_buf(uint8_t *test_buf,
                    const IMB_JOB *job,
                    const uint32_t pli,
                    const uint64_t xgem_hdr)
{
        /* Set plaintext CRC in test buffer for PON */
        uint32_t *buf32 = (uint32_t *) &test_buf[8 + pli - 4];
        uint64_t *buf64 = (uint64_t *) test_buf;
        const uint32_t *tag32 = (uint32_t *) job->auth_tag_output;
        const uint64_t hec_mask = BSWAP64(0xfffffffffffe000);
        const uint64_t xgem_hdr_out = ((const uint64_t *)job->src)[0];

        /* Update CRC if PLI > 4 */
        if (pli > 4)
                buf32[0] = tag32[1];

        /* Check if any bits apart from HEC are modified */
        if ((xgem_hdr_out & hec_mask) != (xgem_hdr & hec_mask)) {
                fprintf(stderr, "XGEM header overwritten outside HEC\n");
                fprintf(stderr, "Original XGEM header: %"PRIx64"\n",
                        xgem_hdr & hec_mask);
                fprintf(stderr, "Output XGEM header: %"PRIx64"\n",
                        xgem_hdr_out & hec_mask);
                return -1;
        }

        /* Modify original XGEM header to include calculated HEC */
        buf64[0] = xgem_hdr_out;

        return 0;
}

/* Modify the test buffer to set the CRC value, so the final
 * decrypted message can be compared against the test buffer */
static void
modify_docsis_crc32_test_buf(uint8_t *test_buf,
                             const IMB_JOB *job, const uint32_t buf_size)
{
        if (buf_size >=
            (IMB_DOCSIS_CRC32_MIN_ETH_PDU_SIZE + IMB_DOCSIS_CRC32_TAG_SIZE)) {
                /* Set plaintext CRC32 in the test buffer */
                memcpy(&test_buf[buf_size - IMB_DOCSIS_CRC32_TAG_SIZE],
                       job->auth_tag_output, IMB_DOCSIS_CRC32_TAG_SIZE);
        }
}

/*
 * Checks for sensitive information in registers, stack and MB_MGR
 * (in this order, to try to minimize pollution of the data left out
 *  after the job completion, due to these actual checks).
 *
 *  Returns -1 if sensitive information was found or 0 if not.
 */
static int
perform_safe_checks(IMB_MGR *mgr, const IMB_ARCH arch, const char *dir)
{
        uint8_t *rsp_ptr;
        uint32_t simd_size = 0;
        void **ooo_ptr;
        unsigned i;

        dump_gps();
        switch (arch) {
        case IMB_ARCH_SSE:
        case IMB_ARCH_NOAESNI:
                dump_xmms_sse();
                simd_size = XMM_MEM_SIZE;
                break;
        case IMB_ARCH_AVX:
                dump_xmms_avx();
                simd_size = XMM_MEM_SIZE;
                break;
        case IMB_ARCH_AVX2:
                dump_ymms();
                simd_size = YMM_MEM_SIZE;
                break;
        case IMB_ARCH_AVX512:
                dump_zmms();
                simd_size = ZMM_MEM_SIZE;
                break;
        default:
                fprintf(stderr,
                        "Error getting the architecture\n");
                return -1;
        }
        if (search_patterns(gps, GP_MEM_SIZE) == 0) {
                fprintf(stderr, "Pattern found in GP registers after %s data\n",
                        dir);
                return -1;
        }
        if (search_patterns(simd_regs, simd_size) == 0) {
                fprintf(stderr,
                        "Pattern found in SIMD registers after %s data\n",
                        dir);
                return -1;
        }
        rsp_ptr = rdrsp();
        if (search_patterns((rsp_ptr - STACK_DEPTH), STACK_DEPTH) == 0) {
                fprintf(stderr, "Pattern found in stack after %s data\n", dir);
                return -1;
        }

        if (search_patterns(mgr, sizeof(IMB_MGR)) == 0) {
                fprintf(stderr, "Pattern found in MB_MGR after %s data\n", dir);
                return -1;
        }

        /* search OOO managers */
        for (ooo_ptr = &mgr->OOO_MGR_FIRST, i = 0;
             ooo_ptr <= &mgr->OOO_MGR_LAST;
             ooo_ptr++, i++) {
                void *ooo_mgr_p = *ooo_ptr;

                if (search_patterns(ooo_mgr_p,
                                    get_ooo_mgr_size(ooo_mgr_p, i)) == 0) {
                        fprintf(stderr,
                                "Pattern found in 000 MGR (%d) after %s data\n",
                                (int)(ooo_ptr - &mgr->OOO_MGR_FIRST), dir);
                        return -1;
                }
        }

        return 0;
}

static void
clear_scratch_simd(const IMB_ARCH arch)
{
        switch (arch) {
        case IMB_ARCH_SSE:
        case IMB_ARCH_NOAESNI:
                clr_scratch_xmms_sse();
                break;
        case IMB_ARCH_AVX:
                clr_scratch_xmms_avx();
                break;
        case IMB_ARCH_AVX2:
                clr_scratch_ymms();
                break;
        case IMB_ARCH_AVX512:
                clr_scratch_zmms();
                break;
        default:
                fprintf(stderr, "Invalid architecture\n");
                exit(EXIT_FAILURE);
        }
}

/* Performs test using AES_HMAC or DOCSIS */
static int
do_test(IMB_MGR *enc_mb_mgr, const IMB_ARCH enc_arch,
        IMB_MGR *dec_mb_mgr, const IMB_ARCH dec_arch,
        const struct params_s *params, struct data *data,
        const unsigned safe_check, const unsigned imix,
        const unsigned num_jobs)
{
        IMB_JOB *job;
        uint32_t i, imix_job_idx = 0;
        int ret = -1;
        uint8_t tag_size = auth_tag_length_bytes[params->hash_alg - 1];
        uint64_t xgem_hdr[MAX_NUM_JOBS] = {0};
        uint8_t tag_size_to_check[MAX_NUM_JOBS];
        struct cipher_auth_keys *enc_keys = &data->enc_keys;
        struct cipher_auth_keys *dec_keys = &data->dec_keys;
        uint8_t *aad = data->aad;
        uint8_t *cipher_iv = data->cipher_iv;
        uint8_t *auth_iv = data->auth_iv;
        uint8_t *in_digest[MAX_NUM_JOBS];
        uint8_t *out_digest[MAX_NUM_JOBS];
        uint8_t *test_buf[MAX_NUM_JOBS] = {NULL};
        uint8_t *src_dst_buf[MAX_NUM_JOBS];
        uint32_t buf_sizes[MAX_NUM_JOBS] = {0};
        uint8_t *ciph_key = data->ciph_key;
        uint8_t *auth_key = data->auth_key;
        unsigned int num_processed_jobs = 0;
        uint8_t next_iv[IMB_AES_BLOCK_SIZE];
        uint16_t pli = 0;

        if (num_jobs == 0)
                return ret;

        /* If performing a test searching for sensitive information,
         * set keys and plaintext to known values,
         * so they can be searched later on in the MB_MGR structure and stack.
         * Otherwise, just randomize the data */
        generate_random_buf(cipher_iv, MAX_IV_SIZE);
        generate_random_buf(auth_iv, MAX_IV_SIZE);
        generate_random_buf(aad, MAX_AAD_SIZE);
        if (safe_check) {
                memset(ciph_key, pattern_cipher_key, MAX_KEY_SIZE);
                memset(auth_key, pattern_auth_key, MAX_KEY_SIZE);
        } else {
                generate_random_buf(ciph_key, MAX_KEY_SIZE);
                generate_random_buf(auth_key, MAX_KEY_SIZE);
        }

        for (i = 0; i < num_jobs; i++) {
                in_digest[i] = data->in_digest[i];
                out_digest[i] = data->out_digest[i];
                tag_size_to_check[i] = tag_size;
                test_buf[i] = data->test_buf[i];
                src_dst_buf[i] = data->src_dst_buf[i];
                /* Prepare buffer sizes */
                if (imix) {
                        uint32_t random_num = rand() % DEFAULT_JOB_SIZE_MAX;

                        imix_job_idx = i;

                        /* If random number is 0, change the size to 16 */
                        if (random_num == 0)
                                random_num = 16;

                        /*
                         * CBC and ECB operation modes do not support lengths
                         * which are non-multiple of block size
                         */
                        if (params->cipher_mode == IMB_CIPHER_CBC ||
                            params->cipher_mode == IMB_CIPHER_ECB ||
                            params->cipher_mode == IMB_CIPHER_CBCS_1_9) {
                                random_num += (IMB_AES_BLOCK_SIZE - 1);
                                random_num &= (~(IMB_AES_BLOCK_SIZE - 1));
                        }

                        if (params->cipher_mode == IMB_CIPHER_DES ||
                            params->cipher_mode == IMB_CIPHER_DES3) {
                                random_num += (IMB_DES_BLOCK_SIZE - 1);
                                random_num &= (~(IMB_DES_BLOCK_SIZE - 1));
                        }

                        /*
                         * KASUMI-UIA1 needs to be at least 9 bytes
                         * (IV + direction bit + '1' + 0s to align to
                         * byte boundary)
                         */
                        if (params->hash_alg == IMB_AUTH_KASUMI_UIA1)
                                if (random_num < (IMB_KASUMI_BLOCK_SIZE + 1))
                                        random_num = 16;

                        buf_sizes[i] = random_num;
                } else
                        buf_sizes[i] = params->buf_size;

                if (params->hash_alg == IMB_AUTH_PON_CRC_BIP) {
                        /* Buf size is XGEM payload, including CRC,
                         * allocate space for XGEM header and padding */
                        pli = buf_sizes[i];
                        buf_sizes[i] += 8;
                        if (buf_sizes[i] < 16)
                                buf_sizes[i] = 16;
                        if (buf_sizes[i] % 4)
                                buf_sizes[i] = (buf_sizes[i] + 3) & 0xfffffffc;
                        /* Only first 4 bytes are checked,
                         * corresponding to BIP */
                        tag_size_to_check[i] = 4;
                }

                if (params->hash_alg == IMB_AUTH_DOCSIS_CRC32) {
                        if (buf_sizes[i] >=
                            (IMB_DOCSIS_CRC32_MIN_ETH_PDU_SIZE +
                             IMB_DOCSIS_CRC32_TAG_SIZE))
                                tag_size_to_check[i] =
                                                IMB_DOCSIS_CRC32_TAG_SIZE;
                        else
                                tag_size_to_check[i] = 0;
                }

                if (safe_check)
                        memset(test_buf[i], pattern_plain_text, buf_sizes[i]);
                else
                        generate_random_buf(test_buf[i], buf_sizes[i]);

                /* For PON, construct the XGEM header, setting valid PLI */
                if (params->hash_alg == IMB_AUTH_PON_CRC_BIP) {
                        /* create XGEM header template */
                        const uint16_t shifted_pli = (pli << 2) & 0xffff;
                        uint64_t *p_src = (uint64_t *)test_buf[i];

                        xgem_hdr[i] = ((shifted_pli >> 8) & 0xff) |
                                       ((shifted_pli & 0xff) << 8);
                        p_src[0] = xgem_hdr[i];
                }
        }

        /*
         * Expand/schedule keys.
         * If checking for sensitive information, first use actual
         * key expansion functions and check the stack for left over
         * information and then set a pattern in the expanded key memory
         * to search for later on.
         * If not checking for sensitive information, just use the key
         * expansion functions.
         */
        if (safe_check) {
                uint8_t *rsp_ptr;

                /* Clear scratch registers before expanding keys to prevent
                 * other functions from storing sensitive data in stack
                 */
                clear_scratch_simd(enc_arch);
                if (prepare_keys(enc_mb_mgr, enc_keys, ciph_key, auth_key,
                                 params, 0) < 0)
                        goto exit;

                rsp_ptr = rdrsp();
                if (search_patterns((rsp_ptr - STACK_DEPTH),
                                    STACK_DEPTH) == 0) {
                        fprintf(stderr, "Pattern found in stack after "
                                "expanding encryption keys\n");
                        goto exit;
                }

                if (prepare_keys(dec_mb_mgr, dec_keys, ciph_key, auth_key,
                                 params, 0) < 0)
                        goto exit;

                rsp_ptr = rdrsp();
                if (search_patterns((rsp_ptr - STACK_DEPTH),
                                    STACK_DEPTH) == 0) {
                        fprintf(stderr, "Pattern found in stack after "
                                "expanding decryption keys\n");
                        goto exit;
                }

                /*
                 * After testing key normal expansion functions,
                 * it is time to setup the keys and key schedules filled
                 * with specific patterns.
                 */
                if (prepare_keys(enc_mb_mgr, enc_keys, ciph_key, auth_key,
                                 params, 1) < 0)
                        goto exit;

                if (prepare_keys(dec_mb_mgr, dec_keys, ciph_key, auth_key,
                                 params, 1) < 0)
                        goto exit;
        } else {
                if (prepare_keys(enc_mb_mgr, enc_keys, ciph_key, auth_key,
                                 params, 0) < 0)
                        goto exit;

                if (prepare_keys(dec_mb_mgr, dec_keys, ciph_key, auth_key,
                                 params, 0) < 0)
                        goto exit;
        }

#ifdef PIN_BASED_CEC
        PinBasedCEC_MarkSecret((uintptr_t) enc_keys->enc_keys,
                               sizeof(enc_keys->enc_keys));
        PinBasedCEC_MarkSecret((uintptr_t) enc_keys->dec_keys,
                               sizeof(enc_keys->dec_keys));
        PinBasedCEC_MarkSecret((uintptr_t) &enc_keys->gdata_key,
                               sizeof(enc_keys->gdata_key));
        PinBasedCEC_MarkSecret((uintptr_t) enc_keys->k1_expanded,
                               sizeof(enc_keys->k1_expanded));
        PinBasedCEC_MarkSecret((uintptr_t) enc_keys->k2, sizeof(enc_keys->k2));
        PinBasedCEC_MarkSecret((uintptr_t) enc_keys->k3, sizeof(enc_keys->k3));

        PinBasedCEC_MarkSecret((uintptr_t) dec_keys->enc_keys,
                               sizeof(dec_keys->enc_keys));
        PinBasedCEC_MarkSecret((uintptr_t) dec_keys->dec_keys,
                               sizeof(dec_keys->dec_keys));
        PinBasedCEC_MarkSecret((uintptr_t) &dec_keys->gdata_key,
                               sizeof(dec_keys->gdata_key));
        PinBasedCEC_MarkSecret((uintptr_t) dec_keys->k1_expanded,
                               sizeof(dec_keys->k1_expanded));
        PinBasedCEC_MarkSecret((uintptr_t) dec_keys->k2, sizeof(dec_keys->k2));
        PinBasedCEC_MarkSecret((uintptr_t) dec_keys->k3, sizeof(dec_keys->k3));
#endif

        for (i = 0; i < num_jobs; i++) {
                imix_job_idx = i;

                job = IMB_GET_NEXT_JOB(enc_mb_mgr);
                /*
                 * Encrypt + generate digest from encrypted message
                 * using architecture under test
                 */
                memcpy(src_dst_buf[i], test_buf[i], buf_sizes[i]);
                if (fill_job(job, params, src_dst_buf[i], in_digest[i], aad,
                             buf_sizes[i], tag_size, IMB_DIR_ENCRYPT, enc_keys,
                             cipher_iv, auth_iv, i, next_iv) < 0)
                        goto exit;

                /* Randomize memory for input digest */
                generate_random_buf(in_digest[i], tag_size);

                /* Clear scratch registers before submitting job to prevent
                 * other functions from storing sensitive data in stack */
                if (safe_check)
                        clear_scratch_simd(enc_arch);
                job = IMB_SUBMIT_JOB(enc_mb_mgr);

                if (job) {
                        unsigned idx = (unsigned)((uintptr_t) job->user_data);

                        if (job->status != IMB_STATUS_COMPLETED) {
                                int errc = imb_get_errno(enc_mb_mgr);

                                fprintf(stderr,
                                        "failed job, status:%d, "
                                        "error code:%d '%s'\n",
                                        job->status, errc,
                                        imb_get_strerror(errc));
                                goto exit;
                        }
                        if (idx != num_processed_jobs) {
                                fprintf(stderr, "job returned out of order\n");
                                goto exit;
                        }
                        num_processed_jobs++;

                        if (params->hash_alg == IMB_AUTH_PON_CRC_BIP) {
                                if (modify_pon_test_buf(test_buf[idx], job,
                                                        pli,
                                                        xgem_hdr[idx]) < 0)
                                        goto exit;
                        }

                        if (params->hash_alg == IMB_AUTH_DOCSIS_CRC32)
                                modify_docsis_crc32_test_buf(test_buf[idx], job,
                                                             buf_sizes[idx]);
                }
        }
        /* Flush rest of the jobs, if there are outstanding jobs */
        while (num_processed_jobs != num_jobs) {
                job = IMB_FLUSH_JOB(enc_mb_mgr);
                while (job != NULL) {
                        unsigned idx = (unsigned)((uintptr_t) job->user_data);

                        if (job->status != IMB_STATUS_COMPLETED) {
                                int errc = imb_get_errno(enc_mb_mgr);

                                fprintf(stderr,
                                        "failed job, status:%d, "
                                        "error code:%d '%s'\n",
                                        job->status, errc,
                                        imb_get_strerror(errc));
                                goto exit;
                        }
                        if (idx != num_processed_jobs) {
                                fprintf(stderr, "job returned out of order\n");
                                goto exit;
                        }
                        num_processed_jobs++;

                        if (params->hash_alg == IMB_AUTH_DOCSIS_CRC32)
                                modify_docsis_crc32_test_buf(test_buf[idx], job,
                                                             buf_sizes[idx]);

                        /* Get more completed jobs */
                        job = IMB_GET_COMPLETED_JOB(enc_mb_mgr);
                }
        }

#ifdef PIN_BASED_CEC
        PinBasedCEC_ClearSecrets();
#endif
        num_processed_jobs = 0;

        /* Check that the registers, stack and MB_MGR do not contain any
         * sensitive information after job is returned */
        if (safe_check)
                if (perform_safe_checks(enc_mb_mgr, enc_arch,
                                        "encrypting") < 0)
                        goto exit;

#ifdef PIN_BASED_CEC
        PinBasedCEC_MarkSecret((uintptr_t) enc_keys->enc_keys,
                               sizeof(enc_keys->enc_keys));
        PinBasedCEC_MarkSecret((uintptr_t) enc_keys->dec_keys,
                               sizeof(enc_keys->dec_keys));
        PinBasedCEC_MarkSecret((uintptr_t) &enc_keys->gdata_key,
                               sizeof(enc_keys->gdata_key));
        PinBasedCEC_MarkSecret((uintptr_t) enc_keys->k1_expanded,
                               sizeof(enc_keys->k1_expanded));
        PinBasedCEC_MarkSecret((uintptr_t) enc_keys->k2, sizeof(enc_keys->k2));
        PinBasedCEC_MarkSecret((uintptr_t) enc_keys->k3, sizeof(enc_keys->k3));

        PinBasedCEC_MarkSecret((uintptr_t) dec_keys->enc_keys,
                               sizeof(dec_keys->enc_keys));
        PinBasedCEC_MarkSecret((uintptr_t) dec_keys->dec_keys,
                               sizeof(dec_keys->dec_keys));
        PinBasedCEC_MarkSecret((uintptr_t) &dec_keys->gdata_key,
                               sizeof(dec_keys->gdata_key));
        PinBasedCEC_MarkSecret((uintptr_t) dec_keys->k1_expanded,
                               sizeof(dec_keys->k1_expanded));
        PinBasedCEC_MarkSecret((uintptr_t) dec_keys->k2, sizeof(dec_keys->k2));
        PinBasedCEC_MarkSecret((uintptr_t) dec_keys->k3, sizeof(dec_keys->k3));
#endif

        for (i = 0; i < num_jobs; i++) {
                imix_job_idx = i;

                job = IMB_GET_NEXT_JOB(dec_mb_mgr);

                /* Randomize memory for output digest */
                generate_random_buf(out_digest[i], tag_size);

                /*
                 * Generate digest from encrypted message and decrypt
                 * using reference architecture
                 */
                if (fill_job(job, params, src_dst_buf[i], out_digest[i], aad,
                             buf_sizes[i], tag_size, IMB_DIR_DECRYPT, dec_keys,
                             cipher_iv, auth_iv, i, next_iv) < 0)
                        goto exit;

                /* Clear scratch registers before submitting job to prevent
                 * other functions from storing sensitive data in stack */
                if (safe_check)
                        clear_scratch_simd(dec_arch);
                job = IMB_SUBMIT_JOB(dec_mb_mgr);

                if (job != NULL) {
                        unsigned idx = (unsigned)((uintptr_t) job->user_data);

                        if (job->status != IMB_STATUS_COMPLETED) {
                                int errc = imb_get_errno(dec_mb_mgr);

                                fprintf(stderr,
                                        "failed job, status:%d, "
                                        "error code:%d '%s'\n",
                                        job->status, errc,
                                        imb_get_strerror(errc));
                                goto exit;
                        }

                        if (idx != num_processed_jobs) {
                                fprintf(stderr, "job returned out of order\n");
                                goto exit;
                        }
                        num_processed_jobs++;
                }
        }

        /* Flush rest of the jobs, if there are outstanding jobs */
        while (num_processed_jobs != num_jobs) {
                job = IMB_FLUSH_JOB(dec_mb_mgr);
                while (job != NULL) {
                        unsigned idx = (unsigned)((uintptr_t) job->user_data);

                        if (job->status != IMB_STATUS_COMPLETED) {
                                int errc = imb_get_errno(enc_mb_mgr);

                                fprintf(stderr,
                                        "failed job, status:%d, "
                                        "error code:%d '%s'\n",
                                        job->status, errc,
                                        imb_get_strerror(errc));
                                goto exit;
                        }
                        if (idx != num_processed_jobs) {
                                fprintf(stderr, "job returned out of order\n");
                                goto exit;
                        }
                        num_processed_jobs++;
                        /* Get more completed jobs */
                        job = IMB_GET_COMPLETED_JOB(dec_mb_mgr);
                }
        }

#ifdef PIN_BASED_CEC
        PinBasedCEC_ClearSecrets();
#endif
        /* Check that the registers, stack and MB_MGR do not contain any
         * sensitive information after job is returned */
        if (safe_check) {
                if (perform_safe_checks(dec_mb_mgr, dec_arch,
                                        "decrypting") < 0)
                        goto exit;
        } else {
                for (i = 0; i < num_jobs; i++) {
                        int goto_exit = 0;

                        imix_job_idx = i;

                        if (params->hash_alg != IMB_AUTH_NULL &&
                            memcmp(in_digest[i], out_digest[i],
                                   tag_size_to_check[i]) != 0) {
                                fprintf(stderr, "\nInput and output tags "
                                                "don't match\n");
                                hexdump(stdout, "Input digest", in_digest[i],
                                        tag_size_to_check[i]);
                                hexdump(stdout, "Output digest", out_digest[i],
                                        tag_size_to_check[i]);
                                goto_exit = 1;
                        }

                        if (params->cipher_mode != IMB_CIPHER_NULL &&
                            memcmp(src_dst_buf[i], test_buf[i],
                                   buf_sizes[i]) != 0) {
                                fprintf(stderr, "\nDecrypted text and "
                                                "plaintext don't match\n");
                                hexdump(stdout, "Plaintext (orig)", test_buf[i],
                                        buf_sizes[i]);
                                hexdump(stdout, "Decrypted msg", src_dst_buf[i],
                                        buf_sizes[i]);
                                goto_exit = 1;
                        }

                        if ((params->hash_alg == IMB_AUTH_PON_CRC_BIP) &&
                            (pli > 4)) {
                                const uint64_t plen = 8 + pli - 4;

                                if (memcmp(src_dst_buf[i] + plen,
                                           out_digest[i] + 4, 4) != 0) {
                                        fprintf(stderr, "\nDecrypted CRC and "
                                                "calculated CRC don't match\n");
                                        hexdump(stdout, "Decrypted CRC",
                                                src_dst_buf[i] + plen, 4);
                                        hexdump(stdout, "Calculated CRC",
                                                out_digest[i] + 4, 4);
                                        goto_exit = 1;
                                }
                        }

                        if (goto_exit)
                                goto exit;
                }
        }

        ret = 0;

exit:
        /* clear data */
        clear_data(data);

        if (ret < 0) {
                printf("Failures in\n");
                print_algo_info(params);
                printf("Encrypting ");
                print_tested_arch(enc_mb_mgr->features, enc_arch);
                printf("Decrypting ");
                print_tested_arch(dec_mb_mgr->features, dec_arch);
                if (imix) {
                        printf("Job #%u, buffer size = %u\n",
                               imix_job_idx, buf_sizes[imix_job_idx]);

                        for (i = 0; i < num_jobs; i++)
                                printf("Other sizes = %u\n", buf_sizes[i]);
                } else
                        printf("Buffer size = %u\n", params->buf_size);
                printf("Key size = %u\n", params->key_size);
                printf("Tag size = %u\n", tag_size);
                printf("AAD size = %u\n", (uint32_t) params->aad_size);
        }

        return ret;
}

/* Runs test for each buffer size */
static void
process_variant(IMB_MGR *enc_mgr, const IMB_ARCH enc_arch,
                IMB_MGR *dec_mgr, const IMB_ARCH dec_arch,
                struct params_s *params, struct data *variant_data,
                const unsigned int safe_check)
{
#ifdef PIN_BASED_CEC
        const uint32_t sizes = job_sizes[RANGE_MAX];
#else
        const uint32_t sizes = params->num_sizes;
#endif
        uint32_t sz;
        uint64_t min_aad_sz = 0;
        uint64_t max_aad_sz, aad_sz;
        unsigned int i, j;

        if (verbose) {
                printf("[INFO] ");
                print_algo_info(params);
        }

        /* Reset the variant data */
        clear_data(variant_data);

        if (params->cipher_mode == IMB_CIPHER_GCM)
                max_aad_sz = MAX_GCM_AAD_SIZE;
        else if (params->cipher_mode == IMB_CIPHER_CCM)
                max_aad_sz = MAX_CCM_AAD_SIZE;
        else
                max_aad_sz = 0;

        for (sz = 0; sz < sizes; sz++) {
#ifdef PIN_BASED_CEC
                const uint32_t buf_size = job_sizes[RANGE_MIN];
#else
                const uint32_t buf_size = job_sizes[RANGE_MIN] +
                        (sz * job_sizes[RANGE_STEP]);
#endif
                for (aad_sz = min_aad_sz; aad_sz <= max_aad_sz; aad_sz++) {
                        params->aad_size = aad_sz;
                        params->buf_size = buf_size;

                        /*
                         * CBC and ECB operation modes do not support lengths
                         * which are non-multiple of block size
                         */
                        if (params->cipher_mode == IMB_CIPHER_CBC ||
                            params->cipher_mode == IMB_CIPHER_ECB ||
                            params->cipher_mode == IMB_CIPHER_CBCS_1_9)
                                if ((buf_size % IMB_AES_BLOCK_SIZE)  != 0)
                                        continue;

                        if (params->cipher_mode == IMB_CIPHER_DES ||
                            params->cipher_mode == IMB_CIPHER_DES3)
                                if ((buf_size % IMB_DES_BLOCK_SIZE)  != 0)
                                        continue;

                        /*
                         * KASUMI-UIA1 needs to be at least 9 bytes
                         * (IV + direction bit + '1' + 0s to align to
                         * byte boundary)
                         */
                        if (params->hash_alg == IMB_AUTH_KASUMI_UIA1)
                                if (buf_size < (IMB_KASUMI_BLOCK_SIZE + 1))
                                        continue;

                        /* Check for sensitive data first, then normal cross
                         * architecture validation */
                        if (safe_check) {
                                int result;

                                result = do_test(enc_mgr, enc_arch, dec_mgr,
                                                 dec_arch, params,
                                                 variant_data, 1, 0, 1);
                                if (result < 0) {
                                        printf("=== Issue found. "
                                               "Checking again...\n");
                                        generate_patterns();
                                        result = do_test(enc_mgr, enc_arch,
                                                         dec_mgr, dec_arch,
                                                         params, variant_data,
                                                         1, 0, 1);

                                        if (result < 0) {
                                                if (verbose)
                                                        printf("FAIL\n");
                                                printf("=== issue confirmed\n");
                                                exit(EXIT_FAILURE);
                                        }
                                        printf("=== false positive\n");
                                }
                        }

                        if (do_test(enc_mgr, enc_arch, dec_mgr, dec_arch,
                                    params, variant_data, 0, 0, 1) < 0)
                                exit(EXIT_FAILURE);
                }

        }

        /* Perform IMIX tests */
        if (imix_enabled) {
                params->aad_size = min_aad_sz;

                for (i = 2; i <= max_num_jobs; i++) {
                        for (j = 0; j < IMIX_ITER; j++) {
                                if (do_test(enc_mgr, enc_arch, dec_mgr,
                                            dec_arch, params, variant_data,
                                            0, 1, i) < 0) {
                                        if (verbose)
                                                printf("FAIL\n");
                                        exit(EXIT_FAILURE);
                                }
                        }
                }
        }
        if (verbose)
                printf("PASS\n");
}

/* Sets cipher direction and key size  */
static void
run_test(const IMB_ARCH enc_arch, const IMB_ARCH dec_arch,
         struct params_s *params, struct data *variant_data,
         const unsigned int safe_check)
{
        IMB_MGR *enc_mgr = NULL;
        IMB_MGR *dec_mgr = NULL;

        if (enc_arch == IMB_ARCH_NOAESNI)
                enc_mgr = alloc_mb_mgr(flags | IMB_FLAG_AESNI_OFF);
        else
                enc_mgr = alloc_mb_mgr(flags);

        if (enc_mgr == NULL) {
                fprintf(stderr, "MB MGR could not be allocated\n");
                exit(EXIT_FAILURE);
        }

        switch (enc_arch) {
        case IMB_ARCH_SSE:
        case IMB_ARCH_NOAESNI:
                init_mb_mgr_sse(enc_mgr);
                break;
        case IMB_ARCH_AVX:
                init_mb_mgr_avx(enc_mgr);
                break;
        case IMB_ARCH_AVX2:
                init_mb_mgr_avx2(enc_mgr);
                break;
        case IMB_ARCH_AVX512:
                init_mb_mgr_avx512(enc_mgr);
                break;
        default:
                fprintf(stderr, "Invalid architecture\n");
                exit(EXIT_FAILURE);
        }

        printf("Encrypting ");
        print_tested_arch(enc_mgr->features, enc_arch);

        if (dec_arch == IMB_ARCH_NOAESNI)
                dec_mgr = alloc_mb_mgr(flags | IMB_FLAG_AESNI_OFF);
        else
                dec_mgr = alloc_mb_mgr(flags);

        if (dec_mgr == NULL) {
                fprintf(stderr, "MB MGR could not be allocated\n");
                exit(EXIT_FAILURE);
        }

        switch (dec_arch) {
        case IMB_ARCH_SSE:
        case IMB_ARCH_NOAESNI:
                init_mb_mgr_sse(dec_mgr);
                break;
        case IMB_ARCH_AVX:
                init_mb_mgr_avx(dec_mgr);
                break;
        case IMB_ARCH_AVX2:
                init_mb_mgr_avx2(dec_mgr);
                break;
        case IMB_ARCH_AVX512:
                init_mb_mgr_avx512(dec_mgr);
                break;
        default:
                fprintf(stderr, "Invalid architecture\n");
                exit(EXIT_FAILURE);
        }

        printf("Decrypting ");
        print_tested_arch(dec_mgr->features, dec_arch);

        if (custom_test) {
                params->key_size = custom_job_params.key_size;
                params->cipher_mode = custom_job_params.cipher_mode;
                params->hash_alg = custom_job_params.hash_alg;
                process_variant(enc_mgr, enc_arch, dec_mgr, dec_arch, params,
                                variant_data, safe_check);
                goto exit;
        }

        IMB_HASH_ALG    hash_alg;
        IMB_CIPHER_MODE c_mode;

        for (c_mode = IMB_CIPHER_CBC; c_mode < IMB_CIPHER_NUM;
             c_mode++) {
                /* Skip IMB_CIPHER_CUSTOM */
                if (c_mode == IMB_CIPHER_CUSTOM)
                        continue;

                params->cipher_mode = c_mode;

                for (hash_alg = IMB_AUTH_HMAC_SHA_1;
                     hash_alg < IMB_AUTH_NUM;
                     hash_alg++) {
                        /* Skip IMB_AUTH_CUSTOM */
                        if (hash_alg == IMB_AUTH_CUSTOM)
                                continue;

                        /* Skip not supported combinations */
                        if ((c_mode == IMB_CIPHER_GCM &&
                            hash_alg != IMB_AUTH_AES_GMAC) ||
                            (c_mode != IMB_CIPHER_GCM &&
                            hash_alg == IMB_AUTH_AES_GMAC))
                                continue;
                        if ((c_mode == IMB_CIPHER_CCM &&
                            hash_alg != IMB_AUTH_AES_CCM) ||
                            (c_mode != IMB_CIPHER_CCM &&
                            hash_alg == IMB_AUTH_AES_CCM))
                                continue;
                        if ((c_mode == IMB_CIPHER_PON_AES_CNTR &&
                            hash_alg != IMB_AUTH_PON_CRC_BIP) ||
                            (c_mode != IMB_CIPHER_PON_AES_CNTR &&
                            hash_alg == IMB_AUTH_PON_CRC_BIP))
                                continue;
                        if (c_mode == IMB_CIPHER_DOCSIS_SEC_BPI &&
                            (hash_alg != IMB_AUTH_NULL &&
                             hash_alg != IMB_AUTH_DOCSIS_CRC32))
                                continue;
                        if (c_mode != IMB_CIPHER_DOCSIS_SEC_BPI &&
                            hash_alg == IMB_AUTH_DOCSIS_CRC32)
                                continue;
                        if (c_mode == IMB_CIPHER_GCM &&
                            (hash_alg == IMB_AUTH_AES_GMAC_128 ||
                             hash_alg == IMB_AUTH_AES_GMAC_192 ||
                             hash_alg == IMB_AUTH_AES_GMAC_256))
                                continue;
                        if ((c_mode == IMB_CIPHER_CHACHA20_POLY1305 &&
                             hash_alg != IMB_AUTH_CHACHA20_POLY1305) ||
                            (c_mode != IMB_CIPHER_CHACHA20_POLY1305 &&
                             hash_alg == IMB_AUTH_CHACHA20_POLY1305))
                                continue;

                        if ((c_mode == IMB_CIPHER_SNOW_V_AEAD &&
                             hash_alg != IMB_AUTH_SNOW_V_AEAD) ||
                            (c_mode != IMB_CIPHER_SNOW_V_AEAD &&
                             hash_alg == IMB_AUTH_SNOW_V_AEAD))
                                continue;

                        /* This test app does not support SGL yet */
                        if ((c_mode == IMB_CIPHER_CHACHA20_POLY1305_SGL) ||
                             (hash_alg == IMB_AUTH_CHACHA20_POLY1305_SGL))
                                continue;

                        if ((c_mode == IMB_CIPHER_GCM_SGL) ||
                            (hash_alg == IMB_AUTH_GCM_SGL))
                                continue;

                        params->hash_alg = hash_alg;

                        uint8_t min_sz = key_sizes[c_mode - 1][0];
                        uint8_t max_sz = key_sizes[c_mode - 1][1];
                        uint8_t step_sz = key_sizes[c_mode - 1][2];
                        uint8_t key_sz;

                        for (key_sz = min_sz; key_sz <= max_sz;
                             key_sz += step_sz) {
                                params->key_size = key_sz;
                                process_variant(enc_mgr, enc_arch, dec_mgr,
                                                dec_arch, params, variant_data,
                                                safe_check);
                        }
                }
        }

exit:
        free_mb_mgr(enc_mgr);
        free_mb_mgr(dec_mgr);
}

/* Prepares data structure for test variants storage,
 * sets test configuration
 */
static void
run_tests(const unsigned int safe_check)
{
        struct params_s params;
        struct data *variant_data = NULL;
        IMB_ARCH enc_arch, dec_arch;
#ifdef PIN_BASED_CEC
        const uint32_t pkt_size = job_sizes[RANGE_MIN];
        const uint32_t num_iter = job_sizes[RANGE_MAX];
#else
        const uint32_t min_size = job_sizes[RANGE_MIN];
        const uint32_t max_size = job_sizes[RANGE_MAX];
        const uint32_t step_size = job_sizes[RANGE_STEP];
#endif

#ifdef PIN_BASED_CEC
        params.num_sizes = 1;
#else
        params.num_sizes = ((max_size - min_size) / step_size) + 1;
#endif
        variant_data = malloc(sizeof(struct data));

        if (variant_data == NULL) {
                fprintf(stderr, "Test data could not be allocated\n");
                exit(EXIT_FAILURE);
        }

        if (verbose) {
#ifdef PIN_BASED_CEC
                printf("Testing buffer size = %u bytes, %u times\n",
                       pkt_size, num_iter);
#else
                if (min_size == max_size)
                        printf("Testing buffer size = %u bytes\n", min_size);
                else
                        printf("Testing buffer sizes from %u to %u "
                               "in steps of %u bytes\n",
                               min_size, max_size, step_size);
#endif
        }
        /* Performing tests for each selected architecture */
        for (enc_arch = IMB_ARCH_NOAESNI; enc_arch < IMB_ARCH_NUM;
             enc_arch++) {
                if (enc_archs[enc_arch] == 0)
                        continue;
                for (dec_arch = IMB_ARCH_NOAESNI; dec_arch < IMB_ARCH_NUM;
                     dec_arch++) {
                        if (dec_archs[dec_arch] == 0)
                                continue;
                        run_test(enc_arch, dec_arch, &params, variant_data,
                                 safe_check);
                }

        } /* end for run */

        free(variant_data);
}

static void usage(const char *app_name)
{
        fprintf(stderr, "Usage: %s [args], "
                "where args are zero or more\n"
                "-h: print this message\n"
                "-v: verbose, prints extra information\n"
                "--enc-arch: encrypting with architecture "
                "(NO-AESNI/SSE/AVX/AVX2/AVX512)\n"
                "--dec-arch: decrypting with architecture "
                "(NO-AESNI/SSE/AVX/AVX2/AVX512)\n"
                "--cipher-algo: Select cipher algorithm to run on the custom "
                "test\n"
                "--hash-algo: Select hash algorithm to run on the custom test\n"
                "--aead-algo: Select AEAD algorithm to run on the custom test\n"
                "--no-avx512: Don't do AVX512\n"
                "--no-avx2: Don't do AVX2\n"
                "--no-avx: Don't do AVX\n"
                "--no-sse: Don't do SSE\n"
                "--aesni-emu: Do AESNI_EMU (disabled by default)\n"
                "--shani-on: use SHA extensions, default: auto-detect\n"
                "--shani-off: don't use SHA extensions\n"
                "--cipher-iv-size: size of cipher IV.\n"
                "--auth-iv-size: size of authentication IV.\n"
                "--job-size: size of the cipher & MAC job in bytes. "
#ifndef PIN_BASED_CEC
                "It can be:\n"
                "            - single value: test single size\n"
                "            - range: test multiple sizes with following format"
                " min:step:max (e.g. 16:16:256)\n"
#else
                "            - size:1:num_iterations format\n"
                "              e.g. 64:1:128 => repeat 128 times operation on a 64 byte buffer\n"
#endif
                "            (-o still applies for MAC)\n"
                "--num-jobs: maximum number of number of jobs to submit in one go "
                "(maximum = %u)\n"
                "--safe-check: check if keys, IVs, plaintext or tags "
                "get cleared from IMB_MGR upon job completion (off by default; "
                "requires library compiled with SAFE_DATA)\n",
                app_name, MAX_NUM_JOBS);
}

static int
get_next_num_arg(const char * const *argv, const int index, const int argc,
                 void *dst, const size_t dst_size)
{
        char *endptr = NULL;
        uint64_t val;

        if (dst == NULL || argv == NULL || index < 0 || argc < 0) {
                fprintf(stderr, "%s() internal error!\n", __func__);
                exit(EXIT_FAILURE);
        }

        if (index >= (argc - 1)) {
                fprintf(stderr, "'%s' requires an argument!\n", argv[index]);
                exit(EXIT_FAILURE);
        }

#ifdef _WIN32
        val = _strtoui64(argv[index + 1], &endptr, 0);
#else
        val = strtoull(argv[index + 1], &endptr, 0);
#endif
        if (endptr == argv[index + 1] || (endptr != NULL && *endptr != '\0')) {
                fprintf(stderr, "Error converting '%s' as value for '%s'!\n",
                        argv[index + 1], argv[index]);
                exit(EXIT_FAILURE);
        }

        switch (dst_size) {
        case (sizeof(uint8_t)):
                *((uint8_t *)dst) = (uint8_t) val;
                break;
        case (sizeof(uint16_t)):
                *((uint16_t *)dst) = (uint16_t) val;
                break;
        case (sizeof(uint32_t)):
                *((uint32_t *)dst) = (uint32_t) val;
                break;
        case (sizeof(uint64_t)):
                *((uint64_t *)dst) = val;
                break;
        default:
                fprintf(stderr, "%s() invalid dst_size %u!\n",
                        __func__, (unsigned) dst_size);
                exit(EXIT_FAILURE);
                break;
        }

        return index + 1;
}

/*
 * Check string argument is supported and if it is, return values associated
 * with it.
 */
static const union params *
check_string_arg(const char *param, const char *arg,
                 const struct str_value_mapping *map,
                 const unsigned int num_avail_opts)
{
        unsigned int i;

        if (arg == NULL) {
                fprintf(stderr, "%s requires an argument\n", param);
                goto exit;
        }

        for (i = 0; i < num_avail_opts; i++)
                if (strcasecmp(arg, map[i].name) == 0)
                        return &(map[i].values);

        /* Argument is not listed in the available options */
        fprintf(stderr, "Invalid argument for %s\n", param);
exit:
        fprintf(stderr, "Accepted arguments: ");
        for (i = 0; i < num_avail_opts; i++)
                fprintf(stderr, "%s ", map[i].name);
        fprintf(stderr, "\n");

        return NULL;
}

static int
parse_range(const char * const *argv, const int index, const int argc,
            uint32_t range_values[NUM_RANGE])
{
        char *token;
        uint32_t number;
        unsigned int i;


        if (range_values == NULL || argv == NULL || index < 0 || argc < 0) {
                fprintf(stderr, "%s() internal error!\n", __func__);
                exit(EXIT_FAILURE);
        }

        if (index >= (argc - 1)) {
                fprintf(stderr, "'%s' requires an argument!\n", argv[index]);
                exit(EXIT_FAILURE);
        }

        char *copy_arg = strdup(argv[index + 1]);

        if (copy_arg == NULL) {
                fprintf(stderr, "%s() internal error!\n", __func__);
                exit(EXIT_FAILURE);
        }

        errno = 0;
        token = strtok(copy_arg, ":");

        /* Try parsing range (minimum, step and maximum values) */
        for (i = 0; i < NUM_RANGE; i++) {
                if (token == NULL)
                        goto no_range;

                number = strtoul(token, NULL, 10);

                if (errno != 0)
                        goto no_range;

                range_values[i] = number;
                token = strtok(NULL, ":");
        }

        if (token != NULL)
                goto no_range;

#ifndef PIN_BASED_CEC
        if (range_values[RANGE_MAX] < range_values[RANGE_MIN]) {
                fprintf(stderr, "Maximum value of range cannot be lower "
                        "than minimum value\n");
                exit(EXIT_FAILURE);
        }

        if (range_values[RANGE_STEP] == 0) {
                fprintf(stderr, "Step value in range cannot be 0\n");
                exit(EXIT_FAILURE);
        }
#endif
        goto end_range;
no_range:
        /* Try parsing as single value */
        get_next_num_arg(argv, index, argc, &job_sizes[RANGE_MIN],
                     sizeof(job_sizes[RANGE_MIN]));

        job_sizes[RANGE_MAX] = job_sizes[RANGE_MIN];

end_range:
        free(copy_arg);
        return (index + 1);

}

int main(int argc, char *argv[])
{
        int i;
        unsigned int arch_id;
        uint8_t arch_support[IMB_ARCH_NUM];
        const union params *values;
        unsigned int cipher_algo_set = 0;
        unsigned int hash_algo_set = 0;
        unsigned int aead_algo_set = 0;
        unsigned int safe_check = 0;

        for (i = 1; i < argc; i++)
                if (strcmp(argv[i], "-h") == 0) {
                        usage(argv[0]);
                        return EXIT_SUCCESS;
                } else if (strcmp(argv[i], "-v") == 0) {
                        verbose = 1;
                } else if (update_flags_and_archs(argv[i],
                                                   enc_archs,
                                                   &flags)) {
                        if (!update_flags_and_archs(argv[i],
                                                     dec_archs,
                                                     &flags)) {
                                fprintf(stderr,
                                       "Same archs should be available\n");
                                return EXIT_FAILURE;
                        }
                } else if (strcmp(argv[i], "--enc-arch") == 0) {

                        /* Use index 1 to skip arch_str_map.name = "NONE" */
                        values = check_string_arg(argv[i], argv[i+1],
                                                  arch_str_map + 1,
                                                  DIM(arch_str_map) - 1);
                        if (values == NULL)
                                return EXIT_FAILURE;

                        /*
                         * Disable all the other architectures
                         * and enable only the specified
                         */
                        memset(enc_archs, 0, sizeof(enc_archs));
                        enc_archs[values->arch_type] = 1;
                        i++;
                } else if (strcmp(argv[i], "--dec-arch") == 0) {
                        /* Use index 1 to skip arch_str_map.name = "NONE" */
                        values = check_string_arg(argv[i], argv[i+1],
                                                  arch_str_map + 1,
                                                  DIM(arch_str_map) - 1);
                        if (values == NULL)
                                return EXIT_FAILURE;

                        /*
                         * Disable all the other architectures
                         * and enable only the specified
                         */
                        memset(dec_archs, 0, sizeof(dec_archs));
                        dec_archs[values->arch_type] = 1;
                        i++;
                } else if (strcmp(argv[i], "--cipher-algo") == 0) {
                        values = check_string_arg(argv[i], argv[i+1],
                                        cipher_algo_str_map,
                                        DIM(cipher_algo_str_map));
                        if (values == NULL)
                                return EXIT_FAILURE;

                        custom_job_params.cipher_mode =
                                        values->job_params.cipher_mode;
                        custom_job_params.key_size =
                                        values->job_params.key_size;
                        custom_test = 1;
                        cipher_algo_set = 1;
                        i++;
                } else if (strcmp(argv[i], "--hash-algo") == 0) {
                        values = check_string_arg(argv[i], argv[i+1],
                                        hash_algo_str_map,
                                        DIM(hash_algo_str_map));
                        if (values == NULL)
                                return EXIT_FAILURE;

                        custom_job_params.hash_alg =
                                        values->job_params.hash_alg;
                        custom_test = 1;
                        hash_algo_set = 1;
                        i++;
                } else if (strcmp(argv[i], "--aead-algo") == 0) {
                        values = check_string_arg(argv[i], argv[i+1],
                                        aead_algo_str_map,
                                        DIM(aead_algo_str_map));
                        if (values == NULL)
                                return EXIT_FAILURE;

                        custom_job_params.cipher_mode =
                                        values->job_params.cipher_mode;
                        custom_job_params.key_size =
                                        values->job_params.key_size;
                        custom_job_params.hash_alg =
                                        values->job_params.hash_alg;
                        custom_test = 1;
                        aead_algo_set = 1;
                        i++;
                } else if (strcmp(argv[i], "--job-size") == 0) {
                        /* Try parsing the argument as a range first */
                        i = parse_range((const char * const *)argv, i, argc,
                                          job_sizes);
                        if (job_sizes[RANGE_MAX] > JOB_SIZE_TOP) {
                                fprintf(stderr,
                                       "Invalid job size %u (max %u)\n",
                                       (unsigned) job_sizes[RANGE_MAX],
                                       JOB_SIZE_TOP);
                                return EXIT_FAILURE;
                        }
                } else if (strcmp(argv[i], "--cipher-iv-size") == 0) {
                        i = get_next_num_arg((const char * const *)argv, i,
                                             argc, &cipher_iv_size,
                                             sizeof(cipher_iv_size));
                        if (cipher_iv_size > MAX_IV_SIZE) {
                                fprintf(stderr, "IV size cannot be "
                                        "higher than %u\n", MAX_IV_SIZE);
                                return EXIT_FAILURE;
                        }
                } else if (strcmp(argv[i], "--auth-iv-size") == 0) {
                        i = get_next_num_arg((const char * const *)argv, i,
                                             argc, &auth_iv_size,
                                             sizeof(auth_iv_size));
                        if (auth_iv_size > MAX_IV_SIZE) {
                                fprintf(stderr, "IV size cannot be "
                                        "higher than %u\n", MAX_IV_SIZE);
                                return EXIT_FAILURE;
                        }
                } else if (strcmp(argv[i], "--num-jobs") == 0) {
                        i = get_next_num_arg((const char * const *)argv, i,
                                             argc, &max_num_jobs,
                                             sizeof(max_num_jobs));
                        if (max_num_jobs > MAX_NUM_JOBS) {
                                fprintf(stderr, "Number of jobs cannot be "
                                        "higher than %u\n", MAX_NUM_JOBS);
                                return EXIT_FAILURE;
                        }
                } else if (strcmp(argv[i], "--safe-check") == 0) {
                        safe_check = 1;
                } else if (strcmp(argv[i], "--imix") == 0) {
                        imix_enabled = 1;
                } else {
                        usage(argv[0]);
                        return EXIT_FAILURE;
                }

        if (custom_test) {
                if (aead_algo_set && (cipher_algo_set || hash_algo_set)) {
                        fprintf(stderr, "AEAD algorithm cannot be used "
                                        "combined with another cipher/hash "
                                        "algorithm\n");
                        return EXIT_FAILURE;
                }
        }

        if (job_sizes[RANGE_MIN] == 0) {
                fprintf(stderr, "Buffer size cannot be 0 unless only "
                                "an AEAD algorithm is tested\n");
                return EXIT_FAILURE;
        }

        /* detect available architectures and features*/
        if (detect_arch(arch_support) < 0)
                return EXIT_FAILURE;

        /* disable tests depending on instruction sets supported */
        for (arch_id = IMB_ARCH_NOAESNI; arch_id < IMB_ARCH_NUM; arch_id++) {
                if (arch_support[arch_id] == 0) {
                        enc_archs[arch_id] = 0;
                        dec_archs[arch_id] = 0;
                        fprintf(stderr,
                                "%s not supported. Disabling %s tests\n",
                                arch_str_map[arch_id].name,
                                arch_str_map[arch_id].name);
                }
        }

        IMB_MGR *p_mgr = alloc_mb_mgr(flags);

        if (p_mgr == NULL) {
                fprintf(stderr, "Error allocating MB_MGR structure!\n");
                return EXIT_FAILURE;
        }

        if (safe_check && ((p_mgr->features & IMB_FEATURE_SAFE_DATA) == 0)) {
                fprintf(stderr, "Library needs to be compiled with SAFE_DATA "
                                "if --safe-check is enabled\n");
                free_mb_mgr(p_mgr);
                return EXIT_FAILURE;
        }
        free_mb_mgr(p_mgr);

        srand(SEED);

        if (safe_check)
                generate_patterns();

        run_tests(safe_check);

        return EXIT_SUCCESS;
}
