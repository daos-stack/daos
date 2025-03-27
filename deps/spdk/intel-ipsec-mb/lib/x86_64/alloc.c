/*******************************************************************************
  Copyright (c) 2018-2021, Intel Corporation

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

#include <stdint.h>
#ifdef LINUX
#include <stdlib.h> /* posix_memalign() and free() */
#else
#include <malloc.h> /* _aligned_malloc() and aligned_free() */
#endif
#include <string.h>
#include <stddef.h> /* offsetof() */
#include "intel-ipsec-mb.h"
#include "ipsec_ooo_mgr.h"
#include "cpu_feature.h"
#include "error.h"

#define IMB_OOO_ROAD_BLOCK 0xDEADCAFEDEADCAFEULL

#define ALIGNMENT 64
#define ALIGN(x, y) ((x + (y - 1)) & (~(y - 1)))

#define OOO_INFO(imb_mgr_ooo_ptr_name__, ooo_mgr_type__) \
        { offsetof(IMB_MGR, imb_mgr_ooo_ptr_name__), \
          ALIGN(sizeof(ooo_mgr_type__), ALIGNMENT),      \
          offsetof(ooo_mgr_type__, road_block) }

const struct {
        size_t ooo_ptr_offset;
        size_t ooo_aligned_size;
        size_t road_block_offset;
} ooo_mgr_table[] = {
        OOO_INFO(aes128_ooo, MB_MGR_AES_OOO),
        OOO_INFO(aes192_ooo, MB_MGR_AES_OOO),
        OOO_INFO(aes256_ooo, MB_MGR_AES_OOO),
        OOO_INFO(docsis128_sec_ooo, MB_MGR_DOCSIS_AES_OOO),
        OOO_INFO(docsis128_crc32_sec_ooo, MB_MGR_DOCSIS_AES_OOO),
        OOO_INFO(docsis256_sec_ooo, MB_MGR_DOCSIS_AES_OOO),
        OOO_INFO(docsis256_crc32_sec_ooo, MB_MGR_DOCSIS_AES_OOO),
        OOO_INFO(des_enc_ooo, MB_MGR_DES_OOO),
        OOO_INFO(des_dec_ooo, MB_MGR_DES_OOO),
        OOO_INFO(des3_enc_ooo, MB_MGR_DES_OOO),
        OOO_INFO(des3_dec_ooo, MB_MGR_DES_OOO),
        OOO_INFO(docsis_des_enc_ooo, MB_MGR_DES_OOO),
        OOO_INFO(docsis_des_dec_ooo, MB_MGR_DES_OOO),
        OOO_INFO(hmac_sha_1_ooo, MB_MGR_HMAC_SHA_1_OOO),
        OOO_INFO(hmac_sha_224_ooo, MB_MGR_HMAC_SHA_256_OOO),
        OOO_INFO(hmac_sha_256_ooo, MB_MGR_HMAC_SHA_256_OOO),
        OOO_INFO(hmac_sha_384_ooo, MB_MGR_HMAC_SHA_512_OOO),
        OOO_INFO(hmac_sha_512_ooo, MB_MGR_HMAC_SHA_512_OOO),
        OOO_INFO(hmac_md5_ooo, MB_MGR_HMAC_MD5_OOO),
        OOO_INFO(aes_xcbc_ooo, MB_MGR_AES_XCBC_OOO),
        OOO_INFO(aes_ccm_ooo, MB_MGR_CCM_OOO),
        OOO_INFO(aes_cmac_ooo, MB_MGR_CMAC_OOO),
        OOO_INFO(aes128_cbcs_ooo, MB_MGR_AES_OOO),
        OOO_INFO(zuc_eea3_ooo, MB_MGR_ZUC_OOO),
        OOO_INFO(zuc_eia3_ooo, MB_MGR_ZUC_OOO),
        OOO_INFO(zuc256_eea3_ooo, MB_MGR_ZUC_OOO),
        OOO_INFO(zuc256_eia3_ooo, MB_MGR_ZUC_OOO),
        OOO_INFO(aes256_ccm_ooo, MB_MGR_CCM_OOO),
	OOO_INFO(aes256_cmac_ooo, MB_MGR_CMAC_OOO),
        OOO_INFO(snow3g_uea2_ooo, MB_MGR_SNOW3G_OOO),
        OOO_INFO(snow3g_uia2_ooo, MB_MGR_SNOW3G_OOO),
};

/**
 * @brief Calculates necessary memory size for IMB_MGR.
 *
 * @return Size for IMB_MGR (aligned to 64 bytes)
 */
size_t imb_get_mb_mgr_size(void)
{
        size_t ooo_total_size = 0;
        unsigned i;

        for (i = 0; i < IMB_DIM(ooo_mgr_table); i++)
                ooo_total_size += ooo_mgr_table[i].ooo_aligned_size;
        /*
         * Add 64 bytes into the maximum size calculation to
         * make sure there is enough room to align the OOO managers.
         */
        return (sizeof(IMB_MGR) + ooo_total_size + ALIGNMENT);
}

static uint8_t *get_ooo_ptr(IMB_MGR *mgr, const size_t offset)
{
        uint8_t *mgr_offset = &((uint8_t *) mgr)[offset];
        uint8_t **ptr = (uint8_t **) mgr_offset;

        return *ptr;
}

