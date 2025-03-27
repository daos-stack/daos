/*****************************************************************************
 Copyright (c) 2020-2021, Intel Corporation

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

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <intel-ipsec-mb.h>

#include "gcm_ctr_vectors_test.h"
#include "utils.h"

int hec_test(struct IMB_MGR *mb_mgr);

static void
test_32_bit(IMB_MGR *mgr, struct test_suite_context *ctx)
{
        static const uint32_t pf19_hec13[] = {
                0x660e4758, 0xcc076e69, 0xcb1f206b,
                0xa611502d, 0x4e1b7320, 0x0a196148,
                0xda034e4f, 0x5e116970, 0xea11646a,
                0xd70a6820, 0xa3186574, 0x41156375,
                0x0d077061, 0x9b1e6f20, 0x6601657a,
                0x5d1d6570, 0x130f2066, 0x631f696e,
                0x6013656e, 0x2e02614d, 0x1b012e61,
                0xd4182064, 0x9a0a6572, 0x2f162020
        };
        unsigned i;

        for (i = 0; i < DIM(pf19_hec13); i++) {
                const uint32_t in = pf19_hec13[i] & (~0xfff10000);
                const uint32_t expected_out = pf19_hec13[i];
                uint32_t out = 0;
                const uint8_t *in_p = (const uint8_t *) &in;
#ifdef DEBUG
                printf("[32-bit %d] PF | HEC:\t0x%08lx", i + 1,
                       (unsigned long) expected_out);
#endif
                out = IMB_HEC_32(mgr, in_p);

                if (out != expected_out) {
                        printf("\tHEC 32 - mismatch!\t0x%08lx\n",
                               (unsigned long) out);
                        test_suite_update(ctx, 0, 1);
                } else {
                        test_suite_update(ctx, 1, 0);
#ifdef DEBUG
                        printf("\tHEC 32 - Pass\n");
#endif
                }
        }
}

static void
test_64_bit(IMB_MGR *mgr, struct test_suite_context *ctx)
{
        static const uint64_t pf51_hec13[] = {
                0x550a4e4f502d4758, 0x48172c696e614b20, 0x8b0c696b616f7269,
                0x7415702073617720, 0x47025320656f4a20, 0x220a69616b754d20,
                0x8e12656375646f72, 0x231a202c6874696d, 0x731a65766144202c,
                0x181a6e6168742064, 0x6e0a726168636952, 0x790f2c646f6f4820,
                0x0517206f7420736b, 0x6e17646f6f472064, 0xf2044c2069655720,
                0x15094320616e6e41, 0x000f44202c6e6f73, 0xe9056e61202c6e69,
                0x9f156146202c6975, 0x80174b2073696e65, 0x471c6320666f2064,
                0x7203206563697262, 0x441f736d69746f68, 0x05042c657372756f,
                0x3d03616772756f42, 0x5f157559202c796b, 0x01066b6e61724620,
                0x6017754a202c7472, 0xe805207569716e61, 0x97186e6566664520,
                0xa808696863692d6e, 0xd21748202c6f754c, 0x8604726567726562
        };
        unsigned i;

        for (i = 0; i < DIM(pf51_hec13); i++) {
                const uint64_t in = pf51_hec13[i] & (~0xfff1000000000000ULL);
                const uint64_t expected_out = pf51_hec13[i];
                uint64_t out = 0;
                const uint8_t *in_p = (const uint8_t *) &in;
#ifdef DEBUG
                printf("[64-bit %d] PF | HEC:\t0x%016llx", i + 1,
                       (unsigned long long)expected_out);
#endif
                out = IMB_HEC_64(mgr, in_p);

                if (out != expected_out) {
                        printf("\tHEC 64 - mismatch!\t0x%016llx\n",
                               (unsigned long long) out);
                        test_suite_update(ctx, 0, 1);
                } else {
                        test_suite_update(ctx, 1, 0);
#ifdef DEBUG
                        printf("\tHEC 64 - Pass\n");
#endif
                }
        }
}

int
hec_test(struct IMB_MGR *mb_mgr)
{
        int errors;
        struct test_suite_context ctx;

        test_suite_start(&ctx, "HEC");

        /* functional validation */
        test_32_bit(mb_mgr, &ctx);
        test_64_bit(mb_mgr, &ctx);

        errors = test_suite_end(&ctx);

        return errors;
}
