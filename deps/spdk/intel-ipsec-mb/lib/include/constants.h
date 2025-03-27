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

#ifndef IMB_CONSTANTS_H_
#define IMB_CONSTANTS_H_

/* define SHA1 constants */
#define H0 0x67452301
#define H1 0xefcdab89
#define H2 0x98badcfe
#define H3 0x10325476
#define H4 0xc3d2e1f0
#define SHA1_PAD_SIZE 8

/* define SHA256 constants */
#define SHA256_H0 0x6a09e667
#define SHA256_H1 0xbb67ae85
#define SHA256_H2 0x3c6ef372
#define SHA256_H3 0xa54ff53a
#define SHA256_H4 0x510e527f
#define SHA256_H5 0x9b05688c
#define SHA256_H6 0x1f83d9ab
#define SHA256_H7 0x5be0cd19
#define SHA256_PAD_SIZE 8

/* define SHA224 constants */
#define SHA224_H0 0xc1059ed8
#define SHA224_H1 0x367cd507
#define SHA224_H2 0x3070dd17
#define SHA224_H3 0xf70e5939
#define SHA224_H4 0xffc00b31
#define SHA224_H5 0x68581511
#define SHA224_H6 0x64f98fa7
#define SHA224_H7 0xbefa4fa4
#define SHA224_PAD_SIZE 8

/* define SHA512 constants */
#define SHA512_H0 0x6a09e667f3bcc908
#define SHA512_H1 0xbb67ae8584caa73b
#define SHA512_H2 0x3c6ef372fe94f82b
#define SHA512_H3 0xa54ff53a5f1d36f1
#define SHA512_H4 0x510e527fade682d1
#define SHA512_H5 0x9b05688c2b3e6c1f
#define SHA512_H6 0x1f83d9abfb41bd6b
#define SHA512_H7 0x5be0cd19137e2179
#define SHA512_PAD_SIZE 16

/* define SHA384 constants */
#define SHA384_H0 0xcbbb9d5dc1059ed8
#define SHA384_H1 0x629a292a367cd507
#define SHA384_H2 0x9159015a3070dd17
#define SHA384_H3 0x152fecd8f70e5939
#define SHA384_H4 0x67332667ffc00b31
#define SHA384_H5 0x8eb44a8768581511
#define SHA384_H6 0xdb0c2e0d64f98fa7
#define SHA384_H7 0x47b5481dbefa4fa4
#define SHA384_PAD_SIZE 16

#endif /* IMB_CONSTANTS_H_ */
