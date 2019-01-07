/**
 * (C) Copyright 2015-2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#define D_LOGFAC	DD_FAC(tests)

#include <string.h>
#include <errno.h>

#include <daos/checksum.h>

int test_checksum_simple(char *cs_name, daos_csum_t *csum,
			 daos_csum_buf_t *csum_buf)
{
	int		rc = 0;
	daos_iov_t	test_iov;
	daos_sg_list_t	sgl;
	char		test_buf[20];

	rc = daos_csum_init(cs_name, csum);
	if (rc != 0) {
		D_PRINT("Error ins initializing checksum\n");
		goto exit;
	}

	strncpy(test_buf, "Test this checksum\n", 20);

	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	daos_iov_set(&test_iov, test_buf, strlen(cs_name));
	sgl.sg_iovs = &test_iov;

	rc = daos_csum_compute(csum, &sgl);
	if (rc != 0)
		D_PRINT("Error in computing checksum\n");

	csum_buf->cs_len = csum_buf->cs_buf_len = daos_csum_get_size(csum);
	D_ALLOC(csum_buf->cs_csum, csum_buf->cs_buf_len);

	daos_csum_get(csum, csum_buf);
	D_PRINT("Checksum for string \"%s\" using  %s is "DF_X64"\n",
		test_buf, cs_name, *(uint64_t *)csum_buf->cs_csum);

	D_FREE(csum_buf->cs_csum);
exit:
	return rc;
}


int main(int argc, char *argv[])
{

	int		rc;
	daos_csum_t	csum_local, csum_cmp_local;
	daos_csum_t	*csum = &csum_local;
	daos_csum_t	*csum_cmp = &csum_cmp_local;
	daos_csum_buf_t	csum_buf;
	int		test_fail = 0;

	rc = test_checksum_simple("crc64", csum, &csum_buf);
	if (rc != 0) {
		D_ERROR("FAIL in test for CRC64 checksum: %d\n", rc);
		return rc;
	}

	rc = test_checksum_simple("crc64", csum_cmp, &csum_buf);
	if (rc != 0) {
		D_ERROR("FAIL in test for CRC64 checksum: %d\n", rc);
		test_fail++;
	}

	rc = daos_csum_compare(csum, csum_cmp);
	if (!rc) {
		D_ERROR("daos_csum_compare - FAIL!\n");
		test_fail++;
	}

	daos_csum_free(csum);
	daos_csum_free(csum_cmp);

	rc = daos_csum_reset(csum);
	if (rc != 0) {
		D_PRINT("Error in reset: %d\n", rc);
		test_fail++;
	}

	rc = test_checksum_simple("crc32", csum, &csum_buf);
	if (rc != 0) {
		D_ERROR("Error in generating crc32 checksum\n");
		test_fail++;
	}
	if (test_fail)
		D_PRINT("%d tests failed\n", test_fail);
	else
		D_PRINT("All tests pass\n");

	return rc;
}
