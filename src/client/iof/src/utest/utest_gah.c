/* Copyright (C) 2016-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <CUnit/Basic.h>

#include <ios_gah.h>

int init_suite(void)
{
	return CUE_SUCCESS;
}

int clean_suite(void)
{
	return CUE_SUCCESS;
}

/** test the size of the GAH struct is indeed 128 bits */
static void test_ios_gah_size(void)
{
	CU_ASSERT(sizeof(struct ios_gah) * 8 == 128);
}

/** test ios_gah_init() */
static void test_ios_gah_init(void)
{
	struct ios_gah_store *ios_gah_store = NULL;

	ios_gah_store = ios_gah_init(4);
	CU_ASSERT(ios_gah_store != NULL);
	CU_ASSERT_FATAL(ios_gah_destroy(ios_gah_store) == -DER_SUCCESS);
}

/** test ios_gah_destroy() */
static void test_ios_gah_destroy(void)
{
	struct ios_gah_store *ios_gah_store = NULL;

	ios_gah_store = ios_gah_init(4);
	CU_ASSERT(ios_gah_store != NULL);
	if (ios_gah_store == NULL)
		return;
	CU_ASSERT(ios_gah_destroy(ios_gah_store) == -DER_SUCCESS);
	CU_ASSERT(ios_gah_destroy(NULL) == -DER_INVAL);
}

/** test ios_gah_allocate() */
static void test_ios_gah_allocate(void)
{
	int rc = -DER_SUCCESS;
	struct ios_gah *ios_gah;
	struct ios_gah_store *ios_gah_store;
	int ii;
	int num_handles = 1024 * 20;

	CU_ASSERT(sizeof(struct ios_gah) * 8 == 128);
	ios_gah_store = ios_gah_init(4);
	CU_ASSERT_FATAL(ios_gah_store != NULL);
	CU_ASSERT(ios_gah_store->rank == 4)
	ios_gah = (struct ios_gah *)calloc(num_handles, sizeof(struct
					      ios_gah));
	CU_ASSERT_FATAL(ios_gah != NULL);

	for (ii = 0; ii < num_handles; ii++) {
		void *data = malloc(512);
		void *info = NULL;

		CU_ASSERT_FATAL(data != NULL);
		rc |= ios_gah_allocate(ios_gah_store, ios_gah + ii, data);
		CU_ASSERT(ios_gah_get_info(ios_gah_store, ios_gah + ii, &info)
			  == -DER_SUCCESS);
		CU_ASSERT(info == data);
	}
	CU_ASSERT(rc == -DER_SUCCESS);
	rc = ios_gah_allocate(ios_gah_store, NULL, NULL);
	CU_ASSERT(rc == -DER_INVAL);

	for (ii = 0; ii < num_handles; ii++) {
		void *data = NULL;

		CU_ASSERT(ios_gah_get_info(ios_gah_store, ios_gah + ii, &data)
			  == -DER_SUCCESS);
		CU_ASSERT(data != NULL);
		free(data);
		CU_ASSERT_FATAL(ios_gah_deallocate(ios_gah_store, ios_gah + ii)
				== -DER_SUCCESS);
	}

	ios_gah_destroy(ios_gah_store);
	free(ios_gah);
}

/** test utility routines  */
static void test_ios_gah_misc(void)
{
	int rc = -DER_SUCCESS;
	struct ios_gah *ios_gah;
	struct ios_gah_store *ios_gah_store;
	int ii;
	int num_handles = 1024 * 20;
	void *internal = NULL;

	CU_ASSERT(sizeof(struct ios_gah) * 8 == 128);
	ios_gah_store = ios_gah_init(4);
	CU_ASSERT_FATAL(ios_gah_store != NULL);
	ios_gah = (struct ios_gah *)calloc(num_handles, sizeof(struct
					      ios_gah));
	CU_ASSERT_FATAL(ios_gah != NULL);

	for (ii = 0; ii < num_handles; ii++) {
		void *data = malloc(512);

		CU_ASSERT_FATAL(data != NULL);
		rc |= ios_gah_allocate(ios_gah_store, ios_gah + ii, data);
	}
	CU_ASSERT(rc == -DER_SUCCESS);

	/** test ios_gah_check_crc() */
	rc = ios_gah_check_crc(NULL);
	CU_ASSERT(rc == -DER_INVAL);
	rc = ios_gah_check_crc(ios_gah);
	CU_ASSERT(rc == -DER_SUCCESS);
	ios_gah->root++;
	rc = ios_gah_check_crc(ios_gah);
	CU_ASSERT(rc == -DER_NO_HDL);
	ios_gah->root--;

	/** test ios_gah_check_version() */
	rc = ios_gah_check_version(NULL);
	CU_ASSERT(rc == -DER_INVAL);
	rc = ios_gah_check_version(ios_gah);
	CU_ASSERT(rc == -DER_SUCCESS);
	ios_gah->version++;
	rc = ios_gah_check_version(ios_gah);
	CU_ASSERT(rc == -DER_MISMATCH);
	ios_gah->version--;

	/** test ios_gah_get_info() */
	CU_ASSERT(ios_gah_get_info(NULL, ios_gah, &internal) != -DER_SUCCESS);
	CU_ASSERT(ios_gah_get_info(ios_gah_store, NULL, &internal) !=
			-DER_SUCCESS);
	CU_ASSERT(ios_gah_get_info(ios_gah_store, ios_gah, NULL) !=
			-DER_SUCCESS);

	for (ii = 0; ii < num_handles; ii++) {
		void *data = NULL;

		CU_ASSERT(ios_gah_get_info(ios_gah_store, ios_gah + ii, &data)
			  == -DER_SUCCESS);
		CU_ASSERT(data != NULL);
		free(data);
		CU_ASSERT_FATAL(ios_gah_deallocate(ios_gah_store, ios_gah + ii)
				== -DER_SUCCESS);
	}
	CU_ASSERT(ios_gah_get_info(ios_gah_store, ios_gah, &internal) !=
			-DER_SUCCESS);

	ios_gah_destroy(ios_gah_store);
	free(ios_gah);
}

int main(int argc, char **argv)
{
	CU_pSuite pSuite = NULL;

	if (CU_initialize_registry() != CUE_SUCCESS)
		return CU_get_error();
	pSuite = CU_add_suite("GAH API test", init_suite, clean_suite);
	if (!pSuite) {
		CU_cleanup_registry();
		return CU_get_error();
	}

	if (!CU_add_test(pSuite, "sizeof(struct ios_gah) test",
			 test_ios_gah_size) ||
	    !CU_add_test(pSuite, "ios_gah_init() test", test_ios_gah_init) ||
	    !CU_add_test(pSuite, "ios_gah_allocate() test",
		    test_ios_gah_allocate) ||
	    !CU_add_test(pSuite, "ios_gah_destroy() test",
		    test_ios_gah_destroy) ||
	    !CU_add_test(pSuite, "ios_gah_misc test", test_ios_gah_misc)) {
		CU_cleanup_registry();
		return CU_get_error();
	}

	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();
	CU_cleanup_registry();

	return CU_get_error();
}
