/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/* daos_hdlr.c - resource and operation-specific handler functions
 * invoked by daos(8) utility
 */

#define D_LOGFAC	DD_FAC(client)
#define ENUM_KEY_BUF		128 /* size of each dkey/akey */
#define ENUM_LARGE_KEY_BUF	(512 * 1024) /* 512k large key */
#define ENUM_DESC_NR		5 /* number of keys/records returned by enum */
#define ENUM_DESC_BUF		512 /* all keys/records returned by enum */
#define LIBSERIALIZE		"libdaos_serialize.so"
#define NUM_SERIALIZE_PROPS	19

#include <stdio.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/xattr.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <dlfcn.h>
#include <daos.h>
#include <daos/common.h>
#include <daos/checksum.h>
#include <daos/rpc.h>
#include <daos/debug.h>
#include <daos/object.h>

#include "daos_types.h"
#include "daos_api.h"
#include "daos_fs.h"
#include "daos_uns.h"
#include "daos_prop.h"
#include "daos_fs_sys.h"

#include "daos_hdlr.h"

struct file_dfs {
	enum {POSIX, DAOS} type;
	int fd;
	daos_off_t offset;
	dfs_obj_t *obj;
	dfs_sys_t *dfs_sys;
};

/* Report an error with a system error number using a standard output format */
#define DH_PERROR_SYS(AP, RC, STR, ...)                                                            \
	do {                                                                                       \
		fprintf((AP)->errstream, STR ": %s (%d)\n", ##__VA_ARGS__, strerror(RC), (RC));    \
	} while (0)

/* Report an error with a daos error number using a standard output format */
#define DH_PERROR_DER(AP, RC, STR, ...)                                                            \
	do {                                                                                       \
		fprintf((AP)->errstream, STR ": %s (%d)\n", ##__VA_ARGS__, d_errdesc(RC), (RC));   \
	} while (0)

static int
cont_destroy_snap_hdlr(struct cmd_args_s *ap);

/* TODO: implement these pool op functions
 * int pool_stat_hdlr(struct cmd_args_s *ap);
 */

int
cont_check_hdlr(struct cmd_args_s *ap)
{
	daos_obj_id_t		oids[OID_ARR_SIZE];
	daos_handle_t		oit;
	daos_anchor_t		anchor = { 0 };
	time_t			begin;
	time_t			end;
	unsigned long		duration;
	unsigned long		checked = 0;
	unsigned long		skipped = 0;
	unsigned long		inconsistent = 0;
	uint32_t		oids_nr;
	int			rc, i;
	int			rc2;

	/* Create a snapshot with OIT */
	rc = daos_cont_create_snap_opt(ap->cont, &ap->epc, NULL,
				       DAOS_SNAP_OPT_CR | DAOS_SNAP_OPT_OIT,
				       NULL);
	if (rc != 0)
		goto out;

	/* Open OIT */
	rc = daos_oit_open(ap->cont, ap->epc, &oit, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "open of container's OIT failed");
		goto out_snap;
	}

	begin = time(NULL);

	fprintf(ap->outstream, "check container %s started at: %s\n", ap->cont_str, ctime(&begin));

	while (!daos_anchor_is_eof(&anchor)) {
		oids_nr = OID_ARR_SIZE;
		rc = daos_oit_list(oit, oids, &oids_nr, &anchor, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "object IDs enumeration failed");
			D_GOTO(out_close, rc);
		}

		for (i = 0; i < oids_nr; i++) {
			rc = daos_obj_verify(ap->cont, oids[i], ap->epc);
			if (rc == -DER_NOSYS) {
				/* XXX: NOT support to verif EC object yet. */
				skipped++;
				continue;
			}

			checked++;
			if (rc == -DER_MISMATCH) {
				fprintf(ap->errstream,
					"found data inconsistency for object: "
					DF_OID"\n", DP_OID(oids[i]));
				inconsistent++;
				continue;
			}

			if (rc < 0) {
				DH_PERROR_DER(ap, rc,
					"check object "DF_OID" failed", DP_OID(oids[i]));
				D_GOTO(out_close, rc);
			}
		}
	}

	end = time(NULL);
	duration = end - begin;
	if (duration == 0)
		duration = 1;

	if (rc == 0 || rc == -DER_NOSYS || rc == -DER_MISMATCH) {
		fprintf(ap->outstream,
			"check container %s completed at: %s\n"
			"checked: %lu\n"
			"skipped: %lu\n"
			"inconsistent: %lu\n"
			"run_time: %lu seconds\n"
			"scan_speed: %lu objs/sec\n",
			ap->cont_str, ctime(&end), checked, skipped,
			inconsistent, duration, (checked + skipped) / duration);
		rc = 0;
	}

out_close:
	rc2 = daos_cont_snap_oit_destroy(ap->cont, oit, NULL);
	if (rc == 0)
		rc = rc2;
	rc2 = daos_oit_close(oit, NULL);
	if (rc == 0)
		rc = rc2;
out_snap:
	rc2 = cont_destroy_snap_hdlr(ap);
	if (rc == 0)
		rc = rc2;
out:
	return rc;
}

