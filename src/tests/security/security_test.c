/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <daos_types.h>
#include <daos_errno.h>
#include <daos_security.h>
#include <daos/security.h>
#include <daos/common.h>
#include "../../security/srv_internal.h"
#include "../../security/auth.pb-c.h"

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
compare_auth_sys(Auth__Sys *auth1, Auth__Sys *auth2)
{
	if (!auth1 || !auth2) {
		printf("compare_auth_sys needs two valid pointers\n");
		return -1;
	}

	if (auth1->user == NULL || auth2->user == NULL) {
		printf("An AuthSys is missing a user\n");
		return -1;
	}

	if (strncmp(auth1->user, auth2->user, DAOS_ACL_MAX_PRINCIPAL_LEN) !=
	    0) {
		printf("Tokens do not have a matching user\n");
		return -1;
	}

	if (auth1->group == NULL || auth2->group == NULL) {
		printf("An AuthSys is missing a group\n");
		return -1;
	}

	if (strncmp(auth1->group, auth2->group, DAOS_ACL_MAX_PRINCIPAL_LEN) !=
	    0) {
		printf("Tokens do not have a matching group\n");
		return -1;
	}

	/* Check to make sure that both or neither are set */
	if (check_valid_pointers(auth1->groups, auth2->groups)) {
		printf("An AuthSys is missing a group list\n");
		return  -1;
	}

	if (auth1->n_groups != auth2->n_groups) {
		printf("Group lists are not of equal length\n");
		return -1;
	}

	for (int i = 0; i < auth1->n_groups; i++) {
		if (strncmp(auth1->groups[i], auth2->groups[i],
			    DAOS_ACL_MAX_PRINCIPAL_LEN) != 0) {
			printf("Group lists do not match\n");
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

	if (auth1->stamp != auth2->stamp) {
		printf("Tokens do not have a matching stamps\n");
		return -1;
	}

	return 0;
}
void
print_auth_sys(Auth__Sys *auth)
{
	if (!auth)
		return;

	printf("AuthSys Token:\n");
	printf("user: %s\n", auth->user);
	printf("group: %s\n", auth->group);
	if (auth->groups) {
		int i;

		printf("groups: ");
		for (i = 0; i < auth->n_groups; i++) {
			printf("%s ", auth->groups[i]);
		}
		printf("\n");
	}
	if (auth->secctx)
		printf("secctx: %s\n", auth->secctx);
	if (auth->machinename)
		printf("machinename: %s\n", auth->machinename);
	printf("stamp: %" PRIu64 "\n", auth->stamp);
}

void
print_auth_verifier(Auth__Token *verifier)
{
	if (!verifier)
		return;

	printf("Authsys Verifier:\n");
	printf("Flavor: %d\n", verifier->flavor);
	if (verifier->data.data != NULL) {
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
	d_iov_t		creds;
	Auth__Credential	*response = NULL;
	Auth__Sys		*credentials = NULL;
	Auth__Token		*validated_token = NULL;
	Auth__Sys		*validated_credentials = NULL;

	memset(&creds, 0, sizeof(d_iov_t));

	ret = dc_sec_request_creds(&creds);

	if (ret != DER_SUCCESS) {
		printf("Failed to obtain credentials with ret: %d\n", ret);
		exit(ret);
	}

	response = auth__credential__unpack(NULL,
					    creds.iov_len,
					    creds.iov_buf);

	credentials = auth__sys__unpack(NULL,
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

	validated_credentials = auth__sys__unpack(NULL,
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

	auth__credential__free_unpacked(response, NULL);
	auth__sys__free_unpacked(credentials, NULL);
	auth__sys__free_unpacked(validated_credentials, NULL);
	auth__token__free_unpacked(validated_token, NULL);
	daos_iov_free(&creds);

	return 0;
}
