/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(mgmt)

#include <sys/stat.h>
#include <fcntl.h>
#include <uuid/uuid.h>

#include <daos_types.h>
#include <daos/common.h>

int
mgmt_file_preallocate(const char *path, uuid_t uuid, daos_size_t scm_size);

int
mgmt_file_preallocate(const char *path, uuid_t uuid, daos_size_t scm_size)
{
	int fd;
	int rc;

	fd = open(path, O_CREAT | O_RDWR, 0600);
	if (fd < 0) {
		rc = daos_errno2der(errno);
		D_ERROR(DF_UUID ": failed to create vos file %s: " DF_RC "\n", DP_UUID(uuid), path,
			DP_RC(rc));
		goto out;
	}

	/** Align to 4K or locking the region based on the size will fail */
	scm_size = D_ALIGNUP(scm_size, 1ULL << 12);
	/**
	 * Pre-allocate blocks for vos files in order to provide
	 * consistent performance and avoid entering into the backend
	 * filesystem allocator through page faults.
	 * Use fallocate(2) instead of posix_fallocate(3) since the
	 * latter is bogus with tmpfs.
	 */
	rc = fallocate(fd, 0, 0, scm_size);
	if (rc) {
		rc = daos_errno2der(errno);
		D_ERROR(DF_UUID ": failed to allocate vos file %s with "
				"size: " DF_U64 ": " DF_RC "\n",
			DP_UUID(uuid), path, scm_size, DP_RC(rc));
		goto out;
	}

	rc = fsync(fd);
	(void)close(fd);
	fd = -1;
	if (rc) {
		rc = daos_errno2der(errno);
		D_ERROR(DF_UUID ": failed to sync vos pool %s: " DF_RC "\n", DP_UUID(uuid), path,
			DP_RC(rc));
		goto out;
	}
out:
	if (fd >= 0) {
		close(fd);
	}

	return DER_SUCCESS;
}