static int
cont_destroy_snap_hdlr(struct cmd_args_s *ap)
{
	daos_epoch_range_t epr;
	int rc;

	if (ap->epc == 0 &&
	    (ap->epcrange_begin == 0 || ap->epcrange_end == 0)) {
		fprintf(ap->errstream,
			"a single epoch or a range must be provided\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (ap->epc != 0 &&
	    (ap->epcrange_begin != 0 || ap->epcrange_end != 0)) {
		fprintf(ap->errstream,
			"both a single epoch and a range not allowed\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (ap->epc != 0) {
		epr.epr_lo = ap->epc;
		epr.epr_hi = ap->epc;
	} else {
		epr.epr_lo = ap->epcrange_begin;
		epr.epr_hi = ap->epcrange_end;
	}

	rc = daos_cont_destroy_snap(ap->cont, epr, NULL);
	if (rc != 0) {
		fprintf(ap->errstream,
			"failed to destroy snapshots for container %s: %s (%d)\n",
			ap->cont_str, d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

out:
	return rc;
}

int
parse_filename_dfs(const char *path, char **_obj_name, char **_cont_name)
{
	char	*f1 = NULL;
	char	*f2 = NULL;
	char	*fname = NULL;
	char	*cont_name = NULL;
	int	path_len;
	int	rc = 0;

	if (path == NULL || _obj_name == NULL || _cont_name == NULL)
		return -DER_INVAL;
	path_len = strlen(path) + 1;

	if (strcmp(path, "/") == 0) {
		D_STRNDUP_S(*_cont_name, "/");
		if (*_cont_name == NULL)
			return -DER_NOMEM;
		*_obj_name = NULL;
		return 0;
	}
	D_STRNDUP(f1, path, path_len);
	if (f1 == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_STRNDUP(f2, path, path_len);
	if (f2 == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	fname = basename(f1);
	cont_name = dirname(f2);

	if (cont_name[0] != '/') {
		char cwd[1024];

		if (getcwd(cwd, 1024) == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		if (strcmp(cont_name, ".") == 0) {
			D_STRNDUP(cont_name, cwd, 1024);
			if (cont_name == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		} else {
			char *new_dir = calloc(strlen(cwd) + strlen(cont_name)
						+ 1, sizeof(char));

			if (new_dir == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			strcpy(new_dir, cwd);
			if (cont_name[0] == '.') {
				strcat(new_dir, &cont_name[1]);
			} else {
				strcat(new_dir, "/");
				strcat(new_dir, cont_name);
			}
			cont_name = new_dir;
		}
		*_cont_name = cont_name;
	} else {
		D_STRNDUP(*_cont_name, cont_name,
			  strlen(cont_name) + 1);
		if (*_cont_name == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}
	D_STRNDUP(*_obj_name, fname, strlen(fname) + 1);
	if (*_obj_name == NULL) {
		D_FREE(*_cont_name);
		D_GOTO(out, rc = -DER_NOMEM);
	}
out:
	D_FREE(f1);
	D_FREE(f2);
	return rc;
}

static int
file_write(struct cmd_args_s *ap, struct file_dfs *file_dfs,
	   const char *file, void *buf, ssize_t *size)
{
	int rc = 0;
	/* posix write returns -1 on error so wrapper uses ssize_t, but
	 * dfs_sys_read takes daos_size_t for size argument
	 */
	daos_size_t tmp_size = *size;

	if (file_dfs->type == POSIX) {
		*size = write(file_dfs->fd, buf, *size);
		if (*size < 0)
			rc = errno;
	} else if (file_dfs->type == DAOS) {
		rc = dfs_sys_write(file_dfs->dfs_sys, file_dfs->obj, buf, file_dfs->offset,
				   &tmp_size, NULL);
		*size = tmp_size;
		if (rc == 0)
			/* update file pointer with number of bytes written */
			file_dfs->offset += *size;
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known '%s' type=%d", file, file_dfs->type);
	}
	return rc;
}

int
file_open(struct cmd_args_s *ap, struct file_dfs *file_dfs,
	  const char *file, int flags, ...)
{
	/* extract the mode */
	int	rc = 0;
	int	mode_set = 0;
	mode_t	mode = 0;

	if (flags & O_CREAT) {
		va_list vap;

		va_start(vap, flags);
		mode = va_arg(vap, mode_t);
		va_end(vap);
		mode_set = 1;
	}

	if (file_dfs->type == POSIX) {
		if (mode_set) {
			file_dfs->fd = open(file, flags, mode);
		} else {
			file_dfs->fd = open(file, flags);
		}
		if (file_dfs->fd < 0) {
			rc = errno;
			DH_PERROR_SYS(ap, rc, "file_open failed on '%s'", file);
		}
	} else if (file_dfs->type == DAOS) {
		rc = dfs_sys_open(file_dfs->dfs_sys, file, mode, flags, 0, 0, NULL,
				  &file_dfs->obj);
		if (rc != 0) {
			DH_PERROR_SYS(ap, rc, "file_open failed on '%s'", file);
		}
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known '%s' type=%d", file, file_dfs->type);
	}
	return rc;
}

static int
file_mkdir(struct cmd_args_s *ap, struct file_dfs *file_dfs,
	   const char *dir, mode_t *mode)
{
	int rc = 0;

	/* continue if directory already exists */
	if (file_dfs->type == POSIX) {
		rc = mkdir(dir, *mode);
		if (rc != 0) {
			/* return error code for POSIX mkdir */
			rc = errno;
		}
	} else if (file_dfs->type == DAOS) {
		rc = dfs_sys_mkdir(file_dfs->dfs_sys, dir, *mode, 0);
		if (rc != 0) {
			D_GOTO(out, rc);
		}
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known '%s' type=%d", dir, file_dfs->type);
	}
out:
	return rc;
}

static int
file_opendir(struct cmd_args_s *ap, struct file_dfs *file_dfs, const char *dir, DIR **_dirp)
{
	DIR *dirp = NULL;
	int rc	  = 0;

	if (file_dfs->type == POSIX) {
		dirp = opendir(dir);
		if (dirp == NULL)
			rc = errno;
	} else if (file_dfs->type == DAOS) {
		rc = dfs_sys_opendir(file_dfs->dfs_sys, dir, 0, &dirp);
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known '%s' type=%d", dir, file_dfs->type);
	}
	*_dirp = dirp;
	return rc;
}

static int
file_readdir(struct cmd_args_s *ap, struct file_dfs *file_dfs, DIR *dirp, struct dirent **_entry)
{
	struct dirent *entry = NULL;
	int rc		     = 0;

	if (file_dfs->type == POSIX) {
		do {
			/* errno set to zero before calling readdir to distinguish error from
			 * end of stream per readdir documentation
			 */
			errno = 0;
			entry = readdir(dirp);
			if (entry == NULL)
				rc = errno;
		} while (errno == EAGAIN);
	} else if (file_dfs->type == DAOS) {
		rc = dfs_sys_readdir(file_dfs->dfs_sys, dirp, &entry);
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known type=%d", file_dfs->type);
	}
	*_entry = entry;
	return rc;
}

static int
file_lstat(struct cmd_args_s *ap, struct file_dfs *file_dfs,
	   const char *path, struct stat *buf)
{
	int rc = 0;

	if (file_dfs->type == POSIX) {
		rc = lstat(path, buf);
		/* POSIX returns -1 on error and sets errno
		 * to the error code
		 */
		if (rc != 0)
			rc = errno;
	} else if (file_dfs->type == DAOS) {
		rc = dfs_sys_stat(file_dfs->dfs_sys, path, O_NOFOLLOW, buf);
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known '%s' type=%d", path, file_dfs->type);
	}
	return rc;
}

static int
file_read(struct cmd_args_s *ap, struct file_dfs *file_dfs,
	  const char *file, void *buf, ssize_t *size)
{
	int rc = 0;
	/* posix read returns -1 on error so wrapper uses ssize_t, but
	 * dfs_sys_read takes daos_size_t for size argument
	 */
	daos_size_t tmp_size = *size;

	if (file_dfs->type == POSIX) {
		*size = read(file_dfs->fd, buf, *size);
		if (*size < 0)
			rc = errno;
	} else if (file_dfs->type == DAOS) {
		rc = dfs_sys_read(file_dfs->dfs_sys, file_dfs->obj, buf, file_dfs->offset,
				  &tmp_size, NULL);
		*size = tmp_size;
		if (rc == 0)
			/* update file pointer with number of bytes read */
			file_dfs->offset += (daos_off_t)*size;
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known '%s' type=%d", file, file_dfs->type);
	}
	return rc;
}

static int
file_closedir(struct cmd_args_s *ap, struct file_dfs *file_dfs, DIR *dirp)
{
	int rc = 0;

	if (file_dfs->type == POSIX) {
		rc = closedir(dirp);
		/* POSIX returns -1 on error and sets errno
		 * to the error code
		 */
		if (rc != 0) {
			rc = errno;
		}
	} else if (file_dfs->type == DAOS) {
		/* dfs returns positive error code already */
		rc = dfs_sys_closedir(dirp);
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known type=%d", file_dfs->type);
	}
	return rc;
}

static int
file_close(struct cmd_args_s *ap, struct file_dfs *file_dfs, const char *file)
{
	int rc = 0;

	if (file_dfs->type == POSIX) {
		rc = close(file_dfs->fd);
		if (rc == 0) {
			file_dfs->fd = -1;
		} else {
			/* POSIX returns -1 on error and sets errno
			 * to the error code
			 */
			rc = errno;
		}
	} else if (file_dfs->type == DAOS) {
		rc = dfs_sys_close(file_dfs->obj);
		if (rc == 0)
			file_dfs->obj = NULL;
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known '%s' type=%d", file, file_dfs->type);
	}
	return rc;
}

static int
file_chmod(struct cmd_args_s *ap, struct file_dfs *file_dfs, const char *path,
	   mode_t mode)
{
	int rc = 0;

	if (file_dfs->type == POSIX) {
		rc = chmod(path, mode);
		/* POSIX returns -1 on error and sets errno
		 * to the error code, return positive errno
		 * set similar to dfs
		 */
		if (rc != 0) {
			rc = errno;
		}
	} else if (file_dfs->type == DAOS) {
		rc = dfs_sys_chmod(file_dfs->dfs_sys, path, mode);
	} else {
		rc = EINVAL;
		DH_PERROR_SYS(ap, rc, "File type not known '%s' type=%d", path, file_dfs->type);
	}
	return rc;
}

static int
fs_copy_file(struct cmd_args_s *ap,
	     struct file_dfs *src_file_dfs,
	     struct file_dfs *dst_file_dfs,
	     struct stat *src_stat,
	     const char *src_path,
	     const char *dst_path)
{
	int src_flags		= O_RDONLY;
	int dst_flags		= O_CREAT | O_TRUNC | O_WRONLY;
	mode_t tmp_mode_file	= S_IRUSR | S_IWUSR;
	int rc;
	uint64_t file_length	= src_stat->st_size;
	uint64_t total_bytes	= 0;
	uint64_t buf_size	= 64 * 1024 * 1024;
	void *buf		= NULL;

	/* Open source file */
	rc = file_open(ap, src_file_dfs, src_path, src_flags);
	if (rc != 0)
		D_GOTO(out, rc = daos_errno2der(rc));

	/* Open destination file */
	rc = file_open(ap, dst_file_dfs, dst_path, dst_flags, tmp_mode_file);
	if (rc != 0)
		D_GOTO(out_src_file, rc = daos_errno2der(rc));

	/* Allocate read/write buffer */
	D_ALLOC(buf, buf_size);
	if (buf == NULL)
		D_GOTO(out_dst_file, rc = -DER_NOMEM);

	/* read from source file, then write to dest file */
	while (total_bytes < file_length) {
		ssize_t left_to_read = buf_size;
		uint64_t bytes_left = file_length - total_bytes;

		if (bytes_left < buf_size)
			left_to_read = (size_t)bytes_left;
		rc = file_read(ap, src_file_dfs, src_path, buf, &left_to_read);
		if (rc != 0) {
			rc = daos_errno2der(rc);
			DH_PERROR_DER(ap, rc, "File read failed");
			D_GOTO(out_buf, rc);
		}
		ssize_t bytes_to_write = left_to_read;

		rc = file_write(ap, dst_file_dfs, dst_path, buf, &bytes_to_write);
		if (rc != 0) {
			rc = daos_errno2der(rc);
			DH_PERROR_DER(ap, rc, "File write failed");
			D_GOTO(out_buf, rc);
		}
		total_bytes += left_to_read;
	}

	/* set perms on destination to original source perms */
	rc = file_chmod(ap, dst_file_dfs, dst_path, src_stat->st_mode);
	if (rc != 0) {
		rc = daos_errno2der(rc);
		DH_PERROR_DER(ap, rc, "updating dst file permissions failed");
		D_GOTO(out_buf, rc);
	}

out_buf:
	D_FREE(buf);
out_dst_file:
	file_close(ap, dst_file_dfs, dst_path);
out_src_file:
	file_close(ap, src_file_dfs, src_path);
out:
	/* reset offsets if there is another file to copy */
	src_file_dfs->offset = 0;
	dst_file_dfs->offset = 0;
	return rc;
}

static int
fs_copy_symlink(struct cmd_args_s *ap,
		struct file_dfs *src_file_dfs,
		struct file_dfs *dst_file_dfs,
		struct stat *src_stat,
		const char *src_path,
		const char *dst_path)
{
	int		rc = 0;
	daos_size_t	len = DFS_MAX_PATH; /* unsigned for dfs_sys_readlink() */
	ssize_t		len2 = DFS_MAX_PATH; /* signed for readlink() */
	char		*symlink_value = NULL;

	D_ALLOC(symlink_value, len + 1);
	if (symlink_value == NULL)
		D_GOTO(out_copy_symlink, rc = -DER_NOMEM);

	if (src_file_dfs->type == POSIX) {
		len2 = readlink(src_path, symlink_value, len2);
		if (len2 < 0) {
			rc = daos_errno2der(errno);
			DH_PERROR_DER(ap, rc, "fs_copy_symlink failed on readlink('%s')",
				      src_path);
			D_GOTO(out_copy_symlink, rc);
		}
		len = (daos_size_t)len2;
	} else if (src_file_dfs->type == DAOS) {
		rc = dfs_sys_readlink(src_file_dfs->dfs_sys, src_path, symlink_value, &len);
		if (rc != 0) {
			rc = daos_errno2der(rc);
			DH_PERROR_DER(ap, rc, "fs_copy_symlink failed on dfs_sys_readlink('%s')",
				      src_path);
			D_GOTO(out_copy_symlink, rc);
		}
		/*length of symlink_value includes the NULL terminator already.*/
		len--;
	} else {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "unknown type for %s", src_path);
		D_GOTO(out_copy_symlink, rc);
	}
	symlink_value[len] = 0;

	if (dst_file_dfs->type == POSIX) {
		rc = symlink(symlink_value, dst_path);
		if ((rc != 0) && (errno == EEXIST)) {
			rc = -DER_EXIST;
			D_DEBUG(DB_TRACE, "Link %s exists.\n", dst_path);
			D_GOTO(out_copy_symlink, rc);
		} else if (rc != 0) {
			rc = daos_errno2der(errno);
			DH_PERROR_DER(ap, rc, "fs_copy_symlink failed on symlink('%s')",
				      dst_path);
			D_GOTO(out_copy_symlink, rc);
		}
	} else if (dst_file_dfs->type == DAOS) {
		rc = dfs_sys_symlink(dst_file_dfs->dfs_sys, symlink_value, dst_path);
		if (rc == EEXIST) {
			rc = -DER_EXIST;
			D_DEBUG(DB_TRACE, "Link %s exists.\n", dst_path);
			D_GOTO(out_copy_symlink, rc);
		} else if (rc != 0) {
			rc = daos_errno2der(rc);
			DH_PERROR_DER(ap, rc,
				      "fs_copy_symlink failed on dfs_sys_readlink('%s')",
				      dst_path);
			D_GOTO(out_copy_symlink, rc);
		}
	} else {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "unknown type for %s", dst_path);
		D_GOTO(out_copy_symlink, rc);
	}
out_copy_symlink:
	D_FREE(symlink_value);
	src_file_dfs->offset = 0;
	dst_file_dfs->offset = 0;
	return rc;
}

static int
fs_copy_dir(struct cmd_args_s *ap,
	    struct file_dfs *src_file_dfs,
	    struct file_dfs *dst_file_dfs,
	    struct stat *src_stat,
	    const char *src_path,
	    const char *dst_path,
	    struct fs_copy_stats *num)
{
	DIR			*src_dir = NULL;
	struct dirent		*entry = NULL;
	char			*next_src_path = NULL;
	char			*next_dst_path = NULL;
	struct stat		next_src_stat;
	mode_t			tmp_mode_dir = S_IRWXU;
	int			rc = 0;

	/* begin by opening source directory */
	rc = file_opendir(ap, src_file_dfs, src_path, &src_dir);
	if (rc != 0) {
		rc = daos_errno2der(rc);
		DH_PERROR_DER(ap, rc, "Cannot open directory '%s'", src_path);
		D_GOTO(out, rc);
	}

	/* create the destination directory if it does not exist. Assume root always exists */
	if (strcmp(dst_path, "/") != 0) {
		rc = file_mkdir(ap, dst_file_dfs, dst_path, &tmp_mode_dir);
		if (rc != 0 && rc != EEXIST) {
			DH_PERROR_SYS(ap, rc, "mkdir '%s' failed", dst_path);
			D_GOTO(out, rc = daos_errno2der(rc));
		}
	}
	/* copy all directory entries */
	while (1) {
		const char *d_name;

		/* walk source directory */
		rc = file_readdir(ap, src_file_dfs, src_dir, &entry);
		if (rc != 0) {
			DH_PERROR_SYS(ap, rc, "Cannot read directory");
			D_GOTO(out, rc = daos_errno2der(rc));
		}

		/* end of stream when entry is NULL and rc == 0 */
		if (!entry) {
			/* There are no more entries in this directory,
			 * so break out of the while loop.
			 */
			break;
		}

		/* Check that the entry is not "src_path"
		 * or src_path's parent.
		 */
		d_name = entry->d_name;
		if ((strcmp(d_name, "..") == 0) ||
		    (strcmp(d_name, ".")) == 0)
			continue;

		/* build the next source path */
		D_ASPRINTF(next_src_path, "%s/%s", src_path, d_name);
		if (next_src_path == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		/* stat the next source path */
		rc = file_lstat(ap, src_file_dfs, next_src_path, &next_src_stat);
		if (rc != 0) {
			rc = daos_errno2der(rc);
			DH_PERROR_DER(ap, rc, "Cannot stat path '%s'", next_src_path);
			D_GOTO(out, rc);
		}

		/* build the next destination path */
		D_ASPRINTF(next_dst_path, "%s/%s", dst_path, d_name);
		if (next_dst_path == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		switch (next_src_stat.st_mode & S_IFMT) {
		case S_IFREG:
			rc = fs_copy_file(ap, src_file_dfs, dst_file_dfs,
					  &next_src_stat, next_src_path,
					  next_dst_path);
			if ((rc != 0) && (rc != -DER_EXIST))
				D_GOTO(out, rc);
			num->num_files++;
			break;
		case S_IFLNK:
			rc = fs_copy_symlink(ap, src_file_dfs, dst_file_dfs,
					     &next_src_stat, next_src_path,
					     next_dst_path);
			if ((rc != 0) && (rc != -DER_EXIST))
				D_GOTO(out, rc);
			num->num_links++;
			break;
		case S_IFDIR:
			rc = fs_copy_dir(ap, src_file_dfs, dst_file_dfs, &next_src_stat,
					 next_src_path, next_dst_path, num);
			if ((rc != 0) && (rc != -DER_EXIST))
				D_GOTO(out, rc);
			num->num_dirs++;
			break;
		default:
			rc = -DER_INVAL;
			DH_PERROR_DER(ap, rc,
				      "Only files, directories, and symlinks are supported");
		}
		D_FREE(next_src_path);
		D_FREE(next_dst_path);
	}

	/* set original source perms on directories after copying */
	rc = file_chmod(ap, dst_file_dfs, dst_path, src_stat->st_mode);
	if (rc != 0) {
		rc = daos_errno2der(rc);
		DH_PERROR_DER(ap, rc, "updating destination permissions failed on '%s'", dst_path);
		D_GOTO(out, rc);
	}
out:
	if (rc != 0) {
		D_FREE(next_src_path);
		D_FREE(next_dst_path);
	}

	if (src_dir != NULL) {
		int close_rc;

		close_rc = file_closedir(ap, src_file_dfs, src_dir);
		if (close_rc != 0) {
			close_rc = daos_errno2der(close_rc);
			DH_PERROR_DER(ap, close_rc, "Could not close '%s'", src_path);
			if (rc == 0)
				rc = close_rc;
		}
	}
	return rc;
}

static int
fs_copy(struct cmd_args_s *ap,
	struct file_dfs *src_file_dfs,
	struct file_dfs *dst_file_dfs,
	const char *src_path,
	const char *dst_path,
	struct fs_copy_stats *num)
{
	int		rc = 0;
	struct stat	src_stat;
	struct stat	dst_stat;
	bool		copy_into_dst = false;
	char		*tmp_path = NULL;
	char		*tmp_dir = NULL;
	char		*tmp_name = NULL;

	/* Make sure the source exists. */
	rc = file_lstat(ap, src_file_dfs, src_path, &src_stat);
	if (rc != 0) {
		rc = daos_errno2der(rc);
		DH_PERROR_DER(ap, rc, "Failed to stat '%s'", src_path);
		D_GOTO(out, rc);
	}

	/* If the destination exists and is a directory,
	 * copy INTO the directory instead of TO it.
	 */
	rc = file_lstat(ap, dst_file_dfs, dst_path, &dst_stat);
	if (rc != 0 && rc != ENOENT) {
		rc = daos_errno2der(rc);
		DH_PERROR_DER(ap, rc, "Failed to stat %s", dst_path);
		D_GOTO(out, rc);
	} else if (rc == 0) {
		if (S_ISDIR(dst_stat.st_mode)) {
			copy_into_dst = true;
		} else if S_ISDIR(src_stat.st_mode) {
			rc = -DER_INVAL;
			DH_PERROR_DER(ap, rc, "Destination exists and is not a directory");
			D_GOTO(out, rc);
		}
	}

	if (copy_into_dst) {
		/* Get the dirname and basename */
		rc = parse_filename_dfs(src_path, &tmp_name, &tmp_dir);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to parse path '%s'", src_path);
			D_GOTO(out, rc);
		}

		/* Build the destination path */
		if (tmp_name != NULL) {
			D_ASPRINTF(tmp_path, "%s/%s", dst_path, tmp_name);
			if (tmp_path == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			dst_path = tmp_path;
		}
	}

	switch (src_stat.st_mode & S_IFMT) {
	case S_IFREG:
		rc = fs_copy_file(ap, src_file_dfs, dst_file_dfs, &src_stat, src_path,
				  dst_path);
		if (rc == 0)
			num->num_files++;
		break;
	case S_IFDIR:
		rc = fs_copy_dir(ap, src_file_dfs, dst_file_dfs, &src_stat, src_path,
				 dst_path, num);
		if (rc == 0)
			num->num_dirs++;
		break;
	default:
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Only files and directories are supported");
		D_GOTO(out, rc);
	}

out:
	if (copy_into_dst) {
		D_FREE(tmp_path);
		D_FREE(tmp_dir);
		D_FREE(tmp_name);
	}
	return rc;
}

static inline void
set_dm_args_default(struct dm_args *dm)
{
	dm->src = NULL;
	dm->dst = NULL;
	dm->src_poh = DAOS_HDL_INVAL;
	dm->src_coh = DAOS_HDL_INVAL;
	dm->dst_poh = DAOS_HDL_INVAL;
	dm->dst_coh = DAOS_HDL_INVAL;
	dm->cont_prop_oid = DAOS_PROP_CO_ALLOCED_OID;
	dm->cont_prop_layout = DAOS_PROP_CO_LAYOUT_TYPE;
	dm->cont_layout = DAOS_PROP_CO_LAYOUT_UNKNOWN;
	dm->cont_oid = 0;
}

/*
 * Free the user attribute buffers created by dm_cont_get_usr_attrs.
 */
void
dm_cont_free_usr_attrs(int n, char ***_names, void ***_buffers, size_t **_sizes)
{
	char	**names = *_names;
	void	**buffers = *_buffers;
	size_t	i;

	if (names != NULL) {
		for (i = 0; i < n; i++)
			D_FREE(names[i]);
		D_FREE(*_names);
	}
	if (buffers != NULL) {
		for (i = 0; i < n; i++)
			D_FREE(buffers[i]);
		D_FREE(*_buffers);
	}
	D_FREE(*_sizes);
}

/*
 * Get the user attributes for a container in a format similar
 * to what daos_cont_set_attr expects.
 * cont_free_usr_attrs should be called to free the allocations.
 */
int
dm_cont_get_usr_attrs(struct cmd_args_s *ap, daos_handle_t coh, int *_n, char ***_names,
		      void ***_buffers, size_t **_sizes)
{
	int		rc = 0;
	uint64_t	total_size = 0;
	uint64_t	cur_size = 0;
	uint64_t	num_attrs = 0;
	uint64_t	name_len = 0;
	char		*name_buf = NULL;
	char		**names = NULL;
	void		**buffers = NULL;
	size_t		*sizes = NULL;
	uint64_t	i;

	/* Get the total size needed to store all names */
	rc = daos_cont_list_attr(coh, NULL, &total_size, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed list user attributes");
		D_GOTO(out, rc);
	}

	/* no attributes found */
	if (total_size == 0) {
		*_n = 0;
		D_GOTO(out, rc);
	}

	/* Allocate a buffer to hold all attribute names */
	D_ALLOC(name_buf, total_size);
	if (name_buf == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* Get the attribute names */
	rc = daos_cont_list_attr(coh, name_buf, &total_size, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to list user attributes");
		D_GOTO(out, rc);
	}

	/* Figure out the number of attributes */
	while (cur_size < total_size) {
		name_len = strnlen(name_buf + cur_size, total_size - cur_size);
		if (name_len == total_size - cur_size) {
			/* end of buf reached but no end of string, ignoring */
			break;
		}
		num_attrs++;
		cur_size += name_len + 1;
	}

	/* Sanity check */
	if (num_attrs == 0) {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Failed to parse user attributes");
		D_GOTO(out, rc);
	}

	/* Allocate arrays for attribute names, buffers, and sizes */
	D_ALLOC_ARRAY(names, num_attrs);
	if (names == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	D_ALLOC_ARRAY(sizes, num_attrs);
	if (sizes == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	D_ALLOC_ARRAY(buffers, num_attrs);
	if (buffers == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* Create the array of names */
	cur_size = 0;
	for (i = 0; i < num_attrs; i++) {
		name_len = strnlen(name_buf + cur_size, total_size - cur_size);
		if (name_len == total_size - cur_size) {
			/* end of buf reached but no end of string, ignoring */
			break;
		}
		D_STRNDUP(names[i], name_buf + cur_size, name_len + 1);
		if (names[i] == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		cur_size += name_len + 1;
	}

	/* Get the buffer sizes */
	rc = daos_cont_get_attr(coh, num_attrs, (const char * const*)names, NULL, sizes, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to get user attribute sizes");
		D_GOTO(out, rc);
	}

	/* Allocate space for each value */
	for (i = 0; i < num_attrs; i++) {
		D_ALLOC(buffers[i], sizes[i] * sizeof(size_t));
		if (buffers[i] == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	/* Get the attribute values */
	rc = daos_cont_get_attr(coh, num_attrs, (const char * const*)names,
				(void * const*)buffers, sizes, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to get user attribute values");
		D_GOTO(out, rc);
	}

	/* Return values to the caller */
	*_n = num_attrs;
	*_names = names;
	*_buffers = buffers;
	*_sizes = sizes;
out:
	if (rc != 0)
		dm_cont_free_usr_attrs(num_attrs, &names, &buffers, &sizes);
	D_FREE(name_buf);
	return rc;
}

/* Copy all user attributes from one container to another. */
int
dm_copy_usr_attrs(struct cmd_args_s *ap, daos_handle_t src_coh, daos_handle_t dst_coh)
{
	int	num_attrs = 0;
	char	**names = NULL;
	void	**buffers = NULL;
	size_t	*sizes = NULL;
	int	rc;

	/* Get all user attributes */
	rc = dm_cont_get_usr_attrs(ap, src_coh, &num_attrs, &names, &buffers, &sizes);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to get user attributes");
		D_GOTO(out, rc);
	}

	/* no attributes to copy */
	if (num_attrs == 0)
		D_GOTO(out, rc = 0);

	rc = daos_cont_set_attr(dst_coh, num_attrs, (char const * const*)names,
				(void const * const*)buffers, sizes, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to set user attributes");
		D_GOTO(out, rc);
	}
out:
	dm_cont_free_usr_attrs(num_attrs, &names, &buffers, &sizes);
	return rc;
}

/*
 * Get the container properties for a container in a format similar
 * to what daos_cont_set_prop expects.
 * The last entry is the ACL and is conditionally set only if
 * the user has permissions.
 */
int
dm_cont_get_all_props(struct cmd_args_s *ap, daos_handle_t coh, daos_prop_t **_props,
		      bool get_oid, bool get_label, bool get_roots)
{
	int		rc;
	daos_prop_t	*props = NULL;
	daos_prop_t	*prop_acl = NULL;
	daos_prop_t	*props_merged = NULL;
	uint32_t        total_props = NUM_SERIALIZE_PROPS;
	/* minimum number of properties that are always allocated/used to start count */
	int             prop_index = NUM_SERIALIZE_PROPS;

	if (get_oid)
		total_props++;

	/* container label is required to be unique, so do not retrieve it for copies.
	 * The label is retrieved for serialization, but only deserialized if the label
	 * no longer exists in the pool
	 */
	if (get_label)
		total_props++;

	if (get_roots)
		total_props++;

	/* Allocate space for all props except ACL. */
	props = daos_prop_alloc(total_props);
	if (props == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* The order of properties MUST match the order expected by serialization  */
	props->dpp_entries[0].dpe_type = DAOS_PROP_CO_EC_CELL_SZ;
	props->dpp_entries[1].dpe_type = DAOS_PROP_CO_LAYOUT_TYPE;
	props->dpp_entries[2].dpe_type = DAOS_PROP_CO_LAYOUT_VER;
	props->dpp_entries[3].dpe_type = DAOS_PROP_CO_CSUM;
	props->dpp_entries[4].dpe_type = DAOS_PROP_CO_CSUM_CHUNK_SIZE;
	props->dpp_entries[5].dpe_type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
	props->dpp_entries[6].dpe_type = DAOS_PROP_CO_REDUN_FAC;
	props->dpp_entries[7].dpe_type = DAOS_PROP_CO_REDUN_LVL;
	props->dpp_entries[8].dpe_type = DAOS_PROP_CO_SNAPSHOT_MAX;
	props->dpp_entries[9].dpe_type = DAOS_PROP_CO_COMPRESS;
	props->dpp_entries[10].dpe_type = DAOS_PROP_CO_ENCRYPT;
	props->dpp_entries[11].dpe_type = DAOS_PROP_CO_OWNER;
	props->dpp_entries[12].dpe_type = DAOS_PROP_CO_OWNER_GROUP;
	props->dpp_entries[13].dpe_type = DAOS_PROP_CO_DEDUP;
	props->dpp_entries[14].dpe_type = DAOS_PROP_CO_DEDUP_THRESHOLD;
	props->dpp_entries[15].dpe_type = DAOS_PROP_CO_EC_PDA;
	props->dpp_entries[16].dpe_type = DAOS_PROP_CO_RP_PDA;
	props->dpp_entries[17].dpe_type = DAOS_PROP_CO_SCRUBBER_DISABLED;
	props->dpp_entries[18].dpe_type = DAOS_PROP_CO_PERF_DOMAIN;

	/* Conditionally get the OID. Should always be true for serialization. */
	if (get_oid) {
		props->dpp_entries[prop_index].dpe_type = DAOS_PROP_CO_ALLOCED_OID;
		prop_index++;
	}

	if (get_label) {
		props->dpp_entries[prop_index].dpe_type = DAOS_PROP_CO_LABEL;
		prop_index++;
	}

	if (get_roots) {
		props->dpp_entries[prop_index].dpe_type = DAOS_PROP_CO_ROOTS;
	}

	/* Get all props except ACL first. */
	rc = daos_cont_query(coh, NULL, props, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to query container");
		D_GOTO(out, rc);
	}

	/* Fetch the ACL separately in case user doesn't have access */
	rc = daos_cont_get_acl(coh, &prop_acl, NULL);
	if (rc == 0) {
		/* ACL will be appended to the end */
		rc = daos_prop_merge2(props, prop_acl, &props_merged);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to set container ACL");
			D_GOTO(out, rc);
		}
		daos_prop_free(props);
		props = props_merged;
	} else if (rc != -DER_NO_PERM) {
		DH_PERROR_DER(ap, rc, "Failed to query container ACL");
		D_GOTO(out, rc);
	}
	rc = 0;
	*_props = props;
out:
	daos_prop_free(prop_acl);
	if (rc != 0)
		daos_prop_free(props);
	return rc;
}

/* check if cont status is unhealthy */
static int
dm_check_cont_status(struct cmd_args_s *ap, daos_handle_t coh, bool *status_healthy)
{
	daos_prop_t		*prop;
	struct daos_prop_entry	*entry;
	struct daos_co_status	stat = {0};
	int			rc = 0;

	prop = daos_prop_alloc(1);
	if (prop == NULL)
		return -DER_NOMEM;

	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_STATUS;

	rc = daos_cont_query(coh, NULL, prop, NULL);
	if (rc) {
		DH_PERROR_DER(ap, rc, "daos container query failed");
		D_GOTO(out, rc);
	}

	entry = &prop->dpp_entries[0];
	daos_prop_val_2_co_status(entry->dpe_val, &stat);
	if (stat.dcs_status == DAOS_PROP_CO_HEALTHY) {
		*status_healthy = true;
	} else {
		*status_healthy = false;
	}
out:
	daos_prop_free(prop);
	return rc;
}

static int
dm_serialize_cont_md(struct cmd_args_s *ap, struct dm_args *ca, daos_prop_t *props,
		     char *preserve_props)
{
	int	rc = 0;
	int	num_attrs = 0;
	char	**names = NULL;
	void	**buffers = NULL;
	size_t	*sizes = NULL;
	void	*handle = NULL;
	int (*daos_cont_serialize_md)(char *, daos_prop_t *props, int, char **, char **, size_t *);

	/* Get all user attributes if any exist */
	rc = dm_cont_get_usr_attrs(ap, ca->src_coh, &num_attrs, &names, &buffers, &sizes);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to get user attributes");
		D_GOTO(out, rc);
	}
	handle = dlopen(LIBSERIALIZE, RTLD_NOW);
	if (handle == NULL) {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "libdaos_serialize.so not found");
		D_GOTO(out, rc);
	}
	daos_cont_serialize_md = dlsym(handle, "daos_cont_serialize_md");
	if (daos_cont_serialize_md == NULL)  {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Failed to lookup daos_cont_serialize_md");
		D_GOTO(out, rc);
	}
	(*daos_cont_serialize_md)(preserve_props, props, num_attrs, names, (char **)buffers, sizes);
out:
	if (num_attrs > 0) {
		dm_cont_free_usr_attrs(num_attrs, &names, &buffers, &sizes);
	}
	return rc;
}

static int
dm_deserialize_cont_md(struct cmd_args_s *ap, struct dm_args *ca, char *preserve_props,
		       daos_prop_t **props)
{
	int		rc = 0;
	void		*handle = NULL;
	int (*daos_cont_deserialize_props)(daos_handle_t, char *, daos_prop_t **props, uint64_t *);

	handle = dlopen(LIBSERIALIZE, RTLD_NOW);
	if (handle == NULL) {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "libdaos_serialize.so not found");
		D_GOTO(out, rc);
	}
	daos_cont_deserialize_props = dlsym(handle, "daos_cont_deserialize_props");
	if (daos_cont_deserialize_props == NULL)  {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Failed to lookup daos_cont_deserialize_props");
		D_GOTO(out, rc);
	}
	(*daos_cont_deserialize_props)(ca->dst_poh, preserve_props, props, &ca->cont_layout);
out:
	return rc;
}

static int
dm_deserialize_cont_attrs(struct cmd_args_s *ap, struct dm_args *ca, char *preserve_props)
{
	int		rc = 0;
	uint64_t	num_attrs = 0;
	char		**names = NULL;
	void		**buffers = NULL;
	size_t		*sizes = NULL;
	void		*handle = NULL;
	int (*daos_cont_deserialize_attrs)(char *, uint64_t *, char ***, void ***, size_t **);

	handle = dlopen(LIBSERIALIZE, RTLD_NOW);
	if (handle == NULL) {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "libdaos_serialize.so not found");
		D_GOTO(out, rc);
	}
	daos_cont_deserialize_attrs = dlsym(handle, "daos_cont_deserialize_attrs");
	if (daos_cont_deserialize_attrs == NULL)  {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Failed to lookup daos_cont_deserialize_attrs");
		D_GOTO(out, rc);
	}
	(*daos_cont_deserialize_attrs)(preserve_props, &num_attrs, &names, &buffers, &sizes);
	if (num_attrs > 0) {
		rc = daos_cont_set_attr(ca->dst_coh, num_attrs, (const char * const*)names,
					(const void * const*)buffers, sizes, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to set user attributes");
			D_GOTO(out, rc);
		}
		dm_cont_free_usr_attrs(num_attrs, &names, &buffers, &sizes);
	}
out:
	return rc;
}

static int
dm_connect(struct cmd_args_s *ap,
	   bool is_posix_copy,
	   struct file_dfs *src_file_dfs,
	   struct file_dfs *dst_file_dfs,
	   struct dm_args *ca,
	   char *sysname,
	   char *path,
	   daos_cont_info_t *src_cont_info,
	   daos_cont_info_t *dst_cont_info)
{
	/* check source pool/conts */
	int				rc = 0;
	struct duns_attr_t		dattr = {0};
	dfs_attr_t			attr = {0};
	daos_prop_t			*props = NULL;
	int				rc2;
	bool				status_healthy;

	/* open src pool, src cont, and mount dfs */
	if (src_file_dfs->type == DAOS) {
		rc = daos_pool_connect(ca->src_pool, sysname, DAOS_PC_RW, &ca->src_poh, NULL, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "failed to connect to source pool");
			D_GOTO(err, rc);
		}
		rc = daos_cont_open(ca->src_poh, ca->src_cont, DAOS_COO_RW, &ca->src_coh,
				    src_cont_info, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "failed to open source container");
			D_GOTO(err, rc);
		}
		if (is_posix_copy) {
			rc = dfs_sys_mount(ca->src_poh, ca->src_coh, O_RDWR,
					   DFS_SYS_NO_LOCK, &src_file_dfs->dfs_sys);
			if (rc != 0) {
				rc = daos_errno2der(rc);
				DH_PERROR_DER(ap, rc, "Failed to mount DFS filesystem on source");
				D_GOTO(err, rc);
			}
		}

		/* do not copy a container that has unhealthy container status */
		rc = dm_check_cont_status(ap, ca->src_coh, &status_healthy);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to check container status");
			D_GOTO(err, rc);
		} else if (!status_healthy) {
			rc = -DER_INVAL;
			DH_PERROR_DER(ap, rc, "Container status is unhealthy, stopping");
			D_GOTO(err, rc);
		}
	}

	/* set cont_layout to POSIX type if the source is not in DAOS, if the
	 * destination is DAOS, and no destination container exists yet,
	 * then it knows to create a POSIX container
	 */
	if (src_file_dfs->type == POSIX)
		ca->cont_layout = DAOS_PROP_CO_LAYOUT_POSIX;

	/* Retrieve source container properties */
	if (src_file_dfs->type != POSIX) {
		/* if moving data from POSIX to DAOS and preserve_props option is on,
		 * then write container properties to the provided hdf5 filename
		 */
		if (ap->preserve_props != NULL && dst_file_dfs->type == POSIX) {
			/* preserve_props option is for filesystem copy (which uses DFS API),
			 * so do not retrieve roots or max oid property.
			 */
			rc = dm_cont_get_all_props(ap, ca->src_coh, &props, false,  true, false);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to get container properties");
				D_GOTO(err, rc);
			}
			rc = dm_serialize_cont_md(ap, ca, props, ap->preserve_props);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to serialize metadata");
				D_GOTO(err, rc);
			}
		}
		/* if DAOS -> DAOS copy container properties from src to dst */
		if (dst_file_dfs->type == DAOS) {
			/* src to dst copies never copy label, and filesystem copies use DFS
			 * so do not copy roots or max oid prop
			 */
			if (is_posix_copy)
				rc = dm_cont_get_all_props(ap, ca->src_coh, &props,
							   false, false, false);
			else
				rc = dm_cont_get_all_props(ap, ca->src_coh, &props,
							   true, false, true);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to get container properties");
				D_GOTO(err, rc);
			}
			ca->cont_layout = props->dpp_entries[1].dpe_val;
		}
	}

	/* open dst pool, dst cont, and mount dfs */
	if (dst_file_dfs->type == DAOS) {
		bool dst_cont_passed = strlen(ca->dst_cont) ? true : false;

		/* only connect if destination pool wasn't already opened */
		if (strlen(ca->dst_pool) != 0) {
			if (!daos_handle_is_valid(ca->dst_poh)) {
				rc = daos_pool_connect(ca->dst_pool, sysname, DAOS_PC_RW,
						       &ca->dst_poh, NULL, NULL);
				if (rc != 0) {
					DH_PERROR_DER(ap, rc,
						      "failed to connect to destination pool");
					D_GOTO(err, rc);
				}
			}
		/* if the dst pool uuid is null that means that this is a UNS destination path, so
		 * we copy the source pool uuid into the destination and try to connect again
		 */
		} else {
			strcpy(ca->dst_pool, ca->src_pool);
			rc = daos_pool_connect(ca->dst_pool, sysname, DAOS_PC_RW, &ca->dst_poh,
					       NULL, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "failed to connect to destination pool");
				D_GOTO(err, rc);
			}
			if (src_file_dfs->type == POSIX)
				dattr.da_type = DAOS_PROP_CO_LAYOUT_POSIX;
			else
				dattr.da_type = ca->cont_layout;
			if (props != NULL)
				dattr.da_props = props;
			rc = duns_create_path(ca->dst_poh, path, &dattr);
			if (rc != 0) {
				DH_PERROR_SYS(ap, rc, "failed to create destination UNS path");
				D_GOTO(err, rc = daos_errno2der(rc));
			}
			snprintf(ca->dst_cont, DAOS_PROP_LABEL_MAX_LEN + 1, "%s", dattr.da_cont);
		}

		/* check preserve_props, if source is from POSIX and destination is DAOS we need
		 * to read container properties from the file that is specified before the DAOS
		 * destination container is created
		 */
		if (ap->preserve_props != NULL && src_file_dfs->type == POSIX) {
			rc = dm_deserialize_cont_md(ap, ca, ap->preserve_props, &props);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to deserialize metadata");
				D_GOTO(err, rc);
			}
		}

		/* try to open container if this is a filesystem copy, and if it fails try to
		 * create a destination, then attempt to open again
		 */
		if (dst_cont_passed) {
			rc = daos_cont_open(ca->dst_poh, ca->dst_cont, DAOS_COO_RW, &ca->dst_coh,
					    dst_cont_info, NULL);
			if (rc != 0 && rc != -DER_NONEXIST) {
				DH_PERROR_DER(ap, rc, "failed to open destination container");
				D_GOTO(err, rc);
			}
		} else {
			rc = -DER_NONEXIST;
		}
		if (rc == -DER_NONEXIST) {
			uuid_t	cuuid;
			bool	dst_cont_is_uuid = true;

			if (dst_cont_passed) {
				rc = uuid_parse(ca->dst_cont, cuuid);
				dst_cont_is_uuid = (rc == 0);
				if (dst_cont_is_uuid) {
					/* Cannot create a container with a user-supplied UUID */
					rc = -DER_NONEXIST;
					DH_PERROR_DER(ap, rc,
						      "failed to open destination container");
					D_GOTO(err, rc);
				}
			}

			if (ca->cont_layout == DAOS_PROP_CO_LAYOUT_POSIX) {
				attr.da_props = props;
				if (dst_cont_is_uuid)
					rc = dfs_cont_create(ca->dst_poh, &cuuid, &attr,
							     NULL, NULL);
				else
					rc = dfs_cont_create_with_label(ca->dst_poh, ca->dst_cont,
									&attr, &cuuid, NULL, NULL);
				if (rc) {
					rc = daos_errno2der(rc);
					DH_PERROR_DER(ap, rc,
						      "failed to create destination container");
					D_GOTO(err, rc);
				}
			} else {
				if (dst_cont_is_uuid)
					rc = daos_cont_create(ca->dst_poh, &cuuid, props, NULL);
				else
					rc = daos_cont_create_with_label(
						ca->dst_poh, ca->dst_cont, props, &cuuid, NULL);
				if (rc) {
					DH_PERROR_DER(ap, rc,
						      "failed to create destination container");
					D_GOTO(err, rc);
				}
			}
			uuid_unparse(cuuid, ca->dst_cont);
			rc = daos_cont_open(ca->dst_poh, ca->dst_cont, DAOS_COO_RW, &ca->dst_coh,
					    dst_cont_info, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "failed to open container");
				D_GOTO(err, rc);
			}
		}
		if (is_posix_copy) {
			rc = dfs_sys_mount(ca->dst_poh, ca->dst_coh, O_RDWR, DFS_SYS_NO_LOCK,
					   &dst_file_dfs->dfs_sys);
			if (rc != 0) {
				rc = daos_errno2der(rc);
				DH_PERROR_DER(ap, rc, "dfs_mount on destination failed");
				D_GOTO(err, rc);
			}
		}

		/* check preserve_props, if source is from POSIX and destination is DAOS we
		 * need to read user attributes from the file that is specified, and set them
		 * in the destination container
		 */
		if (ap->preserve_props != NULL && src_file_dfs->type == POSIX) {
			rc = dm_deserialize_cont_attrs(ap, ca, ap->preserve_props);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to deserialize user attributes");
				D_GOTO(err, rc);
			}
		}
	}
	/* get source container user attributes and copy them to the DAOS destination container */
	if (src_file_dfs->type == DAOS && dst_file_dfs->type == DAOS) {
		rc = dm_copy_usr_attrs(ap, ca->src_coh, ca->dst_coh);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Copying user attributes failed");
			D_GOTO(err, rc);
		}
	}
	D_GOTO(out, rc);
err:
	if (daos_handle_is_valid(ca->dst_coh)) {
		if (dst_file_dfs->dfs_sys != NULL) {
			rc2 = dfs_sys_umount(dst_file_dfs->dfs_sys);
			if (rc2 != 0)
				DH_PERROR_SYS(ap, rc2, "failed to unmount destination container");
		}
		rc2 = daos_cont_close(ca->dst_coh, NULL);
		if (rc2 != 0)
			DH_PERROR_DER(ap, rc2, "failed to close destination container");
	}
	if (daos_handle_is_valid(ca->src_coh)) {
		if (src_file_dfs->dfs_sys != NULL) {
			rc2 = dfs_sys_umount(src_file_dfs->dfs_sys);
			if (rc2 != 0)
				DH_PERROR_SYS(ap, rc2, "failed to unmount source container");
		}
		rc2 = daos_cont_close(ca->src_coh, NULL);
		if (rc2 != 0)
			DH_PERROR_DER(ap, rc2, "failed to close source container");
	}
	if (daos_handle_is_valid(ca->dst_poh)) {
		rc2 = daos_pool_disconnect(ca->dst_poh, NULL);
		if (rc2 != 0)
			DH_PERROR_DER(ap, rc2, "failed to disconnect from destination pool %s",
				      ca->dst_pool);
	}
	if (daos_handle_is_valid(ca->src_poh)) {
		rc2 = daos_pool_disconnect(ca->src_poh, NULL);
		if (rc2 != 0)
			DH_PERROR_DER(ap, rc2, "Failed to disconnect from source pool %s",
				      ca->src_pool);
	}
out:
	if (props != NULL)
		daos_prop_free(props);
	return rc;
}

static inline void
file_set_defaults_dfs(struct file_dfs *file_dfs)
{
	/* set defaults for file_dfs struct */
	file_dfs->type = DAOS;
	file_dfs->fd = -1;
	file_dfs->offset = 0;
	file_dfs->obj = NULL;
	file_dfs->dfs_sys = NULL;
}

static int
dm_disconnect(struct cmd_args_s *ap,
	      bool is_posix_copy,
	      struct dm_args *ca,
	      struct file_dfs *src_file_dfs,
	      struct file_dfs *dst_file_dfs)
{
	 /* The fault injection tests expect no memory leaks but inject faults that
	 * block umount/close/disconnect calls, etc. So, if I use GOTO and return the error
	 * code immediately after a failure to umount/close/disconnect then fault injection
	 * will always report a memory leak. Is it better to immediately return if one of
	 * these fails? This will cause memory leaks in fault injection tests for fs copy,
	 * so not sure what is the best thing to do here.
	 */
	int rc = 0;

	if (src_file_dfs->type == DAOS) {
		if (is_posix_copy) {
			rc = dfs_sys_umount(src_file_dfs->dfs_sys);
			if (rc != 0)
				DH_PERROR_SYS(ap, rc, "failed to unmount source");
			src_file_dfs->dfs_sys = NULL;
		}
		rc = daos_cont_close(ca->src_coh, NULL);
		if (rc != 0)
			DH_PERROR_DER(ap, rc, "failed to close source container");
		rc = daos_pool_disconnect(ca->src_poh, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "failed to disconnect source pool");
			if (rc == -DER_NOMEM) {
				rc = daos_pool_disconnect(ca->src_poh, NULL);
				if (rc != 0)
					DH_PERROR_DER(ap, rc,
						      "failed to disconnect source pool on retry");
			}
		}
	}
	if (dst_file_dfs->type == DAOS) {
		if (is_posix_copy) {
			rc = dfs_sys_umount(dst_file_dfs->dfs_sys);
			if (rc != 0)
				DH_PERROR_SYS(ap, rc, "failed to unmount source");
			dst_file_dfs->dfs_sys = NULL;
		}
		rc = daos_cont_close(ca->dst_coh, NULL);
		if (rc != 0)
			DH_PERROR_DER(ap, rc, "failed to close destination container");
		rc = daos_pool_disconnect(ca->dst_poh, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "failed to disconnect destination pool");
			if (rc == -DER_NOMEM) {
				rc = daos_pool_disconnect(ca->dst_poh, NULL);
				if (rc != 0)
					DH_PERROR_DER(
					    ap, rc,
					    "failed to disconnect destination pool on retry");
			}
		}
	}
	return rc;
}

