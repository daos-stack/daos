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
#include <string.h>
#include <daos/security.h>
#include <daos_types.h>
#include <daos_errno.h>
#include <daos/common.h>
#include "../../security/srv_internal.h"
#include "../../security/security.pb-c.h"

/**
 * This is a bit of a hack to deal with the fact that the security module
 * initializes this variable normally and without the module code we need
 * to set it ourselves
 */
char *ds_sec_server_socket_path = "/var/run/daos_server/daos_server.sock";

static int
check_valid_pointers(void *p1, void *p2)
{
	if (p1 == NULL && p2 != NULL) {
		return -1;
	}
	if (p1 != NULL && p2 == NULL) {
		return -1;
	}
	return 0;
}

int
compare_auth_sys(AuthSys *auth1, AuthSys *auth2)
{
	if (!auth1 || !auth2) {
		printf("compare_auth_sys needs two valid pointers\n");
		return -1;
	}

	if (auth1->has_uid != auth2->has_uid) {
		printf("An AuthSys is missing a uid\n");
		return -1;
	}
	if (auth1->has_uid && (auth1->uid != auth2->uid)) {
		printf("Tokens do not have a matching uid\n");
		return -1;
	}

	if (auth1->has_gid != auth2->has_gid) {
		printf("An AuthSys is missing a gid\n");
		return -1;
	}
	if (auth1->has_gid && (auth1->gid != auth2->gid)) {
		printf("Tokens do not have a matching gid\n");
		return -1;
	}

	/* Check to make sure that both or neither are set */
	if (check_valid_pointers(auth1->gids, auth2->gids)) {
		printf("An AuthSys is missing a gids list\n");
		return  -1;
	}

	if (auth1->n_gids != auth2->n_gids) {
		printf("Gids lists are not of equal length\n");
		return -1;
	}

	for (int i = 0; i < auth1->n_gids; i++) {
		if (auth1->gids[i] != auth2->gids[i]) {
			printf("Gid lists do not match\n");
			return -1;
		}
	}

	if (check_valid_pointers(auth1->secctx, auth2->secctx)) {
		printf("An AuthSys is missing a secctx\n");
		return -1;
	}

	if (auth1->secctx) {
		if (strcmp(auth1->secctx, auth2->secctx)) {
			printf("Secctx entries do not match\n");
			return -1;
		}
	}

	if (check_valid_pointers(auth1->machinename, auth2->machinename)) {
		printf("An AuthSys is missing a machinename\n");
		return -1;
	}

	if (auth1->machinename) {
		if (strcmp(auth1->machinename, auth2->machinename)) {
			printf("machinename entries do not match\n");
			return -1;
		}
	}

	if (auth1->has_stamp != auth2->has_stamp) {
		printf("An AuthSys is missing a stamp\n");
		return -1;
	}
	if (auth1->has_stamp && (auth1->stamp != auth2->stamp)) {
		printf("Tokens do not have a matching stamps\n");
		return -1;
	}

	return 0;
}
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
	AuthToken		*validated_token = NULL;
	AuthSys			*validated_credentials = NULL;

	memset(&creds, 0, sizeof(daos_iov_t));

	ret = dc_sec_request_creds(&creds);

	if (ret != DER_SUCCESS) {
		printf("Failed to obtain credentials with ret: %d\n", ret);
		exit(ret);
	}

	response = security_credential__unpack(NULL,
						creds.iov_len,
						creds.iov_buf);

	credentials = auth_sys__unpack(NULL,
					response->token->data.len,
					response->token->data.data);

	printf("Credentials as obtained from Agent:\n");
	print_auth_sys(credentials);
	print_auth_verifier(response->verifier);

	ret = ds_sec_validate_credentials(&creds, &validated_token);
	if (ret != DER_SUCCESS) {
		printf("Failed to validate credential with ret: %d\n", ret);
		exit(1);
	}

	validated_credentials = auth_sys__unpack(NULL,
					validated_token->data.len,
					validated_token->data.data);

	printf("AuthToken as obtained from Server:\n");
	print_auth_sys(validated_credentials);

	printf("Comparing tokens:\n");
	if (compare_auth_sys(credentials, validated_credentials) != 0) {
		printf("The credentials do not match.\n");
		exit(1);
	} else {
		printf("The credentials match.\n");
	}

	security_credential__free_unpacked(response, NULL);
	auth_sys__free_unpacked(credentials, NULL);
	auth_sys__free_unpacked(validated_credentials, NULL);
	auth_token__free_unpacked(validated_token, NULL);
	daos_iov_free(&creds);

	return 0;
}
