/**
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * DAOS Unified namespace functionality.
 */

#define D_LOGFAC	DD_FAC(duns)

#include <dirent.h>
#include <libgen.h>
#include <sys/xattr.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <dlfcn.h>
#include <regex.h>
#include <pwd.h>
#ifdef LUSTRE_INCLUDE
#include <lustre/lustreapi.h>
#include <linux/lustre/lustre_idl.h>
#endif
#include <daos/common.h>
#include <daos/object.h>
#include "dfuse_ioctl.h"
#include "daos_types.h"
#include "daos.h"
#include "daos_fs.h"
#include "daos_uns.h"

#ifndef FUSE_SUPER_MAGIC
#define FUSE_SUPER_MAGIC	0x65735546
#endif

#ifdef LUSTRE_INCLUDE
#define LIBLUSTRE		"liblustreapi.so"

static bool liblustre_notfound;
/* need to protect against concurrent/multi-threaded attempts to bind ? */
static bool liblustre_binded;
static int (*dir_create_foreign)(const char *, mode_t, __u32, __u32,
				 const char *) = NULL;
static int (*file_create_foreign)(const char *, mode_t, __u32, __u32,
				 const char *) = NULL;
static int (*unlink_foreign)(char *);

static int
bind_liblustre()
{
	void *lib;

	/* bind to lustre lib/API */
	lib = dlopen(LIBLUSTRE, RTLD_NOW);
	if (lib == NULL) {
		liblustre_notfound = true;
		D_ERROR("unable to locate/bind %s, dlerror() says '%s', "
			"reverting to non-lustre behavior.\n",
			LIBLUSTRE, dlerror());
		return EINVAL;
	}

	D_DEBUG(DB_TRACE, "%s has been found and dynamically binded !\n",
		LIBLUSTRE);

	/* now try to map the API methods we need */
	dir_create_foreign = (int (*)(const char *, mode_t, __u32, __u32,
			      const char *))dlsym(lib,
						  "llapi_dir_create_foreign");
	if (dir_create_foreign == NULL) {
		liblustre_notfound = true;
		D_ERROR("unable to resolve llapi_dir_create_foreign symbol, "
			"dlerror() says '%s', Lustre version do not seem to "
			"support foreign LOV/LMV, reverting to non-lustre "
			"behavior.\n", dlerror());
		return EINVAL;
	}

	D_DEBUG(DB_TRACE, "llapi_dir_create_foreign() resolved at %p\n",
		dir_create_foreign);

	file_create_foreign = (int (*)(const char *, mode_t, __u32, __u32,
			      const char *))dlsym(lib,
						  "llapi_file_create_foreign");
	if (file_create_foreign == NULL) {
		liblustre_notfound = true;
		D_ERROR("unable to resolve llapi_file_create_foreign symbol, "
			"dlerror() says '%s', Lustre version do not seem to "
			"support foreign LOV/LMV, reverting to non-lustre "
			"behavior.\n", dlerror());
		return EINVAL;
	}

	D_DEBUG(DB_TRACE, "llapi_file_create_foreign() resolved at %p\n",
		file_create_foreign);

	unlink_foreign = (int (*)(char *))dlsym(lib,
						      "llapi_unlink_foreign");
	if (unlink_foreign == NULL) {
		liblustre_notfound = true;
		D_ERROR("unable to resolve llapi_unlink_foreign symbol, "
			"dlerror() says '%s', Lustre version do not seem to "
			"support foreign daos type, reverting to non-lustre "
			"behavior.\n", dlerror());
		return EINVAL;
	}

	D_DEBUG(DB_TRACE, "llapi_unlink_foreign() resolved at %p\n",
		unlink_foreign);

	liblustre_binded = true;
	return 0;
}