/*
* Parse a path of the format:
* daos://<pool>/<cont>/<path> | <UNS path> | <POSIX path>
* Modifies "path" to be the relative container path, defaulting to "/".
* Returns 0 if a daos path was successfully parsed, a error number if not.
*/
static int
dm_parse_path(struct file_dfs *file, char *path, size_t path_len, char (*pool_str)[],
	      char (*cont_str)[])
{
	struct duns_attr_t	dattr = {0};
	int			rc = 0;
	char			*tmp_path1 = NULL;
	char			*path_dirname = NULL;
	char			*tmp_path2 = NULL;
	char			*path_basename = NULL;

	rc = duns_resolve_path(path, &dattr);
	if (rc == 0) {
		snprintf(*pool_str, DAOS_PROP_LABEL_MAX_LEN + 1, "%s", dattr.da_pool);
		snprintf(*cont_str, DAOS_PROP_LABEL_MAX_LEN + 1, "%s", dattr.da_cont);
		if (dattr.da_rel_path == NULL)
			strncpy(path, "/", path_len);
		else
			strncpy(path, dattr.da_rel_path, path_len);
	} else if (rc == ENOMEM) {
		D_GOTO(out, rc);
	} else {
		/* If basename does not exist yet then duns_resolve_path will fail even if
		 * dirname is a UNS path
		 */

		/* get dirname */
		D_STRNDUP(tmp_path1, path, path_len);
		if (tmp_path1 == NULL)
			D_GOTO(out, rc = ENOMEM);
		path_dirname = dirname(tmp_path1);
		/* reset before calling duns_resolve_path with new string */
		memset(&dattr, 0, sizeof(struct duns_attr_t));

		/* Check if this path represents a daos pool and/or container. */
		rc = duns_resolve_path(path_dirname, &dattr);
		if (rc == 0) {
			/* if duns_resolve_path succeeds then concat basename to da_rel_path */
			D_STRNDUP(tmp_path2, path, path_len);
			if (tmp_path2 == NULL)
				D_GOTO(out, rc = ENOMEM);
			path_basename = basename(tmp_path2);

			/* dirname might be root uns path, if that is the case,
			 * then da_rel_path might be NULL
			 */
			if (dattr.da_rel_path == NULL)
				snprintf(path, path_len, "/%s", path_basename);
			else
				snprintf(path, path_len, "%s/%s", dattr.da_rel_path, path_basename);
			snprintf(*pool_str, DAOS_PROP_LABEL_MAX_LEN + 1, "%s", dattr.da_pool);
			snprintf(*cont_str, DAOS_PROP_LABEL_MAX_LEN + 1, "%s", dattr.da_cont);
		} else if (rc == ENOMEM) {
			/* TODO: Take this path of rc != ENOENT? */
			D_GOTO(out, rc);
		} else if (strncmp(path, "daos://", 7) == 0) {
			/* Error, since we expect a DAOS path */
			D_GOTO(out, rc);
		} else {
			/* not a DAOS path, set type to POSIX,
			 * POSIX dir will be checked with stat
			 * at the beginning of fs_copy
			 */
			rc = 0;
			file->type = POSIX;
		}
	}
out:
	D_FREE(tmp_path1);
	D_FREE(tmp_path2);
	duns_destroy_attr(&dattr);
	return daos_errno2der(rc);
}

