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

#include <stdlib.h>
#include <stdio.h>
#include "xts_128_vect.h"

int main(void)
{

	// Temporary array for the calculated vectors
	uint8_t *ct_test;
	uint8_t *pt_test;

	int i, j;

	// --- Encryption test ---

	// Loop over the vectors
	for (i = 0; i < NVEC; i++) {

		// Allocate space for the calculated ciphertext
		ct_test = malloc(vlist[i].ptlen);
		if (ct_test == NULL) {
			fprintf(stderr, "Can't allocate ciphertext memory\n");
			return -1;
		}

		XTS_AES_128_enc(vlist[i].key2, vlist[i].key1, vlist[i].TW,
				vlist[i].ptlen, vlist[i].PTX, ct_test);

		// Carry out comparison of the calculated ciphertext with
		// the reference
		for (j = 0; j < vlist[i].ptlen; j++) {

			if (ct_test[j] != vlist[i].CTX[j]) {
				// Vectors 1-10 and 15-19 are for the 128 bit code
				printf("\nXTS_AES_128_enc: Vector %d: ",
				       i < 9 ? i + 1 : i + 6);

				printf("failed at byte %d! \n", j);
				return -1;
			}
		}
		printf(".");
	}

	// --- Decryption test ---

	// Loop over the vectors
	for (i = 0; i < NVEC; i++) {

		// Allocate space for the calculated ciphertext
		pt_test = malloc(vlist[i].ptlen);
		if (pt_test == NULL) {
			fprintf(stderr, "Can't allocate plaintext memory\n");
			return -1;
		}

		XTS_AES_128_dec(vlist[i].key2, vlist[i].key1, vlist[i].TW,
				vlist[i].ptlen, vlist[i].CTX, pt_test);

		for (j = 0; j < vlist[i].ptlen; j++) {

			if (pt_test[j] != vlist[i].PTX[j]) {
				// Carry out comparison of the calculated ciphertext with
				// the reference
				printf("\nXTS_AES_128_enc: Vector %d: ",
				       i < 9 ? i + 1 : i + 6);

				printf(" failed at byte %d! \n", j);
				return -1;
			}
		}
		printf(".");
	}
	printf("Pass\n");

	return 0;
}