static int
duns_resolve_lustre_path(const char *path, struct duns_attr_t *attr)
{
	char			str[DUNS_MAX_XATTR_LEN + 1];
	char			*saveptr, *t;
	char			*buf;
	struct lmv_user_md	*lum;
	/* both foreign LOV/LMV have same format */
	struct lmv_foreign_md	*lfm;
	int			fd;
	int			rc;

	/* XXX if a Posix container is not always mapped with a daos foreign dir
	 * with LMV, both LOV and LMV will need to be queried if ENODATA is
	 * returned at 1st, as the file/dir type is hidden to help decide before
	 * due to the symlink fake !!
	 */

	/* XXX if liblustreapi is not binded, do it now ! */
	if (liblustre_binded == false && liblustre_notfound == false) {
		rc = bind_liblustre();
		if (rc)
			return rc;
	} else if (liblustre_notfound == true) {
		/* no liblustreapi.so found, or incompatible */
		return EINVAL;
	} else if (liblustre_binded == false) {
		/* this should not happen */
		D_ERROR("liblustre_notfound == false && liblustre_notfound == "
			"false not expected after bind_liblustre()\n");
		return EINVAL;
	}

	D_DEBUG(DB_TRACE, "Trying to retrieve associated container's infos "
		"from Lustre path '%s'\n", path);

	/* get LOV/LMV
	 * llapi_getstripe() API can not be used here as it frees
	 * param.[fp_lmv_md, fp_lmd->lmd_lmm]  buffer for LOV/LMV after printing
	 * its content, raw/ioctl() way must be used then
	 */
	buf = calloc(1, XATTR_SIZE_MAX);
	if (buf == NULL) {
		D_ERROR("unable to allocate XATTR_SIZE_MAX to get LOV/LMV "
			"for '%s', errno %d(%s).\n", path, errno,
			strerror(errno));
		return ENOMEM;
	}
	fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (fd == -1 && errno != ENOTDIR) {
		D_ERROR("unable to open '%s' errno %d(%s).\n", path, errno,
			strerror(errno));
		return errno;
	} else if (errno == ENOTDIR) {
		/* it is a file so get LOV ! */
		fd = open(path, O_RDONLY | O_NOFOLLOW);
		if (fd == -1) {
			int err = errno;

			D_ERROR("unable to open '%s' errno %d(%s).\n", path, errno,
				strerror(errno));
			return err;
		}

		rc = ioctl(fd, LL_IOC_LOV_GETSTRIPE, buf);
		if (rc != 0) {
			int err = errno;

			D_ERROR("ioctl(LL_IOC_LOV_GETSTRIPE) failed, rc: %d, "
				"errno %d(%s).\n", rc, errno, strerror(errno));
			return err;
		}

		lfm = (struct lmv_foreign_md *)buf;
		/* sanity check */
		if (lfm->lfm_magic != LOV_MAGIC_FOREIGN  ||
		    lfm->lfm_type != LU_FOREIGN_TYPE_SYMLINK ||
		    lfm->lfm_length > DUNS_MAX_XATTR_LEN ||
		    snprintf(str, DUNS_MAX_XATTR_LEN, "%s",
			     lfm->lfm_value) > DUNS_MAX_XATTR_LEN) {
			D_ERROR("Invalid DAOS LOV format (%s).\n", str);
			return EINVAL;
		}
	} else {
		/* it is a dir so get LMV ! */
		lum = (struct lmv_user_md *)buf;
		/* to get LMV and not default one !! */
		lum->lum_magic = LMV_MAGIC_V1;
		/* to confirm we have already a buffer large enough get a
		 * BIG LMV !!
		 */
		lum->lum_stripe_count = (XATTR_SIZE_MAX -
					sizeof(struct lmv_user_md)) /
					sizeof(struct lmv_user_mds_data);
		rc = ioctl(fd, LL_IOC_LMV_GETSTRIPE, buf);
		if (rc != 0) {
			int err = errno;

			D_ERROR("ioctl(LL_IOC_LMV_GETSTRIPE) failed, rc: %d, "
				"errno %d(%s).\n", rc, errno, strerror(errno));
			return err;
		}

		lfm = (struct lmv_foreign_md *)buf;
		/* sanity check */
		if (lfm->lfm_magic != LMV_MAGIC_FOREIGN  ||
		    lfm->lfm_type != LU_FOREIGN_TYPE_SYMLINK ||
		    lfm->lfm_length > DUNS_MAX_XATTR_LEN ||
		    snprintf(str, DUNS_MAX_XATTR_LEN, "%s",
			     lfm->lfm_value) > DUNS_MAX_XATTR_LEN) {
			D_ERROR("Invalid DAOS LMV format (%s).\n", str);
			return EINVAL;
		}
	}

	attr->da_type = (daos_cont_layout_t)lfm->lfm_flags;
	if (attr->da_type == DAOS_PROP_CO_LAYOUT_UNKNOWN) {
		D_ERROR("Invalid DAOS LOV/LMV format: Container layout cannot be"
			" unknown\n");
		return EINVAL;
	}

	t = strtok_r(str, "/", &saveptr);
	if (t == NULL) {
		D_ERROR("Invalid DAOS LOV/LMV format (%s).\n", str);
		return EINVAL;
	}

	/** da_puuid is deprecated, but in case users are still using it parse the uuid there */
	rc = uuid_parse(t, attr->da_puuid);
	if (rc) {
		D_ERROR("Invalid DAOS LOV/LMV format: pool UUID cannot be parsed\n");
		return EINVAL;
	}
	strncpy(attr->da_pool, t, DAOS_PROP_LABEL_MAX_LEN);
	attr->da_pool[DAOS_PROP_LABEL_MAX_LEN] = '\0';

	t = strtok_r(NULL, "/", &saveptr);
	if (t == NULL) {
		D_ERROR("Invalid DAOS LOV/LMV format (%s).\n", str);
		return EINVAL;
	}

	/** da_cuuid is deprecated, but in case users are still using it parse the uuid there */
	rc = uuid_parse(t, attr->da_cuuid);
	if (rc) {
		D_ERROR("Invalid DAOS LMV format: container UUID cannot be parsed\n");
		return EINVAL;
	}
	strncpy(attr->da_cont, t, DAOS_PROP_LABEL_MAX_LEN);
	attr->da_cont[DAOS_PROP_LABEL_MAX_LEN] = '\0';

	/* path is DAOS-foreign and will need to be unlinked using
	 * unlink_foreign API
	 */
	attr->da_on_lustre = true;

	return 0;
}
#endif

#define UUID_REGEX "([a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}){1}"
/** 127 corresponds to DAOS_PROP_LABEL_MAX_LEN. */
#define LABEL_REGEX "([a-zA-Z0-9._:]{1,127})"
#define DAOS_FORMAT "^daos://("UUID_REGEX"|"LABEL_REGEX")/("UUID_REGEX"|"LABEL_REGEX")(/.*)?$"
#define DAOS_FORMAT_NO_PREFIX "^[/]+("UUID_REGEX")/("UUID_REGEX")(/.*)?$"
#define DAOS_FORMAT_NO_CONT "^daos://("UUID_REGEX"|"LABEL_REGEX")[/]?$"

static int
check_direct_format(const char *path, bool no_prefix, bool *pool_only)
{
	regex_t regx;
	int	rc;

	if (no_prefix)
		rc = regcomp(&regx, DAOS_FORMAT_NO_PREFIX, REG_EXTENDED | REG_ICASE);
	else
		rc = regcomp(&regx, DAOS_FORMAT, REG_EXTENDED | REG_ICASE);
	if (rc)
		return EINVAL;

	rc = regexec(&regx, path, 0, NULL, 0);
	regfree(&regx);
	if (rc == 0) {
		*pool_only = false;
		return rc;
	} else if (rc != REG_NOMATCH) {
		return rc;
	}

	rc = regcomp(&regx, DAOS_FORMAT_NO_CONT, REG_EXTENDED | REG_ICASE);
	if (rc)
		return EINVAL;

	rc = regexec(&regx, path, 0, NULL, 0);
	if (rc == 0)
		*pool_only = true;

	regfree(&regx);
	return rc;
}

