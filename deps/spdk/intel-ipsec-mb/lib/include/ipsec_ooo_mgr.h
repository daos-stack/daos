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

#ifndef IMB_IPSEC_MB_INTERNAL_H
#define IMB_IPSEC_MB_INTERNAL_H

#include "intel-ipsec-mb.h"

#define NUM_MD5_DIGEST_WORDS     4
#define NUM_SHA_DIGEST_WORDS     5
#define NUM_SHA_256_DIGEST_WORDS 8
#define NUM_SHA_224_DIGEST_WORDS 7
#define NUM_SHA_512_DIGEST_WORDS 8
#define NUM_SHA_384_DIGEST_WORDS 6

#define SHA_DIGEST_WORD_SIZE      4
#define SHA224_DIGEST_WORD_SIZE   4
#define SHA256_DIGEST_WORD_SIZE   4
#define SHA384_DIGEST_WORD_SIZE   8
#define SHA512_DIGEST_WORD_SIZE   8

/* Number of lanes AVX512, AVX2, AVX and SSE */
#define AVX512_NUM_SHA1_LANES   16
#define AVX512_NUM_SHA256_LANES 16
#define AVX512_NUM_SHA512_LANES 8
#define AVX512_NUM_MD5_LANES    32
#define AVX512_NUM_DES_LANES    16

#define AVX2_NUM_SHA1_LANES     8
#define AVX2_NUM_SHA256_LANES   8
#define AVX2_NUM_SHA512_LANES   4
#define AVX2_NUM_MD5_LANES      16

#define AVX_NUM_SHA1_LANES      4
#define AVX_NUM_SHA256_LANES    4
#define AVX_NUM_SHA512_LANES    2
#define AVX_NUM_MD5_LANES       8

#define SSE_NUM_SHA1_LANES   AVX_NUM_SHA1_LANES
#define SSE_NUM_SHA256_LANES AVX_NUM_SHA256_LANES
#define SSE_NUM_SHA512_LANES AVX_NUM_SHA512_LANES
#define SSE_NUM_MD5_LANES    AVX_NUM_MD5_LANES

/*
 * Each row is sized to hold enough lanes for AVX2, AVX1 and SSE use a subset
 * of each row. Thus one row is not adjacent in memory to its neighboring rows
 * in the case of SSE and AVX1.
 */
#define MD5_DIGEST_SZ    (NUM_MD5_DIGEST_WORDS * AVX512_NUM_MD5_LANES)
#define SHA1_DIGEST_SZ   (NUM_SHA_DIGEST_WORDS * AVX512_NUM_SHA1_LANES)
#define SHA256_DIGEST_SZ (NUM_SHA_256_DIGEST_WORDS * AVX512_NUM_SHA256_LANES)
#define SHA512_DIGEST_SZ (NUM_SHA_512_DIGEST_WORDS * AVX512_NUM_SHA512_LANES)

/* Maximum size of the ZUC state (LFSR (16) + X0-X3 (4) + R1-R2 (2)).
   For AVX512, each takes 16 double words, defining the maximum required size */
#define MAX_ZUC_STATE_SZ 16*(16 + 4 + 2)

/**
 *****************************************************************************
 * @description
 *      Packed structure to store the ZUC state for 16 packets.
 *****************************************************************************/
typedef struct zuc_state_16_s {
    uint32_t lfsrState[16][16];
    /**< State registers of the LFSR */
    uint32_t fR1[16];
    /**< register of F */
    uint32_t fR2[16];
    /**< register of F */
    uint32_t bX0[16];
    /**< Output X0 of the bit reorganization for 16 packets */
    uint32_t bX1[16];
    /**< Output X1 of the bit reorganization for 16 packets */
    uint32_t bX2[16];
    /**< Output X2 of the bit reorganization for 16 packets */
    uint32_t bX3[16];
    /**< Output X3 of the bit reorganization for 16 packets */
} ZucState16_t;

/*
 * Argument structures for various algorithms
 */
typedef struct {
        const uint8_t *in[16];
        uint8_t *out[16];
        const uint32_t *keys[16];
        DECLARE_ALIGNED(imb_uint128_t IV[16], 64);
        DECLARE_ALIGNED(imb_uint128_t key_tab[15][16], 64);
} AES_ARGS;