int
fs_copy_hdlr(struct cmd_args_s *ap)
{
	int			rc = 0;
	int			rc2 = 0;
	char			*src_str = NULL;
	char			*dst_str = NULL;
	size_t			src_str_len = 0;
	size_t			dst_str_len = 0;
	daos_cont_info_t	src_cont_info = {0};
	daos_cont_info_t	dst_cont_info = {0};
	struct file_dfs		src_file_dfs = {0};
	struct file_dfs		dst_file_dfs = {0};
	struct dm_args		*ca = NULL;
	bool			is_posix_copy = true;
	struct fs_copy_stats	*num = NULL;

	D_ALLOC(ca, sizeof(struct dm_args));
	if (ca == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	ap->dm_args = ca;

	D_ALLOC(num, sizeof(struct fs_copy_stats));
	if (num == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	ap->fs_copy_stats = num;

	set_dm_args_default(ca);
	file_set_defaults_dfs(&src_file_dfs);
	file_set_defaults_dfs(&dst_file_dfs);

	src_str_len = strlen(ap->src);
	if (src_str_len == 0) {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Source path required");
		D_GOTO(out, rc);
	}
	D_STRNDUP(src_str, ap->src, src_str_len);
	if (src_str == NULL) {
		rc = -DER_NOMEM;
		DH_PERROR_DER(ap, rc, "Unable to allocate memory for source path");
		D_GOTO(out, rc);
	}
	rc = dm_parse_path(&src_file_dfs, src_str, src_str_len, &ca->src_pool, &ca->src_cont);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "failed to parse source path");
		D_GOTO(out, rc);
	}

	dst_str_len = strlen(ap->dst);
	if (dst_str_len == 0) {
		rc = -DER_INVAL;
		DH_PERROR_DER(ap, rc, "Destination path required");
		D_GOTO(out, rc);
	}
	D_STRNDUP(dst_str, ap->dst, dst_str_len);
	if (dst_str == NULL) {
		rc = -DER_NOMEM;
		DH_PERROR_DER(ap, rc, "Unable to allocate memory for destination path");
		D_GOTO(out, rc);
	}
	rc = dm_parse_path(&dst_file_dfs, dst_str, dst_str_len, &ca->dst_pool, &ca->dst_cont);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "failed to parse destination path");
		D_GOTO(out, rc);
	}

	rc = dm_connect(ap, is_posix_copy, &src_file_dfs, &dst_file_dfs, ca,
			ap->sysname, ap->dst, &src_cont_info, &dst_cont_info);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "fs copy failed to connect");
		D_GOTO(out, rc);
	}

	rc = fs_copy(ap, &src_file_dfs, &dst_file_dfs, src_str, dst_str, num);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "fs copy failed");
		D_GOTO(out_disconnect, rc);
	}

	if (dst_file_dfs.type == POSIX)
		ap->fs_copy_posix = true;