static int
parse_path(const char *path, size_t path_len, size_t *cur_end_idx,
	   size_t *rel_len, char *dir_path, char *rel_path)
{
	size_t	dir_name_len = 0;
	size_t	i;
	size_t	slash_idx = 0;

	if (path == NULL)
		return EINVAL;

	/** Find end, not including trailing slashes */
	for (i = *cur_end_idx - 1; i > 0; i--) {
		if (path[i] != '/')
			break;
	}

	/** Find last slash */
	for (; i > 0; i--) {
		if (path[i] == '/') {
			slash_idx = i;
			break;
		}
	}

	/** Copy the dirname */
	if (slash_idx == 0)
		dir_name_len = 1;
	else
		dir_name_len = slash_idx;

	/** Copy the remaining path rel_path */
	*rel_len = path_len - dir_name_len + 1;
	if (*rel_len > 0) {
		strncpy(rel_path, path + slash_idx, *rel_len);
		rel_path[*rel_len] = '\0';
	}

	strncpy(dir_path, path, dir_name_len);
	dir_path[dir_name_len] = '\0';

	*cur_end_idx = slash_idx;
	return 0;
}

static int
resolve_direct_path(const char *path, struct duns_attr_t *attr, bool no_prefix, bool pool_only)
{
	char	*saveptr, *t;
	char	*dir;
	int	rc = 0;

	D_STRNDUP(dir, path, PATH_MAX);
	if (dir == NULL)
		return ENOMEM;

	t = strtok_r(dir, "/", &saveptr);
	if (t == NULL) {
		D_ERROR("Invalid DAOS format (%s).\n", path);
		D_GOTO(err, rc = EINVAL);
	}

	/** if there is a daos: prefix, skip over it */
	if (!no_prefix) {
		t = strtok_r(NULL, "/", &saveptr);
		if (t == NULL) {
			D_ERROR("Invalid DAOS format (%s).\n", path);
			D_GOTO(err, rc = EINVAL);
		}
	}

	/** Deprecated - parse the pool uuid */
	rc = uuid_parse(t, attr->da_puuid);
	if (rc) {
		rc = 0;
		if (!daos_label_is_valid(t))
			D_GOTO(err, rc = EINVAL);
	}
	strncpy(attr->da_pool, t, DAOS_PROP_LABEL_MAX_LEN);
	attr->da_pool[DAOS_PROP_LABEL_MAX_LEN] = '\0';

	if (pool_only) {
		D_FREE(dir);
		return 0;
	}

	t = strtok_r(NULL, "/", &saveptr);
	if (t == NULL) {
		D_ERROR("Invalid DAOS format (%s).\n", path);
		D_GOTO(err, rc = EINVAL);
	}

	/** Deprecated - parse the container uuid */
	rc = uuid_parse(t, attr->da_cuuid);
	if (rc) {
		rc = 0;
		/** redundant, but to make sure property check is OK */
		if (!daos_label_is_valid(t))
			D_GOTO(err, rc = EINVAL);
	}
	strncpy(attr->da_cont, t, DAOS_PROP_LABEL_MAX_LEN);
	attr->da_cont[DAOS_PROP_LABEL_MAX_LEN] = '\0';

	/** if there is a relative path, parse it out */
	t = strtok_r(NULL, "", &saveptr);
	if (t != NULL) {
		D_ASPRINTF(attr->da_rel_path, "/%s", t);
		if (attr->da_rel_path == NULL)
			D_GOTO(err, rc = ENOMEM);
	}

out:
	D_FREE(dir);
	return rc;
err:
	duns_destroy_attr(attr);
	goto out;
}