typedef struct {
        DECLARE_ALIGNED(uint32_t digest[SHA1_DIGEST_SZ], 32);
        uint8_t *data_ptr[AVX512_NUM_SHA1_LANES];
} SHA1_ARGS;

typedef struct {
        DECLARE_ALIGNED(uint32_t digest[SHA256_DIGEST_SZ], 32);
        uint8_t *data_ptr[AVX512_NUM_SHA256_LANES];
} SHA256_ARGS;

typedef struct {
        DECLARE_ALIGNED(uint64_t digest[SHA512_DIGEST_SZ], 32);
        uint8_t *data_ptr[AVX512_NUM_SHA512_LANES];
}  SHA512_ARGS;

typedef struct {
        DECLARE_ALIGNED(uint32_t digest[MD5_DIGEST_SZ], 32);
        uint8_t *data_ptr[AVX512_NUM_MD5_LANES];
} MD5_ARGS;

typedef struct {
        const uint8_t *in[16];
        const uint32_t *keys[16];
        DECLARE_ALIGNED(imb_uint128_t ICV[16], 32);
        DECLARE_ALIGNED(imb_uint128_t key_tab[11][16], 64);
} AES_XCBC_ARGS_x16;

typedef struct {
        const uint8_t *in[AVX512_NUM_DES_LANES];
        uint8_t *out[AVX512_NUM_DES_LANES];
        const uint8_t *keys[AVX512_NUM_DES_LANES];
        uint32_t IV[AVX512_NUM_DES_LANES * 2]; /* uint32_t is more handy here */
        uint32_t partial_len[AVX512_NUM_DES_LANES];
        uint32_t block_len[AVX512_NUM_DES_LANES];
        const uint8_t *last_in[AVX512_NUM_DES_LANES];
        uint8_t *last_out[AVX512_NUM_DES_LANES];
} DES_ARGS_x16;

typedef struct {
        DECLARE_ALIGNED(const uint8_t *in[16], 64);
        DECLARE_ALIGNED(uint8_t *out[16], 64);
        const uint8_t *keys[16];
        DECLARE_ALIGNED(uint8_t iv[16*32], 32);
        DECLARE_ALIGNED(uint32_t digest[16], 64);
        /* Memory for 128 bytes of KS for 16 buffers */
        DECLARE_ALIGNED(uint32_t ks[16 * 2 * 16], 64);
} ZUC_ARGS_x16;

/**
 *****************************************************************************
 * @description
 *      Structure to store the Snow3G state for 16 packets.
 *****************************************************************************/
typedef struct {
        void *in[16];
        void *out[16];
        void *keys[16];
        void *iv[16];
        uint32_t LFSR_0[16];
        uint32_t LFSR_1[16];
        uint32_t LFSR_2[16];
        uint32_t LFSR_3[16];
        uint32_t LFSR_4[16];
        uint32_t LFSR_5[16];
        uint32_t LFSR_6[16];
        uint32_t LFSR_7[16];
        uint32_t LFSR_8[16];
        uint32_t LFSR_9[16];
        uint32_t LFSR_10[16];
        uint32_t LFSR_11[16];
        uint32_t LFSR_12[16];
        uint32_t LFSR_13[16];
        uint32_t LFSR_14[16];
        uint32_t LFSR_15[16];
        uint32_t FSM_1[16];
        uint32_t FSM_2[16];
        uint32_t FSM_3[16];
        uint64_t INITIALIZED[16];
        uint64_t byte_length[16];
} SNOW3G_ARGS;

/* AES out-of-order scheduler fields */
typedef struct {
        AES_ARGS args;
        DECLARE_ALIGNED(uint16_t lens[16], 16);
        /* each nibble is index (0...15) of an unused lane,
         * the last nibble is set to F as a flag
         */
        uint64_t unused_lanes;
        IMB_JOB *job_in_lane[16];
        uint64_t num_lanes_inuse;
        DECLARE_ALIGNED(uint64_t lens64[16], 64);
        uint64_t road_block;
} MB_MGR_AES_OOO;