static void set_ooo_ptr(IMB_MGR *mgr, const size_t offset, uint8_t *new_ptr)
{
        uint8_t *mgr_offset = &((uint8_t *) mgr)[offset];
        uint8_t **ptr = (uint8_t **) mgr_offset;

        *ptr = new_ptr;
}

static void set_road_block(uint8_t *ooo_ptr, const size_t offset)
{
        uint64_t *p_road_block = (uint64_t *) &ooo_ptr[offset];

        *p_road_block = IMB_OOO_ROAD_BLOCK;
}

/*
 * Set last 8 bytes of OOO mgrs to predefined pattern
 *
 * This is to assist in searching for sensitive data remaining
 * in the heap after algorithmic code completes
 */
static void set_ooo_mgr_road_block(IMB_MGR *mgr)
{
        unsigned n;

        for (n = 0; n < IMB_DIM(ooo_mgr_table); n++)
                set_road_block(get_ooo_ptr(mgr,
                                           ooo_mgr_table[n].ooo_ptr_offset),
                               ooo_mgr_table[n].road_block_offset);
}

/**
 * @brief Initializes IMB_MGR pointers to out-of-order managers with
 *        use of externally allocated memory.
 *
 * imb_get_mb_mgr_size() should be called to know how much memory
 * should be allocated externally.
 *
 * init_mb_mgr_XXX() must be called after this function call,
 * whereas XXX is the desired architecture (including "auto"),
 * only if reset_mgr is set to 0.
 *
 * @param mem_ptr a pointer to allocated memory
 * @param flags multi-buffer manager flags
 *     IMB_FLAG_SHANI_OFF - disable use (and detection) of SHA extensions,
 *                          currently SHANI is only available for SSE
 *     IMB_FLAG_AESNI_OFF - disable use (and detection) of AES extensions.
 *
 * @param reset_mgr if 0, IMB_MGR structure is not cleared, else it is.
 *
 * @return Pointer to IMB_MGR structure
 */
IMB_MGR *imb_set_pointers_mb_mgr(void *mem_ptr, const uint64_t flags,
                                 const unsigned reset_mgr)
{
        if (mem_ptr == NULL) {
                imb_set_errno(mem_ptr, ENOMEM);
                return NULL;
        }

        IMB_MGR *ptr = (IMB_MGR *) mem_ptr;
        uint8_t *ptr8 = (uint8_t *) ptr;
        uint8_t *free_mem = &ptr8[ALIGN(sizeof(IMB_MGR), ALIGNMENT)];
        const size_t mem_size = imb_get_mb_mgr_size();
        unsigned i;

        if (reset_mgr) {
                /* Zero out MB_MGR memory */
                memset(mem_ptr, 0, mem_size);
        } else {
                IMB_ARCH used_arch = (IMB_ARCH) ptr->used_arch;

                /* Reset function pointers from previously used architecture */
                switch (used_arch) {
                case IMB_ARCH_NOAESNI:
                        init_mb_mgr_sse_no_aesni_internal(ptr, 0);
                        break;
                case IMB_ARCH_SSE:
                        init_mb_mgr_sse_internal(ptr, 0);
                        break;
                case IMB_ARCH_AVX:
                        init_mb_mgr_avx_internal(ptr, 0);
                        break;
                case IMB_ARCH_AVX2:
                        init_mb_mgr_avx2_internal(ptr, 0);
                        break;
                case IMB_ARCH_AVX512:
                        init_mb_mgr_avx512_internal(ptr, 0);
                        break;
                default:
                        break;
                }
        }

        imb_set_errno(ptr, 0);
        ptr->flags = flags; /* save the flags for future use in init */
        ptr->features = cpu_feature_adjust(flags, cpu_feature_detect());

        /* Set OOO pointers */
        for (i = 0; i < IMB_DIM(ooo_mgr_table); i++) {
                set_ooo_ptr(ptr, ooo_mgr_table[i].ooo_ptr_offset, free_mem);
                free_mem = &free_mem[ooo_mgr_table[i].ooo_aligned_size];
                IMB_ASSERT((uintptr_t)(free_mem - ptr8) <= mem_size);
        }
        set_ooo_mgr_road_block(ptr);

        return ptr;
}

static void *
alloc_aligned_mem(const size_t size)
{
        void *ptr;

#ifdef LINUX
        if (posix_memalign((void **)&ptr, ALIGNMENT, size))
                return NULL;
#else
        ptr = _aligned_malloc(size, ALIGNMENT);
#endif

        IMB_ASSERT(ptr != NULL);

        return ptr;
}

static void
free_mem(void *ptr)
{
#ifdef LINUX
        free(ptr);
#else
        _aligned_free(ptr);
#endif
}

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
IMB_MGR *alloc_mb_mgr(uint64_t flags)
{
        IMB_MGR *ptr = NULL;

        ptr = alloc_aligned_mem(imb_get_mb_mgr_size());
        IMB_ASSERT(ptr != NULL);
        if (ptr != NULL) {
                imb_set_pointers_mb_mgr(ptr, flags, 1);
        } else {
                imb_set_errno(ptr, ENOMEM);
                return NULL;
        }

        return ptr;
}

/**
 * @brief Frees memory allocated previously by alloc_mb_mgr()
 *
 * @param ptr a pointer to allocated MB_MGR structure
 *
 */
void free_mb_mgr(IMB_MGR *ptr)
{
        IMB_ASSERT(ptr != NULL);

        /* Free IMB_MGR */
        free_mem(ptr);
}