int
duns_resolve_path(const char *path, struct duns_attr_t *attr)
{
	ssize_t		s;
	char		str[DUNS_MAX_XATTR_LEN];
	struct statfs	fs;
	bool		pool_only = false;
	bool		no_prefix = false;
	char		*realp = NULL;
	char		*rel_path = NULL;
	char		*dir_path = NULL;
	size_t		path_len;
	size_t		cur_idx;
	size_t		rel_len = 0;
	int		rc;
#ifdef LUSTRE_INCLUDE
	char *dir = NULL;
#endif

	if (path == NULL || strlen(path) == 0)
		return EINVAL;
	if (attr == NULL)
		return EINVAL;
	if (attr->da_no_prefix || attr->da_flags & DUNS_NO_PREFIX)
		no_prefix = true;

	/** for now just set this to NULL to use the default DAOS system */
	attr->da_sys = NULL;

	/**
	 * If caller requested to not check the file system path, we do the
	 * direct format parsing right away regardless of the format.
	 */
	if (attr->da_flags & DUNS_NO_CHECK_PATH ||
	    check_direct_format(path, no_prefix, &pool_only) == 0) {
		D_DEBUG(DB_TRACE, "DUNS resolve to direct path: %s\n", path);
		return resolve_direct_path(path, attr, no_prefix, pool_only);
	}

	/** no match for direct format, do the UNS fs check */

	rc = statfs(path, &fs);

#ifdef LUSTRE_INCLUDE
	/* since statfs follows symlinks, may need to use directory to
	 * determine FS type, if on Lustre and path is a foreign symlink
	 */
	if (rc == -1 || fs.f_type != LL_SUPER_MAGIC) {
		int		errno_save = errno;
		struct statfs	fs2;
		char		*dirp;
		int		rc2;

		D_STRNDUP(dir, path, PATH_MAX);
		if (dir == NULL)
			return ENOMEM;

		dirp = dirname(dir);
		/* need to statfs() at the directory level to catch the
		 * case where leaf of path is a Lustre foreign symlink
		 * which would have acted as a real symlink for previous
		 * statfs()
		 */
		rc2 = statfs(dirp, &fs2);
		if (rc2 == 0 && fs2.f_type == LL_SUPER_MAGIC) {
			rc2 = duns_resolve_lustre_path(path, attr);
			if (rc2 == 0)
				D_GOTO(out, rc = rc2);

			/* if Lustre specific method fails, fallback to try the
			 * normal way...
			 */
		}
		errno = errno_save;
	}
#endif

	if (rc == -1) {
		int err = errno;

		D_INFO("Failed to statfs %s: %s\n", path, strerror(errno));
		D_GOTO(out, rc = err);
	}

	D_REALPATH(realp, path);
	if (realp == NULL)
		D_GOTO(out, rc = errno);

	path_len = strnlen(realp, PATH_MAX);
	if (path_len > PATH_MAX - 1)
		D_GOTO(out, rc = ENAMETOOLONG);

	D_ALLOC(rel_path, path_len + 1);
	if (rel_path == NULL)
		D_GOTO(out, rc = ENOMEM);

	D_STRNDUP(dir_path, realp, path_len);
	if (dir_path == NULL)
		D_GOTO(out, rc = ENOMEM);

	cur_idx = path_len;

	while (1) {
		s = lgetxattr(dir_path, DUNS_XATTR_NAME, &str,
			      DUNS_MAX_XATTR_LEN);
		if (s < 0 || s > DUNS_MAX_XATTR_LEN) {
			int err = errno;

			if (err == ENODATA) {
				if (cur_idx == 0 || (attr->da_flags &
						     DUNS_NO_REVERSE_LOOKUP))
					D_INFO("Path does not represent a DAOS link\n");
				else
					goto parse;
			} else if (err == ENOTSUP) {
				D_INFO("Path is not in a filesystem that"
				       " supports the DAOS unified namespace\n");
			} else if (s > DUNS_MAX_XATTR_LEN) {
				err = EIO;
				D_ERROR("Invalid xattr length\n");
			} else {
				D_ERROR("Invalid DAOS unified namespace xattr:"
					" %s\n", strerror(err));
			}

			D_GOTO(out, rc = err);
		}

		/** On success, parse the attribute */
		rc = duns_parse_attr(&str[0], s, attr);
		if (rc) {
			D_ERROR("Invalid xattr format: %d (%s)\n", rc, strerror(rc));
			D_GOTO(out, rc);
		}
		/** if the xattr parsing succeeds, break */
		break;
parse:
		rc = parse_path(realp, path_len, &cur_idx, &rel_len, dir_path, rel_path);
		if (rc) {
			D_ERROR("Failed to parse %s (%s)\n",
				path, strerror(rc));
			D_GOTO(out, rc);
		}
	}

	if (cur_idx != path_len) {
		D_ASSERT(rel_path);
		D_STRNDUP(attr->da_rel_path, rel_path, rel_len);
		if (attr->da_rel_path == NULL)
			D_GOTO(out, rc = ENOMEM);
	}

out:
#ifdef LUSTRE_INCLUDE
	D_FREE(dir);
#endif
	D_FREE(rel_path);
	D_FREE(dir_path);
	D_FREE(realp);
	return rc;
}

int
duns_parse_attr(char *str, daos_size_t len, struct duns_attr_t *attr)
{
	char	*local;
	char	*saveptr = NULL, *t;
	int	rc;

	D_STRNDUP(local, str, len);
	if (!local)
		return ENOMEM;

	t = strtok_r(local, ".", &saveptr);
	if (t == NULL) {
		D_ERROR("Invalid DAOS xattr format (%s).\n", str);
		D_GOTO(err, rc = EINVAL);
	}

	t = strtok_r(NULL, ":", &saveptr);
	if (t == NULL) {
		D_ERROR("Invalid DAOS xattr format (%s).\n", str);
		D_GOTO(err, rc = EINVAL);
	}
	daos_parse_ctype(t, &attr->da_type);
	if (attr->da_type == DAOS_PROP_CO_LAYOUT_UNKNOWN) {
		D_ERROR("Invalid DAOS xattr format: Container layout cannot be unknown\n");
		D_GOTO(err, rc = EINVAL);
	}

	t = strtok_r(NULL, "/", &saveptr);
	if (t == NULL) {
		D_ERROR("Invalid DAOS xattr format (%s).\n", str);
		D_GOTO(err, rc = EINVAL);
	}

	/** da_puuid is deprecated, but in case users are still using it parse the uuid there */
	rc = uuid_parse(t, attr->da_puuid);
	if (rc) {
		D_ERROR("Invalid DAOS xattr format: pool UUID cannot be parsed\n");
		D_GOTO(err, rc = EINVAL);
	}
	strncpy(attr->da_pool, t, DAOS_PROP_LABEL_MAX_LEN);
	attr->da_pool[DAOS_PROP_LABEL_MAX_LEN] = '\0';

	t = strtok_r(NULL, "/", &saveptr);
	if (t == NULL) {
		D_ERROR("Invalid DAOS xattr format (%s).\n", str);
		D_GOTO(err, rc = EINVAL);
	}

	/** da_cuuid is deprecated, but in case users are still using it parse the uuid there */
	rc = uuid_parse(t, attr->da_cuuid);
	if (rc) {
		D_ERROR("Invalid DAOS xattr format: container UUID cannot be parsed\n");
		D_GOTO(err, rc = EINVAL);
	}
	strncpy(attr->da_cont, t, DAOS_PROP_LABEL_MAX_LEN);
	attr->da_cont[DAOS_PROP_LABEL_MAX_LEN] = '\0';

	rc = 0;
err:
	D_FREE(local);
	return rc;
}

/* Setup any container ACLs so multi-user dfuse can access it.
 *
 * Returns a system error code.
 */
static int
duns_set_fuse_acl(uid_t uid, daos_handle_t coh)
{
	int              rc = 0;
	struct daos_acl *acl;
	struct daos_ace *ace;
	char            *name;

	if (geteuid() == uid)
		return 0;

	rc = daos_acl_uid_to_principal(uid, &name);
	if (rc != 0)
		return daos_der2errno(rc);

	ace = daos_ace_create(DAOS_ACL_USER, name);
	if (ace == NULL) {
		D_ERROR("daos_ace_create() failed.\n");
		D_GOTO(out_name, rc = EIO);
	}

	ace->dae_access_types = DAOS_ACL_ACCESS_ALLOW;
	ace->dae_allow_perms  = DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE | DAOS_ACL_PERM_GET_PROP;

	acl = daos_acl_create(&ace, 1);
	if (acl == NULL)
		D_GOTO(out_ace, rc = EIO);

	rc = daos_cont_update_acl(coh, acl, NULL);
	if (rc) {
		D_ERROR("daos_cont_update_acl() failed, " DF_RC "\n", DP_RC(rc));
		rc = daos_der2errno(rc);
	}

	daos_acl_free(acl);

out_ace:
	daos_ace_free(ace);
out_name:
	D_FREE(name);
	return rc;
}