/* DOCSIS AES out-of-order scheduler fields */
typedef struct {
        AES_ARGS args;
        DECLARE_ALIGNED(uint16_t lens[16], 16);
        /* each nibble is index (0...15) of an unused lane,
         * the last nibble is set to F as a flag
         */
        uint64_t unused_lanes;
        IMB_JOB *job_in_lane[16];
        uint64_t num_lanes_inuse;
        DECLARE_ALIGNED(imb_uint128_t crc_init[16], 64);
        DECLARE_ALIGNED(uint16_t crc_len[16], 16);
        DECLARE_ALIGNED(uint8_t crc_done[16], 16);
        uint64_t road_block;
} MB_MGR_DOCSIS_AES_OOO;

/* AES XCBC out-of-order scheduler fields */
typedef struct {
        DECLARE_ALIGNED(uint8_t final_block[2 * 16], 32);
        IMB_JOB *job_in_lane;
        uint64_t final_done;
} XCBC_LANE_DATA;

typedef struct {
        AES_XCBC_ARGS_x16 args;
        DECLARE_ALIGNED(uint16_t lens[16], 32);
        /* each byte is index (0...3) of unused lanes
         * byte 4 is set to FF as a flag
         */
        uint64_t unused_lanes;
        XCBC_LANE_DATA ldata[16];
        uint64_t num_lanes_inuse;
        uint64_t road_block;
} MB_MGR_AES_XCBC_OOO;

/* AES-CCM out-of-order scheduler structure */
typedef struct {
        AES_ARGS args; /* need to re-use AES arguments */
        DECLARE_ALIGNED(uint16_t lens[16], 32);
        DECLARE_ALIGNED(uint16_t init_done[16], 32);
        /* each byte is index (0...3) of unused lanes
         * byte 4 is set to FF as a flag
         */
        uint64_t unused_lanes;
        DECLARE_ALIGNED(IMB_JOB *job_in_lane[16], 16);
        uint64_t num_lanes_inuse;
        DECLARE_ALIGNED(uint8_t init_blocks[16 * (4 * 16)], 64);
        uint64_t road_block;
} MB_MGR_CCM_OOO;


/* AES-CMAC out-of-order scheduler structure */
typedef struct {
        AES_ARGS args; /* need to re-use AES arguments */
        DECLARE_ALIGNED(uint16_t lens[16], 32);
        DECLARE_ALIGNED(uint16_t init_done[16], 32);
        /* each byte is index (0...3) of unused lanes
         * byte 4 is set to FF as a flag
         */
        uint64_t unused_lanes;
        DECLARE_ALIGNED(IMB_JOB *job_in_lane[16], 16);
        uint64_t num_lanes_inuse;
        DECLARE_ALIGNED(uint8_t scratch[16 * 16], 32);
        uint64_t road_block;
} MB_MGR_CMAC_OOO;


/* DES out-of-order scheduler fields */
typedef struct {
        DES_ARGS_x16 args;
        DECLARE_ALIGNED(uint16_t lens[16], 16);
        /* each nibble is index (0...7) of unused lanes
         * nibble 8 is set to F as a flag
         */
        uint64_t unused_lanes;
        IMB_JOB *job_in_lane[16];
        uint64_t num_lanes_inuse;
        uint64_t road_block;
} MB_MGR_DES_OOO;

/* ZUC out-of-order scheduler fields */
typedef struct {
        ZUC_ARGS_x16 args;
        DECLARE_ALIGNED(uint16_t lens[16], 16);
        uint64_t unused_lanes;
        IMB_JOB *job_in_lane[16];
        uint64_t num_lanes_inuse;
        DECLARE_ALIGNED(uint32_t state[MAX_ZUC_STATE_SZ], 64);
        uint16_t init_not_done;
        uint16_t unused_lane_bitmask;
        uint64_t road_block;
} MB_MGR_ZUC_OOO;

/* HMAC-SHA1 and HMAC-SHA256/224 */
typedef struct {
        /* YMM aligned access to extra_block */
        DECLARE_ALIGNED(uint8_t extra_block[2 * IMB_SHA1_BLOCK_SIZE+8], 32);
        IMB_JOB *job_in_lane;
        uint8_t outer_block[64];
        uint32_t outer_done;
        uint32_t extra_blocks; /* num extra blocks (1 or 2) */
        uint32_t size_offset;  /* offset in extra_block to start of
                                * size field */
        uint32_t start_offset; /* offset to start of data */
} HMAC_SHA1_LANE_DATA;