out_disconnect:
	/* umount dfs, close conts, and disconnect pools */
	rc2 = dm_disconnect(ap, is_posix_copy, ca, &src_file_dfs, &dst_file_dfs);
	if (rc2 != 0)
		DH_PERROR_DER(ap, rc2, "failed to disconnect");
out:
	D_FREE(src_str);
	D_FREE(dst_str);
	return rc;
}

static int
cont_clone_recx_single(struct cmd_args_s *ap,
		       daos_key_t *dkey,
		       daos_handle_t *src_oh,
		       daos_handle_t *dst_oh,
		       daos_iod_t *iod)
{
	/* if iod_type is single value just fetch iod size from source
	 * and update in destination object
	 */
	daos_size_t	buf_len = (*iod).iod_size;
	char		buf[buf_len];
	d_sg_list_t	sgl;
	d_iov_t		iov;
	int		rc;

	/* set sgl values */
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &iov;
	d_iov_set(&iov, buf, buf_len);

	rc = daos_obj_fetch(*src_oh, DAOS_TX_NONE, 0, dkey, 1, iod, &sgl,
			    NULL, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to fetch source value");
		D_GOTO(out, rc);
	}
	rc = daos_obj_update(*dst_oh, DAOS_TX_NONE, 0, dkey, 1, iod,
			     &sgl, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to update destination value");
		D_GOTO(out, rc);
	}
out:
	return rc;
}