static int
create_cont(daos_handle_t poh, struct duns_attr_t *attrp, bool create_with_label,
	    struct dfuse_user_reply *dur)
{
	int rc;

	if (attrp->da_type == DAOS_PROP_CO_LAYOUT_POSIX) {
		dfs_attr_t     dfs_attr = {};
		daos_handle_t  coh;
		daos_handle_t *ch = NULL;

		if (dur)
			ch = &coh;

		/** TODO: set Lustre FID here. */
		dfs_attr.da_id = 0;
		dfs_attr.da_oclass_id = attrp->da_oclass_id;
		dfs_attr.da_dir_oclass_id = attrp->da_dir_oclass_id;
		dfs_attr.da_file_oclass_id = attrp->da_file_oclass_id;
		dfs_attr.da_chunk_size = attrp->da_chunk_size;
		dfs_attr.da_props = attrp->da_props;
		if (attrp->da_hints[0] != 0)
			memcpy(dfs_attr.da_hints, attrp->da_hints, DAOS_CONT_HINT_MAX_LEN);
		if (create_with_label)
			rc = dfs_cont_create_with_label(poh, attrp->da_cont, &dfs_attr,
							&attrp->da_cuuid, ch, NULL);
		else
			rc = dfs_cont_create(poh, &attrp->da_cuuid, &dfs_attr, ch, NULL);
		if (rc == -DER_SUCCESS && dur) {
			int rc2;

			rc  = duns_set_fuse_acl(dur->uid, coh);
			rc2 = daos_cont_close(coh, NULL);
			if (rc2 != -DER_SUCCESS) {
				D_ERROR("failed to close container: " DF_RC "\n", DP_RC(rc2));
				if (rc2 == -DER_NOMEM)
					daos_cont_close(coh, NULL);
			}
		}
	} else {
		daos_prop_t	*prop;
		int		 nr = 1;

		if (attrp->da_props != NULL)
			nr = attrp->da_props->dpp_nr + 1;

		prop = daos_prop_alloc(nr);
		if (prop == NULL) {
			D_ERROR("Failed to allocate container prop.\n");
			return ENOMEM;
		}
		if (attrp->da_props != NULL) {
			rc = daos_prop_copy(prop, attrp->da_props);
			if (rc) {
				daos_prop_free(prop);
				D_ERROR("failed to copy properties "DF_RC"\n", DP_RC(rc));
				return daos_der2errno(rc);
			}
		}
		prop->dpp_entries[prop->dpp_nr - 1].dpe_type = DAOS_PROP_CO_LAYOUT_TYPE;
		prop->dpp_entries[prop->dpp_nr - 1].dpe_val = attrp->da_type;
		if (create_with_label)
			rc = daos_cont_create_with_label(poh, attrp->da_cont, prop,
							 &attrp->da_cuuid, NULL);
		else
			rc = daos_cont_create(poh, &attrp->da_cuuid, prop, NULL);
		if (rc)
			rc = daos_der2errno(rc);
		daos_prop_free(prop);
	}
	if (rc == 0 && !create_with_label)
		uuid_unparse(attrp->da_cuuid, attrp->da_cont);
	return rc;
}

#ifdef LUSTRE_INCLUDE
static int
duns_create_lustre_path(daos_handle_t poh, daos_pool_info_t info, const char *path,
			mode_t mode, struct duns_attr_t *attrp)
{
	char			str[DUNS_MAX_XATTR_LEN + 1];
	int			len;
	int			rc, rc2;

	/* XXX pool must already be created, and associated DFuse-mount should already be mounted */

	/* XXX if liblustreapi is not binded, do it now ! */
	if (liblustre_binded == false && liblustre_notfound == false) {
		rc = bind_liblustre();
		if (rc)
			return EINVAL;
	}

	uuid_unparse(info.pi_uuid, attrp->da_pool);

	/* create container */
	rc = create_cont(poh, attrp, false, NULL);
	if (rc) {
		D_ERROR("Failed to create container (%s)\n", strerror(rc));
		D_GOTO(err, rc);
	}

	/** create file/dir and store the daos attributes in the path LOV/LMV */
	len = snprintf(str, DUNS_MAX_XATTR_LEN, DUNS_LUSTRE_XATTR_FMT,
		       attrp->da_pool, attrp->da_cont);
	if (len < 0) {
		D_ERROR("Failed to create foreign LOV/LMV value\n");
		D_GOTO(err_cont, rc = EINVAL);
	}

	if (attrp->da_type == DAOS_PROP_CO_LAYOUT_POSIX) {
		rc = (*dir_create_foreign)(path, mode, LU_FOREIGN_TYPE_SYMLINK,
					   (__u32)attrp->da_type, str);
		if (rc) {
			D_ERROR("Failed to create Lustre dir '%s' with foreign "
				"LMV '%s' (rc = %d).\n", path, str, rc);
			D_GOTO(err_cont, rc = EINVAL);
		}
	} else {
		rc = (*file_create_foreign)(path, mode, LU_FOREIGN_TYPE_SYMLINK,
					    (__u32)attrp->da_type, str);
		if (rc < 0) {
			/* llapi_file_crate_foreign() returns fd upon success but
			 * -errno upon error
			 */
			D_ERROR("Failed to create Lustre file '%s' with foreign "
				"LOV '%s' (rc = %d).\n", path, str, rc);
			D_GOTO(err_cont, rc = EINVAL);
		} else {
			rc = 0;
		}
	}

	return rc;

err_cont:
	rc2 = daos_cont_destroy(poh, attrp->da_cont, 1, NULL);
	if (rc2)
		D_ERROR("Failed to cleanup created container %s "DF_RC"\n", attrp->da_cont,
			DP_RC(rc2));
err:
	return rc;
}

