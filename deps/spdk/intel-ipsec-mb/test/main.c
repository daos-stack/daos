/*****************************************************************************
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
*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <intel-ipsec-mb.h>

#include "gcm_ctr_vectors_test.h"
#include "customop_test.h"
#include "utils.h"

extern int des_test(const enum arch_type arch, struct IMB_MGR *mb_mgr);
extern int ccm_test(struct IMB_MGR *mb_mgr);
extern int cmac_test(struct IMB_MGR *mb_mgr);
extern int hmac_sha1_test(struct IMB_MGR *mb_mgr);
extern int hmac_sha256_sha512_test(struct IMB_MGR *mb_mgr);
extern int hmac_md5_test(struct IMB_MGR *mb_mgr);
extern int aes_test(struct IMB_MGR *mb_mgr);
extern int ecb_test(struct IMB_MGR *mb_mgr);
extern int sha_test(struct IMB_MGR *mb_mgr);
extern int chained_test(struct IMB_MGR *mb_mgr);
extern int api_test(struct IMB_MGR *mb_mgr, uint64_t flags);
extern int pon_test(struct IMB_MGR *mb_mgr);
extern int zuc_test(struct IMB_MGR *mb_mgr);
extern int kasumi_test(struct IMB_MGR *mb_mgr);
extern int snow3g_test(struct IMB_MGR *mb_mgr);
extern int direct_api_test(struct IMB_MGR *mb_mgr);
extern int clear_mem_test(struct IMB_MGR *mb_mgr);
extern int hec_test(struct IMB_MGR *mb_mgr);
extern int xcbc_test(struct IMB_MGR *mb_mgr);
extern int aes_cbcs_test(struct IMB_MGR *mb_mgr);
extern int crc_test(struct IMB_MGR *mb_mgr);
extern int chacha_test(struct IMB_MGR *mb_mgr);
extern int poly1305_test(struct IMB_MGR *mb_mgr);
extern int chacha20_poly1305_test(struct IMB_MGR *mb_mgr);
extern int null_test(struct IMB_MGR *mb_mgr);
extern int snow_v_test(struct IMB_MGR *mb_mgr);
extern int direct_api_param_test(struct IMB_MGR *mb_mgr);


#include "do_test.h"

static void
usage(const char *name)
{
	fprintf(stderr,
                "Usage: %s [args], where args are zero or more\n"
                "--no-aesni-emu: Don't do AESNI emulation\n"
                "--no-avx512: Don't do AVX512\n"
		"--no-avx2: Don't do AVX2\n"
		"--no-avx: Don't do AVX\n"
		"--no-sse: Don't do SSE\n"
                "--auto-detect: auto detects current architecture "
                "to run the tests\n  Note: Auto detection "
                "option now run by default and will be removed in the future\n"
		"--shani-on: use SHA extensions, default: auto-detect\n"
		"--shani-off: don't use SHA extensions\n", name);
}

static void
print_hw_features(void)
{
        const struct {
                uint64_t feat_val;
                const char *feat_name;
        } feat_tab[] = {
                { IMB_FEATURE_SHANI, "SHANI" },
                { IMB_FEATURE_AESNI, "AESNI" },
                { IMB_FEATURE_PCLMULQDQ, "PCLMULQDQ" },
                { IMB_FEATURE_CMOV, "CMOV" },
                { IMB_FEATURE_SSE4_2, "SSE4.2" },
                { IMB_FEATURE_AVX, "AVX" },
                { IMB_FEATURE_AVX2, "AVX2" },
                { IMB_FEATURE_AVX512_SKX, "AVX512(SKX)" },
                { IMB_FEATURE_VAES, "VAES" },
                { IMB_FEATURE_VPCLMULQDQ, "VPCLMULQDQ" },
                { IMB_FEATURE_GFNI, "GFNI" },
                { IMB_FEATURE_AVX512_IFMA, "AVX512-IFMA" },
                { IMB_FEATURE_BMI2, "BMI2" },
        };
        IMB_MGR *p_mgr = NULL;
        unsigned i;

        printf("Detected hardware features:\n");

        p_mgr = alloc_mb_mgr(0);
        if (p_mgr == NULL) {
                printf("\tERROR\n");
                return;
        }

        for (i = 0; i < IMB_DIM(feat_tab); i++) {
                const uint64_t val = feat_tab[i].feat_val;

                printf("\t%-*.*s : %s\n", 12, 12, feat_tab[i].feat_name,
                       ((p_mgr->features & val) == val) ? "OK" : "n/a");
        }

        free_mb_mgr(p_mgr);
}

int
main(int argc, char **argv)
{
        uint8_t arch_support[IMB_ARCH_NUM];
        int i, atype, auto_detect = 0;
        uint64_t flags = 0;
        int errors = 0;

        /* Check version number */
        if (imb_get_version() < IMB_VERSION(0, 50, 0))
                printf("Library version detection unsupported!\n");
        else
                printf("Detected library version: %s\n", imb_get_version_str());

        /* Print available CPU features */
        print_hw_features();

        /* Detect available architectures and features */
        if (detect_arch(arch_support) < 0)
                return EXIT_FAILURE;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0) {
			usage(argv[0]);
			return EXIT_SUCCESS;
		} else if (update_flags_and_archs(argv[i],
                                                  arch_support,
                                                  &flags))
			continue;
		else if (strcmp(argv[i], "--auto-detect") == 0)
                        (void) auto_detect; /* legacy option - to be removed */
		else {
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

        /* Go through architectures */
        for (atype = IMB_ARCH_NOAESNI; atype < IMB_ARCH_NUM; atype++) {
                IMB_MGR *p_mgr = NULL;

                if (!arch_support[atype])
                        continue;
                if (atype == IMB_ARCH_NOAESNI)
                        p_mgr = alloc_mb_mgr(flags | IMB_FLAG_AESNI_OFF);
                else
                        p_mgr = alloc_mb_mgr(flags);

                if (p_mgr == NULL) {
                        printf("Error allocating MB_MGR structure!\n");
                        return EXIT_FAILURE;
                }

                switch (atype) {
                case IMB_ARCH_SSE:
                case IMB_ARCH_NOAESNI:
                        init_mb_mgr_sse(p_mgr);
                        break;
                case IMB_ARCH_AVX:
                        init_mb_mgr_avx(p_mgr);
                        break;
                case IMB_ARCH_AVX2:
                        init_mb_mgr_avx2(p_mgr);
                        break;
                case IMB_ARCH_AVX512:
                        init_mb_mgr_avx512(p_mgr);
                        break;
                }

                print_tested_arch(p_mgr->features, atype);

                errors += known_answer_test(p_mgr);
                errors += do_test(p_mgr);
                errors += ctr_test(p_mgr);
                errors += pon_test(p_mgr);
                errors += xcbc_test(p_mgr);
                errors += gcm_test(p_mgr);
                errors += customop_test(p_mgr);
                errors += des_test(atype, p_mgr);
                errors += ccm_test(p_mgr);
                errors += cmac_test(p_mgr);
                errors += zuc_test(p_mgr);
                errors += kasumi_test(p_mgr);
                errors += snow3g_test(p_mgr);
                errors += hmac_sha1_test(p_mgr);
                errors += hmac_sha256_sha512_test(p_mgr);
                errors += hmac_md5_test(p_mgr);
                errors += aes_test(p_mgr);
                errors += ecb_test(p_mgr);
                errors += sha_test(p_mgr);
                errors += chained_test(p_mgr);
                errors += hec_test(p_mgr);
                errors += aes_cbcs_test(p_mgr);
                errors += chacha_test(p_mgr);
                errors += poly1305_test(p_mgr);
                errors += api_test(p_mgr, flags);
                errors += direct_api_test(p_mgr);
                errors += clear_mem_test(p_mgr);
                errors += crc_test(p_mgr);
                errors += chacha20_poly1305_test(p_mgr);
                errors += null_test(p_mgr);
                errors += snow_v_test(p_mgr);
                errors += direct_api_param_test(p_mgr);

                free_mb_mgr(p_mgr);
        }

        if (errors) {
                printf("Test completed: FAIL\n");
                return EXIT_FAILURE;
        }

        printf("Test completed: PASS\n");

        return EXIT_SUCCESS;
}