static int
cont_clone_recx_array(struct cmd_args_s *ap,
		      daos_key_t *dkey,
		      daos_key_t *akey,
		      daos_handle_t *src_oh,
		      daos_handle_t *dst_oh,
		      daos_iod_t *iod)
{
	int			rc = 0;
	int			i = 0;
	daos_size_t		buf_len = 0;
	daos_size_t		buf_len_alloc = 0;
	uint32_t		number = 5;
	daos_anchor_t		recx_anchor = {0};
	d_sg_list_t		sgl;
	d_iov_t			iov;
	daos_epoch_range_t	eprs[5];
	daos_recx_t		recxs[5];
	daos_size_t		size;
	char			*buf = NULL;
	char			*prev_buf = NULL;

	while (!daos_anchor_is_eof(&recx_anchor)) {
		/* list all recx for this dkey/akey */
		number = 5;
		rc = daos_obj_list_recx(*src_oh, DAOS_TX_NONE, dkey, akey, &size, &number, recxs,
					eprs, &recx_anchor, true, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to list recx");
			D_GOTO(out, rc);
		}

		/* if no recx is returned for this dkey/akey move on */
		if (number == 0)
			continue;

		/* set iod values */
		(*iod).iod_type  = DAOS_IOD_ARRAY;
		(*iod).iod_nr    = number;
		(*iod).iod_recxs = recxs;
		(*iod).iod_size  = size;

		/* set sgl values */
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = &iov;
		sgl.sg_nr     = 1;

		/* allocate/reallocate a single buffer */
		buf_len = 0;
		prev_buf = buf;
		for (i = 0; i < number; i++) {
			buf_len += recxs[i].rx_nr;
		}
		buf_len *= size;
		if (buf_len > buf_len_alloc) {
			D_REALLOC_NZ(buf, prev_buf, buf_len);
			if (buf == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			buf_len_alloc = buf_len;
		}
		d_iov_set(&iov, buf, buf_len);

		/* fetch recx values from source */
		rc = daos_obj_fetch(*src_oh, DAOS_TX_NONE, 0, dkey, 1, iod, &sgl, NULL, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to fetch source recx");
			D_GOTO(out, rc);
		}

		/* Sanity check that fetch returns as expected */
		if (sgl.sg_nr_out != 1) {
			DH_PERROR_DER(ap, rc, "Failed to fetch source recx");
			D_GOTO(out, rc = -DER_INVAL);
		}

		/* update fetched recx values and place in
		 * destination object
		 */
		rc = daos_obj_update(*dst_oh, DAOS_TX_NONE, 0, dkey, 1, iod, &sgl, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to update destination recx");
			D_GOTO(out, rc);
		}
	}
out:
	D_FREE(buf);
	return rc;
}

static int
cont_clone_list_akeys(struct cmd_args_s *ap,
		      daos_handle_t *src_oh,
		      daos_handle_t *dst_oh,
		     daos_key_t diov)
{
	int		rc = 0;
	int		j = 0;
	char		*ptr;
	daos_anchor_t	akey_anchor = {0};
	daos_key_desc_t	akey_kds[ENUM_DESC_NR] = {0};
	uint32_t	akey_number = ENUM_DESC_NR;
	char		*akey = NULL;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	daos_key_t	aiov;
	daos_iod_t	iod;
	char		*small_key = NULL;
	char		*large_key = NULL;
	char		*key_buf = NULL;
	daos_size_t	key_buf_len = 0;

	D_ALLOC(small_key, ENUM_DESC_BUF);
	if (small_key == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	D_ALLOC(large_key, ENUM_LARGE_KEY_BUF);
	if (large_key == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* loop to enumerate akeys */
	while (!daos_anchor_is_eof(&akey_anchor)) {
		memset(akey_kds, 0, sizeof(akey_kds));
		memset(small_key, 0, ENUM_DESC_BUF);
		memset(large_key, 0, ENUM_LARGE_KEY_BUF);
		akey_number = ENUM_DESC_NR;

		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = &iov;

		key_buf = small_key;
		key_buf_len = ENUM_DESC_BUF;
		d_iov_set(&iov, key_buf, key_buf_len);

		/* get akeys */
		rc = daos_obj_list_akey(*src_oh, DAOS_TX_NONE, &diov, &akey_number, akey_kds,
					&sgl, &akey_anchor, NULL);
		if (rc == -DER_KEY2BIG) {
			/* call list dkey again with bigger buffer */
			key_buf = large_key;
			key_buf_len = ENUM_LARGE_KEY_BUF;
			d_iov_set(&iov, key_buf, key_buf_len);
			rc = daos_obj_list_akey(*src_oh, DAOS_TX_NONE, &diov, &akey_number,
						akey_kds, &sgl, &akey_anchor, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to list akeys");
				D_GOTO(out, rc);
			}
		}

		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to list akeys");
			D_GOTO(out, rc);
		}

		/* if no akeys returned move on */
		if (akey_number == 0)
			continue;

		/* parse out individual akeys based on key length and
		 * number of dkeys returned
		 */
		for (ptr = key_buf, j = 0; j < akey_number; j++) {
			D_ALLOC(akey, key_buf_len);
			if (akey == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			memcpy(akey, ptr, akey_kds[j].kd_key_len);
			d_iov_set(&aiov, (void *)akey, akey_kds[j].kd_key_len);

			/* set iod values */
			iod.iod_nr    = 1;
			iod.iod_type  = DAOS_IOD_SINGLE;
			iod.iod_size  = DAOS_REC_ANY;
			iod.iod_recxs = NULL;
			iod.iod_name  = aiov;

			/* do fetch with sgl == NULL to check if iod type
			 * (ARRAY OR SINGLE VAL)
			 */
			rc = daos_obj_fetch(*src_oh, DAOS_TX_NONE, 0, &diov,
					    1, &iod, NULL, NULL, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to fetch source object");
				D_FREE(akey);
				D_GOTO(out, rc);
			}

			/* if iod_size == 0 then this is a DAOS_IOD_ARRAY
			 * type
			 */
			if ((int)iod.iod_size == 0) {
				rc = cont_clone_recx_array(ap, &diov, &aiov, src_oh, dst_oh, &iod);
				if (rc != 0) {
					DH_PERROR_DER(ap, rc, "Failed to copy record");
					D_FREE(akey);
					D_GOTO(out, rc);
				}
			} else {
				rc = cont_clone_recx_single(ap, &diov, src_oh, dst_oh, &iod);
				if (rc != 0) {
					DH_PERROR_DER(ap, rc, "Failed to copy record");
					D_FREE(akey);
					D_GOTO(out, rc);
				}
			}
			/* advance to next akey returned */
			ptr += akey_kds[j].kd_key_len;
			D_FREE(akey);
		}
	}
out:
	D_FREE(small_key);
	D_FREE(large_key);
	return rc;
}

static int
cont_clone_list_dkeys(struct cmd_args_s *ap,
		      daos_handle_t *src_oh,
		      daos_handle_t *dst_oh)
{
	int		rc = 0;
	int		j = 0;
	char		*ptr;
	daos_anchor_t	dkey_anchor = {0};
	daos_key_desc_t	dkey_kds[ENUM_DESC_NR] = {0};
	uint32_t	dkey_number = ENUM_DESC_NR;
	char		*dkey = NULL;
	d_sg_list_t	sgl;
	d_iov_t		iov;
	daos_key_t	diov;
	char		*small_key = NULL;
	char		*large_key = NULL;
	char		*key_buf = NULL;
	daos_size_t	key_buf_len = 0;

	D_ALLOC(small_key, ENUM_DESC_BUF);
	if (small_key == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	D_ALLOC(large_key, ENUM_LARGE_KEY_BUF);
	if (large_key == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* loop to enumerate dkeys */
	while (!daos_anchor_is_eof(&dkey_anchor)) {
		memset(dkey_kds, 0, sizeof(dkey_kds));
		memset(small_key, 0, ENUM_DESC_BUF);
		memset(large_key, 0, ENUM_LARGE_KEY_BUF);
		dkey_number = ENUM_DESC_NR;

		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs   = &iov;

		key_buf = small_key;
		key_buf_len = ENUM_DESC_BUF;
		d_iov_set(&iov, key_buf, key_buf_len);

		/* get dkeys */
		rc = daos_obj_list_dkey(*src_oh, DAOS_TX_NONE, &dkey_number, dkey_kds,
					&sgl, &dkey_anchor, NULL);
		if (rc == -DER_KEY2BIG) {
			/* call list dkey again with bigger buffer */
			key_buf = large_key;
			key_buf_len = ENUM_LARGE_KEY_BUF;
			d_iov_set(&iov, key_buf, key_buf_len);
			rc = daos_obj_list_dkey(*src_oh, DAOS_TX_NONE, &dkey_number, dkey_kds,
						&sgl, &dkey_anchor, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to list dkeys");
				D_GOTO(out, rc);
			}
		}

		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to list dkeys");
			D_GOTO(out, rc);
		}

		/* if no dkeys were returned move on */
		if (dkey_number == 0)
			continue;

		/* parse out individual dkeys based on key length and
		 * number of dkeys returned
		 */
		for (ptr = key_buf, j = 0; j < dkey_number; j++) {
			D_ALLOC(dkey, key_buf_len);
			if (dkey == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			memcpy(dkey, ptr, dkey_kds[j].kd_key_len);
			d_iov_set(&diov, (void *)dkey, dkey_kds[j].kd_key_len);

			/* enumerate and parse akeys */
			rc = cont_clone_list_akeys(ap, src_oh, dst_oh, diov);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to list akeys");
				D_FREE(dkey);
				D_GOTO(out, rc);
			}
			ptr += dkey_kds[j].kd_key_len;
			D_FREE(dkey);
		}
	}
out:
	D_FREE(small_key);
	D_FREE(large_key);
	return rc;
}

int
cont_clone_hdlr(struct cmd_args_s *ap)
{
	int			rc = 0;
	int			rc2 = 0;
	int			i = 0;
	daos_cont_info_t	src_cont_info;
	daos_cont_info_t	dst_cont_info;
	daos_obj_id_t		oids[OID_ARR_SIZE];
	daos_anchor_t		anchor;
	uint32_t		oids_nr;
	daos_handle_t		toh;
	daos_epoch_t		epoch;
	struct			dm_args *ca = NULL;
	bool			is_posix_copy = false;
	daos_handle_t		oh;
	daos_handle_t		dst_oh;
	struct file_dfs		src_cp_type = {0};
	struct file_dfs		dst_cp_type = {0};
	char			*src_str = NULL;
	char			*dst_str = NULL;
	size_t			src_str_len = 0;
	size_t			dst_str_len = 0;
	daos_epoch_range_t	epr;

	D_ALLOC(ca, sizeof(struct dm_args));
	if (ca == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	ap->dm_args = ca;

	set_dm_args_default(ca);
	file_set_defaults_dfs(&src_cp_type);
	file_set_defaults_dfs(&dst_cp_type);

	src_str_len = strlen(ap->src);
	D_STRNDUP(src_str, ap->src, src_str_len);
	if (src_str == NULL) {
		rc = -DER_NOMEM;
		DH_PERROR_DER(ap, rc, "Unable to allocate memory for source path");
		D_GOTO(out, rc);
	}
	rc = dm_parse_path(&src_cp_type, src_str, src_str_len, &ca->src_pool, &ca->src_cont);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to parse source path");
		D_GOTO(out, rc);
	}

	dst_str_len = strlen(ap->dst);
	D_STRNDUP(dst_str, ap->dst, dst_str_len);
	if (dst_str == NULL) {
		rc = -DER_NOMEM;
		DH_PERROR_DER(ap, rc, "Unable to allocate memory for destination path");
		D_GOTO(out, rc);
	}
	rc = dm_parse_path(&dst_cp_type, dst_str, dst_str_len, &ca->dst_pool, &ca->dst_cont);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to parse destination path");
		D_GOTO(out, rc);
	}

	if (strlen(ca->dst_cont) != 0) {
		/* make sure destination container does not already exist for object level copies
		 */
		rc = daos_pool_connect(ca->dst_pool, ap->sysname, DAOS_PC_RW, &ca->dst_poh,
				       NULL, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to connect to destination pool");
			D_GOTO(out, rc);
		}
		/* make sure this destination container doesn't exist already,
		 * if it does, exit
		 */
		rc = daos_cont_open(ca->dst_poh, ca->dst_cont, DAOS_COO_RW, &ca->dst_coh,
				    &dst_cont_info, NULL);
		if (rc == 0) {
			fprintf(ap->errstream,
				"This destination container already exists. Please provide a "
				"destination container uuid that does not exist already, or "
				"provide an existing pool or new UNS path of the "
				"form:\n\t--dst </$pool> | <path/to/uns>\n");
			/* disconnect from only destination and exit */
			rc = daos_cont_close(ca->dst_coh, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to close destination container");
				D_GOTO(out, rc);
			}
			rc = daos_pool_disconnect(ca->dst_poh, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc2,
					      "failed to disconnect from destination pool %s",
					      ca->dst_pool);
				D_GOTO(out, rc);
			}
			D_GOTO(out, rc = 1);
		}
	}

	rc = dm_connect(ap, is_posix_copy, &dst_cp_type, &src_cp_type, ca, ap->sysname,
			ap->dst, &src_cont_info, &dst_cont_info);
	if (rc != 0) {
		D_GOTO(out_disconnect, rc);
	}
	rc = daos_cont_create_snap_opt(ca->src_coh, &epoch, NULL,
				       DAOS_SNAP_OPT_CR | DAOS_SNAP_OPT_OIT, NULL);
	if (rc) {
		DH_PERROR_DER(ap, rc, "Failed to create snapshot");
		D_GOTO(out_disconnect, rc);
	}
	rc = daos_oit_open(ca->src_coh, epoch, &toh, NULL);
	if (rc != 0) {
		DH_PERROR_DER(ap, rc, "Failed to open object iterator");
		D_GOTO(out_snap, rc);
	}
	memset(&anchor, 0, sizeof(anchor));
	while (!daos_anchor_is_eof(&anchor)) {
		oids_nr = OID_ARR_SIZE;
		rc = daos_oit_list(toh, oids, &oids_nr, &anchor, NULL);
		if (rc != 0) {
			DH_PERROR_DER(ap, rc, "Failed to list objects");
			D_GOTO(out_oit, rc);
		}

		/* list object ID's */
		for (i = 0; i < oids_nr; i++) {
			rc = daos_obj_open(ca->src_coh, oids[i], DAOS_OO_RW, &oh, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to open source object");
				D_GOTO(out_oit, rc);
			}
			rc = daos_obj_open(ca->dst_coh, oids[i], DAOS_OO_RW, &dst_oh, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to open destination object");
				D_GOTO(err_dst, rc);
			}
			rc = cont_clone_list_dkeys(ap, &oh, &dst_oh);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to list keys");
				D_GOTO(err_obj, rc);
			}
			rc = daos_obj_close(oh, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to close source object");
				D_GOTO(out_oit, rc);
			}
			rc = daos_obj_close(dst_oh, NULL);
			if (rc != 0) {
				DH_PERROR_DER(ap, rc, "Failed to close destination object");
				D_GOTO(err_dst, rc);
			}
		}
	}
	D_GOTO(out_oit, rc);
err_obj:
	rc2 = daos_obj_close(dst_oh, NULL);
	if (rc2 != 0) {
		DH_PERROR_DER(ap, rc2, "Failed to close destination object");
	}
err_dst:
	rc2 = daos_obj_close(oh, NULL);
	if (rc2 != 0) {
		DH_PERROR_DER(ap, rc2, "Failed to close source object");
	}
out_oit:
	rc2 = daos_cont_snap_oit_destroy(ca->src_coh, toh, NULL);
	if (rc2 != 0)
		DH_PERROR_DER(ap, rc2, "Failed to destroy oit");
	rc2 = daos_oit_close(toh, NULL);
	if (rc2 != 0) {
		DH_PERROR_DER(ap, rc2, "Failed to close object iterator");
		D_GOTO(out, rc2);
	}
out_snap:
	epr.epr_lo = epoch;
	epr.epr_hi = epoch;
	rc2 = daos_cont_destroy_snap(ca->src_coh, epr, NULL);
	if (rc2 != 0) {
		DH_PERROR_DER(ap, rc2, "Failed to destroy snapshot");
	}
out_disconnect:
	/* close src and dst pools, conts */
	rc2 = dm_disconnect(ap, is_posix_copy, ca, &src_cp_type, &dst_cp_type);
	if (rc2 != 0) {
		DH_PERROR_DER(ap, rc2, "Failed to disconnect");
	}
out:
	D_FREE(src_str);
	D_FREE(dst_str);
	if (ca) {
		D_FREE(ca->src);
		D_FREE(ca->dst);
	}
	return rc;
}

int
dfuse_count_query(struct cmd_args_s *ap)
{
	struct dfuse_mem_query query = {};
	int                    rc    = -DER_SUCCESS;
	int                    fd;

	fd = open(ap->path, O_NOFOLLOW, O_RDONLY);
	if (fd < 0) {
		rc = errno;
		if (rc != ENOENT)
			DH_PERROR_SYS(ap, rc, "Failed to open path");
		return daos_errno2der(rc);
	}

	query.ino = ap->dfuse_mem.ino;

	rc = ioctl(fd, DFUSE_IOCTL_COUNT_QUERY, &query);
	if (rc < 0) {
		rc = errno;
		if (rc == ENOTTY) {
			rc = -DER_MISC;
		} else {
			DH_PERROR_SYS(ap, rc, "ioctl failed");
			rc = daos_errno2der(errno);
		}
		goto close;
	}

	ap->dfuse_mem.inode_count     = query.inode_count;
	ap->dfuse_mem.fh_count        = query.fh_count;
	ap->dfuse_mem.pool_count      = query.pool_count;
	ap->dfuse_mem.container_count = query.container_count;
	ap->dfuse_mem.found           = query.found;

close:
	close(fd);
	return rc;
}

/* Dfuse cache evict (and helper).
 * Open a path and make a ioctl call for dfuse to evict it.  IF the path is the root then dfuse
 * cannot do this so perform the same over all the top-level directory entries instead.
 */

static int
dfuse_evict_helper(int fd, struct dfuse_mem_query *query)
{
	struct dirent *ent;
	DIR           *dir;
	int            rc = 0;

	dir = fdopendir(fd);
	if (dir == 0) {
		rc = errno;
		return rc;
	}

	while ((ent = readdir(dir)) != NULL) {
		int cfd;

		cfd = openat(fd, ent->d_name, O_NOFOLLOW, O_RDONLY);
		if (cfd < 0) {
			rc = errno;
			goto out;
		}

		rc = ioctl(cfd, DFUSE_IOCTL_DFUSE_EVICT, query);
		close(cfd);
		if (rc < 0) {
			rc = errno;
			goto out;
		}
	}

out:
	closedir(dir);
	return rc;
}

int
dfuse_evict(struct cmd_args_s *ap)
{
	struct dfuse_mem_query query = {};
	struct stat            buf;
	int                    rc = -DER_SUCCESS;
	int                    fd;

	fd = open(ap->path, O_NOFOLLOW, O_RDONLY);
	if (fd < 0) {
		rc = errno;
		DH_PERROR_SYS(ap, rc, "Failed to open path");
		return daos_errno2der(rc);
	}

	rc = fstat(fd, &buf);
	if (rc < 0) {
		rc = errno;
		DH_PERROR_SYS(ap, rc, "Failed to stat file");
		rc = daos_errno2der(rc);
		goto close;
	}

	if (buf.st_ino == 1) {
		rc = dfuse_evict_helper(fd, &query);
		if (rc != 0) {
			DH_PERROR_SYS(ap, rc, "Unable to traverse root");
			rc = daos_errno2der(rc);
			goto close;
		}
		goto out;
	}

	rc = ioctl(fd, DFUSE_IOCTL_DFUSE_EVICT, &query);
	if (rc < 0) {
		rc = errno;
		if (rc == ENOTTY) {
			rc = -DER_MISC;
		} else {
			DH_PERROR_SYS(ap, rc, "ioctl failed");
			rc = daos_errno2der(errno);
		}
		goto close;
	}

	ap->dfuse_mem.ino = buf.st_ino;
out:
	ap->dfuse_mem.inode_count     = query.inode_count;
	ap->dfuse_mem.fh_count        = query.fh_count;
	ap->dfuse_mem.pool_count      = query.pool_count;
	ap->dfuse_mem.container_count = query.container_count;

close:
	close(fd);
	return rc;
}