static int
duns_link_lustre_path(const char *pool, const char *cont, daos_cont_layout_t type,
		      const char *path, mode_t mode)
{
	char			str[DUNS_MAX_XATTR_LEN + 1];
	int			len;
	int			rc, rc2;

	/* XXX if liblustreapi is not binded, do it now ! */
	if (liblustre_binded == false && liblustre_notfound == false) {
		rc = bind_liblustre();
		if (rc)
			return EINVAL;
	}

	/** create file/dir and store the daos attributes in the path LOV/LMV */
	len = snprintf(str, DUNS_MAX_XATTR_LEN, DUNS_LUSTRE_XATTR_FMT, pool, cont);
	if (len < 0) {
		D_ERROR("Failed to create foreign LOV/LMV value\n");
		return EINVAL;
	}

	if (type == DAOS_PROP_CO_LAYOUT_POSIX) {
		rc = (*dir_create_foreign)(path, mode, LU_FOREIGN_TYPE_SYMLINK, (__u32)type, str);
		if (rc) {
			D_ERROR("Failed to create Lustre dir '%s' with foreign "
				"LMV '%s' (rc = %d).\n", path, str, rc);
			return EINVAL;
		}
	} else {
		rc = (*file_create_foreign)(path, mode, LU_FOREIGN_TYPE_SYMLINK, (__u32)type, str);
		if (rc < 0) {
			/* llapi_file_crate_foreign() returns fd upon success but
			 * -errno upon error
			 */
			D_ERROR("Failed to create Lustre file '%s' with foreign "
				"LOV '%s' (rc = %d).\n", path, str, rc);
			return EINVAL;
		}
		rc = 0;
	}

	return rc;
}
#endif

