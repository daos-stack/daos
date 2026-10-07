/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_WAL_H
#define DAOS_DDB_WAL_H

struct bio_meta_context;
struct ddb_ctx;

/**
 * Open the meta and WAL blobs of a MD-on-SSD VOS target without opening the VOS pool, so that the
 * WAL is not replayed. Only one WAL session can be open at a time and it is exclusive with an open
 * pool.
 *
 * The blobs may belong to a damaged pool: the meta and WAL headers are verified before the session
 * is opened and an inconsistent header is reported as an error, never asserted on.
 *
 * @param path		VOS target path in the format "[/dir/]<pool-uuid>/(vos-N|rdb-pool)". The
 *			target file itself does not need to exist.
 * @param db_path	Path to the VOS metadata DB directory (SMD/NVMe). If NULL or empty, the DB
 *			directory is derived from the leading path component of path.
 * @param ctx		DDB context.
 * @param mc		BIO meta context of the opened session, set on success.
 *
 * @retval 0			Success.
 * @retval -DER_INVAL		path or db_path is not a valid VOS target path.
 * @retval -DER_NO_SERVICE	SPDK cannot be re-initialized in this process (already reported
 *				through ctx).
 * @retval -DER_NOTSUPPORTED	The DB path is not configured in MD-on-SSD mode: no WAL to open.
 * @retval -DER_NONEXIST	The system DB has no meta/WAL blob for the pool/target.
 * @retval -DER_UNINIT		The meta or WAL header has a bad magic.
 * @retval -DER_DF_INCOMPT	The meta or WAL header has an unsupported version.
 * @retval -DER_CSUM		The meta or WAL header has a bad checksum.
 * @retval -DER_DF_INVAL	The meta or WAL header has a valid checksum but an inconsistent
 *				content (details in the DAOS log).
 * @retval -DER_NOMEM		Out of memory.
 * @retval <0			Other VOS or SPDK error.
 */
int
dw_open(const char *path, const char *db_path, struct ddb_ctx *ctx, struct bio_meta_context **mc);

/**
 * Close a WAL session opened by dw_open() and release the standalone VOS instance.
 *
 * @param mc		BIO meta context returned by dw_open().
 *
 * @return		0 on success, negative DER error code otherwise.
 */
int
dw_close(struct bio_meta_context *mc);

#endif /* DAOS_DDB_WAL_H */
