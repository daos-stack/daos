/**********************************************************************
  Copyright(c) 2011-2016 Intel Corporation All rights reserved.

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
#include <stdlib.h>		// for rand
#include <string.h>		// for memcmp
#include <aes_xts.h>
#include <aes_keyexp.h>

#define TEST_LEN  (1024*1024)
#define TEST_SIZE (4096)
#ifndef RANDOMS
# define RANDOMS  10
#endif

void mk_rand_data(unsigned char *k1, unsigned char *k2, unsigned char *k3, unsigned char *p,
		  int n)
{
	int i;
	for (i = 0; i < 16; i++) {
		*k1++ = rand();
		*k2++ = rand();
		*k3++ = rand();
	}
	for (i = 0; i < n; i++)
		*p++ = rand();

}

int main(void)
{
	int t, n;

	unsigned char key1[16], key2[16], tinit[16];
	unsigned char *pt, *ct, *dt;

	int align, size, min_size;
	unsigned char *efence_pt;
	unsigned char *efence_ct;
	unsigned char *efence_dt;

	unsigned char *origin_pt;
	unsigned char *origin_ct;
	unsigned char *origin_dt;

	unsigned char key1_exp_enc[16 * 11], key1_exp_dec[16 * 11];
	unsigned char key2_exp_tw[16 * 11];
	int i;

	printf("aes_xts_128 enc/dec rand test, %d sets of %d max: ", RANDOMS, TEST_LEN);
	pt = malloc(TEST_LEN);
	ct = malloc(TEST_LEN);
	dt = malloc(TEST_LEN);

	if (NULL == pt || NULL == ct || NULL == dt) {
		printf("malloc of testsize failed\n");
		return -1;
	}

	mk_rand_data(key1, key2, tinit, pt, TEST_LEN);
	XTS_AES_128_enc(key2, key1, tinit, TEST_LEN, pt, ct);
	XTS_AES_128_dec(key2, key1, tinit, TEST_LEN, ct, dt);

	if (memcmp(pt, dt, TEST_LEN)) {
		printf("fail\n");
		return -1;
	}
	putchar('.');

	// Do tests with random data, keys and message size
	for (t = 0; t < RANDOMS; t++) {
		n = rand() % (TEST_LEN);
		if (n < 17)
			continue;

		mk_rand_data(key1, key2, tinit, pt, n);
		XTS_AES_128_enc(key2, key1, tinit, n, pt, ct);
		XTS_AES_128_dec(key2, key1, tinit, n, ct, dt);

		if (memcmp(pt, dt, n)) {
			printf("fail rand %d, size %d\n", t, n);
			return -1;
		}
		putchar('.');
		fflush(0);
	}

	// Run tests at end of buffer for Electric Fence
	align = 1;
	min_size = 16;
	for (size = 0; size <= TEST_SIZE - min_size; size += align) {

		// Line up TEST_SIZE from end
		efence_pt = pt + TEST_LEN - TEST_SIZE + size;
		efence_ct = ct + TEST_LEN - TEST_SIZE + size;
		efence_dt = dt + TEST_LEN - TEST_SIZE + size;

		mk_rand_data(key1, key2, tinit, efence_pt, TEST_SIZE - size);
		XTS_AES_128_enc(key2, key1, tinit, TEST_SIZE - size, efence_pt, efence_ct);
		XTS_AES_128_dec(key2, key1, tinit, TEST_SIZE - size, efence_ct, efence_dt);

		if (memcmp(efence_pt, efence_dt, TEST_SIZE - size)) {
			printf("efence: fail size %d\n", TEST_SIZE - size);
			return -1;
		}
		putchar('.');
		fflush(0);
	}

	origin_pt = malloc(TEST_LEN);
	origin_ct = malloc(TEST_LEN);
	origin_dt = malloc(TEST_LEN);
	if (NULL == origin_pt || NULL == origin_ct || NULL == origin_dt) {
		printf("malloc of testsize failed\n");
		return -1;
	}
	// For data lengths from 0 to 15 bytes, the functions return without any error
	// codes, without reading or writing any data.
	for (size = TEST_SIZE - min_size + align; size <= TEST_SIZE; size += align) {

		// Line up TEST_SIZE from end
		efence_pt = pt + TEST_LEN - TEST_SIZE + size;
		efence_ct = ct + TEST_LEN - TEST_SIZE + size;
		efence_dt = dt + TEST_LEN - TEST_SIZE + size;

		mk_rand_data(key1, key2, tinit, efence_pt, TEST_SIZE - size);
		memcpy(efence_ct, efence_pt, TEST_SIZE - size);
		memcpy(efence_dt, efence_pt, TEST_SIZE - size);
		memcpy(origin_pt, efence_pt, TEST_SIZE - size);
		memcpy(origin_ct, efence_ct, TEST_SIZE - size);
		memcpy(origin_dt, efence_dt, TEST_SIZE - size);

		XTS_AES_128_enc(key2, key1, tinit, TEST_SIZE - size, efence_pt, efence_ct);
		XTS_AES_128_dec(key2, key1, tinit, TEST_SIZE - size, efence_ct, efence_dt);

		if (memcmp(efence_pt, origin_pt, TEST_SIZE - size)) {
			printf("efence_pt: fail size %d\n", TEST_SIZE - size);
			return -1;
		}
		if (memcmp(efence_ct, origin_ct, TEST_SIZE - size)) {
			printf("efence_ct: fail size %d\n", TEST_SIZE - size);
			return -1;
		}
		if (memcmp(efence_dt, origin_dt, TEST_SIZE - size)) {
			printf("efence_dt: fail size %d\n", TEST_SIZE - size);
			return -1;
		}
		putchar('.');
		fflush(0);
	}

	for (i = 0; i < 16 * 11; i++) {
		key2_exp_tw[i] = rand();
	}

	for (size = 0; size <= TEST_SIZE - min_size; size += align) {

		// Line up TEST_SIZE from end
		efence_pt = pt + TEST_LEN - TEST_SIZE + size;
		efence_ct = ct + TEST_LEN - TEST_SIZE + size;
		efence_dt = dt + TEST_LEN - TEST_SIZE + size;

		mk_rand_data(key1, key2, tinit, efence_pt, TEST_SIZE - size);
		aes_keyexp_128(key1, key1_exp_enc, key1_exp_dec);

		XTS_AES_128_enc_expanded_key(key2_exp_tw, key1_exp_enc, tinit,
					     TEST_SIZE - size, efence_pt, efence_ct);
		XTS_AES_128_dec_expanded_key(key2_exp_tw, key1_exp_dec, tinit,
					     TEST_SIZE - size, efence_ct, efence_dt);

		if (memcmp(efence_pt, efence_dt, TEST_SIZE - size)) {
			printf("efence_expanded_key: fail size %d\n", TEST_SIZE - size);
			return -1;
		}
		putchar('.');
		fflush(0);
	}

	// For data lengths from 0 to 15 bytes, the functions return without any error
	// codes, without reading or writing any data.
	for (size = TEST_SIZE - min_size + align; size <= TEST_SIZE; size += align) {

		// Line up TEST_SIZE from end
		efence_pt = pt + TEST_LEN - TEST_SIZE + size;
		efence_ct = ct + TEST_LEN - TEST_SIZE + size;
		efence_dt = dt + TEST_LEN - TEST_SIZE + size;

		mk_rand_data(key1, key2, tinit, efence_pt, TEST_SIZE - size);
		memcpy(efence_ct, efence_pt, TEST_SIZE - size);
		memcpy(efence_dt, efence_pt, TEST_SIZE - size);
		memcpy(origin_pt, efence_pt, TEST_SIZE - size);
		memcpy(origin_ct, efence_ct, TEST_SIZE - size);
		memcpy(origin_dt, efence_dt, TEST_SIZE - size);

		aes_keyexp_128(key1, key1_exp_enc, key1_exp_dec);

		XTS_AES_128_enc_expanded_key(key2_exp_tw, key1_exp_enc, tinit,
					     TEST_SIZE - size, efence_pt, efence_ct);
		XTS_AES_128_dec_expanded_key(key2_exp_tw, key1_exp_dec, tinit,
					     TEST_SIZE - size, efence_ct, efence_dt);

		if (memcmp(efence_pt, origin_pt, TEST_SIZE - size)) {
			printf("efence_expanded_key for pt: fail size %d\n", TEST_SIZE - size);
			return -1;
		}
		if (memcmp(efence_ct, origin_ct, TEST_SIZE - size)) {
			printf("efence_expanded_key for ct: fail size %d\n", TEST_SIZE - size);
			return -1;
		}
		if (memcmp(efence_dt, origin_dt, TEST_SIZE - size)) {
			printf("efence_expanded_key for dt: fail size %d\n", TEST_SIZE - size);
			return -1;
		}
		putchar('.');
		fflush(0);
	}

	printf("Pass\n");

	return 0;
}