int
duns_create_path(daos_handle_t poh, const char *path, struct duns_attr_t *attrp)
{
	char			type[10];
	daos_pool_info_t	info = {0};
	char			str[DUNS_MAX_XATTR_LEN];
	int			len;
	bool			no_prefix = false;
	bool			backend_dfuse = false;
	bool			pool_only;
	size_t			path_len;
	int			rc, rc2;
	struct dfuse_user_reply dur = {};

	if (path == NULL) {
		D_ERROR("Invalid path\n");
		return EINVAL;
	}

	path_len = strnlen(path, PATH_MAX);

	if (attrp->da_no_prefix || attrp->da_flags & DUNS_NO_PREFIX)
		no_prefix = true;

	rc = check_direct_format(path, no_prefix, &pool_only);
	if (rc == 0) {
		if (pool_only) {
			D_ERROR("Invalid DUNS format: %s\n", path);
			return EINVAL;
		}

		rc = resolve_direct_path(path, attrp, no_prefix, pool_only);
		if (rc) {
			D_ERROR("Failed to resolve direct path '%s': %d (%s)\n", path, rc,
				strerror(rc));
			return rc;
		}

		if (daos_label_is_valid(attrp->da_cont))
			rc = create_cont(poh, attrp, true, NULL);
		else
			rc = create_cont(poh, attrp, false, NULL);
		if (rc)
			D_ERROR("Failed to create container: %d (%s)\n", rc, strerror(rc));
		return rc;
	}

	rc = daos_pool_query(poh, NULL, &info, NULL, NULL);
	if (rc) {
		D_ERROR("Failed to query pool info" DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	if (!uuid_is_null(attrp->da_puuid)) {
		if (uuid_compare(attrp->da_puuid, info.pi_uuid) != 0) {
			D_ERROR("Pool open handle must match the passed in uuid\n");
			return EINVAL;
		}
	}

	if (attrp->da_type == DAOS_PROP_CO_LAYOUT_POSIX) {
		struct statfs	fs;
		char		*dir, *dirp;
		mode_t		mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;

		D_STRNDUP(dir, path, path_len);
		if (dir == NULL)
			return ENOMEM;

		dirp = dirname(dir);
		rc = statfs(dirp, &fs);
		if (rc == -1) {
			int err = errno;

			D_ERROR("Failed to statfs dir %s: %d (%s)\n", dirp, err, strerror(err));
			D_FREE(dir);
			return err;
		}

#ifdef LUSTRE_INCLUDE
		if (fs.f_type == LL_SUPER_MAGIC) {
			rc = duns_create_lustre_path(poh, info, path, mode, attrp);
			if (rc == 0)
				return 0;
			/* if Lustre specific method fails, fallback to try the normal way... */
		}
#endif

		/** create a new directory if POSIX/MPI-IO container */
		rc = mkdir(path, mode);
		if (rc == -1) {
			rc = errno;

			D_ERROR("Failed to create dir %s: %d (%s)\n", path, rc, strerror(rc));
			D_FREE(dir);
			return rc;
		}

		/* Open the parent directory for the new container so we can call a dfuse iotcl
		 * to discover the user running dfuse.
		 */
		if (fs.f_type == FUSE_SUPER_MAGIC) {
			int fd;

			fd = open(dirp, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
			if (fd == -1) {
				int err = errno;

				D_ERROR("Dfuse open failed '%s': %d (%s)\n", dirp, err,
					strerror(err));
				D_FREE(dir);
				return err;
			}

			rc = ioctl(fd, DFUSE_IOCTL_DFUSE_USER, &dur);
			close(fd);
			if (rc == -1) {
				int err = errno;

				D_ERROR("Dfuse ioctl failed %s: %d (%s)\n", dirp, err,
					strerror(err));
				D_FREE(dir);
				return err;
			}
			backend_dfuse = true;
		}
		D_FREE(dir);
	} else if (attrp->da_type != DAOS_PROP_CO_LAYOUT_UNKNOWN) {
		/** create a new file for other container types */
		int fd;
		mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
#ifdef LUSTRE_INCLUDE
		struct statfs   fs;
		char            *dir, *dirp;

		D_STRNDUP(dir, path, path_len);
		if (dir == NULL)
			return ENOMEM;

		dirp = dirname(dir);
		rc = statfs(dirp, &fs);
		if (rc == -1) {
			int err = errno;

			D_ERROR("Failed to statfs dir %s: %d (%s)\n", dirp, err, strerror(err));
			D_FREE(dir);
			return err;
		}
		D_FREE(dir);

		if (fs.f_type == LL_SUPER_MAGIC) {
			rc = duns_create_lustre_path(poh, info, path, mode, attrp);
			if (rc == 0)
				return 0;
			/* if Lustre specific method fails, fallback to try the normal way... */
		}
#endif

		fd = open(path, O_CREAT | O_EXCL, mode);
		if (fd == -1) {
			rc = errno;

			D_ERROR("Failed to create file %s: %d (%s)\n", path, rc, strerror(rc));
			return rc;
		}
		close(fd);
	} else {
		D_ERROR("Invalid container layout.\n");
		return EINVAL;
	}

	uuid_unparse(info.pi_uuid, attrp->da_pool);
	daos_unparse_ctype(attrp->da_type, type);

	/** Create container */
	rc = create_cont(poh, attrp, false, backend_dfuse ? &dur : NULL);
	if (rc) {
		D_ERROR("Failed to create container: %d (%s)\n", rc, strerror(rc));
		D_GOTO(err_link, rc);
	}

	/** store the daos attributes in the path xattr */
	len =
	    snprintf(str, DUNS_MAX_XATTR_LEN, DUNS_XATTR_FMT, type, attrp->da_pool, attrp->da_cont);
	if (len < 0) {
		D_ERROR("Failed to create xattr value\n");
		D_GOTO(err_cont, rc = EINVAL);
	}
	rc = lsetxattr(path, DUNS_XATTR_NAME, str, len + 1, 0);
	if (rc) {
		rc = errno;
		if (rc == ENOTSUP) {
			D_INFO("Path is not in a filesystem that supports the DAOS unified "
			       "namespace\n");
		} else {
			D_ERROR("Failed to set DAOS xattr: %d (%s)\n", rc, strerror(rc));
		}
		goto err_cont;
	}
	if (backend_dfuse) {
		struct stat finfo;
		/*
		 * This next stat will cause dfuse to lookup the entry point and perform a
		 * container connect, therefore this data will be read from root of the new
		 * container, not the directory.
		 *
		 * TODO: This could call getxattr to verify success.
		 */
		rc = stat(path, &finfo);
		if (rc) {
			rc = errno;
			D_ERROR("Failed to access new container: %d (%s)\n", rc, strerror(rc));
			goto err_link;
		}
	}

	return rc;
err_cont:
	rc2 = daos_cont_destroy(poh, attrp->da_cont, 1, NULL);
	if (rc2)
		D_ERROR("Failed to cleanup created container %s (%d)\n", attrp->da_cont, rc2);
err_link:
	if (attrp->da_type == DAOS_PROP_CO_LAYOUT_POSIX)
		rmdir(path);
	else if (attrp->da_type != DAOS_PROP_CO_LAYOUT_UNKNOWN)
		unlink(path);
	return rc;
}

int
duns_link_cont(daos_handle_t poh, const char *cont, const char *path)
{
	daos_handle_t		coh;
	daos_prop_t		*prop;
	struct daos_prop_entry	*entry;
	daos_pool_info_t	pinfo = {0};
	daos_cont_info_t	cinfo = {0};
	daos_cont_layout_t	type;
	char			pool_str[DAOS_UUID_STR_SIZE];
	char			cont_str[DAOS_UUID_STR_SIZE];
	int			len;
	char			str[DUNS_MAX_XATTR_LEN];
	char			type_str[10];
	int			rc, rc2;

	if (path == NULL) {
		D_ERROR("Invalid path\n");
		return EINVAL;
	}

	rc = daos_pool_query(poh, NULL, &pinfo, NULL, NULL);
	if (rc) {
		D_ERROR("Failed to query pool info" DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	rc = daos_cont_open(poh, cont, DAOS_COO_RO, &coh, &cinfo, NULL);
	if (rc) {
		D_ERROR("daos_cont_open() failed "DF_RC"\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	prop = daos_prop_alloc(1);
	if (prop == NULL)
		D_GOTO(out_cont, rc = ENOMEM);
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_LAYOUT_TYPE;
	rc = daos_cont_query(coh, NULL, prop, NULL);
	if (rc) {
		daos_prop_free(prop);
		D_ERROR("daos_cont_query() failed, "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_cont, rc = daos_der2errno(rc));
	}
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_LAYOUT_TYPE);
	if (entry == NULL) {
		daos_prop_free(prop);
		D_ERROR("Invalid container type\n");
		D_GOTO(out_cont, rc = EINVAL);
	}
	type = entry->dpe_val;
	daos_prop_free(prop);

	uuid_unparse(pinfo.pi_uuid, pool_str);
	uuid_unparse(cinfo.ci_uuid, cont_str);

	if (type == DAOS_PROP_CO_LAYOUT_POSIX) {
		struct statfs	fs;
		char		*dir, *dirp;
		mode_t		mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
		size_t		path_len;

		path_len = strnlen(path, PATH_MAX);
		D_STRNDUP(dir, path, path_len);
		if (dir == NULL)
			D_GOTO(out_cont, rc = ENOMEM);
		dirp = dirname(dir);

		rc = statfs(dirp, &fs);
		if (rc == -1) {
			int err = errno;

			D_ERROR("Failed to statfs dir %s: %d (%s)\n", dirp, err, strerror(err));
			D_FREE(dir);
			D_GOTO(out_cont, rc = err);
		}
		D_FREE(dir);
#ifdef LUSTRE_INCLUDE
		if (fs.f_type == LL_SUPER_MAGIC) {
			rc = duns_link_lustre_path(pool_str, cont_str, type, path, mode);
			if (rc == 0)
				return 0;
			/* if Lustre specific method fails, fallback to try the normal way... */
		}
#endif

		/** create a new directory if POSIX container */
		rc = mkdir(path, mode);
		if (rc == -1) {
			rc = errno;
			D_ERROR("Failed to create dir %s: %d (%s)\n", path, rc, strerror(rc));
			D_GOTO(out_cont, rc);
		}

		/* Open the parent directory for the new container so we can call a dfuse iotcl
		 * to discover the user running dfuse.
		 */
		if (fs.f_type == FUSE_SUPER_MAGIC) {
			struct stat finfo;
			/*
			 * This next stat will cause dfuse to lookup the entry point and perform a
			 * container connect, therefore this data will be read from root of the new
			 * container, not the directory.
			 *
			 * TODO: This could call getxattr to verify success.
			 */
			rc = stat(path, &finfo);
			if (rc) {
				rc = errno;
				D_ERROR("Failed to access container: %d (%s)\n", rc, strerror(rc));
				D_GOTO(err_link, rc);
			}
		}
	} else if (type != DAOS_PROP_CO_LAYOUT_UNKNOWN) {
		/** create a new file for other container types */
		int fd;
		mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
#ifdef LUSTRE_INCLUDE
		struct statfs   fs;
		char            *dir, *dirp;

		D_STRNDUP(dir, path, path_len);
		if (dir == NULL)
			D_GOTO(out_cont, rc = ENOMEM);

		dirp = dirname(dir);
		rc = statfs(dirp, &fs);
		if (rc == -1) {
			int err = errno;

			D_ERROR("Failed to statfs dir %s: %d (%s)\n", dirp, err, strerror(err));
			D_FREE(dir);
			D_GOTO(out_cont, rc = err);
			return err;
		}
		D_FREE(dir);

		if (fs.f_type == LL_SUPER_MAGIC) {
			rc = duns_link_lustre_path(pool_str, cont_str, type, path, mode);
			if (rc == 0)
				return 0;
			/* if Lustre specific method fails, fallback to try the normal way... */
		}
#endif

		fd = open(path, O_CREAT | O_EXCL, mode);
		if (fd == -1) {
			rc = errno;
			D_ERROR("Failed to create file %s: %d (%s)\n", path, rc, strerror(rc));
			D_GOTO(out_cont, rc);
		}
		close(fd);
	} else {
		D_ERROR("Invalid container layout.\n");
		D_GOTO(out_cont, rc = EINVAL);
	}

	/** store the daos attributes in the path xattr */
	daos_unparse_ctype(type, type_str);
	len = snprintf(str, DUNS_MAX_XATTR_LEN, DUNS_XATTR_FMT, type_str, pool_str, cont_str);
	if (len < 0) {
		D_ERROR("Failed to create xattr value\n");
		D_GOTO(err_link, rc = EINVAL);
	}
	rc = lsetxattr(path, DUNS_XATTR_NAME, str, len + 1, 0);
	if (rc) {
		rc = errno;
		if (rc == ENOTSUP) {
			D_INFO("Path is not in a filesystem that supports the DAOS unified "
			       "namespace\n");
		} else {
			D_ERROR("Failed to set DAOS xattr: %d (%s)\n", rc, strerror(rc));
		}
		D_GOTO(err_link, rc);
	}

out_cont:
	rc2 = daos_cont_close(coh, NULL);
	if (rc == 0)
		rc = rc2;
	return rc;
err_link:
	if (type == DAOS_PROP_CO_LAYOUT_POSIX)
		rmdir(path);
	else if (type != DAOS_PROP_CO_LAYOUT_UNKNOWN)
		unlink(path);
	goto out_cont;
}

int
duns_destroy_path(daos_handle_t poh, const char *path)
{
	struct duns_attr_t dattr = {0};
	int	rc;

	/* Resolve pool, container UUIDs from path */
	rc = duns_resolve_path(path, &dattr);
	if (rc) {
		D_ERROR("duns_resolve_path() Failed on path %s (%d)\n", path, rc);
		return rc;
	}

	/** Destroy the container */
	rc = daos_cont_destroy(poh, dattr.da_cont, 1, NULL);
	if (rc) {
		D_ERROR("Failed to destroy container (%d)\n", rc);
		/** recreate the link ? */
		return daos_der2errno(rc);
	}

	if (dattr.da_type == DAOS_PROP_CO_LAYOUT_POSIX) {
#ifdef LUSTRE_INCLUDE
		if (dattr.da_on_lustre)
			rc = (*unlink_foreign)((char *)path);
		else
#endif
			rc = rmdir(path);
		if (rc) {
			int err = errno;

			D_ERROR("Failed to remove %sdir %s: %s\n",
				dattr.da_on_lustre ? "Lustre " : " ", path, strerror(errno));
			return err;
		}
	} else if (dattr.da_type != DAOS_PROP_CO_LAYOUT_UNKNOWN) {
#ifdef LUSTRE_INCLUDE
		if (dattr.da_on_lustre)
			rc = (*unlink_foreign)((char *)path);
		else
#endif
			rc = unlink(path);
		if (rc) {
			int err = errno;

			D_ERROR("Failed to unlink %sfile %s: %s\n",
				dattr.da_on_lustre ? "Lustre " : " ", path, strerror(errno));
			return err;
		}
	}

	return 0;
}

int
duns_set_sys_name(struct duns_attr_t *attrp, const char *sys)
{
	if (attrp == NULL)
		return EINVAL;
	D_STRNDUP(attrp->da_sys, sys, DAOS_SYS_NAME_MAX_LEN);
	if (attrp->da_sys == NULL)
		return ENOMEM;

	return 0;
}

void
duns_destroy_attr(struct duns_attr_t *attrp)
{
	if (attrp == NULL)
		return;
	D_FREE(attrp->da_rel_path);
	D_FREE(attrp->da_sys);
}
