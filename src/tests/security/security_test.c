/*
 * (C) Copyright 2018 Intel Corporation.
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <daos/security.h>
#include <daos_types.h>
#include <daos_errno.h>
#include <daos/common.h>
#include "../../security/security.pb-c.h"

void
print_auth_sys(AuthSys *auth)
{
	if (!auth)
		return;

	printf("AuthSys Token:\n");
	if (auth->has_uid)
		printf("uid: %u\n", auth->uid);
	if (auth->has_gid)
		printf("gid: %u\n", auth->gid);
	if (auth->gids) {
		int i;

		printf("gids: ");
		for (i = 0; i < auth->n_gids; i++) {
			printf("%u ", auth->gids[i]);
		}
		printf("\n");
	}
	if (auth->secctx)
		printf("secctx: %s\n", auth->secctx);
	if (auth->machinename)
		printf("machinename: %s\n", auth->machinename);
	if (auth->has_stamp)
		printf("stamp: %" PRIu64 "\n", auth->stamp);
}

void
print_auth_verifier(AuthToken *verifier)
{
	if (!verifier)
		return;

	printf("Authsys Verifier:\n");
	printf("Flavor: %d\n", verifier->flavor);
	if (verifier->has_data) {
		int i;

		printf("Verifier: ");
		for (i = 0; i < verifier->data.len; i++) {
			printf("%02hhX", verifier->data.data[i]);
		}
		printf("\n");
	}
}

int
main(int argc, char **argv)
{
	int			ret;
	daos_iov_t		creds;
	SecurityCredential	*response = NULL;
	AuthSys			*credentials = NULL;

	memset(&creds, 0, sizeof(daos_iov_t));

	ret = dc_sec_request_creds(&creds);

	if (ret != DER_SUCCESS) {
		printf("We failed with ret: %d\n", ret);
		exit(ret);
	}

	response = security_credential__unpack(NULL,
						creds.iov_len,
						creds.iov_buf);

	credentials = auth_sys__unpack(NULL,
					response->token->data.len,
					response->token->data.data);

	print_auth_sys(credentials);
	print_auth_verifier(response->verifier);

	daos_iov_free(&creds);

	return 0;
}
