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
#ifdef __WIN32
#include <intrin.h>
#endif

#include "cpu_feature.h"

struct cpuid_regs {
        uint32_t eax;
        uint32_t ebx;
        uint32_t ecx;
        uint32_t edx;
};

static struct cpuid_regs cpuid_1_0;
static struct cpuid_regs cpuid_7_0;

/*
 * A C wrapper for CPUID opcode
 *
 * Parameters:
 *    [in] leaf    - CPUID leaf number (EAX)
 *    [in] subleaf - CPUID sub-leaf number (ECX)
 *    [out] out    - registers structure to store results of CPUID into
 */
static void
__mbcpuid(const unsigned leaf, const unsigned subleaf, struct cpuid_regs *out)
{
#ifdef _WIN32
        /* Windows */
        int regs[4];

        __cpuidex(regs, leaf, subleaf);
        out->eax = regs[0];
        out->ebx = regs[1];
        out->ecx = regs[2];
        out->edx = regs[3];
#else
        /* Linux */
        asm volatile("mov %4, %%eax\n\t"
                     "mov %5, %%ecx\n\t"
                     "cpuid\n\t"
                     "mov %%eax, %0\n\t"
                     "mov %%ebx, %1\n\t"
                     "mov %%ecx, %2\n\t"
                     "mov %%edx, %3\n\t"
                     : "=g" (out->eax), "=g" (out->ebx), "=g" (out->ecx),
                       "=g" (out->edx)
                     : "g" (leaf), "g" (subleaf)
                     : "%eax", "%ebx", "%ecx", "%edx");
#endif /* Linux */
}

static uint32_t detect_shani(void)
{
        /* Check presence of SHANI - bit 29 of EBX */
        return (cpuid_7_0.ebx & (1 << 29));
}

static uint32_t detect_aesni(void)
{
        /* Check presence of AESNI - bit 25 of ECX */
        return (cpuid_1_0.ecx & (1 << 25));
}

static uint32_t detect_pclmulqdq(void)
{
        /* Check presence of PCLMULQDQ - bit 1 of ECX */
        return (cpuid_1_0.ecx & (1 << 1));
}

static uint32_t detect_cmov(void)
{
        /* Check presence of CMOV - bit 15 of EDX */
        return (cpuid_1_0.edx & (1 << 15));
}

static uint32_t detect_sse42(void)
{
        /* Check presence of SSE4.2 - bit 20 of ECX */
        return (cpuid_1_0.ecx & (1 << 20));
}

static uint32_t detect_avx(void)
{
        /* Check presence of AVX - bit 28 of ECX */
        return (cpuid_1_0.ecx & (1 << 28));
}

static uint32_t detect_avx2(void)
{
        /* Check presence of AVX2 - bit 5 of EBX */
        return (cpuid_7_0.ebx & (1 << 5));
}

static uint32_t detect_avx512f(void)
{
        /* Check presence of AVX512F - bit 16 of EBX */
        return (cpuid_7_0.ebx & (1 << 16));
}

static uint32_t detect_avx512dq(void)
{
        /* Check presence of AVX512DQ - bit 17 of EBX */
        return (cpuid_7_0.ebx & (1 << 17));
}

static uint32_t detect_avx512cd(void)
{
        /* Check presence of AVX512CD - bit 28 of EBX */
        return (cpuid_7_0.ebx & (1 << 28));
}

static uint32_t detect_avx512bw(void)
{
        /* Check presence of AVX512BW - bit 30 of EBX */
        return (cpuid_7_0.ebx & (1 << 30));
}

static uint32_t detect_avx512vl(void)
{
        /* Check presence of AVX512VL - bit 31 of EBX */
        return (cpuid_7_0.ebx & (1 << 31));
}

static uint32_t detect_vaes(void)
{
        /* Check presence of VAES - bit 9 of ECX */
        return (cpuid_7_0.ecx & (1 << 9));
}

static uint32_t detect_vpclmulqdq(void)
{
        /* Check presence of VAES - bit 10 of ECX */
        return (cpuid_7_0.ecx & (1 << 10));
}

static uint32_t detect_gfni(void)
{
        /* Check presence of GFNI - bit 8 of ECX */
        return (cpuid_7_0.ecx & (1 << 8));
}

static uint32_t detect_avx512_ifma(void)
{
        /* Check presence of AVX512-IFMA - bit 21 of EBX */
        return (cpuid_7_0.ebx & (1 << 21));
}

static uint32_t detect_bmi2(void)
{
        /* Check presence of BMI2 - bit 8 of EBX */
        return (cpuid_7_0.ebx & (1 << 8));
}

uint64_t cpu_feature_detect(void)
{
        static const struct {
                unsigned req_leaf_number;
                uint64_t feat;
                uint32_t (*detect_fn)(void);
        } feat_tab[] = {
                { 7, IMB_FEATURE_SHANI, detect_shani },
                { 1, IMB_FEATURE_AESNI, detect_aesni },
                { 1, IMB_FEATURE_PCLMULQDQ, detect_pclmulqdq },
                { 1, IMB_FEATURE_CMOV, detect_cmov },
                { 1, IMB_FEATURE_SSE4_2, detect_sse42 },
                { 1, IMB_FEATURE_AVX, detect_avx },
                { 7, IMB_FEATURE_AVX2, detect_avx2 },
                { 7, IMB_FEATURE_AVX512F, detect_avx512f },
                { 7, IMB_FEATURE_AVX512DQ, detect_avx512dq },
                { 7, IMB_FEATURE_AVX512CD, detect_avx512cd },
                { 7, IMB_FEATURE_AVX512BW, detect_avx512bw },
                { 7, IMB_FEATURE_AVX512VL, detect_avx512vl },
                { 7, IMB_FEATURE_VAES, detect_vaes },
                { 7, IMB_FEATURE_VPCLMULQDQ, detect_vpclmulqdq },
                { 7, IMB_FEATURE_GFNI, detect_gfni },
                { 7, IMB_FEATURE_AVX512_IFMA, detect_avx512_ifma },
                { 7, IMB_FEATURE_BMI2, detect_bmi2 },
        };
        struct cpuid_regs r;
        unsigned hi_leaf_number = 0;
        uint64_t features = 0;
        unsigned i;

        /* Get highest supported CPUID leaf number */
        __mbcpuid(0x0, 0x0, &r);
        hi_leaf_number = r.eax;

        /* Get the most common CPUID leafs to speed up the detection */
        if (hi_leaf_number >= 1)
                __mbcpuid(0x1, 0x0, &cpuid_1_0);

        if (hi_leaf_number >= 7)
                __mbcpuid(0x7, 0x0, &cpuid_7_0);

        for (i = 0; i < IMB_DIM(feat_tab); i++) {
                if (hi_leaf_number < feat_tab[i].req_leaf_number)
                        continue;

                if (feat_tab[i].detect_fn() != 0)
                        features |= feat_tab[i].feat;
        }

#ifdef SAFE_DATA
        features |= IMB_FEATURE_SAFE_DATA;
#endif
#ifdef SAFE_PARAM
        features |= IMB_FEATURE_SAFE_PARAM;
#endif

        return features;
}

uint64_t cpu_feature_adjust(const uint64_t flags, uint64_t features)
{
        if (flags & IMB_FLAG_SHANI_OFF)
                features &= ~IMB_FEATURE_SHANI;

        if (flags & IMB_FLAG_AESNI_OFF)
                features &= ~IMB_FEATURE_AESNI;

        return features;
}