/* HMAC-SHA512/384 */
typedef struct {
        DECLARE_ALIGNED(uint8_t extra_block[2*IMB_SHA_512_BLOCK_SIZE + 16], 32);
        uint8_t outer_block[IMB_SHA_512_BLOCK_SIZE];
        IMB_JOB *job_in_lane;
        uint32_t outer_done;
        uint32_t extra_blocks; /* num extra blocks (1 or 2) */
        uint32_t size_offset;  /* offset in extra_block to start of
                                * size field */
        uint32_t start_offset; /* offset to start of data */
} HMAC_SHA512_LANE_DATA;

/*
 * unused_lanes contains a list of unused lanes stored as bytes or as
 * nibbles depending on the arch. The end of list is either FF or F.
 */
typedef struct {
        SHA1_ARGS args;
        DECLARE_ALIGNED(uint16_t lens[16], 32);
        uint64_t unused_lanes;
        HMAC_SHA1_LANE_DATA ldata[AVX512_NUM_SHA1_LANES];
        uint32_t num_lanes_inuse;
        uint64_t road_block;
} MB_MGR_HMAC_SHA_1_OOO;

typedef struct {
        SHA256_ARGS args;
        DECLARE_ALIGNED(uint16_t lens[16], 16);
        uint64_t unused_lanes;
        HMAC_SHA1_LANE_DATA ldata[AVX512_NUM_SHA256_LANES];
        uint32_t num_lanes_inuse;
        uint64_t road_block;
} MB_MGR_HMAC_SHA_256_OOO;

typedef struct {
        SHA512_ARGS args;
        DECLARE_ALIGNED(uint16_t lens[8], 16);
        uint64_t unused_lanes;
        HMAC_SHA512_LANE_DATA ldata[AVX512_NUM_SHA512_LANES];
        uint64_t road_block;
} MB_MGR_HMAC_SHA_512_OOO;

/* MD5-HMAC out-of-order scheduler fields */
typedef struct {
        MD5_ARGS args;
        DECLARE_ALIGNED(uint16_t lens[AVX512_NUM_MD5_LANES], 16);
        /*
         * In the avx2 case, all 16 nibbles of unused lanes are used.
         * In that case num_lanes_inuse is used to detect the end of the list
         */
        uint64_t unused_lanes;
        HMAC_SHA1_LANE_DATA ldata[AVX512_NUM_MD5_LANES];
        uint32_t num_lanes_inuse;
        uint64_t road_block;
} MB_MGR_HMAC_MD5_OOO;

/* SNOW3G out-of-order scheduler fields */
typedef struct {
        DECLARE_ALIGNED(SNOW3G_ARGS args, 64);
        uint32_t lens[16];
        IMB_JOB *job_in_lane[16];
        uint32_t bits_fixup[16];
        uint64_t init_mask;
        uint64_t unused_lanes;
        uint64_t num_lanes_inuse;
        uint64_t init_done;
        /* Auth only - reserve 32 bytes to store KS for 16 buffers */
        DECLARE_ALIGNED(uint32_t ks[8 * 16], 32);
        uint64_t road_block;
} MB_MGR_SNOW3G_OOO;

IMB_DLL_LOCAL void
init_mb_mgr_sse_no_aesni_internal(IMB_MGR *state, const int reset_mgrs);
IMB_DLL_LOCAL void
init_mb_mgr_sse_internal(IMB_MGR *state, const int reset_mgrs);
IMB_DLL_LOCAL void
init_mb_mgr_avx_internal(IMB_MGR *state, const int reset_mgrs);
IMB_DLL_LOCAL void
init_mb_mgr_avx2_internal(IMB_MGR *state, const int reset_mgrs);
IMB_DLL_LOCAL void
init_mb_mgr_avx512_internal(IMB_MGR *state, const int reset_mgrs);

#endif /* IMB_IPSEC_MB_INTERNAL_H */
