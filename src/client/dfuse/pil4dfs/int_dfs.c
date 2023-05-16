/**
 * (C) Copyright 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC     DD_FAC(il)

#include <stdio.h>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <mntent.h>
#include <signal.h>
#include <inttypes.h>

#include <daos/debug.h>
#include <gurt/list.h>
#include <gurt/common.h>
#include <gurt/hash.h>
#include <daos.h>
#include <daos_fs.h>
#include <daos_uns.h>
#include <dfuse_ioctl.h>
#include <daos_prop.h>
#include <daos/common.h>
#include "dfs_internal.h"

#include "hook.h"

/* D_ALLOC and D_FREE can not be used in query_path(). It causes dead lock during daos_init(). */
#define FREE(ptr)	do {free(ptr); (ptr) = NULL; } while (0)

/* Use very large synthetic FD to distinguish regular FD from Kernel */

/* FD_FILE_BASE - The base number of the file descriptor for a regular file.
 * The fd allocate from this lib is always larger than FD_FILE_BASE.
 */
#define FD_FILE_BASE        (0x20000000)

/* FD_FILE_BASE - The base number of the file descriptor for a directory.
 * The fd allocate from this lib is always larger than FD_FILE_BASE.
 */
#define FD_DIR_BASE         (0x40000000)

/* The max number of mount points for DAOS mounted simultaneously */
#define MAX_DAOS_MT         (8)

#define READ_DIR_BATCH_SIZE (96)
#define MAX_FD_DUP2ED       (8)

#define MAX_MMAP_BLOCK      (64)

#define MAX_OPENED_FILE     (2048)
#define MAX_OPENED_FILE_M1  ((MAX_OPENED_FILE)-1)
#define MAX_OPENED_DIR      (512)
#define MAX_OPENED_DIR_M1   ((MAX_OPENED_DIR)-1)

/* The buffer size used for reading/writing in rename() */
#define FILE_BUFFER_SIZE    (64 * 1024 * 1024)

/* Create a fake st_ino in stat for a path */
#define FAKE_ST_INO(path)   (d_hash_string_u32(path, strnlen(path, DFS_MAX_PATH)))

/* structure allocated for dfs container */
struct dfs_mt {
	dfs_t               *dfs;
	daos_handle_t        poh;
	daos_handle_t        coh;
	struct d_hash_table *dfs_dir_hash;
	int                  len_fs_root;

	_Atomic uint32_t     inited;
	char                *pool, *cont;
	char                 fs_root[DFS_MAX_PATH];
};

static _Atomic uint64_t        num_read;
static _Atomic uint64_t        num_write;
static _Atomic uint64_t        num_open;
static _Atomic uint64_t        num_stat;
static _Atomic uint64_t        num_opendir;
static _Atomic uint64_t        num_readdir;
static _Atomic uint64_t        num_unlink;
static _Atomic uint64_t        num_seek;
static _Atomic uint64_t        num_mkdir;
static _Atomic uint64_t        num_rmdir;
static _Atomic uint64_t        num_rename;
static _Atomic uint64_t        num_mmap;

static _Atomic uint32_t        daos_init_cnt;

static bool            bLog;
static long int        page_size;

static bool            daos_inited;
static int             num_dfs;
static struct dfs_mt   dfs_list[MAX_DAOS_MT];

static void
discover_daos_mount(void);
static void
discover_dfuse(void);
static int
retrieve_handles_from_fuse(int idx);
static int
init_dfs(int idx);
static int
query_dfs_mount(const char *dfs_mount);

/* structure allocated for a FD for a file */
struct file_obj {
	struct dfs_mt   *dfs_mt;
	dfs_obj_t       *file;
	dfs_obj_t       *parent;
	int              open_flag;
	int              ref_count;
	unsigned int     st_ino;
	int              idx_mmap;
	off_t            offset;
	char             item_name[DFS_MAX_NAME];
};

/* structure allocated for a FD for a dir */
struct dir_obj {
	int              fd;
	uint32_t         num_ents;
	dfs_obj_t       *dir;
	long int         offset;
	struct dfs_mt   *dfs_mt;
	int              open_flag;
	int              ref_count;
	unsigned int     st_ino;
	daos_anchor_t    anchor;
	/* path and ents will be allocated together dynamically since they are large. */
	char            *path;
	struct dirent   *ents;
};

struct mmap_obj {
	/* The base address of this memory block */
	char            *addr;
	size_t           length;
	/* the size of file. It is needed when write back to storage. */
	size_t           file_size;
	int              prot;
	int              flags;
	/* The fd used when mmap is called */
	int              fd;
	/* num_pages = length / page_size */
	int              num_pages;
	int              num_dirty_pages;
	off_t            offset;
	/* An array to indicate whether a page is updated or not */
	bool            *updated;
};

struct fd_dup2 {
	int fd_src, fd_dest;
	bool dest_closed;
};

/* working dir of current process */
static char            cur_dir[DFS_MAX_PATH] = "";
static bool            segv_handler_inited;
/* Old segv handler */
struct sigaction       old_segv;

/* the flag to indicate whether initlization is finished or not */
static bool            hook_enabled;
static pthread_mutex_t lock_dfs;
static pthread_mutex_t lock_fd;
static pthread_mutex_t lock_dirfd;
static pthread_mutex_t lock_mmap;
static pthread_mutex_t lock_fd_dup2ed;

/* store ! umask to apply on mode when creating file to honor system umask */
static mode_t          mode_not_umask;

static void
finalize_dfs(void);
static void
update_cwd(void);

struct dir_hdl {
	d_list_t   entry;
	dfs_obj_t *oh;
	char       name[DFS_MAX_PATH];
};

static inline struct dir_hdl *
hdl_obj(d_list_t *rlink)
{
	return container_of(rlink, struct dir_hdl, entry);
}

static bool
key_cmp(struct d_hash_table *htable, d_list_t *rlink, const void *key, unsigned int ksize)
{
	struct dir_hdl *hdl = hdl_obj(rlink);

	return (strncmp(hdl->name, (const char *)key, DFS_MAX_PATH) == 0);
}

static void
rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	int             rc;
	struct dir_hdl *hdl = hdl_obj(rlink);

	rc = dfs_release(hdl->oh);
	if (rc == ENOMEM)
		rc = dfs_release(hdl->oh);
	if (rc && bLog)
		D_WARN("dfs_release() failed: %s", strerror(rc));
	D_FREE(hdl);
}

static bool
rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	return true;
}

static uint32_t
rec_hash(struct d_hash_table *htable, d_list_t *rlink)
{
	struct dir_hdl *hdl = hdl_obj(rlink);

	return d_hash_string_u32(hdl->name, strnlen(hdl->name, DFS_MAX_PATH));
}

static d_hash_table_ops_t hdl_hash_ops = {.hop_key_cmp    = key_cmp,
					  .hop_rec_decref = rec_decref,
					  .hop_rec_free   = rec_free,
					  .hop_rec_hash   = rec_hash};

/**
 * Look up a path in hash table to get the dfs_obj for a dir.
 * If not found in hash table, call to dfs_lookup() to open the object on DAOS.
 * If the object is corresponding to a dir, insert the object into hash table.
 * Since many DFS APIs need parent dir DFS object as parameter, we use a hash
 * table to cache parent objects for efficiency.
 */
static int
lookup_insert_dir(int idx_dfs, const char *name, mode_t *mode, dfs_obj_t **obj)
{
	struct dir_hdl *hdl;
	dfs_obj_t      *oh;
	d_list_t       *rlink;
	size_t          len;
	int             rc;

	if (obj == NULL) {
		D_ERROR("Invalid pointer!");
		return EFAULT;
	}
	*obj = NULL;
	len = strnlen(name, DFS_MAX_PATH);
	if (len >= DFS_MAX_PATH) {
		D_ERROR("path is too long!");
		return ENAMETOOLONG;
	}
	rlink = d_hash_rec_find(dfs_list[idx_dfs].dfs_dir_hash, name, len);
	if (rlink != NULL) {
		hdl = hdl_obj(rlink);
		*obj = hdl->oh;
		return 0;
	}

	rc = dfs_lookup(dfs_list[idx_dfs].dfs, name, O_RDWR, &oh, mode, NULL);
	if (rc)
		return rc;

	if (mode && !S_ISDIR(*mode)) {
		*obj = oh;
		return 0;
	}

	D_ALLOC_PTR(hdl);
	if (hdl == NULL) {
		dfs_release(oh);
		return ENOMEM;
	}

	/* "len + 1" is used to avoid compiling issue on Ubuntu. */
	strncpy(hdl->name, name, len + 1);
	hdl->oh = oh;

	rc = d_hash_rec_insert(dfs_list[idx_dfs].dfs_dir_hash, hdl->name, len, &hdl->entry, true);
	if (rc) {
		D_ERROR("Failed to insert dir handle in hashtable.");
		dfs_release(hdl->oh);
		D_FREE(hdl);
		return daos_der2errno(rc);
	}
	*obj = hdl->oh;
	return 0;
}

static int (*ld_open)(const char *pathname, int oflags, ...);
static int (*libc_open)(const char *pathname, int oflags, ...);
static int (*pthread_open)(const char *pathname, int oflags, ...);

static int (*libc_close_nocancel)(int fd);

static int (*libc_close)(int fd);
static int (*pthread_close)(int fd);

static ssize_t (*libc_read)(int fd, void *buf, size_t count);
static ssize_t (*pthread_read)(int fd, void *buf, size_t count);

static ssize_t (*next_pread)(int fd, void *buf, size_t size, off_t offset);

static ssize_t (*libc_write)(int fd, const void *buf, size_t count);
static ssize_t (*pthread_write)(int fd, const void *buf, size_t count);

static ssize_t (*next_pwrite)(int fd, const void *buf, size_t size, off_t offset);

static off_t (*libc_lseek)(int fd, off_t offset, int whence);
static off_t (*pthread_lseek)(int fd, off_t offset, int whence);

static int (*next_fxstat)(int vers, int fd, struct stat *buf);

static int (*next_statfs)(const char *pathname, struct statfs *buf);
static int (*next_fstatfs)(int fd, struct statfs *buf);

static int (*next_statvfs)(const char *pathname, struct statvfs *buf);

static DIR *(*next_opendir)(const char *name);

static DIR *(*next_fdopendir)(int fd);

static int (*next_closedir)(DIR *dirp);

static struct dirent *(*next_readdir)(DIR *dirp);

static int (*next_mkdir)(const char *path, mode_t mode);

static int (*next_mkdirat)(int dirfd, const char *pathname, mode_t mode);

static int (*next_xstat)(int ver, const char *path, struct stat *stat_buf);

static int (*libc_lxstat)(int ver, const char *path, struct stat *stat_buf);

static int (*next_fxstatat)(int ver, int dirfd, const char *path, struct stat *stat_buf, int flags);

static int (*next_statx)(int dirfd, const char *path, int flags, unsigned int mask,
			 struct statx *statx_buf);

static int (*next_isatty)(int fd);

static int (*next_access)(const char *pathname, int mode);

static int (*next_faccessat)(int dirfd, const char *pathname, int mode, int flags);

static int (*next_chdir)(const char *path);

static int (*next_fchdir)(int fd);

static int (*next_rmdir)(const char *path);

static int (*next_rename)(const char *old_name, const char *new_name);

static char *(*next_getcwd)(char *buf, size_t size);

static int (*libc_unlink)(const char *path);

static int (*next_unlinkat)(int dirfd, const char *path, int flags);

static int (*next_fsync)(int fd);

static int (*next_truncate)(const char *path, off_t length);

static int (*next_ftruncate)(int fd, off_t length);

static int (*next_chmod)(const char *path, mode_t mode);

static int (*next_fchmod)(int fd, mode_t mode);

static int (*next_fchmodat)(int dirfd, const char *path, mode_t mode, int flags);

static int (*next_utime)(const char *path, const struct utimbuf *times);

static int (*next_utimes)(const char *path, const struct timeval times[2]);

static int (*next_futimens)(int fd, const struct timespec times[2]);

static int (*next_utimensat)(int dirfd, const char *path, const struct timespec times[2],
			     int flags);

static int (*next_openat)(int dirfd, const char *pathname, int flags, ...);

static int (*next_openat_2)(int dirfd, const char *pathname, int flags);

static int (*libc_fcntl)(int fd, int cmd, ...);

static int (*next_ioctl)(int fd, unsigned long request, ...);

static int (*next_dup)(int oldfd);

static int (*next_dup2)(int oldfd, int newfd);

static int (*next_symlink)(const char *symvalue, const char *path);

static int (*next_symlinkat)(const char *symvalue, int dirfd, const char *path);

static ssize_t (*next_readlink)(const char *path, char *buf, size_t size);
static ssize_t (*next_readlinkat)(int dirfd, const char *path, char *buf, size_t size);

static void * (*next_mmap)(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
static int (*next_munmap)(void *addr, size_t length);

static void (*next_exit)(int rc);

/* typedef int (*org_dup3)(int oldfd, int newfd, int flags); */
/* static org_dup3 real_dup3=NULL; */

/**
 *static int (*real_execve)(const char *filename, char *const argv[], char *const envp[]);
 * static int (*real_execvp)(const char *filename, char *const argv[]);
 * static int (*real_execv)(const char *filename, char *const argv[]);
 * static pid_t (*real_fork)();
 */

/* start NOT supported by DAOS */
static int (*next_posix_fadvise)(int fd, off_t offset, off_t len, int advice);
static int (*next_flock)(int fd, int operation);
static int (*next_fallocate)(int fd, int mode, off_t offset, off_t len);
static int (*next_posix_fallocate)(int fd, off_t offset, off_t len);
static int (*next_posix_fallocate64)(int fd, off64_t offset, off64_t len);
static int (*next_tcgetattr)(int fd, void *termios_p);
/* end NOT supported by DAOS */

/* to do!! */
/**
 * static char * (*org_realpath)(const char *pathname, char *resolved_path);
 * org_realpath real_realpath=NULL;
 */

static int
remove_dot_dot(char path[], int *len);
static int
remove_dot_and_cleanup(char szPath[], int len);

static struct file_obj   *file_list[MAX_OPENED_FILE];
static struct dir_obj    *dir_list[MAX_OPENED_DIR];
static struct mmap_obj    mmap_list[MAX_MMAP_BLOCK];

/* last_fd==-1 means the list is empty. No active fd in list. */
static int                next_free_fd, last_fd       = -1, num_fd;
static int                next_free_dirfd, last_dirfd = -1, num_dirfd;
static int                next_free_map, last_map     = -1, num_map;

static int
find_next_available_fd(struct file_obj *obj, int *new_fd);
static int
find_next_available_dirfd(struct dir_obj *obj, int *new_fd);
static int
find_next_available_map(int *idx);
static void
free_fd(int idx);
static void
free_dirfd(int idx);
static void
free_map(int idx);

static void
register_handler(int sig, struct sigaction *old_handler);

static void
print_summary(void);

static int       num_fd_dup2ed;
struct fd_dup2   fd_dup2_list[MAX_FD_DUP2ED];

static void
init_fd_dup2_list(void);
/* return dest fd */
static int
query_fd_forward_dest(int fd_src);

static int
new_close_common(int (*next_close)(int fd), int fd);

static int
query_dfs_mount(const char *path)
{
	int i, idx = -1, max_len = -1;

	for (i = 0; i < num_dfs; i++) {
		/* To find in existing list with max length */
		if (strncmp(path, dfs_list[i].fs_root, dfs_list[i].len_fs_root) == 0 &&
		    (path[dfs_list[i].len_fs_root] == '/' ||
		    path[dfs_list[i].len_fs_root] == 0)) {
			if (dfs_list[i].len_fs_root > max_len) {
				idx     = i;
				max_len = dfs_list[i].len_fs_root;
			}
		}
	}

	/* -1 means the mount point is not found in dfs_list. */
	return idx;
}

static void
discover_daos_mount(void)
{
	int   idx, len_fs_root;
	char *fs_root   = NULL;
	char *pool      = NULL;
	char *container = NULL;
	char *pt_end    = NULL;

	/* Find the list of dfuse from /proc/mounts */
	discover_dfuse();

	/* Add the mount if env DAOS_MOUNT_POINT is set. */
	fs_root = getenv("DAOS_MOUNT_POINT");
	if (fs_root == NULL)
		return;

	idx = query_dfs_mount(fs_root);
	if (idx >= 0)
		return;

	/* daos_init() is not called yet, so we can not use D_WARN() etc. */
	/* Not found in existing list, then append this new mount point. */
	len_fs_root = strnlen(fs_root, DFS_MAX_PATH);
	if (len_fs_root >= DFS_MAX_PATH) {
		printf("warning> DAOS_MOUNT_POINT is too long. It is ignored.");
		return;
	}

	pool = getenv("DAOS_POOL");
	if (pool == NULL) {
		printf("warning> DAOS_POOL is not set.\n");
		return;
	}

	container = getenv("DAOS_CONTAINER");
	if (container == NULL) {
		printf("warning> DAOS_CONTAINER is not set.\n");
		return;
	}

	pt_end = stpncpy(dfs_list[num_dfs].fs_root, fs_root, DFS_MAX_PATH - 1);
	if ((long int)(pt_end - dfs_list[num_dfs].fs_root) >= (DFS_MAX_PATH - 1)) {
		printf("dfs_list[num_dfs].fs_root[] is not large enough.\nQuit\n");
		return;
	}
	dfs_list[num_dfs].pool         = pool;
	dfs_list[num_dfs].cont         = container;
	dfs_list[num_dfs].dfs_dir_hash = NULL;
	dfs_list[num_dfs].len_fs_root  = len_fs_root;
	atomic_store_relaxed(&dfs_list[num_dfs].inited, 0);
	num_dfs++;
}

static void
discover_dfuse(void)
{
	FILE          *fp;
	const char     mnt_type_fuse[] = "fuse.daos";
	struct mntent *fs_entry;
	char          *pt_end;

	num_dfs = 0;

	fp = setmntent("/proc/self/mounts", "r");
	/* daos_init() is not called yet, so we can not use D_WARN() etc. */

	if (fp == NULL) {
		fprintf(stderr, "Error> failed to open file: /proc/self/mounts\n");
		return;
	}

	while ((fs_entry = getmntent(fp)) != NULL) {
		if (strncmp(fs_entry->mnt_type, mnt_type_fuse, sizeof(mnt_type_fuse)) == 0) {
			dfs_list[num_dfs].dfs_dir_hash = NULL;
			dfs_list[num_dfs].len_fs_root  = strnlen(fs_entry->mnt_dir, DFS_MAX_PATH);
			if (dfs_list[num_dfs].len_fs_root >= DFS_MAX_PATH) {
				fprintf(stderr, "Warning> mnt_dir[] is too long!\n"
					"Skip this entry.\n");
				continue;
			}
			atomic_store_relaxed(&dfs_list[num_dfs].inited, 0);
			dfs_list[num_dfs].pool         = NULL;
			pt_end = stpncpy(dfs_list[num_dfs].fs_root, fs_entry->mnt_dir,
					 DFS_MAX_PATH - 1);
			if ((long int)(pt_end - dfs_list[num_dfs].fs_root) >= (DFS_MAX_PATH - 1)) {
				fprintf(stderr, "Warning> fs_root[] is too long.\n"
					"Skip this entry.\n");
				continue;
			}
			num_dfs++;
		}
	}

	endmntent(fp);
}

static int
retrieve_handles_from_fuse(int idx)
{
	struct dfuse_hs_reply hs_reply;
	int                   fd, cmd, rc, errno_saved;
	d_iov_t               iov = {};
	char                  *buff = NULL;
	size_t                buff_size;

	fd = libc_open(dfs_list[idx].fs_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (fd < 0) {
		if (bLog)
			D_ERROR("failed to open dir: %s", dfs_list[idx].fs_root);
		return (-1);
	}

	cmd = _IOR(DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_SIZE, struct dfuse_hs_reply);
	rc  = ioctl(fd, cmd, &hs_reply);
	if (rc != 0) {
		if (errno == EPERM)
			errno = EACCES;
		if (bLog)
			D_ERROR("failed to query size info from dfuse with ioctl().");
		goto err;
	}

	/* To determine the size of buffer we need to accommodate the data from fuse */
	buff_size = max(hs_reply.fsr_pool_size, hs_reply.fsr_cont_size);
	buff_size = max(buff_size, hs_reply.fsr_dfs_size);
	D_ALLOC(buff, buff_size);
	if (buff == NULL)
		goto err;

	iov.iov_buf = buff;
	cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_POH, hs_reply.fsr_pool_size);
	rc  = ioctl(fd, cmd, iov.iov_buf);
	if (rc != 0) {
		if (bLog)
			D_ERROR("failed to query pool handle from dfuse with ioctl().");
		goto err;
	}
	iov.iov_buf_len = hs_reply.fsr_pool_size;
	iov.iov_len     = iov.iov_buf_len;
	rc              = daos_pool_global2local(iov, &dfs_list[idx].poh);
	if (rc != 0) {
		if (bLog)
			D_ERROR("failed to create pool handle in daos_pool_global2local().");
		errno = daos_der2errno(rc);
		goto err;
	}

	cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_COH, hs_reply.fsr_cont_size);
	rc  = ioctl(fd, cmd, iov.iov_buf);
	if (rc != 0) {
		if (bLog)
			D_ERROR("failed to query container handle from dfuse with ioctl().");
		goto err;
	}
	iov.iov_buf_len = hs_reply.fsr_cont_size;
	iov.iov_len     = iov.iov_buf_len;
	rc              = daos_cont_global2local(dfs_list[idx].poh, iov, &dfs_list[idx].coh);
	if (rc != 0) {
		if (bLog)
			D_ERROR("failed to create container handle in daos_pool_global2local().");
		errno = daos_der2errno(rc);
		goto err;
	}

	cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_DOH, hs_reply.fsr_dfs_size);
	rc  = ioctl(fd, cmd, iov.iov_buf);
	if (rc != 0) {
		if (bLog)
			D_ERROR("failed to query DFS handle from dfuse with ioctl().");
		goto err;
	}
	iov.iov_buf_len = hs_reply.fsr_dfs_size;
	iov.iov_len     = iov.iov_buf_len;
	rc = dfs_global2local(dfs_list[idx].poh, dfs_list[idx].coh, 0, iov, &dfs_list[idx].dfs);
	if (rc != 0) {
		if (bLog)
			D_ERROR("failed to create DFS handle in daos_pool_global2local().");
		errno = daos_der2errno(rc);
		goto err;
	}

	rc = d_hash_table_create(D_HASH_FT_EPHEMERAL | D_HASH_FT_MUTEX | D_HASH_FT_LRU, 6, NULL,
				 &hdl_hash_ops, &dfs_list[idx].dfs_dir_hash);
	if (rc) {
		errno = daos_der2errno(rc);
		goto err;
	}
	D_FREE(buff);

	return 0;

err:
	errno_saved = errno;
	libc_close(fd);
	D_FREE(buff);
	errno = errno_saved;
	return (-1);
}

/* Check whether path starts with "DAOS://" */
static bool
is_path_start_with_daos(const char *path, char *pool, char *cont, char **rel_path)
{
	int rc;
	struct duns_attr_t attr = {0};

	if (path[0] != 'D' && path[0] != 'd')
		return false;
	if (path[1] != 'A' && path[1] != 'a')
		return false;
	if (path[2] != 'O' && path[2] != 'o')
		return false;
	if (path[3] != 'S' && path[3] != 's')
		return false;

	/* The path does not start with "DAOS://". */
	if (path[4] != ':' || path[5] != '/' || path[6] != '/')
		return false;

	attr.da_flags = DUNS_NO_CHECK_PATH;
	rc = duns_resolve_path(path, &attr);
	if (rc)
		return false;

	snprintf(pool, DAOS_PROP_LABEL_MAX_LEN + 1, "%s", attr.da_pool);
	snprintf(cont, DAOS_PROP_LABEL_MAX_LEN + 1, "%s", attr.da_cont);
	/* attr.da_rel_path is allocated dynamically. It should be freed later. */
	*rel_path = attr.da_rel_path;

	return true;
}

/** determine whether a path (both relative and absolute) is on DAOS or not. If yes,
 *  returns parent object, item name, full path of parent dir, full absolute path, and
 *  the pointer to struct dfs_mt.
 *  Dynamically allocate 2 * DFS_MAX_PATH for *parent_dir and *full_path in one malloc().
 */
static int
query_path(const char *szInput, int *is_target_path, dfs_obj_t **parent, char *item_name,
	   char **parent_dir, char **full_path, struct dfs_mt **dfs_mt)
{
	int    pos, len;
	mode_t mode;
	bool   with_daos_prefix;
	char   pool[DAOS_PROP_MAX_LABEL_BUF_LEN + 1];
	char   cont[DAOS_PROP_MAX_LABEL_BUF_LEN + 1];
	char  *rel_path = NULL;
	int    idx_dfs, rc, rc2;
	char  *pt_end = NULL;
	char  *full_path_parse = NULL;

	/* determine whether the path starts with daos://pool/cont/. Need more work. */
	with_daos_prefix = is_path_start_with_daos(szInput, pool, cont, &rel_path);
	if (with_daos_prefix) {
		/* TO DO. Need more work!!! query_dfs_pool_cont(pool, cont)*/
		*is_target_path = 0;
		return 0;
	}

	/* handle special cases. Needed to work with git. */
	if ((strncmp(szInput, "http://", 7) == 0) ||
	    (strncmp(szInput, "https://", 8) == 0) ||
	    (strncmp(szInput, "git://", 6) == 0)) {
		*is_target_path = 0;
		return 0;
	}

	*full_path = NULL;
	/* D_ALLOC() macro is not suitable since daos_init() may be not called yet.
	 * It causes dead lock inside daos_init which queries file stat and calls
	 * query_path(). D_ALLOC calls D_DEBUG and accesses lock.
	 */
	*parent_dir = malloc(DFS_MAX_PATH * 2);
	if (*parent_dir == NULL)
		goto out_oom;
	/* allocate both for *parent_dir and *full_path above */
	*full_path = *parent_dir + DFS_MAX_PATH;

	full_path_parse = malloc(DFS_MAX_PATH + 4);
	if (full_path_parse == NULL)
		goto out_oom;

	if (strncmp(szInput, ".", 2) == 0) {
		/* special case for current work directory */
		pt_end = stpncpy(full_path_parse, cur_dir, DFS_MAX_PATH);
		len = (int)(pt_end - full_path_parse);
		if (len >= DFS_MAX_PATH) {
			if (daos_inited && bLog)
				D_ERROR("full_path_parse[] is not large enough.");
			D_GOTO(out_err, rc = ENAMETOOLONG);
		}
	} else if (szInput[0] == '/') {
		/* absolute path */
		pt_end = stpncpy(full_path_parse, szInput, DFS_MAX_PATH);
		len = (int)(pt_end - full_path_parse);
		if (len >= DFS_MAX_PATH) {
			if (daos_inited && bLog)
				D_ERROR("full_path_parse[] is not large enough.");
			D_GOTO(out_err, rc = ENAMETOOLONG);
		}
	} else {
		/* relative path */
		len = snprintf(full_path_parse, DFS_MAX_PATH, "%s/%s", cur_dir, szInput);
		if (len >= DFS_MAX_PATH) {
			if (daos_inited && bLog)
				D_ERROR("The length of path is too long.");
			D_GOTO(out_err, rc = ENAMETOOLONG);
		}
	}

	/* Remove '/./'; Replace '//' with '/'; Remove '/' at the end of path. */
	len = remove_dot_and_cleanup(full_path_parse, len);

	/* standarlize and determine whether a path is a target path or not */

	/* Assume full_path_parse[] = "/A/B/C/../D/E", it will be "/A/B/D/E" after
	 * remove_dot_dot.
	 */
	rc = remove_dot_dot(full_path_parse, &len);
	if (rc)
		D_GOTO(out_err, rc);

	/* determine whether the path contains any known dfs mount point */
	idx_dfs = query_dfs_mount(full_path_parse);

	if (idx_dfs >= 0) {
		/* trying to avoid lock as much as possible */
		if (!daos_inited) {
			/* daos_init() is expensive to call. We call it only when necessary. */
			rc = daos_init();
			if (rc) {
				fprintf(stderr, "Error> daos_init failed, rc: "DF_RC"\n",
					DP_RC(rc));
				*is_target_path = 0;
				goto out_normal;
			}
			daos_inited = true;
			atomic_fetch_add_relaxed(&daos_init_cnt, 1);
		}

		/* dfs info can be set up after daos has been initialized. */
		*dfs_mt = &dfs_list[idx_dfs];
		/* trying to avoid lock as much as possible. Need to make sure whether
		 * it is safe enough or not!
		 */
		if (atomic_load_relaxed(&((*dfs_mt)->inited)) == 0) {
			rc = pthread_mutex_lock(&lock_dfs);
			D_ASSERT(rc == 0);
			if (atomic_load_relaxed(&((*dfs_mt)->inited)) == 0) {
				if ((*dfs_mt)->pool == NULL) {
					/* retrieve dfs, pool and container handles from dfuse */
					rc = retrieve_handles_from_fuse(idx_dfs);
					if (rc) {
						/* Let the original functions to handle it. */
						*is_target_path = 0;
						rc2 = pthread_mutex_unlock(&lock_dfs);
						D_ASSERT(rc2 == 0);
						goto out_normal;
					}
				} else {
					rc = init_dfs(idx_dfs);
					if (rc) {
						/* Let the original functions to handle it. */
						*is_target_path = 0;
						rc2 = pthread_mutex_unlock(&lock_dfs);
						D_ASSERT(rc2 == 0);
						goto out_normal;
					}
				}
				atomic_store_relaxed(&((*dfs_mt)->inited), 1);
			}
			rc = pthread_mutex_unlock(&lock_dfs);
			D_ASSERT(rc == 0);
		}
		*is_target_path = 1;

		/* root dir */
		if (full_path_parse[(*dfs_mt)->len_fs_root] == 0) {
			*parent       = NULL;
			(*parent_dir)[0] = '\0';
			item_name[0]  = '/';
			item_name[1]  = '\0';
			strncpy(*full_path, "/", 2);
		} else {
			/* full_path holds the full path inside dfs container */
			strncpy(*full_path, full_path_parse + (*dfs_mt)->len_fs_root, len + 1);
			for (pos = len - 1; pos >= (*dfs_mt)->len_fs_root; pos--) {
				if (full_path_parse[pos] == '/')
					break;
			}
			int len_item_name;

			len_item_name = strnlen(full_path_parse + pos + 1, len);
			if (len_item_name >= DFS_MAX_NAME) {
				if (daos_inited && bLog)
					D_ERROR("item_name[] is not large enough.");
				D_GOTO(out_err, rc = ENAMETOOLONG);
			}
			strncpy(item_name, full_path_parse + pos + 1, len_item_name + 1);
			/* the item under root directory */
			if (pos == (*dfs_mt)->len_fs_root) {
				*parent       = NULL;
				(*parent_dir)[0] = '/';
				(*parent_dir)[1] = '\0';
			} else {
				/* Need to look up the parent directory */
				full_path_parse[pos] = 0;
				strncpy(*parent_dir, full_path_parse + (*dfs_mt)->len_fs_root,
					len + 1);
			}
			/* look up the dfs object from hash table for the parent dir */
			rc = lookup_insert_dir(idx_dfs, *parent_dir, &mode, parent);
			/* parent dir does not exist or something wrong */
			if (rc)
				D_GOTO(out_err, rc);
		}
	} else {
		*is_target_path = 0;
		*parent         = NULL;
		item_name[0]    = '\0';
	}

out_normal:
	FREE(full_path_parse);
	return 0;

out_err:
	FREE(full_path_parse);
	FREE(*parent_dir);
	return rc;

out_oom:
	FREE(full_path_parse);
	FREE(*parent_dir);
	if (bLog)
		D_ERROR("fail to allocate memory.");
	return ENOMEM;
}

static int
remove_dot_dot(char path[], int *len)
{
	char *p_Offset_2Dots, *p_Back, *pTmp, *pMax, *new_str;
	int   i, nNonZero;

	/* the length of path[] is already checked in the caller of this function. */

	p_Offset_2Dots = strstr(path, "/../");
again:
	nNonZero = 0;
	if (p_Offset_2Dots == path) {
		if (daos_inited && bLog)
			D_ERROR("wrong path: %s", path);
		return EINVAL;
	}

	while (p_Offset_2Dots > 0) {
		pMax = p_Offset_2Dots + 4;
		for (p_Back = p_Offset_2Dots - 2; p_Back >= path; p_Back--) {
			if (*p_Back == '/') {
				for (pTmp = p_Back; pTmp < (pMax - 1); pTmp++)
					*pTmp = 0;
				break;
			}
		}
		p_Offset_2Dots = strstr(p_Offset_2Dots + 3, "/../");
		if (p_Offset_2Dots == NULL)
			break;
	}

	new_str = path;
	for (i = 0; i < *len; i++) {
		if (path[i]) {
			new_str[nNonZero] = path[i];
			nNonZero++;
		}
	}
	new_str[nNonZero] = 0;
	*len = nNonZero;

	p_Offset_2Dots = strstr(path, "/../");
	if (p_Offset_2Dots)
		goto again;
	else
		return 0;
}

/* Remove '/./'. Replace '//' with '/'. Remove '/.'. Remove '/' at the end of path. */
static int
remove_dot_and_cleanup(char path[], int len)
{
	char *p_Offset_Dots, *p_Offset_Slash, *new_str;
	int   i, nNonZero = 0;

	/* the length of path[] is already checked in the caller of this function. */

	p_Offset_Dots = strstr(path, "/./");

	while (p_Offset_Dots > 0) {
		p_Offset_Dots[0] = 0;
		p_Offset_Dots[1] = 0;
		p_Offset_Dots    = strstr(p_Offset_Dots + 2, "/./");
		if (p_Offset_Dots == NULL)
			break;
	}

	/* replace "//" with "/" */
	p_Offset_Slash = strstr(path, "//");
	while (p_Offset_Slash > 0) {
		p_Offset_Slash[0] = 0;
		p_Offset_Slash    = strstr(p_Offset_Slash + 1, "//");
		if (p_Offset_Slash == NULL)
			break;
	}

	/* remove '/.' at the end */
	if (len > 2 && strncmp(path + len - 2, "/.", 3) == 0) {
		p_Offset_Slash = path + len - 2;
		p_Offset_Slash[0] = 0;
		p_Offset_Slash[1] = 0;
	}

	new_str = path;
	for (i = 0; i < len; i++) {
		if (path[i]) {
			new_str[nNonZero] = path[i];
			nNonZero++;
		}
	}
	/* remove "/" at the end of path */
	new_str[nNonZero] = 0;
	if (new_str[1] == 0 && new_str[0] == '/')
		/* root dir */
		return 1;
	for (i = nNonZero - 1; i >= 0; i--) {
		if (new_str[i] == '/') {
			new_str[i] = 0;
			nNonZero--;
		} else {
			break;
		}
	}

	return nNonZero;
}

static void
init_fd_list(void)
{
	/* daos_init() is not called yet, so we can not use D_ERROR() etc. */
	if (pthread_mutex_init(&lock_fd, NULL) != 0) {
		fprintf(stderr, "Error> mutex create_new_lock lock_fd init failed\n");
		exit(1);
	}
	if (pthread_mutex_init(&lock_dirfd, NULL) != 0) {
		fprintf(stderr, "Error> mutex create_new_lock lock_dirfd init failed\n");
		exit(1);
	}
	if (pthread_mutex_init(&lock_mmap, NULL) != 0) {
		fprintf(stderr, "Error> mutex create_new_lock lock_mmap init failed\n");
		exit(1);
	}
	if (pthread_mutex_init(&lock_fd_dup2ed, NULL) != 0) {
		fprintf(stderr, "Error> mutex create_new_lock lock_fd_dup2ed init failed\n");
		exit(1);
	}
	/* fatal error above: failure to create mutexes. */

	memset(file_list, 0, sizeof(struct file_obj *) * MAX_OPENED_FILE);
	memset(dir_list, 0, sizeof(struct dir_obj *) * MAX_OPENED_DIR);
	memset(mmap_list, 0, sizeof(struct mmap_obj) * MAX_MMAP_BLOCK);

	next_free_fd    = 0;
	last_fd         = -1;
	next_free_dirfd = 0;
	last_dirfd      = -1;
	next_free_map   = 0;
	last_map        = -1;
	num_fd = num_dirfd = num_map = 0;
}

static int
find_next_available_fd(struct file_obj *obj, int *new_fd)
{
	int i, idx = -1, rc;
	struct file_obj *new_obj = NULL;
	bool allocated = false;

	if (obj == NULL) {
		D_ALLOC_PTR(new_obj);
		if (new_obj == NULL)
			return ENOMEM;
		new_obj->file      = NULL;
		new_obj->idx_mmap  = -1;
		new_obj->ref_count = 0;
		allocated          = true;
	} else {
		new_obj = obj;
	}

	rc = pthread_mutex_lock(&lock_fd);
	D_ASSERT(rc == 0);
	if (next_free_fd < 0) {
		rc = pthread_mutex_unlock(&lock_fd);
		D_ASSERT(rc == 0);
		if (allocated)
			D_FREE(new_obj);
		if (bLog)
			D_ERROR("failed to allocate fd: out of space.");
		return EMFILE;
	}
	idx = next_free_fd;
	if (idx >= 0) {
		new_obj->ref_count++;
		file_list[idx] = new_obj;
	}
	if (next_free_fd > last_fd)
		last_fd = next_free_fd;
	next_free_fd = -1;

	for (i = idx + 1; i < MAX_OPENED_FILE; i++) {
		if (file_list[i] == NULL) {
			/* available, then update next_free_fd */
			next_free_fd = i;
			break;
		}
	}

	num_fd++;
	rc = pthread_mutex_unlock(&lock_fd);
	D_ASSERT(rc == 0);

	*new_fd = idx;
	return 0;
}

static int
find_next_available_dirfd(struct dir_obj *obj, int *new_dir_fd)
{
	int i, idx = -1, rc;
	struct dir_obj *new_obj;
	bool allocated = false;

	if (obj == NULL) {
		D_ALLOC(new_obj, sizeof(struct dir_obj));
		if (new_obj == NULL)
			return ENOMEM;
		new_obj->dir       = NULL;
		new_obj->ref_count = 0;
		allocated          = true;
	} else {
		new_obj            = obj;
	}

	rc = pthread_mutex_lock(&lock_dirfd);
	D_ASSERT(rc == 0);
	if (next_free_dirfd < 0) {
		rc = pthread_mutex_unlock(&lock_dirfd);
		D_ASSERT(rc == 0);
		if (allocated)
			D_FREE(new_obj);
		if (bLog)
			D_ERROR("Error> Failed to allocate dirfd: out of space.");
		return EMFILE;
	}
	idx = next_free_dirfd;
	if (idx >= 0) {
		new_obj->ref_count++;
		dir_list[idx] = new_obj;
	}
	if (next_free_dirfd > last_dirfd)
		last_dirfd = next_free_dirfd;

	next_free_dirfd = -1;

	for (i = idx + 1; i < MAX_OPENED_DIR; i++) {
		if (dir_list[i] == NULL) {
			/* available, then update next_free_dirfd */
			next_free_dirfd = i;
			break;
		}
	}

	num_dirfd++;
	rc = pthread_mutex_unlock(&lock_dirfd);
	D_ASSERT(rc == 0);

	*new_dir_fd = idx;
	return 0;
}

static int
find_next_available_map(int *idx)
{
	int i, rc;

	*idx = -1;
	rc = pthread_mutex_lock(&lock_mmap);
	D_ASSERT(rc == 0);
	if (next_free_map < 0) {
		rc = pthread_mutex_unlock(&lock_mmap);
		D_ASSERT(rc == 0);
		if (bLog)
			D_ERROR("Error> Failed to allocate space from mmap_list[]: out of space.");
		return EMFILE;
	}
	*idx = next_free_map;
	if (next_free_map > last_map)
		last_map = next_free_map;
	next_free_map = -1;

	for (i = (*idx) + 1; i < MAX_MMAP_BLOCK; i++) {
		if (mmap_list[i].addr == NULL) {
			/* available, then update next_free_map */
			next_free_map = i;
			break;
		}
	}

	num_map++;
	rc = pthread_mutex_unlock(&lock_mmap);
	D_ASSERT(rc == 0);

	return 0;
}

/* May need to support duplicated fd as duplicated dirfd too. */
static void
free_fd(int idx)
{
	int              i, rc;
	struct file_obj *saved_obj = NULL;

	rc = pthread_mutex_lock(&lock_fd);
	D_ASSERT(rc == 0);
	if (file_list[idx]->idx_mmap >= 0 && file_list[idx]->idx_mmap < MAX_MMAP_BLOCK) {
		/* file_list[idx].idx_mmap >= MAX_MMAP_BLOCK means fd needs closed after munmap()*/
		file_list[idx]->idx_mmap += MAX_MMAP_BLOCK;
		rc = pthread_mutex_unlock(&lock_fd);
		D_ASSERT(rc == 0);
		return;
	}

	file_list[idx]->ref_count--;
	if (file_list[idx]->ref_count == 0)
		saved_obj = file_list[idx];
	file_list[idx] = NULL;

	if (idx < next_free_fd)
		next_free_fd = idx;

	if (idx == last_fd) {
		for (i = idx - 1; i >= 0; i--) {
			if (file_list[i]) {
				last_fd = i;
				break;
			}
		}
	}

	num_fd--;
	rc = pthread_mutex_unlock(&lock_fd);
	D_ASSERT(rc == 0);

	if (saved_obj) {
		rc = dfs_release(saved_obj->file);
		/** This memset() is not necessary. It is left here intended. In case of duplicated
		 *  fd exists, multiple fd could point to same struct file_obj. struct file_obj is
		 *  supposed to be freed only when reference count reaches zero. With zeroing out
		 *  the memory, we could observe issues immediately if something is wrong (e.g.,
		 *  one fd is still points to this struct). We can remove this later after we
		 *  make sure the code about adding and decreasing the reference to the struct
		 *  working as expected.
		 */
		memset(saved_obj, 0, sizeof(struct file_obj));
		D_FREE(saved_obj);
		if (rc && bLog)
			D_ERROR("DBG> dfs_release() failed. %s\n", strerror(rc));
	}
}

static void
free_dirfd(int idx)
{
	int             i, rc;
	struct dir_obj *saved_obj = NULL;

	rc = pthread_mutex_lock(&lock_dirfd);
	D_ASSERT(rc == 0);

	dir_list[idx]->ref_count--;
	if (dir_list[idx]->ref_count == 0)
		saved_obj = dir_list[idx];
	dir_list[idx] = NULL;

	if (idx < next_free_dirfd)
		next_free_dirfd = idx;

	if (idx == last_dirfd) {
		for (i = idx - 1; i >= 0; i--) {
			if (dir_list[i]) {
				last_dirfd = i;
				break;
			}
		}
	}

	num_dirfd--;
	rc = pthread_mutex_unlock(&lock_dirfd);
	D_ASSERT(rc == 0);

	if (saved_obj) {
		/* free memory for path and ents. */
		D_FREE(saved_obj->path);
		D_FREE(saved_obj->ents);
		rc = dfs_release(saved_obj->dir);
		if (rc && bLog) {
			D_ERROR("failed to call dfs_release(). rc = %d, %s\n",
				rc, strerror(rc));
		}
		/** This memset() is not necessary. It is left here intended. In case of duplicated
		 *  fd exists, multiple fd could point to same struct dir_obj. struct dir_obj is
		 *  supposed to be freed only when reference count reaches zero. With zeroing out
		 *  the memory, we could observe issues immediately if something is wrong (e.g.,
		 *  one fd is still points to this struct). We can remove this later after we
		 *  make sure the code about adding and decreasing the reference to the struct
		 *  working as expected.
		 */
		memset(saved_obj, 0, sizeof(struct dir_obj));
		D_FREE(saved_obj);
	}
}

static void
free_map(int idx)
{
	int i, rc;

	rc = pthread_mutex_lock(&lock_mmap);
	D_ASSERT(rc == 0);
	mmap_list[idx].addr = NULL;
	/* Need to call free_fd(). */
	if (file_list[mmap_list[idx].fd - FD_FILE_BASE]->idx_mmap >= MAX_MMAP_BLOCK)
		free_fd(mmap_list[idx].fd - FD_FILE_BASE);
	mmap_list[idx].fd = -1;

	if (idx < next_free_map)
		next_free_map = idx;

	if (idx == last_map) {
		for (i = idx - 1; i >= 0; i--) {
			if (mmap_list[i].addr) {
				last_map = i;
				break;
			}
		}
	}

	num_map--;
	rc = pthread_mutex_unlock(&lock_mmap);
	D_ASSERT(rc == 0);
}

static int
Get_Fd_Redirected(int fd)
{
	int i, rc, fd_ret = fd;

	if (fd >= FD_FILE_BASE)
		return fd;

	rc = pthread_mutex_lock(&lock_fd_dup2ed);
	D_ASSERT(rc == 0);
	if (num_fd_dup2ed > 0) {
		for (i = 0; i < MAX_FD_DUP2ED; i++) {
			if (fd_dup2_list[i].fd_src == fd) {
				fd_ret = fd_dup2_list[i].fd_dest;
				break;
			}
		}
	}
	rc = pthread_mutex_unlock(&lock_fd_dup2ed);
	D_ASSERT(rc == 0);

	return fd_ret;
}

/* This fd is a fake fd. There exists a associated kernel fd with dup2.
 * Need to check whether fd is in fd_dup2_list[], set dest_closed true
 * if yes. Otherwise, close the fake fd.
 */
static void
close_dup_fd_dest_fakefd(int fd)
{
	int i, rc;

	if (fd < FD_FILE_BASE)
		return;

	rc = pthread_mutex_lock(&lock_fd_dup2ed);
	D_ASSERT(rc == 0);
	if (num_fd_dup2ed > 0) {
		for (i = 0; i < MAX_FD_DUP2ED; i++) {
			if (fd_dup2_list[i].fd_dest == fd) {
				fd_dup2_list[i].dest_closed = true;
				rc = pthread_mutex_unlock(&lock_fd_dup2ed);
				D_ASSERT(rc == 0);
				return;
			}
		}
	}
	rc = pthread_mutex_unlock(&lock_fd_dup2ed);
	D_ASSERT(rc == 0);

	free_fd(fd - FD_FILE_BASE);
}

/* This fd is a fd from kernel and it is associated with a fake fd.
 * Need to 1) close(fd) 2) remove the entry in fd_dup2_list[] 3) close
 * the fake fd if dest_closed is true.
 */
static int
close_dup_fd_src(int (*next_close)(int fd), int fd)
{
	int i, rc, idx_dup = -1, fd_dest = -1;

	/* close the fd from kernel */
	rc = next_close(fd);
	if (rc != 0)
		return (-1);

	/* remove the fd_dup entry */
	rc = pthread_mutex_lock(&lock_fd_dup2ed);
	D_ASSERT(rc == 0);
	if (num_fd_dup2ed > 0) {
		for (i = 0; i < MAX_FD_DUP2ED; i++) {
			if (fd_dup2_list[i].fd_src == fd) {
				idx_dup = i;
				if (fd_dup2_list[i].dest_closed)
					fd_dest = fd_dup2_list[i].fd_dest;
				/* clear the value to free */
				fd_dup2_list[i].fd_src  = -1;
				fd_dup2_list[i].fd_dest = -1;
				fd_dup2_list[i].dest_closed = false;
				num_fd_dup2ed--;
				break;
			}
		}
	}
	rc = pthread_mutex_unlock(&lock_fd_dup2ed);
	D_ASSERT(rc == 0);

	if (idx_dup < 0) {
		D_ERROR("failed to find fd %d in fd_dup2_list[].", fd);
		errno = EINVAL;
		return (-1);
	}
	if (fd_dest > 0)
		free_fd(fd_dest - FD_FILE_BASE);

	return 0;
}

static void
init_fd_dup2_list(void)
{
	int i, rc;

	rc = pthread_mutex_lock(&lock_fd_dup2ed);
	D_ASSERT(rc == 0);
	for (i = 0; i < MAX_FD_DUP2ED; i++) {
		fd_dup2_list[i].fd_src  = -1;
		fd_dup2_list[i].fd_dest = -1;
		fd_dup2_list[i].dest_closed = false;
	}
	rc = pthread_mutex_unlock(&lock_fd_dup2ed);
	D_ASSERT(rc == 0);
}

static int
allocate_dup2ed_fd(const int fd_src, const int fd_dest)
{
	int i, rc;

	/* Not many applications use dup2(). Normally the number of fd duped is small. */
	rc = pthread_mutex_lock(&lock_fd_dup2ed);
	D_ASSERT(rc == 0);
	if (num_fd_dup2ed < MAX_FD_DUP2ED) {
		for (i = 0; i < MAX_FD_DUP2ED; i++) {
			if (fd_dup2_list[i].fd_src == -1) {
				fd_dup2_list[i].fd_src  = fd_src;
				fd_dup2_list[i].fd_dest = fd_dest;
				fd_dup2_list[i].dest_closed = false;
				num_fd_dup2ed++;
				rc = pthread_mutex_unlock(&lock_fd_dup2ed);
				D_ASSERT(rc == 0);
				return i;
			}
		}
	}
	rc = pthread_mutex_unlock(&lock_fd_dup2ed);
	D_ASSERT(rc == 0);

	if (bLog)
		D_ERROR("fd_dup2_list[] is out of space. MAX_FD_DUP2ED needs to be increased\n");
	errno = EMFILE;
	return (-1);
}

static int
query_fd_forward_dest(int fd_src)
{
	int i, rc, fd_dest = -1;

	rc = pthread_mutex_lock(&lock_fd_dup2ed);
	D_ASSERT(rc == 0);
	if (num_fd_dup2ed > 0) {
		for (i = 0; i < MAX_FD_DUP2ED; i++) {
			if (fd_src == fd_dup2_list[i].fd_src) {
				fd_dest = fd_dup2_list[i].fd_dest;
				rc = pthread_mutex_unlock(&lock_fd_dup2ed);
				D_ASSERT(rc == 0);
				return fd_dest;
			}
		}
	}
	rc = pthread_mutex_unlock(&lock_fd_dup2ed);
	D_ASSERT(rc == 0);
	return -1;
}

static int
allocate_a_fd_from_kernel(void)
{
	struct timespec times_loc;
	char            file_name[128];

	clock_gettime(CLOCK_REALTIME, &times_loc);
	snprintf(file_name, sizeof(file_name) - 1, "dummy_%ld_%ld", times_loc.tv_sec,
		 times_loc.tv_nsec);
	return memfd_create(file_name, 0);
}

static void
close_all_duped_fd(void)
{
	int i;

	/* Only the main thread will call this function in the destruction phase */
	for (i = 0; i < MAX_FD_DUP2ED; i++) {
		if (fd_dup2_list[i].fd_src >= 0)
			close_dup_fd_src(libc_close, fd_dup2_list[i].fd_src);
	}
	num_fd_dup2ed = 0;
}

static int
check_path_with_dirfd(int dirfd, char **full_path_out, const char *rel_path, int *error)
{
	int len_str;

	*error = 0;
	*full_path_out = NULL;

	if (dirfd >= FD_DIR_BASE) {
		len_str = asprintf(full_path_out, "%s/%s",
				   dir_list[dirfd - FD_DIR_BASE]->path, rel_path);
		if (len_str >= DFS_MAX_PATH)
			goto out_toolong;
		else if (len_str < 0)
			goto out_oom;
	} else if (dirfd == AT_FDCWD) {
		len_str = asprintf(full_path_out, "%s/%s", cur_dir, rel_path);
		if (len_str >= DFS_MAX_PATH)
			goto out_toolong;
		else if (len_str < 0)
			goto out_oom;
	} else {
		char    path_fd_dir[32];
		ssize_t bytes_read;

		/* 32 bytes should be more than enough */
		snprintf(path_fd_dir, 32, "/proc/self/fd/%d", dirfd);
		*full_path_out = malloc(DFS_MAX_PATH);
		if (*full_path_out == NULL)
			goto out_oom;
		bytes_read = readlink(path_fd_dir, *full_path_out, DFS_MAX_PATH);
		if (bytes_read >= DFS_MAX_PATH) {
			(*full_path_out)[DFS_MAX_PATH - 1] = 0;
			goto out_toolong;
		} else if (bytes_read < 0) {
			goto out_readlink;
		}
		len_str = snprintf(*full_path_out + bytes_read, DFS_MAX_PATH - bytes_read, "/%s",
				   rel_path);
		if ((len_str + (int)bytes_read) >= DFS_MAX_PATH)
			goto out_toolong;
		(*full_path_out)[len_str + (int)bytes_read] = 0;
	}

	return query_dfs_mount(*full_path_out);

out_toolong:
	*error = ENAMETOOLONG;
	if (*full_path_out) {
		free(*full_path_out);
		*full_path_out = NULL;
	}
	if (bLog)
		D_ERROR("path is too long.");
	return (-1);

out_oom:
	*error = ENOMEM;
	if (bLog)
		D_ERROR("fail to allocate memory.");
	return (-1);

out_readlink:
	*error = errno;
	if (*full_path_out) {
		free(*full_path_out);
		*full_path_out = NULL;
	}
	if (bLog)
		D_ERROR("error in readlink(). %s", strerror(errno));
	return (-1);
}

static int
open_common(int (*real_open)(const char *pathname, int oflags, ...), const char *caller_name,
	    const char *pathname, int oflags, ...)
{
	unsigned int     mode     = 0664;
	int              two_args = 1, rc, is_target_path, idx_fd, idx_dirfd;
	dfs_obj_t       *dfs_obj;
	dfs_obj_t       *parent;
	mode_t           mode_query = 0, mode_parent = 0;
	struct dfs_mt   *dfs_mt;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path = NULL;

	if (pathname == NULL) {
		errno = EFAULT;
		return (-1);
	}
	if (oflags & O_CREAT) {
		va_list arg;

		va_start(arg, oflags);
		mode = va_arg(arg, unsigned int);
		mode = mode & mode_not_umask;
		va_end(arg);
		two_args = 0;
	}

	if (!hook_enabled)
		goto org_func;

	rc = query_path(pathname, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc == ENOENT)
		D_GOTO(out_error, rc = ENOENT);

	if (!is_target_path)
		goto org_func;

	if (oflags & __O_TMPFILE) {
		if (!parent && (strncmp(item_name, "/", 2) == 0))
			rc = dfs_access(dfs_mt->dfs, NULL, NULL, X_OK | W_OK);
		else
			rc = dfs_access(dfs_mt->dfs, parent, item_name, X_OK | W_OK);
		if (rc) {
			if (rc == 1)
				rc = 13;
			D_GOTO(out_error, rc);
		}
	}

	if ((oflags & O_RDWR) && (oflags & O_CREAT)) {
		if (parent == NULL) {
			dfs_obj_t *parent_obj;

			rc = dfs_lookup(dfs_mt->dfs, "/", O_RDONLY, &parent_obj,
					&mode_parent, NULL);
			if (rc)
				D_GOTO(out_error, rc);
			dfs_release(parent_obj);
		} else {
			rc = dfs_get_mode(parent, &mode_parent);
			if (rc)
				D_GOTO(out_error, rc);
		}
		if ((S_IXUSR & mode_parent) == 0 || (S_IWUSR & mode_parent) == 0)
			D_GOTO(out_error, rc = EACCES);
	}
	/* file/dir should be handled by DFS */
	if (oflags & O_CREAT) {
		rc = dfs_open(dfs_mt->dfs, parent, item_name, mode | S_IFREG, oflags, 0, 0, NULL,
			      &dfs_obj);
		mode_query = S_IFREG;
	} else if (!parent && (strncmp(item_name, "/", 2) == 0)) {
		rc = dfs_lookup(dfs_mt->dfs, "/", oflags, &dfs_obj, &mode_query, NULL);
	} else {
		rc = dfs_lookup_rel(dfs_mt->dfs, parent, item_name, oflags, &dfs_obj, &mode_query,
				    NULL);
	}

	if (rc)
		D_GOTO(out_error, rc);

	if (S_ISDIR(mode_query)) {
		rc = find_next_available_dirfd(NULL, &idx_dirfd);
		if (rc)
			D_GOTO(out_error, rc);

		dir_list[idx_dirfd]->dfs_mt      = dfs_mt;
		dir_list[idx_dirfd]->fd          = idx_dirfd + FD_DIR_BASE;
		dir_list[idx_dirfd]->offset      = 0;
		dir_list[idx_dirfd]->dir         = dfs_obj;
		dir_list[idx_dirfd]->num_ents    = 0;
		dir_list[idx_dirfd]->st_ino      = FAKE_ST_INO(full_path);
		memset(&dir_list[idx_dirfd]->anchor, 0, sizeof(daos_anchor_t));
		dir_list[idx_dirfd]->path        = NULL;
		dir_list[idx_dirfd]->ents        = NULL;
		if (strncmp(full_path, "/", 2) == 0)
			full_path[0] = 0;

		/* allocate memory for ents[]. */
		D_ALLOC_ARRAY(dir_list[idx_dirfd]->ents, READ_DIR_BATCH_SIZE);
		if (dir_list[idx_dirfd]->ents == NULL) {
			free_dirfd(idx_dirfd);
			D_GOTO(out_error, rc = ENOMEM);
		}
		D_ASPRINTF(dir_list[idx_dirfd]->path, "%s%s", dfs_mt->fs_root, full_path);
		if (dir_list[idx_dirfd]->path == NULL) {
			free_dirfd(idx_dirfd);
			D_GOTO(out_error, rc = ENOMEM);
		}
		if (strnlen(dir_list[idx_dirfd]->path, DFS_MAX_PATH) >= DFS_MAX_PATH) {
			if (bLog)
				D_ERROR("path is longer than DFS_MAX_PATH\n!");
			free_dirfd(idx_dirfd);
			D_GOTO(out_error, rc = ENAMETOOLONG);
		}
		FREE(parent_dir);

		return (idx_dirfd + FD_DIR_BASE);
	}
	if (bLog)
		atomic_fetch_add_relaxed(&num_open, 1);

	rc = find_next_available_fd(NULL, &idx_fd);
	if (rc)
		D_GOTO(out_error, rc);

	file_list[idx_fd]->dfs_mt      = dfs_mt;
	file_list[idx_fd]->file        = dfs_obj;
	file_list[idx_fd]->parent      = parent;
	file_list[idx_fd]->st_ino      = FAKE_ST_INO(full_path);
	file_list[idx_fd]->idx_mmap    = -1;
	file_list[idx_fd]->open_flag   = oflags;
	/* NEED to set at the end of file if O_APPEND!!!!!!!! */
	file_list[idx_fd]->offset = 0;
	strncpy(file_list[idx_fd]->item_name, item_name, DFS_MAX_NAME);

	FREE(parent_dir);

	return (idx_fd + FD_FILE_BASE);

org_func:
	FREE(parent_dir);
	if (two_args)
		return real_open(pathname, oflags);
	else
		return real_open(pathname, oflags, mode);
out_error:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

/* When the open() in ld.so is called, new_open_ld() will be executed. */

static int
new_open_ld(const char *pathname, int oflags, ...)
{
	unsigned int mode;
	int          two_args = 1, rc;

	if (oflags & O_CREAT) {
		va_list arg;

		va_start(arg, oflags);
		mode = va_arg(arg, unsigned int);
		va_end(arg);
		two_args = 0;
	}

	if (two_args)
		rc = open_common(ld_open, "new_open_ld", pathname, oflags);
	else
		rc = open_common(ld_open, "new_open_ld", pathname, oflags, mode);

	return rc;
}

/* When the open() in libc.so is called, new_open_libc() will be executed. */
static int
new_open_libc(const char *pathname, int oflags, ...)
{
	unsigned int mode;
	int          two_args = 1, rc;

	if (oflags & O_CREAT) {
		va_list arg;

		va_start(arg, oflags);
		mode = va_arg(arg, unsigned int);
		va_end(arg);
		two_args = 0;
	}

	if (two_args)
		rc = open_common(libc_open, "new_open_libc", pathname, oflags);
	else
		rc = open_common(libc_open, "new_open_libc", pathname, oflags, mode);

	return rc;
}

/* When the open() in libpthread.so is called, new_open_pthread() will be executed. */
static int
new_open_pthread(const char *pathname, int oflags, ...)
{
	unsigned int mode;
	int          two_args = 1, rc;

	if (oflags & O_CREAT) {
		va_list arg;

		va_start(arg, oflags);
		mode = va_arg(arg, unsigned int);
		va_end(arg);
		two_args = 0;
	}

	if (two_args)
		rc = open_common(pthread_open, "new_open_pthread", pathname, oflags);
	else
		rc = open_common(pthread_open, "new_open_pthread", pathname, oflags, mode);

	return rc;
}

static int
new_close_common(int (*next_close)(int fd), int fd)
{
	int fd_Directed;

	if (!hook_enabled)
		return next_close(fd);

	fd_Directed = Get_Fd_Redirected(fd);

	if (fd_Directed >= FD_DIR_BASE) {
		/* directory */
		free_dirfd(fd_Directed - FD_DIR_BASE);
		return 0;
	} else if (fd_Directed >= FD_FILE_BASE) {
		/* This fd is a kernel fd. There was a duplicate fd created. */
		if (fd < FD_FILE_BASE)
			return close_dup_fd_src(next_close, fd);

		/* This fd is a fake fd. There exists a associated kernel fd with dup2. */
		close_dup_fd_dest_fakefd(fd);
		return 0;
	}

	return next_close(fd);
}

static int
new_close_libc(int fd)
{
	return new_close_common(libc_close, fd);
}

static int
new_close_pthread(int fd)
{
	return new_close_common(pthread_close, fd);
}

static int
new_close_nocancel(int fd)
{
	return new_close_common(libc_close_nocancel, fd);
}

static ssize_t
read_comm(ssize_t (*next_read)(int fd, void *buf, size_t size), int fd, void *buf, size_t size)
{
	ssize_t rc;

	if (!hook_enabled)
		return next_read(fd, buf, size);

	if (fd >= FD_FILE_BASE) {
		rc = pread(fd, buf, size, file_list[fd - FD_FILE_BASE]->offset);
		if (rc >= 0)
			file_list[fd - FD_FILE_BASE]->offset += rc;
		return rc;
	} else {
		return next_read(fd, buf, size);
	}
}

static ssize_t
new_read_libc(int fd, void *buf, size_t size)
{
	return read_comm(libc_read, fd, buf, size);
}

static ssize_t
new_read_pthread(int fd, void *buf, size_t size)
{
	return read_comm(pthread_read, fd, buf, size);
}

ssize_t
pread(int fd, void *buf, size_t size, off_t offset)
{
	daos_size_t bytes_read;
	char       *ptr = (char *)buf;
	int         rc;
	d_iov_t     iov;
	d_sg_list_t sgl;

	if (size == 0)
		return 0;

	if (next_pread == NULL) {
		next_pread = dlsym(RTLD_NEXT, "pread64");
		D_ASSERT(next_pread != NULL);
	}

	if (!hook_enabled || fd < FD_FILE_BASE)
		return next_pread(fd, buf, size, offset);

	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, (void *)ptr, size);
	sgl.sg_iovs = &iov;
	rc          = dfs_read(file_list[fd - FD_FILE_BASE]->dfs_mt->dfs,
			       file_list[fd - FD_FILE_BASE]->file, &sgl,
			       offset, &bytes_read, NULL);
	if (rc) {
		if (bLog)
			D_ERROR("dfs_read(%p, %zu) failed (%d): %s\n",
				(void *)ptr, size, rc, strerror(rc));
		errno      = rc;
		bytes_read = -1;
	}

	if (bLog)
		atomic_fetch_add_relaxed(&num_read, 1);

	return (ssize_t)bytes_read;
}

ssize_t
pread64(int fd, void *buf, size_t size, off_t offset) __attribute__((alias("pread")));

ssize_t
__pread64(int fd, void *buf, size_t size, off_t offset) __attribute__((alias("pread")));

ssize_t
write_comm(ssize_t (*next_write)(int fd, const void *buf, size_t size), int fd, const void *buf,
	   size_t size)
{
	ssize_t rc;

	if (!hook_enabled)
		return next_write(fd, buf, size);

	if (fd >= FD_FILE_BASE) {
		rc = pwrite(fd, buf, size, file_list[fd - FD_FILE_BASE]->offset);
		if (rc >= 0)
			file_list[fd - FD_FILE_BASE]->offset += rc;
		return rc;
	} else {
		return next_write(fd, buf, size);
	}
}

static ssize_t
new_write_libc(int fd, const void *buf, size_t size)
{
	return write_comm(libc_write, fd, buf, size);
}

static ssize_t
new_write_pthread(int fd, const void *buf, size_t size)
{
	return write_comm(pthread_write, fd, buf, size);
}

ssize_t
pwrite(int fd, const void *buf, size_t size, off_t offset)
{
	char       *ptr = (char *)buf;
	int         rc;
	d_iov_t     iov;
	d_sg_list_t sgl;

	if (size == 0)
		return 0;

	if (next_pwrite == NULL) {
		next_pwrite = dlsym(RTLD_NEXT, "pwrite64");
		D_ASSERT(next_pwrite != NULL);
	}
	if (!hook_enabled || fd < FD_FILE_BASE)
		return next_pwrite(fd, buf, size, offset);

	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, (void *)ptr, size);
	sgl.sg_iovs = &iov;
	rc          = dfs_write(file_list[fd - FD_FILE_BASE]->dfs_mt->dfs,
				file_list[fd - FD_FILE_BASE]->file, &sgl, offset, NULL);
	if (rc) {
		if (bLog)
			D_ERROR("dfs_write(%p, %zu) failed (%d): %s\n",
				(void *)ptr, size, rc, strerror(rc));
		errno = rc;
		return (-1);
	}
	if (bLog)
		atomic_fetch_add_relaxed(&num_write, 1);

	return size;
}

ssize_t
pwrite64(int fd, const void *buf, size_t size, off_t offset) __attribute__((alias("pwrite")));

ssize_t
__pwrite64(int fd, const void *buf, size_t size, off_t offset) __attribute__((alias("pwrite")));

static int
new_fxstat(int vers, int fd, struct stat *buf)
{
	int rc;

	if (!hook_enabled || fd < FD_FILE_BASE)
		return next_fxstat(vers, fd, buf);

	if (fd < FD_DIR_BASE) {
		rc          = dfs_ostat(file_list[fd - FD_FILE_BASE]->dfs_mt->dfs,
					file_list[fd - FD_FILE_BASE]->file, buf);
		buf->st_ino = file_list[fd - FD_FILE_BASE]->st_ino;
	} else {
		rc          = dfs_ostat(dir_list[fd - FD_DIR_BASE]->dfs_mt->dfs,
					dir_list[fd - FD_DIR_BASE]->dir, buf);
		buf->st_ino = dir_list[fd - FD_DIR_BASE]->st_ino;
	}

	if (rc) {
		if (bLog)
			D_ERROR("Failed to call dfs_ostat. %s\n", strerror(rc));
		errno = rc;
		rc    = -1;
	}

	if (bLog)
		atomic_fetch_add_relaxed(&num_stat, 1);

	return 0;
}

static int
new_xstat(int ver, const char *path, struct stat *stat_buf)
{
	int              is_target_path, rc;
	dfs_obj_t       *parent;
	dfs_obj_t       *obj;
	struct dfs_mt   *dfs_mt;
	mode_t           mode;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	if (!hook_enabled)
		return next_xstat(ver, path, stat_buf);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;
	if (bLog)
		atomic_fetch_add_relaxed(&num_stat, 1);

	if (!parent && (strncmp(item_name, "/", 2) == 0)) {
		rc = dfs_lookup(dfs_mt->dfs, "/", O_RDONLY, &obj, &mode, stat_buf);
	} else {
		rc = dfs_lookup_rel(dfs_mt->dfs, parent, item_name, O_RDONLY, &obj, &mode,
				    stat_buf);
	}
	stat_buf->st_mode = mode;
	if (rc)
		D_GOTO(out_err, rc);

	stat_buf->st_ino = FAKE_ST_INO(full_path);
	dfs_release(obj);
	if (S_ISDIR(mode))
		/* st_nlink should be 2 or larger number for dir */
		stat_buf->st_nlink = 2;
	FREE(parent_dir);

	return 0;

out_org:
	FREE(parent_dir);
	return next_xstat(ver, path, stat_buf);

out_err:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

static int
new_lxstat(int ver, const char *path, struct stat *stat_buf)
{
	int              is_target_path, rc;
	dfs_obj_t       *parent;
	struct dfs_mt   *dfs_mt;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	if (!hook_enabled)
		return libc_lxstat(ver, path, stat_buf);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;
	if (bLog)
		atomic_fetch_add_relaxed(&num_stat, 1);

	if (!parent && (strncmp(item_name, "/", 2) == 0))
		rc = dfs_stat(dfs_mt->dfs, NULL, NULL, stat_buf);
	else
		rc = dfs_stat(dfs_mt->dfs, parent, item_name, stat_buf);
	if (rc)
		goto out_err;
	stat_buf->st_ino = FAKE_ST_INO(full_path);
	if (S_ISDIR(stat_buf->st_mode))
		/* st_nlink should be 2 or larger number for dir in rm */
		stat_buf->st_nlink = 2;
	FREE(parent_dir);
	return 0;

out_org:
	FREE(parent_dir);
	return libc_lxstat(ver, path, stat_buf);

out_err:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

static int
new_fxstatat(int ver, int dirfd, const char *path, struct stat *stat_buf, int flags)
{
	int  idx_dfs, error = 0, rc;
	char *full_path = NULL;

	if (!hook_enabled)
		return next_fxstatat(ver, dirfd, path, stat_buf, flags);

	if (path[0] == '/') {
		/* Absolute path, dirfd is ignored */
		if (flags & AT_SYMLINK_NOFOLLOW)
			return new_lxstat(1, path, stat_buf);
		else
			return new_xstat(1, path, stat_buf);
	}

	idx_dfs = check_path_with_dirfd(dirfd, &full_path, path, &error);
	if (error)
		goto out_err;

	if (idx_dfs >= 0) {
		if (flags & AT_SYMLINK_NOFOLLOW)
			rc = new_lxstat(1, full_path, stat_buf);
		else
			rc = new_xstat(1, full_path, stat_buf);
	} else {
		rc = next_fxstatat(ver, dirfd, path, stat_buf, flags);
	}

	error = errno;
	if (full_path) {
		free(full_path);
		errno = error;
	}
	return rc;

out_err:
	errno = error;
	return (-1);
}

static void
copy_stat_to_statx(const struct stat *stat_buf, struct statx *statx_buf)
{
	memset(statx_buf, 0, sizeof(struct statx));
	statx_buf->stx_blksize = stat_buf->st_blksize;
	statx_buf->stx_nlink   = stat_buf->st_nlink;
	statx_buf->stx_uid     = stat_buf->st_uid;
	statx_buf->stx_gid     = stat_buf->st_gid;
	statx_buf->stx_mode    = stat_buf->st_mode;
	statx_buf->stx_ino     = stat_buf->st_ino;
	statx_buf->stx_size    = stat_buf->st_size;
	statx_buf->stx_blocks  = stat_buf->st_blocks;

	statx_buf->stx_atime.tv_sec  = stat_buf->st_atim.tv_sec;
	statx_buf->stx_atime.tv_nsec = stat_buf->st_atim.tv_nsec;

	statx_buf->stx_btime.tv_sec  = stat_buf->st_mtim.tv_sec;
	statx_buf->stx_btime.tv_nsec = stat_buf->st_mtim.tv_nsec;

	statx_buf->stx_ctime.tv_sec  = stat_buf->st_ctim.tv_sec;
	statx_buf->stx_ctime.tv_nsec = stat_buf->st_ctim.tv_nsec;

	statx_buf->stx_mtime.tv_sec  = stat_buf->st_mtim.tv_sec;
	statx_buf->stx_mtime.tv_nsec = stat_buf->st_mtim.tv_nsec;
}

int
statx(int dirfd, const char *path, int flags, unsigned int mask, struct statx *statx_buf)
{
	int         rc, idx_dfs, error = 0;
	struct stat stat_buf;
	char        *full_path = NULL;

	if (next_statx == NULL) {
		next_statx = dlsym(RTLD_NEXT, "statx");
		D_ASSERT(next_statx != NULL);
	}
	if (!hook_enabled)
		return next_statx(dirfd, path, flags, mask, statx_buf);

	/* absolute path, dirfd is ignored */
	if (path[0] == '/') {
		if (flags & AT_SYMLINK_NOFOLLOW)
			rc = new_lxstat(1, path, &stat_buf);
		else
			rc = new_xstat(1, path, &stat_buf);
		copy_stat_to_statx(&stat_buf, statx_buf);
		return rc;
	}

	idx_dfs = check_path_with_dirfd(dirfd, &full_path, path, &error);
	if (error)
		goto out_err;

	if (idx_dfs >= 0) {
		if (flags & AT_SYMLINK_NOFOLLOW)
			rc = new_lxstat(1, full_path, &stat_buf);
		else
			rc = new_xstat(1, full_path, &stat_buf);
		error = errno;
		copy_stat_to_statx(&stat_buf, statx_buf);
	} else {
		rc = next_statx(dirfd, path, flags, mask, statx_buf);
		error = errno;
	}
	if (full_path) {
		free(full_path);
		errno = error;
	}
	return rc;

out_err:
	if (full_path)
		free(full_path);
	errno = error;
	return (-1);
}

static off_t
lseek_comm(off_t (*next_lseek)(int fd, off_t offset, int whence), int fd, off_t offset, int whence)
{
	int         rc;
	off_t       new_offset;
	struct stat fstat;

	if (!hook_enabled || fd < FD_FILE_BASE)
		return next_lseek(fd, offset, whence);

	if (bLog)
		atomic_fetch_add_relaxed(&num_seek, 1);

	switch (whence) {
	case SEEK_SET:
		new_offset = offset;
		break;
	case SEEK_CUR:
		new_offset = file_list[fd - FD_FILE_BASE]->offset + offset;
		break;
	case SEEK_END:
		fstat.st_size = 0;
		rc            = new_fxstat(1, fd, &fstat);
		if (rc != 0)
			return (-1);
		new_offset = fstat.st_size + offset;
		break;
	default:
		errno = EINVAL;
		return (-1);
	}

	if (new_offset < 0) {
		errno = EINVAL;
		return (-1);
	}

	file_list[fd - FD_FILE_BASE]->offset = new_offset;
	return new_offset;
}

static off_t
new_lseek_libc(int fd, off_t offset, int whence)
{
	return lseek_comm(libc_lseek, fd, offset, whence);
}

static off_t
new_lseek_pthread(int fd, off_t offset, int whence)
{
	return lseek_comm(pthread_lseek, fd, offset, whence);
}

int
statfs(const char *pathname, struct statfs *sfs)
{
	daos_pool_info_t info = {.pi_bits = DPI_SPACE};
	dfs_obj_t       *parent;
	int              rc, is_target_path;
	struct dfs_mt   *dfs_mt;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	if (next_statfs == NULL) {
		next_statfs = dlsym(RTLD_NEXT, "statfs");
		D_ASSERT(next_statfs != NULL);
	}

	if (!hook_enabled)
		return next_statfs(pathname, sfs);

	rc = query_path(pathname, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;

	rc = daos_pool_query(dfs_mt->poh, NULL, &info, NULL, NULL);
	if (rc != 0)
		goto out_err;

	sfs->f_blocks = info.pi_space.ps_space.s_total[DAOS_MEDIA_SCM] +
			info.pi_space.ps_space.s_total[DAOS_MEDIA_NVME];
	sfs->f_bfree = info.pi_space.ps_space.s_free[DAOS_MEDIA_SCM] +
		       info.pi_space.ps_space.s_free[DAOS_MEDIA_NVME];
	sfs->f_bsize  = 1;
	sfs->f_files  = -1;
	sfs->f_ffree  = -1;
	sfs->f_bavail = sfs->f_bfree;

	FREE(parent_dir);
	return 0;

out_org:
	FREE(parent_dir);
	return next_statfs(pathname, sfs);

out_err:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

int
fstatfs(int fd, struct statfs *sfs)
{
	int              rc;
	struct dfs_mt   *dfs_mt;
	daos_pool_info_t info = {.pi_bits = DPI_SPACE};

	if (next_fstatfs == NULL) {
		next_fstatfs = dlsym(RTLD_NEXT, "fstatfs");
		D_ASSERT(next_fstatfs != NULL);
	}

	if (!hook_enabled)
		return next_fstatfs(fd, sfs);

	if (fd < FD_FILE_BASE)
		return next_fstatfs(fd, sfs);

	if (fd < FD_DIR_BASE)
		dfs_mt = file_list[fd - FD_FILE_BASE]->dfs_mt;
	else
		dfs_mt = dir_list[fd - FD_DIR_BASE]->dfs_mt;
	rc = daos_pool_query(dfs_mt->poh, NULL, &info, NULL, NULL);
	if (rc != 0) {
		errno = rc;
		return (-1);
	}

	sfs->f_blocks = info.pi_space.ps_space.s_total[DAOS_MEDIA_SCM] +
			info.pi_space.ps_space.s_total[DAOS_MEDIA_NVME];
	sfs->f_bfree = info.pi_space.ps_space.s_free[DAOS_MEDIA_SCM] +
		       info.pi_space.ps_space.s_free[DAOS_MEDIA_NVME];
	sfs->f_bsize  = 1;
	sfs->f_files  = -1;
	sfs->f_ffree  = -1;
	sfs->f_bavail = sfs->f_bfree;

	return 0;
}

int
statfs64(const char *pathname, struct statfs64 *sfs) __attribute__((alias("statfs")));

int
__statfs(const char *pathname, struct statfs *sfs)
	__attribute__((alias("statfs"), leaf, nonnull, nothrow));

int
statvfs(const char *pathname, struct statvfs *svfs)
{
	daos_pool_info_t info = {.pi_bits = DPI_SPACE};
	dfs_obj_t       *parent;
	int              rc, is_target_path;
	struct dfs_mt   *dfs_mt;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	if (next_statvfs == NULL) {
		next_statvfs = dlsym(RTLD_NEXT, "statvfs");
		D_ASSERT(next_statvfs != NULL);
	}

	if (!hook_enabled)
		return next_statvfs(pathname, svfs);

	rc = query_path(pathname, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;

	rc = daos_pool_query(dfs_mt->poh, NULL, &info, NULL, NULL);
	if (rc) {
		if (bLog)
			D_ERROR("Failed to query pool: "DF_RC"\n", DP_RC(rc));
		errno = daos_der2errno(rc);
		D_GOTO(out_err, rc = errno);
	}

	svfs->f_blocks = info.pi_space.ps_space.s_total[DAOS_MEDIA_SCM] +
			 info.pi_space.ps_space.s_total[DAOS_MEDIA_NVME];
	svfs->f_bfree  = info.pi_space.ps_space.s_free[DAOS_MEDIA_SCM] +
			 info.pi_space.ps_space.s_free[DAOS_MEDIA_NVME];
	svfs->f_bsize  = 1;
	svfs->f_files  = -1;
	svfs->f_ffree  = -1;
	svfs->f_bavail = svfs->f_bfree;

	FREE(parent_dir);
	return 0;

out_org:
	FREE(parent_dir);
	return next_statvfs(pathname, svfs);

out_err:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

int
statvfs64(const char *__restrict pathname, struct statvfs64 *__restrict svfs)
	__attribute__((alias("statvfs")));

DIR *
opendir(const char *path)
{
	int              is_target_path, idx_dirfd, rc;
	dfs_obj_t       *parent, *dir_obj;
	mode_t           mode;
	struct dfs_mt   *dfs_mt;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	if (next_opendir == NULL) {
		next_opendir = dlsym(RTLD_NEXT, "opendir");
		D_ASSERT(next_opendir != NULL);
	}
	if (!hook_enabled)
		return next_opendir(path);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err_ret, rc);
	if (!is_target_path) {
		FREE(parent_dir);
		return next_opendir(path);
	}
	if (bLog)
		atomic_fetch_add_relaxed(&num_opendir, 1);

	if (!parent && (strncmp(item_name, "/", 2) == 0)) {
		/* dfs_lookup() is needed for root dir */
		rc = dfs_lookup(dfs_mt->dfs, "/", O_RDONLY, &dir_obj, &mode, NULL);
		if (rc)
			D_GOTO(out_err_ret, rc);
	} else {
		rc = dfs_open(dfs_mt->dfs, parent, item_name, S_IFDIR, O_RDONLY, 0, 0, NULL,
			      &dir_obj);
		if (rc)
			D_GOTO(out_err_ret, rc);
		rc = dfs_get_mode(dir_obj, &mode);
		if (rc)
			D_GOTO(out_err, rc);
	}

	if ((S_IRUSR & mode) == 0)
		D_GOTO(out_err, rc = EACCES);

	rc = find_next_available_dirfd(NULL, &idx_dirfd);
	if (rc)
		D_GOTO(out_err, rc);

	dir_list[idx_dirfd]->dfs_mt   = dfs_mt;
	dir_list[idx_dirfd]->fd       = idx_dirfd + FD_DIR_BASE;
	dir_list[idx_dirfd]->offset   = 0;
	dir_list[idx_dirfd]->dir      = dir_obj;
	dir_list[idx_dirfd]->num_ents = 0;
	memset(&dir_list[idx_dirfd]->anchor, 0, sizeof(daos_anchor_t));
	if (strncmp(full_path, "/", 2) == 0)
		full_path[0] = 0;
	/* allocate memory for ents[]. */
	D_ALLOC_ARRAY(dir_list[idx_dirfd]->ents, READ_DIR_BATCH_SIZE);
	if (dir_list[idx_dirfd]->ents == NULL) {
		free_dirfd(idx_dirfd);
		D_GOTO(out_err, rc = ENOMEM);
	}
	/* allocate memory for path[]. */
	D_ASPRINTF(dir_list[idx_dirfd]->path, "%s%s", dfs_mt->fs_root, full_path);
	if (dir_list[idx_dirfd]->path == NULL) {
		free_dirfd(idx_dirfd);
		D_GOTO(out_err, rc = ENOMEM);
	}
	if (strnlen(dir_list[idx_dirfd]->path, DFS_MAX_PATH) >= DFS_MAX_PATH) {
		if (bLog)
			D_ERROR("path is longer than DFS_MAX_PATH.");
		free_dirfd(idx_dirfd);
		D_GOTO(out_err, rc = ENAMETOOLONG);
	}

	FREE(parent_dir);

	return (DIR *)(dir_list[idx_dirfd]);

out_err:
	dfs_release(dir_obj);

out_err_ret:
	FREE(parent_dir);
	errno = rc;
	return NULL;
}

DIR *
fdopendir(int fd)
{
	if (next_fdopendir == NULL) {
		next_fdopendir = dlsym(RTLD_NEXT, "fdopendir");
		D_ASSERT(next_fdopendir != NULL);
	}
	if (!hook_enabled || fd < FD_DIR_BASE)
		return next_fdopendir(fd);
	if (bLog)
		atomic_fetch_add_relaxed(&num_opendir, 1);

	return (DIR *)(dir_list[fd - FD_DIR_BASE]);
}

int
openat(int dirfd, const char *path, int oflags, ...)
{
	unsigned int mode;
	int          two_args = 1, idx_dfs, error = 0, rc;
	char         *full_path = NULL;

	if (next_openat == NULL) {
		next_openat = dlsym(RTLD_NEXT, "openat");
		D_ASSERT(next_openat != NULL);
	}

	if (oflags & O_CREAT) {
		va_list arg;

		va_start(arg, oflags);
		mode = va_arg(arg, unsigned int);
		va_end(arg);
		two_args = 0;
	}

	if (!hook_enabled)
		goto org_func;

	/* Absolute path, dirfd is ignored */
	if (path[0] == '/') {
		if (two_args)
			return open_common(libc_open, "new_openat", path, oflags);
		else
			return open_common(libc_open, "new_openat", path, oflags, mode);
	}

	/* Relative path */
	idx_dfs = check_path_with_dirfd(dirfd, &full_path, path, &error);
	if (error)
		goto out_err;

	if (idx_dfs >= 0) {
		if (two_args)
			rc = open_common(libc_open, "new_openat", full_path, oflags);
		else
			rc = open_common(libc_open, "new_openat", full_path, oflags, mode);

		error = errno;
		if (full_path) {
			free(full_path);
			errno = error;
		}
		return rc;
	}

	if (full_path)
		free(full_path);

org_func:
	if (two_args)
		return next_openat(dirfd, path, oflags);
	else
		return next_openat(dirfd, path, oflags, mode);

out_err:
	if (full_path)
		free(full_path);
	errno = error;
	return (-1);
}

int
openat64(int dirfd, const char *pathname, int oflags, ...) __attribute__((alias("openat")));

int
__openat_2(int dirfd, const char *path, int oflags)
{
	int  idx_dfs, error = 0, rc;
	char *full_path = NULL;

	/* Check whether path is NULL or not since application provides dirp */
	if (path == NULL) {
		errno = EFAULT;
		return (-1);
	}
	if (next_openat_2 == NULL) {
		next_openat_2 = dlsym(RTLD_NEXT, "__openat_2");
		D_ASSERT(next_openat_2 != NULL);
	}
	if (!hook_enabled)
		return next_openat_2(dirfd, path, oflags);

	if (path[0] == '/')
		return open_common(libc_open, "__openat_2", path, oflags);

	idx_dfs = check_path_with_dirfd(dirfd, &full_path, path, &error);
	if (error)
		goto out_err;

	if (idx_dfs >= 0)
		rc = open_common(libc_open, "__openat_2", full_path, oflags);
	else
		rc = next_openat_2(dirfd, path, oflags);

	error = errno;
	if (full_path) {
		free(full_path);
		errno = error;
	}
	return rc;

out_err:
	errno = error;
	return (-1);
}

int
closedir(DIR *dirp)
{
	int fd;

	if (next_closedir == NULL) {
		next_closedir = dlsym(RTLD_NEXT, "closedir");
		D_ASSERT(next_closedir != NULL);
	}
	if (!hook_enabled)
		return next_closedir(dirp);

	/* Check whether dirp is NULL or not since application provides dirp */
	if (!dirp) {
		if (bLog)
			D_ERROR("invalid parameter: dirp is NULL in closedir().");
		errno = EINVAL;
		return (-1);
	}

	fd = dirfd(dirp);
	if (fd >= FD_DIR_BASE) {
		free_dirfd(dirfd(dirp) - FD_DIR_BASE);
		return 0;
	} else {
		return next_closedir(dirp);
	}
}

static struct dirent *
new_readdir(DIR *dirp)
{
	int               rc        = 0;
	int               len_str   = 0;
	struct dir_obj   *mydir     = (struct dir_obj *)dirp;
	char              *full_path = NULL;

	if (!hook_enabled || mydir->fd < FD_FILE_BASE)
		return next_readdir(dirp);

	if (mydir->fd < FD_DIR_BASE) {
		if (bLog)
			D_ERROR("invalid fd in readdir. Unexpected.");
		errno = EINVAL;
		return NULL;
	}
	if (bLog)
		atomic_fetch_add_relaxed(&num_readdir, 1);

	if (mydir->num_ents)
		goto out_readdir;

	mydir->num_ents = READ_DIR_BATCH_SIZE;
	while (!daos_anchor_is_eof(&mydir->anchor)) {
		rc = dfs_readdir(dir_list[mydir->fd - FD_DIR_BASE]->dfs_mt->dfs, mydir->dir,
				 &mydir->anchor, &mydir->num_ents, mydir->ents);
		if (rc != 0)
			goto out_null_readdir;

		/* We have an entry, so return it */
		if (mydir->num_ents != 0)
			goto out_readdir;
	}

out_null_readdir:
	mydir->num_ents = 0;
	errno           = rc;
	return NULL;
out_readdir:
	mydir->num_ents--;
	mydir->offset++;
	len_str = asprintf(&full_path, "%s/%s",
			   dir_list[mydir->fd - FD_DIR_BASE]->path +
			   dir_list[mydir->fd - FD_DIR_BASE]->dfs_mt->len_fs_root,
			   mydir->ents[mydir->num_ents].d_name);
	if (len_str >= DFS_MAX_PATH) {
		if (bLog)
			D_ERROR("path is too long.");
		free(full_path);
		mydir->num_ents = 0;
		errno = ENAMETOOLONG;
		return NULL;
	} else if (len_str < 0) {
		if (bLog)
			D_ERROR("failed to allocate memory for full_path[].");
		mydir->num_ents = 0;
		errno = ENAMETOOLONG;
		return NULL;
	}
	mydir->ents[mydir->num_ents].d_ino = FAKE_ST_INO(full_path);
	free(full_path);
	return &mydir->ents[mydir->num_ents];
}

/**
 *static char** pre_envp(char *const envp[])
 *{
 *	int	i, num_entry = 0;
 *	char	**new_envp;
 *
 *	if (envp == NULL) {
 *		num_entry = 0;
 *	} else if (envp[0] == NULL) {
 *		num_entry = 0;
 *	} else {
 *		while (envp[num_entry])	{
 *			num_entry++;
 *		}
 *	}
 *
 *	new_envp = malloc(sizeof(char *) * (num_entry + 5));
 *	assert(new_envp != NULL);
 *	for (i = 0; i < num_entry; i++)	{
 *		new_envp[i] = envp[i];
 *	}
 *	for (i = 0; i < 5; i++)	{
 *		new_envp[num_entry + i] = envp_lib[i];
 *	}
 *
 *	return new_envp;
 *}
 *
 *static int
 *new_execve(const char *filename, char *const argv[], char *const envp[])
 *{
 *	printf("DBG> execve(%s)\n", filename);
 *	return real_execve(filename, argv, pre_envp(envp));
 *}
 *
 *static int
 *new_execvp(const char *filename, char *const argv[])
 *{
 *	printf("DBG> execvp(%s)\n", filename);
 *	return real_execve(filename, argv, pre_envp(NULL));
 *}
 *
 *static int
 *new_execv(const char *filename, char *const argv[])
 *{
 *	printf("DBG> execv(%s)\n", filename);
 *	pre_envp(NULL);
 *	return real_execve(filename, argv, pre_envp(NULL));
 *}
 *
 *static pid_t
 *new_fork(void)
 *{
 *	pid_t pid;
 *	pid = real_fork();
 *	if (pid) {
 *		// parent process: do nothing
 *		return pid;
 *	} else {
 *		init_dfs();
 *		return pid;
 *	}
 *}
 */

int
mkdir(const char *path, mode_t mode)
{
	int              is_target_path, rc;
	dfs_obj_t       *parent;
	struct dfs_mt   *dfs_mt;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	if (next_mkdir == NULL) {
		next_mkdir = dlsym(RTLD_NEXT, "mkdir");
		D_ASSERT(next_mkdir != NULL);
	}
	if (!hook_enabled)
		return next_mkdir(path, mode);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;
	if (bLog)
		atomic_fetch_add_relaxed(&num_mkdir, 1);

	if (!parent && (strncmp(item_name, "/", 2) == 0))
		D_GOTO(out_err, rc = EEXIST);

	rc = dfs_mkdir(dfs_mt->dfs, parent, item_name, mode & mode_not_umask, 0);
	if (rc)
		D_GOTO(out_err, rc);

	FREE(parent_dir);
	return 0;

out_org:
	FREE(parent_dir);
	return next_mkdir(path, mode);

out_err:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

int
mkdirat(int dirfd, const char *path, mode_t mode)
{
	int  idx_dfs, error = 0, rc;
	char *full_path = NULL;

	if (next_mkdirat == NULL) {
		next_mkdirat = dlsym(RTLD_NEXT, "mkdirat");
		D_ASSERT(next_mkdirat != NULL);
	}
	if (!hook_enabled)
		return next_mkdirat(dirfd, path, mode);

	if (path[0] == '/')
		return mkdir(path, mode);

	idx_dfs = check_path_with_dirfd(dirfd, &full_path, path, &error);
	if (error)
		goto out_err;

	if (idx_dfs >= 0)
		rc = mkdir(full_path, mode);
	else
		rc = next_mkdirat(dirfd, path, mode);

	error = errno;
	if (full_path) {
		free(full_path);
		errno = error;
	}
	return rc;

out_err:
	errno = error;
	return (-1);
}

int
rmdir(const char *path)
{
	int              is_target_path, rc;
	dfs_obj_t       *parent;
	struct dfs_mt   *dfs_mt;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	if (next_rmdir == NULL) {
		next_rmdir = dlsym(RTLD_NEXT, "rmdir");
		D_ASSERT(next_rmdir != NULL);
	}
	if (!hook_enabled)
		return next_rmdir(path);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;
	if (bLog)
		atomic_fetch_add_relaxed(&num_rmdir, 1);

	rc = dfs_remove(dfs_mt->dfs, parent, item_name, false, NULL);
	if (rc)
		D_GOTO(out_err, rc);

	FREE(parent_dir);
	return 0;

out_org:
	FREE(parent_dir);
	return next_rmdir(path);

out_err:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

int
symlink(const char *symvalue, const char *path)
{
	int              is_target_path, rc;
	dfs_obj_t       *parent, *obj;
	struct dfs_mt   *dfs_mt;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	if (next_symlink == NULL) {
		next_symlink = dlsym(RTLD_NEXT, "symlink");
		D_ASSERT(next_symlink != NULL);
	}
	if (!hook_enabled)
		return next_symlink(symvalue, path);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;

	rc = dfs_open(dfs_mt->dfs, parent, item_name, S_IFLNK, O_CREAT | O_EXCL, 0, 0, symvalue,
		      &obj);
	if (rc)
		goto out_err;
	rc = dfs_release(obj);
	if (rc)
		goto out_err;

	FREE(parent_dir);
	return 0;

out_org:
	FREE(parent_dir);
	return next_symlink(symvalue, path);

out_err:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

int
symlinkat(const char *symvalue, int dirfd, const char *path)
{
	int  idx_dfs, error = 0, rc;
	char *full_path = NULL;

	if (next_symlinkat == NULL) {
		next_symlinkat = dlsym(RTLD_NEXT, "symlinkat");
		D_ASSERT(next_symlinkat != NULL);
	}
	if (!hook_enabled)
		return next_symlinkat(symvalue, dirfd, path);

	if (path[0] == '/')
		return symlink(symvalue, path);

	idx_dfs = check_path_with_dirfd(dirfd, &full_path, path, &error);
	if (error)
		goto out_err;

	if (idx_dfs >= 0)
		rc = symlink(symvalue, full_path);
	else
		rc = next_symlinkat(symvalue, dirfd, path);

	error = errno;
	if (full_path) {
		free(full_path);
		errno = error;
	}
	return rc;

out_err:
	errno = error;
	return (-1);
}

ssize_t
readlink(const char *path, char *buf, size_t size)
{
	int              is_target_path, rc, rc2;
	dfs_obj_t       *parent, *obj;
	struct dfs_mt   *dfs_mt;
	daos_size_t      str_len = size;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	if (next_readlink == NULL) {
		next_readlink = dlsym(RTLD_NEXT, "readlink");
		D_ASSERT(next_readlink != NULL);
	}
	if (!hook_enabled)
		return next_readlink(path, buf, size);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;

	rc =
	    dfs_lookup_rel(dfs_mt->dfs, parent, item_name, O_RDONLY | O_NOFOLLOW, &obj, NULL, NULL);
	if (rc)
		goto out_err;
	rc = dfs_get_symlink_value(obj, buf, &str_len);
	if (rc)
		goto out_release;
	rc = dfs_release(obj);
	if (rc)
		goto out_err;
	/* The NULL at the end should not be included in the length */
	FREE(parent_dir);
	return (str_len - 1);

out_org:
	FREE(parent_dir);
	return next_readlink(path, buf, size);

out_release:
	rc2 = dfs_release(obj);
	if (rc2 & bLog)
		D_ERROR("failed to dfs_release().");

out_err:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

ssize_t
readlinkat(int dirfd, const char *path, char *buf, size_t size)
{
	int  idx_dfs, error = 0, rc;
	char *full_path = NULL;

	if (next_readlinkat == NULL) {
		next_readlinkat = dlsym(RTLD_NEXT, "readlinkat");
		D_ASSERT(next_readlinkat != NULL);
	}
	if (!hook_enabled)
		return next_readlinkat(dirfd, path, buf, size);

	if (path[0] == '/')
		return readlink(path, buf, size);

	idx_dfs = check_path_with_dirfd(dirfd, &full_path, path, &error);
	if (error)
		goto out_err;

	if (idx_dfs >= 0)
		rc = readlink(full_path, buf, size);
	else
		rc = next_readlinkat(dirfd, path, buf, size);

	error = errno;
	if (full_path) {
		free(full_path);
		errno = error;
	}
	return rc;

out_err:
	errno = error;
	return (-1);
}

static ssize_t
write_all(int fd, const void *buf, size_t count)
{
	ssize_t rc, byte_written = 0;
	char   *p_buf = (char *)buf;

	if (fd >= FD_FILE_BASE)
		return write(fd, buf, count);

	while (count != 0 && (rc = write(fd, p_buf, count)) != 0) {
		if (rc == -1) {
			if (errno == EINTR)
				continue;
			else if (errno == ENOSPC)
				/* out of space. Quit immediately. */
				return -1;
			if (bLog)
				D_ERROR("error in write_all: %s", strerror(errno));
			return -1;
		}
		byte_written += rc;
		count -= rc;
		p_buf += rc;
	}
	return byte_written;
}

int
rename(const char *old_name, const char *new_name)
{
	int              is_target_path1, is_target_path2, rc = -1;
	dfs_obj_t       *parent_old, *parent_new;
	dfs_obj_t       *obj_old, *obj_new;
	struct dfs_mt   *dfs_mt1, *dfs_mt2;
	struct stat      stat_old, stat_new;
	mode_t           mode_old, mode_new;
	int              type_old, type_new;
	unsigned char   *buff = NULL;
	d_sg_list_t      sgl_data;
	d_iov_t          iov_buf;
	daos_size_t      byte_left, byte_to_write, byte_read;
	daos_size_t      link_len;
	int              fd, errno_save;
	FILE            *fIn = NULL;
	char             item_name_old[DFS_MAX_NAME], item_name_new[DFS_MAX_NAME];
	char             *parent_dir_old = NULL;
	char             *full_path_old  = NULL;
	char             *parent_dir_new = NULL;
	char             *full_path_new  = NULL;
	char             *symlink_value  = NULL;

	if (next_rename == NULL) {
		next_rename = dlsym(RTLD_NEXT, "rename");
		D_ASSERT(next_rename != NULL);
	}
	if (!hook_enabled)
		return next_rename(old_name, new_name);

	rc = query_path(old_name, &is_target_path1, &parent_old, item_name_old,
			&parent_dir_old, &full_path_old, &dfs_mt1);
	if (rc)
		D_GOTO(out_err, rc);

	rc = query_path(new_name, &is_target_path2, &parent_new, item_name_new,
			&parent_dir_new, &full_path_new, &dfs_mt2);
	if (rc)
		D_GOTO(out_err, rc);

	if (is_target_path1 == 0 && is_target_path2 == 0)
		goto out_org;

	if (bLog)
		atomic_fetch_add_relaxed(&num_rename, 1);

	if (is_target_path1 && is_target_path2) {
		/* Both old and new are on DAOS */
		rc = dfs_move(dfs_mt1->dfs, parent_old, item_name_old, parent_new, item_name_new,
			      NULL);
		if (rc)
			D_GOTO(out_err, rc);
		return 0;
	} else if (is_target_path1 == 1 && is_target_path2 == 0) {
		/* Old_name is on DAOS and new_name is on non-DAOS filesystem */

		/* Renaming a root dir of a DAOS container */
		if (!parent_old && (strncmp(item_name_old, "/", 2) == 0))
			D_GOTO(out_err, rc = EINVAL);

		rc = dfs_lookup_rel(dfs_mt1->dfs, parent_old, item_name_old, O_RDONLY | O_NOFOLLOW,
				    &obj_old, &mode_old, &stat_old);
		if (rc)
			D_GOTO(out_err, rc);
		type_old = mode_old & S_IFMT;
		if (type_old != S_IFLNK && type_old != S_IFREG && type_old != S_IFDIR) {
			if (bLog)
				D_ERROR("unsupported type for old file %s\n", old_name);
			D_GOTO(out_err, rc = ENOTSUP);
		}

		rc = stat(new_name, &stat_new);
		if (rc != 0 && errno != ENOENT)
			D_GOTO(out_err, rc);
		if (rc == 0) {
			type_new = stat_new.st_mode & S_IFMT;
			if (type_new != S_IFLNK && type_new != S_IFREG && type_new != S_IFDIR) {
				if (bLog)
					D_ERROR("unsupported type for new file %s\n", new_name);
				D_GOTO(out_err, rc = ENOTSUP);
			}

			/* New_name exists on non-DAOS filesystem, remove first. */;
			/* The behavior depends on type_old and type_new!!! */

			if (type_old == type_new || (type_old == S_IFREG && type_new == S_IFLNK) ||
			    (type_old == S_IFLNK && type_new == S_IFREG)) {
				rc = unlink(new_name);
				if (rc != 0)
					D_GOTO(out_old, rc);
			} else if (type_old != S_IFDIR && type_new == S_IFDIR) {
				/* Is a directory */
				D_GOTO(out_old, rc = EISDIR);
			} else if (type_old == S_IFDIR && type_new != S_IFDIR) {
				/* Not a directory */
				D_GOTO(out_old, rc = ENOTDIR);
			}
		}

		switch (type_old) {
		case S_IFLNK:
			D_ALLOC(symlink_value, DFS_MAX_PATH);
			if (symlink_value == NULL) {
				if (bLog)
					D_ERROR("failed to allocate memory for symlink_value");
				D_GOTO(out_old, rc = ENOMEM);
			}
			rc = dfs_get_symlink_value(obj_old, symlink_value, &link_len);
			if (link_len >= DFS_MAX_PATH) {
				if (bLog)
					D_ERROR("link is too long. link_len = %" PRIu64, link_len);
				D_GOTO(out_old, rc = ENAMETOOLONG);
			}
			if (rc)
				D_GOTO(out_old, rc);

			rc = symlink(symlink_value, new_name);
			if (rc != 0)
				D_GOTO(out_old, rc);
			break;
		case S_IFREG:
			/* Read the old file, write to the new file */
			D_ALLOC(buff, ((stat_old.st_size > FILE_BUFFER_SIZE) ? FILE_BUFFER_SIZE
									     : stat_old.st_size));
			if (buff == NULL)
				D_GOTO(out_old, rc = ENOMEM);

			fd = open(new_name, O_WRONLY | O_CREAT, stat_old.st_mode);
			if (fd < 0)
				D_GOTO(out_old, rc);

			byte_left          = stat_old.st_size;
			sgl_data.sg_nr     = 1;
			sgl_data.sg_nr_out = 0;
			sgl_data.sg_iovs   = &iov_buf;
			while (byte_left > 0) {
				byte_to_write =
				    byte_left > FILE_BUFFER_SIZE ? FILE_BUFFER_SIZE : byte_left;
				d_iov_set(&iov_buf, buff, byte_to_write);
				rc = dfs_read(dfs_mt1->dfs, obj_old, &sgl_data,
					      stat_old.st_size - byte_left, &byte_read, NULL);
				if (rc != 0) {
					close(fd);
					errno = rc;
					D_GOTO(out_old, rc);
				}
				if (byte_read != byte_to_write) {
					close(fd);
					/* Unexpected!!! */
					if (bLog)
						D_ERROR("error to read %" PRIu64
							" bytes from %s\n",
							byte_to_write, old_name);
					D_GOTO(out_old, rc = EREMOTEIO);
				}

				rc = write_all(fd, buff, byte_to_write);
				if (rc == -1) {
					rc = errno;
					close(fd);
					goto out_old;
				}
				if (rc != byte_to_write) {
					close(fd);
					/* Unexpected!!! */
					if (bLog)
						D_ERROR("error to write %" PRIu64
							" bytes from %s\n",
							byte_to_write, new_name);
					D_GOTO(out_old, rc = EREMOTEIO);
				}
				byte_left -= rc;
			}
			close(fd);
			D_FREE(buff);
			break;
		case S_IFDIR:
			rc = dfs_release(obj_old);
			if (rc)
				goto out_err;
			rc = mkdir(new_name, stat_old.st_mode);
			if (rc != 0)
				D_GOTO(out_err, rc = errno);
			rc = dfs_remove(dfs_mt1->dfs, parent_old, item_name_old, false, NULL);
			if (rc)
				goto out_err;
			break;
		}
		return chmod(new_name, stat_old.st_mode);
	} else if (is_target_path1 == 0 && is_target_path2 == 1) {
		/* Old_name is on non-DAOS and new_name is on DAOS filesystem */

		rc = stat(old_name, &stat_old);
		if (rc != 0)
			D_GOTO(out_err, rc = errno);
		type_old = stat_old.st_mode & S_IFMT;
		if (type_old != S_IFLNK && type_old != S_IFREG && type_old != S_IFDIR) {
			if (bLog)
				D_ERROR("unsupported type for old file %s", old_name);
			D_GOTO(out_err, rc = ENOTSUP);
		}

		if (!parent_new && (strncmp(item_name_new, "/", 2) == 0))
			/* Renaming a root dir of a DAOS container */
			D_GOTO(out_err, rc = EINVAL);

		rc = dfs_lookup_rel(dfs_mt2->dfs, parent_new, item_name_new, O_RDONLY | O_NOFOLLOW,
				    &obj_new, &mode_new, &stat_new);
		if (rc != 0 && rc != ENOENT)
			goto out_err;
		if (rc == 0) {
			type_new = mode_new & S_IFMT;
			if (type_new != S_IFLNK && type_new != S_IFREG && type_new != S_IFDIR) {
				if (bLog)
					D_ERROR("unsupported type for new file %s\n", new_name);
				dfs_release(obj_new);
				D_GOTO(out_err, rc = ENOTSUP);
			}

			/* New_name exists on DAOS filesystem, remove first. */
			/* The behavior depends on type_old and type_new!!! */

			if ((type_old == type_new) ||
			    (type_old == S_IFREG && type_new == S_IFLNK) ||
			    (type_old == S_IFLNK && type_new == S_IFREG)) {
				/* Unlink then finish renaming */
				rc = dfs_release(obj_new);
				if (rc)
					goto out_err;
				rc = dfs_remove(dfs_mt2->dfs, parent_new, item_name_new, false,
						NULL);
				if (rc)
					goto out_err;
			}
			if (type_old != S_IFDIR && type_new == S_IFDIR) {
				/* Is a directory */
				D_GOTO(out_new, rc = EISDIR);
			}
			if (type_old == S_IFDIR && type_new != S_IFDIR) {
				/* Not a directory */
				D_GOTO(out_new, rc = ENOTDIR);
			}
		}
		/* New_name was removed, now create a new one from the old one */
		switch (type_old) {
		case S_IFLNK:
			D_ALLOC(symlink_value, DFS_MAX_PATH);
			if (symlink_value == NULL) {
				if (bLog)
					D_ERROR("failed to allocate memory for symlink_value");
				D_GOTO(out_err, rc = ENOMEM);
			}
			link_len = readlink(old_name, symlink_value, DFS_MAX_PATH - 1);
			if (link_len >= DFS_MAX_PATH) {
				if (bLog)
					D_ERROR("link is too long. link_len = %" PRIu64, link_len);
				D_GOTO(out_err, rc = ENAMETOOLONG);
			} else if (link_len < 0) {
				if (bLog)
					D_ERROR("readlink failed: %s", strerror(errno));
				D_GOTO(out_err, rc = errno);
			}

			rc =
			    dfs_open(dfs_mt2->dfs, parent_new, item_name_new, DEFFILEMODE | S_IFLNK,
				     O_RDWR | O_CREAT, 0, 0, symlink_value, &obj_new);
			if (rc != 0)
				goto out_err;
			rc = dfs_release(obj_new);
			if (rc != 0)
				D_GOTO(out_err, rc);
			break;
		case S_IFREG:
			/* Read the old file, write to the new file */
			fIn = fopen(old_name, "r");
			if (fIn == NULL)
				D_GOTO(out_err, rc = errno);
			rc = dfs_open(dfs_mt2->dfs, parent_new, item_name_new, S_IFREG,
				      O_RDWR | O_CREAT, 0, 0, NULL, &obj_new);
			if (rc) {
				fclose(fIn);
				D_GOTO(out_err, rc);
			}
			D_ALLOC(buff, ((stat_old.st_size > FILE_BUFFER_SIZE) ? FILE_BUFFER_SIZE
									     : stat_old.st_size));
			if (buff == NULL)
				D_GOTO(out_new, rc = ENOMEM);

			byte_left          = stat_old.st_size;
			sgl_data.sg_nr     = 1;
			sgl_data.sg_nr_out = 0;
			sgl_data.sg_iovs   = &iov_buf;
			while (byte_left > 0) {
				byte_to_write =
				    byte_left > FILE_BUFFER_SIZE ? FILE_BUFFER_SIZE : byte_left;
				byte_read = fread(buff, 1, byte_to_write, fIn);
				if (byte_read != byte_to_write)
					D_GOTO(out_new, rc = errno);

				d_iov_set(&iov_buf, buff, byte_to_write);
				rc = dfs_write(dfs_mt2->dfs, obj_new, &sgl_data,
					       stat_old.st_size - byte_left, NULL);
				if (rc != 0)
					D_GOTO(out_new, rc);
				byte_left -= byte_to_write;
			}
			rc = fclose(fIn);
			if (rc != 0) {
				dfs_release(obj_new);
				D_GOTO(out_err, rc = errno);
			}
			dfs_release(obj_new);
			break;
		case S_IFDIR:
			rc =
			    dfs_mkdir(dfs_mt2->dfs, parent_new, item_name_new, stat_old.st_mode, 0);
			if (rc != 0)
				goto out_err;
			rc = dfs_remove(dfs_mt1->dfs, parent_old, item_name_old, false, NULL);
			if (rc)
				goto out_err;

			break;
		}

		/* This could be improved later by calling daos_obj_update() directly. */
		rc = dfs_chmod(dfs_mt2->dfs, parent_new, item_name_new, stat_old.st_mode);
		if (rc)
			goto out_err;
	}

	return 0;

out_org:
	FREE(parent_dir_old);
	FREE(parent_dir_new);
	return next_rename(old_name, new_name);

out_old:
	D_FREE(symlink_value);
	FREE(parent_dir_old);
	FREE(parent_dir_new);
	D_FREE(buff);
	errno_save = rc;
	rc = dfs_release(obj_old);
	if (rc && bLog)
		D_ERROR("error in dfs_release(). %s\n", strerror(rc));
	errno = errno_save;
	return (-1);

out_new:
	FREE(parent_dir_old);
	FREE(parent_dir_new);
	D_FREE(buff);
	fclose(fIn);
	errno_save = rc;
	rc = dfs_release(obj_new);
	if (rc && bLog)
		D_ERROR("error in dfs_release(). %s\n", strerror(rc));
	errno = errno_save;
	return (-1);

out_err:
	D_FREE(symlink_value);
	FREE(parent_dir_old);
	FREE(parent_dir_new);
	errno = rc;
	return (-1);
}

char *
getcwd(char *buf, size_t size)
{
	if (next_getcwd == NULL) {
		next_getcwd = dlsym(RTLD_NEXT, "getcwd");
		D_ASSERT(next_getcwd != NULL);
	}

	if (!hook_enabled)
		return next_getcwd(buf, size);

	if (cur_dir[0] != '/')
		update_cwd();

	if (query_dfs_mount(cur_dir) < 0)
		return next_getcwd(buf, size);

	if (buf == NULL) {
		size_t len;

		len = strnlen(cur_dir, size);
		if (len >= size) {
			errno = ERANGE;
			return NULL;
		}
		return strdup(cur_dir);
	}

	strncpy(buf, cur_dir, size);
	return buf;
}

int
isatty(int fd)
{
	if (next_isatty == NULL) {
		next_isatty = dlsym(RTLD_NEXT, "isatty");
		D_ASSERT(next_isatty != NULL);
	}
	if (!hook_enabled)
		return next_isatty(fd);

	if (fd >= FD_FILE_BASE)
		/* non-terminal */
		return 0;
	else
		return next_isatty(fd);
}

int
__isatty(int fd) __attribute__((alias("isatty"), leaf, nothrow));

int
access(const char *path, int mode)
{
	dfs_obj_t       *parent;
	int              rc, is_target_path;
	struct dfs_mt   *dfs_mt;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	if (next_access == NULL) {
		next_access = dlsym(RTLD_NEXT, "access");
		D_ASSERT(next_access != NULL);
	}
	if (!hook_enabled)
		return next_access(path, mode);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;

	if (!parent && (strncmp(item_name, "/", 2) == 0))
		rc = dfs_access(dfs_mt->dfs, NULL, NULL, mode);
	else
		rc = dfs_access(dfs_mt->dfs, parent, item_name, mode);
	if (rc)
		D_GOTO(out_err, rc);
	FREE(parent_dir);
	return 0;

out_org:
	FREE(parent_dir);
	return next_access(path, mode);

out_err:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

int
faccessat(int dirfd, const char *path, int mode, int flags)
{
	int  idx_dfs, error = 0, rc;
	char *full_path = NULL;

	if (next_faccessat == NULL) {
		next_faccessat = dlsym(RTLD_NEXT, "faccessat");
		D_ASSERT(next_faccessat != NULL);
	}
	if (!hook_enabled)
		return next_faccessat(dirfd, path, mode, flags);

	/* absolute path, dirfd is ignored */
	if (path[0] == '/')
		return access(path, mode);

	idx_dfs = check_path_with_dirfd(dirfd, &full_path, path, &error);
	if (error)
		goto out_err;

	if (idx_dfs >= 0)
		rc = access(full_path, mode);
	else
		rc = next_faccessat(dirfd, path, mode, flags);

	error = errno;
	if (full_path) {
		free(full_path);
		errno = error;
	}
	return rc;

out_err:
	errno = error;
	return (-1);
}

int
chdir(const char *path)
{
	int              is_target_path, rc, len_str, errno_save;
	dfs_obj_t       *parent;
	struct stat      stat_buf;
	struct dfs_mt   *dfs_mt;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	if (next_chdir == NULL) {
		next_chdir = dlsym(RTLD_NEXT, "chdir");
		D_ASSERT(next_chdir != NULL);
	}
	if (!hook_enabled)
		return next_chdir(path);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path) {
		FREE(parent_dir);
		rc = next_chdir(path);
		errno_save = errno;
		if (rc == 0)
			update_cwd();
		errno = errno_save;
		return rc;
	}

	if (!parent && (strncmp(item_name, "/", 2) == 0))
		rc = dfs_stat(dfs_mt->dfs, NULL, NULL, &stat_buf);
	else
		rc = dfs_stat(dfs_mt->dfs, parent, item_name, &stat_buf);
	if (rc)
		D_GOTO(out_err, rc);
	if (!S_ISDIR(stat_buf.st_mode)) {
		if (bLog)
			D_ERROR("%s is not a directory.\n", path);
		D_GOTO(out_err, rc = ENOTDIR);
	}
	rc = dfs_access(dfs_mt->dfs, parent, item_name, X_OK);
	if (rc)
		D_GOTO(out_err, rc);
	len_str = snprintf(cur_dir, DFS_MAX_PATH, "%s%s", dfs_mt->fs_root, full_path);
	if (len_str >= DFS_MAX_PATH) {
		if (bLog)
			D_ERROR("path is too long.");
		D_GOTO(out_err, rc = ENAMETOOLONG);
	}

	FREE(parent_dir);
	return 0;

out_err:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

int
fchdir(int dirfd)
{
	char *pt_end = NULL;

	if (next_fchdir == NULL) {
		next_fchdir = dlsym(RTLD_NEXT, "fchdir");
		D_ASSERT(next_fchdir != NULL);
	}
	if (!hook_enabled)
		return next_fchdir(dirfd);

	if (dirfd < FD_DIR_BASE)
		return next_fchdir(dirfd);

	pt_end = stpncpy(cur_dir, dir_list[dirfd - FD_DIR_BASE]->path, DFS_MAX_PATH - 1);
	if ((long int)(pt_end - cur_dir) >= DFS_MAX_PATH - 1) {
		if (bLog)
			D_ERROR("path is too long.");
		errno = ENAMETOOLONG;
		return (-1);
	}
	return 0;
}

static int
new_unlink(const char *path)
{
	int              is_target_path, rc;
	dfs_obj_t       *parent;
	struct dfs_mt   *dfs_mt;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	if (!hook_enabled)
		return libc_unlink(path);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;

	if (bLog)
		atomic_fetch_add_relaxed(&num_unlink, 1);

	rc = dfs_remove(dfs_mt->dfs, parent, item_name, false, NULL);
	if (rc)
		D_GOTO(out_err, rc);

	FREE(parent_dir);
	return 0;

out_org:
	FREE(parent_dir);
	return libc_unlink(path);

out_err:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

int
unlinkat(int dirfd, const char *path, int flags)
{
	int              is_target_path, rc, error = 0;
	dfs_obj_t       *parent;
	struct dfs_mt   *dfs_mt;
	int              idx_dfs;
	char             item_name[DFS_MAX_NAME];
	char             *full_path = NULL;
	char             *parent_dir = NULL;
	char             *full_path_dummy  = NULL;

	if (next_unlinkat == NULL) {
		next_unlinkat = dlsym(RTLD_NEXT, "unlinkat");
		D_ASSERT(next_unlinkat != NULL);
	}
	if (!hook_enabled)
		return next_unlinkat(dirfd, path, flags);

	if (path[0] == '/') {
		/* absolute path, dirfd is ignored */
		rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
				&full_path_dummy, &dfs_mt);
		if (rc)
			D_GOTO(out_err, rc);
		if (!is_target_path)
			goto out_org;
		if (bLog)
			atomic_fetch_add_relaxed(&num_unlink, 1);

		rc = dfs_remove(dfs_mt->dfs, parent, item_name, false, NULL);
		if (rc)
			D_GOTO(out_err_abs, rc);

		FREE(parent_dir);
		return 0;
	}

	idx_dfs = check_path_with_dirfd(dirfd, &full_path, path, &error);
	if (error)
		goto out_err;

	if (idx_dfs >= 0)
		rc = new_unlink(full_path);
	else
		rc = next_unlinkat(dirfd, path, flags);

	error = errno;
	if (full_path)
		free(full_path);
	if (rc)
		errno = error;
	return rc;

out_org:
	FREE(parent_dir);
	return next_unlinkat(dirfd, path, flags);

out_err_abs:
	FREE(parent_dir);
	errno = rc;
	return (-1);

out_err:
	errno = error;
	return (-1);
}

int
fsync(int fd)
{
	if (next_fsync == NULL) {
		next_fsync = dlsym(RTLD_NEXT, "fsync");
		D_ASSERT(next_fsync != NULL);
	}
	if (!hook_enabled || fd < FD_FILE_BASE)
		return next_fsync(fd);

	if (fd >= FD_DIR_BASE) {
		errno = EINVAL;
		return (-1);
	}

	/* TODO real fsync */
	return 0;
}

int
ftruncate(int fd, off_t length)
{
	int rc;

	if (next_ftruncate == NULL) {
		next_ftruncate = dlsym(RTLD_NEXT, "ftruncate");
		D_ASSERT(next_ftruncate != NULL);
	}
	if (!hook_enabled || fd < FD_FILE_BASE)
		return next_ftruncate(fd, length);

	if (fd >= FD_DIR_BASE) {
		errno = EINVAL;
		return (-1);
	}

	rc = dfs_punch(file_list[fd - FD_FILE_BASE]->dfs_mt->dfs,
		       file_list[fd - FD_FILE_BASE]->file, length, DFS_MAX_FSIZE);
	if (rc) {
		errno = rc;
		return (-1);
	}
	return 0;
}

int
ftruncate64(int fd, off_t length) __attribute__((alias("ftruncate"), leaf, nothrow));

int
truncate(const char *path, off_t length)
{
	int              is_target_path, rc, rc2;
	dfs_obj_t       *parent, *file_obj;
	mode_t           mode;
	struct dfs_mt   *dfs_mt;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	if (next_truncate == NULL) {
		next_truncate = dlsym(RTLD_NEXT, "truncate");
		D_ASSERT(next_truncate != NULL);
	}
	if (!hook_enabled)
		return next_truncate(path, length);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;

	rc = dfs_open(dfs_mt->dfs, parent, item_name, S_IFREG, O_RDWR, 0, 0, NULL, &file_obj);
	if (rc)
		D_GOTO(out_err, rc);
	if (!S_ISREG(mode)) {
		if (bLog)
			D_ERROR("%s is not a regular file.\n", path);
		D_GOTO(out_err, rc = EISDIR);
	}
	rc = dfs_punch(dfs_mt->dfs, file_obj, length, DFS_MAX_FSIZE);
	rc2 = dfs_release(file_obj);
	if (rc)
		D_GOTO(out_err, rc);
	if (rc2)
		D_GOTO(out_err, rc = rc2);

	FREE(parent_dir);
	return 0;

out_org:
	FREE(parent_dir);
	return next_truncate(path, length);

out_err:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

static int
chmod_with_flag(const char *path, mode_t mode, int flag)
{
	int              rc, is_target_path;
	dfs_obj_t       *parent;
	struct dfs_mt   *dfs_mt;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	if (next_chmod == NULL) {
		next_chmod = dlsym(RTLD_NEXT, "chmod");
		D_ASSERT(next_chmod != NULL);
	}

	if (!hook_enabled)
		return next_chmod(path, mode);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;

	/* POSIX API uses AT_SYMLINK_NOFOLLOW; DFS dfs_lookup() uses O_NOFOLLOW. */
	if (flag & AT_SYMLINK_NOFOLLOW)
		flag |= O_NOFOLLOW;
	if (!parent && (strncmp(item_name, "/", 2) == 0))
		rc = dfs_chmod(dfs_mt->dfs, NULL, NULL, mode);
	else
		rc = dfs_chmod(dfs_mt->dfs, parent, item_name, mode);
	if (rc)
		D_GOTO(out_err, rc);

	FREE(parent_dir);
	return 0;

out_org:
	FREE(parent_dir);
	return next_chmod(path, mode);

out_err:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

/* dfs_chmod will dereference symlinks as default */
int
chmod(const char *path, mode_t mode)
{
	if (next_chmod == NULL) {
		next_chmod = dlsym(RTLD_NEXT, "chmod");
		D_ASSERT(next_chmod != NULL);
	}

	if (!hook_enabled)
		return next_chmod(path, mode);

	return chmod_with_flag(path, mode, 0);
}

int
fchmod(int fd, mode_t mode)
{
	int rc;

	if (next_fchmod == NULL) {
		next_fchmod = dlsym(RTLD_NEXT, "fchmod");
		D_ASSERT(next_fchmod != NULL);
	}

	if (!hook_enabled || fd < FD_FILE_BASE)
		return next_fchmod(fd, mode);

	if (fd >= FD_DIR_BASE) {
		errno = EINVAL;
		return (-1);
	}

	rc =
	    dfs_chmod(file_list[fd - FD_FILE_BASE]->dfs_mt->dfs,
		      file_list[fd - FD_FILE_BASE]->parent,
		      file_list[fd - FD_FILE_BASE]->item_name, mode);
	if (rc) {
		errno = rc;
		return (-1);
	}

	return 0;
}

int
fchmodat(int dirfd, const char *path, mode_t mode, int flag)
{
	int  idx_dfs, error = 0, rc;
	char *full_path = NULL;

	if (next_fchmodat == NULL) {
		next_fchmodat = dlsym(RTLD_NEXT, "fchmodat");
		D_ASSERT(next_fchmodat != NULL);
	}

	if (!hook_enabled)
		return next_fchmodat(dirfd, path, mode, flag);

	if (path[0] == '/')
		return chmod_with_flag(path, mode, flag);

	idx_dfs = check_path_with_dirfd(dirfd, &full_path, path, &error);
	if (error)
		goto out_err;

	if (idx_dfs >= 0)
		rc = chmod_with_flag(full_path, mode, flag);
	else
		rc = next_fchmodat(dirfd, path, mode, flag);

	error = errno;
	if (full_path) {
		free(full_path);
		errno = error;
	}
	return rc;

out_err:
	errno = error;
	return (-1);
}

int
utime(const char *path, const struct utimbuf *times)
{
	int              is_target_path, rc;
	dfs_obj_t       *obj, *parent;
	mode_t           mode;
	struct stat      stbuf;
	struct timespec  times_loc;
	struct dfs_mt   *dfs_mt;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	if (next_utime == NULL) {
		next_utime = dlsym(RTLD_NEXT, "utime");
		D_ASSERT(next_utime != NULL);
	}
	if (!hook_enabled)
		return next_utime(path, times);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;

	if (!parent && (strncmp(item_name, "/", 2) == 0))
		rc = dfs_lookup(dfs_mt->dfs, "/", O_RDWR, &obj, &mode, &stbuf);
	else
		rc = dfs_lookup_rel(dfs_mt->dfs, parent, item_name, O_RDWR, &obj, &mode, &stbuf);
	if (rc) {
		if (bLog)
			D_ERROR("fail to lookup %s. %s\n", full_path, strerror(rc));
		D_GOTO(out_err, rc);
	}

	if (times == NULL) {
		clock_gettime(CLOCK_REALTIME, &times_loc);
		stbuf.st_atim.tv_sec  = times_loc.tv_sec;
		stbuf.st_atim.tv_nsec = times_loc.tv_nsec;
		stbuf.st_mtim.tv_sec  = times_loc.tv_sec;
		stbuf.st_mtim.tv_nsec = times_loc.tv_nsec;
	} else {
		stbuf.st_atim.tv_sec  = times->actime;
		stbuf.st_atim.tv_nsec = 0;
		stbuf.st_mtim.tv_sec  = times->modtime;
		stbuf.st_mtim.tv_nsec = 0;
	}

	rc = dfs_osetattr(dfs_mt->dfs, obj, &stbuf, DFS_SET_ATTR_ATIME | DFS_SET_ATTR_MTIME);
	if (rc) {
		dfs_release(obj);
		D_GOTO(out_err, rc);
	}

	rc = dfs_release(obj);
	if (rc)
		D_GOTO(out_err, rc);

	FREE(parent_dir);
	return 0;

out_org:
	FREE(parent_dir);
	return next_utime(path, times);

out_err:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

int
utimes(const char *path, const struct timeval times[2])
{
	int              is_target_path, rc;
	dfs_obj_t       *obj, *parent;
	mode_t           mode;
	struct stat      stbuf;
	struct timespec  times_loc;
	struct dfs_mt   *dfs_mt;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	if (next_utimes == NULL) {
		next_utimes = dlsym(RTLD_NEXT, "utimes");
		D_ASSERT(next_utimes != NULL);
	}
	if (!hook_enabled)
		return next_utimes(path, times);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;

	if (!parent && (strncmp(item_name, "/", 2) == 0))
		rc = dfs_lookup(dfs_mt->dfs, "/", O_RDWR, &obj, &mode, &stbuf);
	else
		rc = dfs_lookup_rel(dfs_mt->dfs, parent, item_name, O_RDWR, &obj, &mode, &stbuf);
	if (rc) {
		if (bLog)
			D_ERROR("fail to lookup %s. %s\n", full_path, strerror(rc));
		D_GOTO(out_err, rc);
	}

	if (times == NULL) {
		clock_gettime(CLOCK_REALTIME, &times_loc);
		stbuf.st_atim.tv_sec  = times_loc.tv_sec;
		stbuf.st_atim.tv_nsec = times_loc.tv_nsec;
		stbuf.st_mtim.tv_sec  = times_loc.tv_sec;
		stbuf.st_mtim.tv_nsec = times_loc.tv_nsec;
	} else {
		stbuf.st_atim.tv_sec  = times[0].tv_sec;
		stbuf.st_atim.tv_nsec = times[0].tv_usec * 1000;
		stbuf.st_mtim.tv_sec  = times[1].tv_sec;
		stbuf.st_mtim.tv_nsec = times[1].tv_usec * 1000;
	}

	rc = dfs_osetattr(dfs_mt->dfs, obj, &stbuf, DFS_SET_ATTR_ATIME | DFS_SET_ATTR_MTIME);
	if (rc) {
		if (bLog)
			D_ERROR("fail to set attribute. %s\n", strerror(rc));
		dfs_release(obj);
		D_GOTO(out_err, rc);
	}

	rc = dfs_release(obj);
	if (rc)
		D_GOTO(out_err, rc);

	FREE(parent_dir);
	return 0;

out_org:
	FREE(parent_dir);
	return next_utimes(path, times);

out_err:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

static int
utimens_timespec(const char *path, const struct timespec times[2], int flags)
{
	int              is_target_path, rc;
	dfs_obj_t       *obj, *parent;
	mode_t           mode;
	struct stat      stbuf;
	struct timespec  times_loc;
	struct timeval   times_us[2];
	struct dfs_mt   *dfs_mt;
	char             item_name[DFS_MAX_NAME];
	char             *parent_dir = NULL;
	char             *full_path  = NULL;

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path) {
		times_us[0].tv_sec  = times[0].tv_sec;
		times_us[0].tv_usec = times[0].tv_nsec / 100;
		times_us[1].tv_sec  = times[1].tv_sec;
		times_us[1].tv_usec = times[1].tv_nsec / 100;
		FREE(parent_dir);
		return next_utimes(path, times_us);
	}

	flags |= O_RDWR;
	/* POSIX API uses AT_SYMLINK_NOFOLLOW; DFS dfs_lookup() uses O_NOFOLLOW. */
	if (flags & AT_SYMLINK_NOFOLLOW)
		flags |= O_NOFOLLOW;
	if (!parent && (strncmp(item_name, "/", 2) == 0))
		rc = dfs_lookup(dfs_mt->dfs, "/", flags, &obj, &mode, &stbuf);
	else
		rc = dfs_lookup_rel(dfs_mt->dfs, parent, item_name, flags, &obj, &mode, &stbuf);
	if (rc)
		D_GOTO(out_err, rc);

	if (times == NULL) {
		clock_gettime(CLOCK_REALTIME, &times_loc);
		stbuf.st_atim.tv_sec  = times_loc.tv_sec;
		stbuf.st_atim.tv_nsec = times_loc.tv_nsec;
		stbuf.st_mtim.tv_sec  = times_loc.tv_sec;
		stbuf.st_mtim.tv_nsec = times_loc.tv_nsec;
	} else {
		stbuf.st_atim.tv_sec  = times[0].tv_sec;
		stbuf.st_atim.tv_nsec = times[0].tv_nsec;
		stbuf.st_mtim.tv_sec  = times[1].tv_sec;
		stbuf.st_mtim.tv_nsec = times[1].tv_nsec;
	}

	rc = dfs_osetattr(dfs_mt->dfs, obj, &stbuf, DFS_SET_ATTR_ATIME | DFS_SET_ATTR_MTIME);
	if (rc) {
		dfs_release(obj);
		D_GOTO(out_err, rc);
	}

	rc = dfs_release(obj);
	if (rc)
		D_GOTO(out_err, rc);

	FREE(parent_dir);
	return 0;

out_err:
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

int
utimensat(int dirfd, const char *path, const struct timespec times[2], int flags)
{
	int  idx_dfs, error = 0, rc;
	char *full_path = NULL;

	if (next_utimensat == NULL) {
		next_utimensat = dlsym(RTLD_NEXT, "utimensat");
		D_ASSERT(next_utimensat != NULL);
	}
	if (!hook_enabled)
		return next_utimensat(dirfd, path, times, flags);

	/* check path is NULL or not. path could be NULL since it is from application */
	if (path == NULL) {
		errno = EFAULT;
		return -1;
	}

	/* absolute path, dirfd is ignored */
	if (path[0] == '/')
		return utimens_timespec(path, times, flags);

	idx_dfs = check_path_with_dirfd(dirfd, &full_path, path, &error);
	if (error)
		goto out_err;

	if (idx_dfs >= 0)
		rc = utimens_timespec(full_path, times, flags);
	else
		rc = next_utimensat(dirfd, path, times, flags);

	error = errno;
	if (full_path) {
		free(full_path);
		errno = error;
	}
	return rc;

out_err:
	errno = error;
	return (-1);
}

int
futimens(int fd, const struct timespec times[2])
{
	int             rc;
	struct timespec times_loc;
	struct stat     stbuf;

	if (next_futimens == NULL) {
		next_futimens = dlsym(RTLD_NEXT, "futimens");
		D_ASSERT(next_futimens != NULL);
	}
	if (!hook_enabled || fd < FD_FILE_BASE)
		return next_futimens(fd, times);

	if (times == NULL) {
		clock_gettime(CLOCK_REALTIME, &times_loc);
		stbuf.st_atim.tv_sec  = times_loc.tv_sec;
		stbuf.st_atim.tv_nsec = times_loc.tv_nsec;
		stbuf.st_mtim.tv_sec  = times_loc.tv_sec;
		stbuf.st_mtim.tv_nsec = times_loc.tv_nsec;
	} else {
		stbuf.st_atim.tv_sec  = times[0].tv_sec;
		stbuf.st_atim.tv_nsec = times[0].tv_nsec;
		stbuf.st_mtim.tv_sec  = times[1].tv_sec;
		stbuf.st_mtim.tv_nsec = times[1].tv_nsec;
	}

	rc = dfs_osetattr(file_list[fd - FD_FILE_BASE]->dfs_mt->dfs,
			  file_list[fd - FD_FILE_BASE]->file, &stbuf,
			  DFS_SET_ATTR_ATIME | DFS_SET_ATTR_MTIME);
	if (rc) {
		errno = rc;
		return (-1);
	}

	return 0;
}

static int
new_fcntl(int fd, int cmd, ...)
{
	int     fd_Directed, param, OrgFunc = 1;
	int     Next_Dirfd, Next_fd, rc;
	va_list arg;

	switch (cmd) {
	case F_DUPFD:
	case F_DUPFD_CLOEXEC:
	case F_GETFD:
	case F_SETFD:
	case F_SETFL:
	case F_GETFL:
	case F_SETOWN:
	case F_SETSIG:
	case F_SETLEASE:
	case F_NOTIFY:
	case F_SETPIPE_SZ:
	case F_ADD_SEALS:
		va_start(arg, cmd);
		param = va_arg(arg, int);
		va_end(arg);

		fd_Directed = Get_Fd_Redirected(fd);

		if (!hook_enabled)
			return libc_fcntl(fd, cmd, param);

		if (cmd == F_GETFL) {
			if (fd_Directed >= FD_DIR_BASE)
				return dir_list[fd_Directed - FD_DIR_BASE]->open_flag;
			else if (fd_Directed >= FD_FILE_BASE)
				return file_list[fd_Directed - FD_FILE_BASE]->open_flag;
			else
				return libc_fcntl(fd, cmd);
		}

		if (fd_Directed >= FD_FILE_BASE) {
			if (cmd == F_SETFD)
				return 0;
		}

		if (fd_Directed >= FD_FILE_BASE)
			OrgFunc = 0;

		if ((cmd == F_DUPFD) || (cmd == F_DUPFD_CLOEXEC)) {
			if (fd_Directed >= FD_DIR_BASE) {
				rc = find_next_available_dirfd(
						dir_list[fd_Directed - FD_DIR_BASE], &Next_Dirfd);
				if (rc) {
					errno = rc;
					return (-1);
				}
				return (Next_Dirfd + FD_DIR_BASE);
			} else if (fd_Directed >= FD_FILE_BASE) {
				rc = find_next_available_fd(
						file_list[fd_Directed - FD_FILE_BASE], &Next_fd);
				if (rc) {
					errno = rc;
					return (-1);
				}
				return (Next_fd + FD_FILE_BASE);
			}
		} else if ((cmd == F_GETFD) || (cmd == F_SETFD)) {
			if (OrgFunc == 0)
				return 0;
		}
		/**		else if (cmd == F_GETFL)	{
		 *		}
		 */
		return libc_fcntl(fd, cmd, param);
	case F_SETLK:
	case F_SETLKW:
	case F_GETLK:
	case F_OFD_SETLK:
	case F_OFD_SETLKW:
	case F_OFD_GETLK:
	case F_GETOWN_EX:
	case F_SETOWN_EX:
		va_start(arg, cmd);
		param = va_arg(arg, int);
		va_end(arg);

		if (!hook_enabled)
			return libc_fcntl(fd, cmd, param);

		return libc_fcntl(fd, cmd, param);
	default:
		return libc_fcntl(fd, cmd);
	}
}

int
ioctl(int fd, unsigned long request, ...)
{
	va_list                         arg;
	void                           *param;
	struct dfuse_user_reply        *reply;

	va_start(arg, request);
	param = va_arg(arg, void *);
	va_end(arg);

	if (next_ioctl == NULL) {
		next_ioctl = dlsym(RTLD_NEXT, "ioctl");
		D_ASSERT(next_ioctl != NULL);
	}
	if (!hook_enabled || fd < FD_FILE_BASE)
		return next_ioctl(fd, request, param);

	/* To pass existing test of ioctl() with DFUSE_IOCTL_DFUSE_USER */
	/* Provided to pass dfuse_test                                  */
	if ((request & 0xFFFFFFFF) == 0x8008A3cA) {
		reply = (struct dfuse_user_reply *)param;
		reply->uid = getuid();
		reply->gid = getgid();
		return 0;
	}

	if (bLog)
		D_ERROR("ioctl() is not implemented yet.\n");
	errno = ENOTSUP;

	return -1;
}

int
dup(int oldfd)
{
	int fd_directed;

	if (next_dup == NULL) {
		next_dup = dlsym(RTLD_NEXT, "dup");
		D_ASSERT(next_dup != NULL);
	}
	if (!hook_enabled)
		return next_dup(oldfd);

	fd_directed = Get_Fd_Redirected(oldfd);
	if (fd_directed >= FD_FILE_BASE)
		return new_fcntl(oldfd, F_DUPFD, 0);

	return next_dup(fd_directed);
}

int
dup2(int oldfd, int newfd)
{
	int fd, fd_Directed, idx, rc;

	/* Need more work later. */
	if (next_dup2 == NULL) {
		next_dup2 = dlsym(RTLD_NEXT, "dup2");
		D_ASSERT(next_dup2 != NULL);
	}
	if (!hook_enabled)
		return next_dup2(oldfd, newfd);

	if (oldfd == newfd) {
		if (oldfd < FD_FILE_BASE)
			return next_dup2(oldfd, newfd);
		else
			return newfd;
	}
	if ((oldfd < FD_FILE_BASE) && (newfd < FD_FILE_BASE))
		return next_dup2(oldfd, newfd);

	if (newfd >= FD_FILE_BASE) {
		if (bLog)
			D_ERROR("unimplemented yet for newfd >= FD_FILE_BASE.\n");
		errno = ENOTSUP;
		return -1;
	}
	fd_Directed = query_fd_forward_dest(newfd);
	if (fd_Directed >= FD_FILE_BASE) {
		if (bLog)
			D_ERROR("unimplemented yet for fd_Directed >= FD_FILE_BASE.\n");
		errno = ENOTSUP;
		return -1;
	}

	if (oldfd >= FD_FILE_BASE)
		fd_Directed = oldfd;
	else
		fd_Directed = query_fd_forward_dest(oldfd);
	if (fd_Directed >= FD_FILE_BASE) {
		rc = close(newfd);
		if (rc != 0 && errno != EBADF)
			return -1;
		fd = allocate_a_fd_from_kernel();
		if (fd != newfd) {
			if (bLog)
				D_ERROR("failed to get the desired fd in dup2().");
			errno = EAGAIN;
			return (-1);
		}
		idx = allocate_dup2ed_fd(fd, fd_Directed);
		if (idx >= 0)
			return fd;
		else
			return idx;
	} else {
		next_dup2(oldfd, newfd);
	}
	return -1;
}

int
__dup2(int oldfd, int newfd) __attribute__((alias("dup2"), leaf, nothrow));

/**
 *int
 *dup3(int oldfd, int newfd, int flags)
 *{
 *	int i, fd_Directed;
 *
 *	if (real_dup3 == NULL)	{
 *		real_dup3 = dlsym(RTLD_NEXT, "dup3");
 *		D_ASSERT(real_dup3 != NULL);
 *	}
 *	if (!hook_enabled)
 *		return real_dup3(oldfd, newfd, flags);
 *	if (oldfd == newfd) {
 *		if (oldfd < FD_FILE_BASE)
 *			return real_dup3(oldfd, newfd, flags);
 *		else
 *			return newfd;
 *	}
 *	if (oldfd < FD_FILE_BASE)
 *		return real_dup3(oldfd, newfd, flags);
 *
 *	fd_Directed = Get_Fd_Redirected(oldfd);
 *
 *	for (i = 0; i <MAX_FD_DUP2ED; i++)	{
 *		if ((fd_dup2_list[i].fd_dest != oldfd) && (fd_dup2_list[i].fd_src==newfd))	{
 *			close(fd_dup2_list[i].fd_dest);
 *			fd_dup2_list[i].fd_src = -1;
 *			fd_dup2_list[i].fd_dest = -1;
 *		}
 *	}
 *
 *	if ((fd_Directed>=FD_FILE_BASE) && (newfd<FD_FILE_BASE))	{
 *		if (num_fd_dup2ed >= MAX_FD_DUP2ED)	{
 *			printf("ERROR: num_fd_dup2ed >= MAX_FD_DUP2ED\n");
 *			errno = EBADF;
 *			return -1;
 *		}
 *		else	{
 *			for (i = 0; i < MAX_FD_DUP2ED; i++)	{
 *				if (fd_dup2_list[i].fd_src == -1)	{	// available
 *					fd_dup2_list[i].fd_src = newfd;
 *					fd_dup2_list[i].fd_dest = fd_Directed;
 *					num_fd_dup2ed++;
 *					return newfd;
 *				}
 *			}
 *		}
 *	}
 *	else if ((fd_Directed == newfd) && (fd_Directed >= FD_FILE_BASE))	{
 *		return newfd;
 *	}
 *	else {
 *		return real_dup3(oldfd, newfd, flags);
 *	}
 *	return -1;
 *}
 */

void *
new_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	int             rc, idx_map;
	struct stat     stat_buf;
	void            *addr_ret;

	if (!hook_enabled || fd < FD_FILE_BASE)
		return next_mmap(addr, length, prot, flags, fd, offset);

	if (bLog)
		atomic_fetch_add_relaxed(&num_mmap, 1);

	addr_ret = next_mmap(addr, length, prot, flags | MAP_ANONYMOUS, -1, offset);
	if (addr_ret == MAP_FAILED)
		return MAP_FAILED;

	rc = dfs_ostat(file_list[fd - FD_FILE_BASE]->dfs_mt->dfs,
		       file_list[fd - FD_FILE_BASE]->file, &stat_buf);
	if (rc) {
		errno = rc;
		return MAP_FAILED;
	}

	rc = find_next_available_map(&idx_map);
	if (rc) {
		if (bLog)
			D_ERROR("mmap_list is out of space.");
		errno = rc;
		return MAP_FAILED;
	}

	file_list[fd - FD_FILE_BASE]->idx_mmap = idx_map;
	mmap_list[idx_map].addr = (char *)addr_ret;
	mmap_list[idx_map].length = length;
	mmap_list[idx_map].file_size = stat_buf.st_size;
	mmap_list[idx_map].prot = prot;
	mmap_list[idx_map].flags = flags;
	mmap_list[idx_map].fd = fd;
	mmap_list[idx_map].num_pages = length & (page_size - 1) ?
				       (length / page_size + 1) :
				       (length / page_size);
	mmap_list[idx_map].num_dirty_pages = 0;
	mmap_list[idx_map].offset = offset;
	D_ALLOC(mmap_list[idx_map].updated, sizeof(bool)*mmap_list[idx_map].num_pages);
	if (mmap_list[idx_map].updated == NULL) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	memset(mmap_list[idx_map].updated, 0, sizeof(bool)*mmap_list[idx_map].num_pages);

	/* Clear all permissions on these pages, so segv will be triggered by read/write */
	rc = mprotect(addr_ret, length, PROT_NONE);
	if (rc < 0)
		return (void *)(-1);
	if (!segv_handler_inited) {
		rc = pthread_mutex_lock(&lock_mmap);
		D_ASSERT(rc == 0);
		register_handler(SIGSEGV, &old_segv);
		segv_handler_inited = true;
		rc = pthread_mutex_unlock(&lock_mmap);
		D_ASSERT(rc == 0);
	}
	return addr_ret;
}

static int
flush_dirty_pages_to_file(int idx)
{
	int             idx_file, idx_page = 0, idx_page2, rc, num_pages;
	size_t          addr_min, addr_max;
	d_iov_t         iov;
	d_sg_list_t     sgl;

	num_pages = mmap_list[idx].num_pages;
	idx_file = mmap_list[idx].fd - FD_FILE_BASE;
	while (idx_page < num_pages) {
		/* To find the next non-dirty page */
		idx_page2 = idx_page + 1;
		while (idx_page2 < num_pages && mmap_list[idx].updated[idx_page2])
			idx_page2++;
		/* Write pages [idx_page, idx_page2-1] to file */
		sgl.sg_nr     = 1;
		sgl.sg_nr_out = 0;
		addr_min      = page_size*idx_page + mmap_list[idx].offset;
		addr_max      = addr_min + (idx_page2 - idx_page)*page_size;
		if (addr_max > mmap_list[idx].file_size)
			addr_max = mmap_list[idx].file_size;
		d_iov_set(&iov, (void *)(mmap_list[idx].addr + addr_min), addr_max - addr_min);
		sgl.sg_iovs = &iov;
		rc          = dfs_write(file_list[idx_file]->dfs_mt->dfs,
					file_list[idx_file]->file, &sgl, addr_min, NULL);
		if (rc) {
			errno = rc;
			return (-1);
		}

		/* Find the next updated page */
		idx_page = idx_page2 + 1;
		while (idx_page < num_pages && !mmap_list[idx].updated[idx_page])
			idx_page++;
	}

	return 0;
}

int
new_munmap(void *addr, size_t length)
{
	int i, rc;

	if (!hook_enabled)
		return next_munmap(addr, length);

	for (i = 0; i <= last_map; i++) {
		if (mmap_list[i].addr == addr) {
			if (mmap_list[i].flags & MAP_SHARED &&
			    mmap_list[i].num_dirty_pages) {
				/* Need to flush dirty pages to file */
				rc = flush_dirty_pages_to_file(i);
				if (rc < 0)
					return rc;
			}
			D_FREE(mmap_list[i].updated);
			free_map(i);

			return next_munmap(addr, length);
		}
	}

	return next_munmap(addr, length);
}

int
posix_fadvise(int fd, off_t offset, off_t len, int advice)
{
	if (next_posix_fadvise == NULL) {
		next_posix_fadvise = dlsym(RTLD_NEXT, "posix_fadvise");
		D_ASSERT(next_posix_fadvise != NULL);
	}
	if (!hook_enabled || fd < FD_FILE_BASE)
		return next_posix_fadvise(fd, offset, len, advice);

	/* Hint to turn off caching. */
	if (advice == POSIX_FADV_DONTNEED)
		return 0;
/**
 *	if (bLog)
 *		D_ERROR("posix_fadvise() is not implemented yet.\n");
 */
	errno = ENOTSUP;
	return -1;
}

int
posix_fadvise64(int fd, off_t offset, off_t len, int advice)
	__attribute__((alias("posix_fadvise")));

int
flock(int fd, int operation)
{
	if (next_flock == NULL) {
		next_flock = dlsym(RTLD_NEXT, "flock");
		D_ASSERT(next_flock != NULL);
	}
	if (!hook_enabled || fd < FD_FILE_BASE)
		return next_flock(fd, operation);

	if (bLog)
		D_ERROR("flock() is not implemented yet.\n");
	errno = ENOTSUP;
	return -1;
}

int
fallocate(int fd, int mode, off_t offset, off_t len)
{
	if (next_fallocate == NULL) {
		next_fallocate = dlsym(RTLD_NEXT, "fallocate");
		D_ASSERT(next_fallocate != NULL);
	}
	if (!hook_enabled || fd < FD_FILE_BASE)
		return next_fallocate(fd, mode, offset, len);

	if (bLog)
		D_ERROR("fallocate() is not implemented yet.\n");
	errno = ENOTSUP;
	return -1;
}

int
posix_fallocate(int fd, off_t offset, off_t len)
{
	if (next_posix_fallocate == NULL) {
		next_posix_fallocate = dlsym(RTLD_NEXT, "posix_fallocate");
		D_ASSERT(next_posix_fallocate != NULL);
	}
	if (!hook_enabled || fd < FD_FILE_BASE)
		return next_posix_fallocate(fd, offset, len);

	if (bLog)
		D_ERROR("posix_fallocate() is not implemented yet.\n");
	errno = ENOTSUP;
	return -1;
}

int
posix_fallocate64(int fd, off64_t offset, off64_t len)
{
	if (next_posix_fallocate64 == NULL) {
		next_posix_fallocate64 = dlsym(RTLD_NEXT, "posix_fallocate64");
		D_ASSERT(next_posix_fallocate64 != NULL);
	}
	if (!hook_enabled || fd < FD_FILE_BASE)
		return next_posix_fallocate64(fd, offset, len);

	if (bLog)
		D_ERROR("posix_fallocate64() is not implemented yet.\n");
	errno = ENOTSUP;
	return -1;
}

int
tcgetattr(int fd, void *termios_p)
{
	if (next_tcgetattr == NULL) {
		next_tcgetattr = dlsym(RTLD_NEXT, "tcgetattr");
		D_ASSERT(next_tcgetattr != NULL);
	}
	if (!hook_enabled || fd < FD_FILE_BASE)
		return next_tcgetattr(fd, termios_p);

	if (bLog)
		D_ERROR("tcgetattr() is not implemented yet.\n");
	errno = ENOTSUP;
	return -1;
}

static void
new_exit(int rc)
{
	if (!hook_enabled)
		return next_exit(rc);

	print_summary();
	next_exit(rc);
}

static void
update_cwd(void)
{
	char *cwd = NULL;
	char *pt_end = NULL;

	/* daos_init() may be not called yet. */
	cwd = get_current_dir_name();

	if (cwd == NULL) {
		fprintf(stderr, "Error> Fail to get CWD with get_current_dir_name().\nQuit\n");
		exit(1);
	} else {
		pt_end = stpncpy(cur_dir, cwd, DFS_MAX_PATH - 1);
		if ((long int)(pt_end - cur_dir) >= DFS_MAX_PATH - 1) {
			fprintf(stderr, "Error> cwd path is too long.\nQuit\n");
			exit(1);
		}
		free(cwd);
	}
}

static int
query_mmap_block(char *addr)
{
	int i;

	for (i = 0 ; i <= last_map; i++) {
		if (mmap_list[i].addr) {
			if (addr >= mmap_list[i].addr &&
			    addr < (mmap_list[i].addr + mmap_list[i].length))
				return i;
		}
	}
	return (-1);
}

static void *
align_to_page_boundary(void *addr)
{
	return (void *)((unsigned long int)addr & ~(page_size - 1));
}

static void
sig_handler(int code, siginfo_t *siginfo, void *ctx)
{
	char                   *addr, err_msg[256];
	size_t                  bytes_read, length;
	size_t                  addr_min, addr_max, idx_page;
	int                     rc, fd, idx_map;
	d_iov_t                 iov;
	d_sg_list_t             sgl;
	struct ucontext_t      *context = (struct ucontext_t *)ctx;

	if (code != SIGSEGV)
		return old_segv.sa_sigaction(code, siginfo, context);
	addr = (char *)siginfo->si_addr;
	idx_map = query_mmap_block(addr);
	if (idx_map < 0)
		return old_segv.sa_sigaction(code, siginfo, context);

	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	addr_min      = (size_t)align_to_page_boundary(addr);
	/* Out of the range of file size */
	if ((addr_min + (size_t)mmap_list[idx_map].offset - (size_t)mmap_list[idx_map].addr) >
	    mmap_list[idx_map].file_size)
		return old_segv.sa_sigaction(code, siginfo, context);
	/* We read only one page at this time. Reading multiple pages may get better performance. */
	addr_max      = addr_min + page_size;
	if ((addr_max - (size_t)mmap_list[idx_map].addr + (size_t)mmap_list[idx_map].offset) >
	    mmap_list[idx_map].file_size)
		addr_max = mmap_list[idx_map].file_size - (size_t)mmap_list[idx_map].offset +
			   (size_t)mmap_list[idx_map].addr;
	d_iov_set(&iov, (void *)addr_min, addr_max - addr_min);
	sgl.sg_iovs = &iov;
	fd = mmap_list[idx_map].fd - FD_FILE_BASE;

	length = addr_max - addr_min;
	length = (length & (page_size - 1) ? (length + page_size - (length & (page_size - 1))) :
		(length));
	/* Restore the read & write permission on page.                                     */
	/* Need more work here! App may read first, write later. We do label the page dirty. */
	rc = mprotect((void *)addr_min, length, PROT_READ | PROT_WRITE);
	if (rc < 0) {
		/* It is tricky to print in signal handler. Use write(STDERR_FILENO, ...) */
		snprintf(err_msg, 256, "Error in mprotect() in signal handler. %s\n",
			 strerror(errno));
		rc = libc_write(STDERR_FILENO, err_msg, strnlen(err_msg, 256));
		exit(1);
	}

	rc          = dfs_read(file_list[fd]->dfs_mt->dfs,
			       file_list[fd]->file, &sgl,
			       addr_min -  (size_t)mmap_list[idx_map].addr +
			       (size_t)mmap_list[idx_map].offset, &bytes_read, NULL);
	if (rc) {
		/* It is tricky to print in signal handler. Use write(STDERR_FILENO, ...) */
		snprintf(err_msg, 256, "Error in dfs_read() in signal handler. %s\n",
			 strerror(errno));
		rc = libc_write(STDERR_FILENO, err_msg, strnlen(err_msg, 256));
	}
#if defined(__x86_64__)
	if (context->uc_mcontext.gregs[REG_ERR] & 0x2) {
		/* Write fault, set flag for dirty page which will be written to file later */
		idx_page = (addr_min - (size_t)mmap_list[idx_map].addr) / page_size;
		mmap_list[idx_map].updated[idx_page] = true;
		mmap_list[idx_map].num_dirty_pages++;

	} else if (context->uc_mcontext.gregs[REG_ERR] & 0x1) {
		/* Read fault, do nothing */
	}
#elif defined(__aarch64__)
	/* #define ESR_ELx_CM (UL(1) << 8) */
	if ((context->uc_mcontext.__reserved[0x219] & 1) == 0) {
		/* Fault is not from executing instruction */
		/* #define ESR_ELx_WNR (UL(1) << 6) */
		if (context->uc_mcontext.__reserved[0x218] & 0x40) {
			/* Write fault */
			idx_page = (addr_min - (size_t)mmap_list[idx_map].addr) / page_size;
			mmap_list[idx_map].updated[idx_page] = true;
			mmap_list[idx_map].num_dirty_pages++;

		} else{
			/* Read fault, do nothing */
		}
	}
#else
#error Unsupported architecture. Only x86_64 and aarch64 are supported.
#endif
}

static void
register_handler(int sig, struct sigaction *old_handler)
{
	struct sigaction        action;
	int                     rc;

	action.sa_flags = SA_RESTART;
	action.sa_handler = NULL;
	action.sa_sigaction = sig_handler;
	action.sa_flags |= SA_SIGINFO;
	sigemptyset(&action.sa_mask);

	rc = sigaction(sig, &action, old_handler);
	if (rc != 0) {
		fprintf(stderr, "Error> sigaction failed with %d. errno: %d\n", rc, errno);
		exit(1);
	}
}

static __attribute__((constructor)) void
init_myhook(void)
{
	mode_t umask_old;
	char   *env_log;

	/* DAOS log utility is not available since daos_init() is not called yet. */

	umask_old = umask(0);
	umask(umask_old);
	mode_not_umask = ~umask_old;
	page_size = sysconf(_SC_PAGESIZE);

	env_log = getenv("D_IL_REPORT");
	if (env_log) {
		bLog = true;
		if (strncmp(env_log, "0", 2) == 0 || strncasecmp(env_log, "false", 6) == 0)
			bLog = false;
	}

	update_cwd();
	if (pthread_mutex_init(&lock_dfs, NULL) != 0) {
		fprintf(stderr, "Error> mutex create_new_lock lock_dfs failed\n");
		/* fatal error: failure to create a mutex. */
		exit(1);
	}
	init_fd_list();

	register_a_hook("ld", "open64", (void *)new_open_ld, (long int *)(&ld_open));
	register_a_hook("libc", "open64", (void *)new_open_libc, (long int *)(&libc_open));
	register_a_hook("libpthread", "open64", (void *)new_open_pthread,
			(long int *)(&pthread_open));

	register_a_hook("libc", "__close", (void *)new_close_libc, (long int *)(&libc_close));
	register_a_hook("libpthread", "__close", (void *)new_close_pthread,
			(long int *)(&pthread_close));
	register_a_hook("libc", "__close_nocancel", (void *)new_close_nocancel,
			(long int *)(&libc_close_nocancel));

	register_a_hook("libc", "__read", (void *)new_read_libc, (long int *)(&libc_read));
	register_a_hook("libpthread", "__read", (void *)new_read_pthread,
			(long int *)(&pthread_read));
	register_a_hook("libc", "__write", (void *)new_write_libc, (long int *)(&libc_write));
	register_a_hook("libpthread", "__write", (void *)new_write_pthread,
			(long int *)(&pthread_write));

	register_a_hook("libc", "lseek64", (void *)new_lseek_libc, (long int *)(&libc_lseek));
	register_a_hook("libpthread", "lseek64", (void *)new_lseek_pthread,
			(long int *)(&pthread_lseek));

	register_a_hook("libc", "unlink", (void *)new_unlink, (long int *)(&libc_unlink));

	register_a_hook("libc", "__fxstat", (void *)new_fxstat, (long int *)(&next_fxstat));
	register_a_hook("libc", "__xstat", (void *)new_xstat, (long int *)(&next_xstat));
	/* Many variants for lxstat: _lxstat, __lxstat, ___lxstat, __lxstat64 */
	register_a_hook("libc", "__lxstat", (void *)new_lxstat, (long int *)(&libc_lxstat));
	register_a_hook("libc", "__fxstatat", (void *)new_fxstatat, (long int *)(&next_fxstatat));
	register_a_hook("libc", "readdir", (void *)new_readdir, (long int *)(&next_readdir));

	register_a_hook("libc", "fcntl", (void *)new_fcntl, (long int *)(&libc_fcntl));

	register_a_hook("libc", "mmap", (void *)new_mmap, (long int *)(&next_mmap));
	register_a_hook("libc", "munmap", (void *)new_munmap, (long int *)(&next_munmap));

	register_a_hook("libc", "exit", (void *)new_exit, (long int *)(&next_exit));

	/**	register_a_hook("libc", "execve", (void *)new_execve, (long int *)(&real_execve));
	 *	register_a_hook("libc", "execvp", (void *)new_execvp, (long int *)(&real_execvp));
	 *	register_a_hook("libc", "execv", (void *)new_execv, (long int *)(&real_execv));
	 *	register_a_hook("libc", "fork", (void *)new_fork, (long int *)(&real_fork));
	 */

	init_fd_dup2_list();
	discover_daos_mount();

	install_hook();

	hook_enabled = 1;
}

static void
print_summary(void)
{
	uint64_t op_sum = 0;
	uint64_t read_loc, write_loc, open_loc, stat_loc;
	uint64_t opendir_loc, readdir_loc, unlink_loc, seek_loc;
	uint64_t mkdir_loc, rmdir_loc, rename_loc, mmap_loc;

	if (!bLog)
		return;

	read_loc    = atomic_load_relaxed(&num_read);
	write_loc   = atomic_load_relaxed(&num_write);
	open_loc    = atomic_load_relaxed(&num_open);
	stat_loc    = atomic_load_relaxed(&num_stat);
	opendir_loc = atomic_load_relaxed(&num_opendir);
	readdir_loc = atomic_load_relaxed(&num_readdir);
	unlink_loc  = atomic_load_relaxed(&num_unlink);
	seek_loc    = atomic_load_relaxed(&num_seek);
	mkdir_loc   = atomic_load_relaxed(&num_mkdir);
	rmdir_loc   = atomic_load_relaxed(&num_rmdir);
	rename_loc  = atomic_load_relaxed(&num_rename);
	mmap_loc    = atomic_load_relaxed(&num_mmap);
	fprintf(stderr, "libpil4dfs intercepting summary for ops on DFS:\n");
	fprintf(stderr, "[read   ]  %" PRIu64 "\n", read_loc);
	fprintf(stderr, "[write  ]  %" PRIu64 "\n", write_loc);
	fprintf(stderr, "\n");
	fprintf(stderr, "[open   ]  %" PRIu64 "\n", open_loc);
	fprintf(stderr, "[stat   ]  %" PRIu64 "\n", stat_loc);
	fprintf(stderr, "[opendir]  %" PRIu64 "\n", opendir_loc);
	fprintf(stderr, "[readdir]  %" PRIu64 "\n", readdir_loc);
	fprintf(stderr, "[unlink ]  %" PRIu64 "\n", unlink_loc);
	fprintf(stderr, "[seek   ]  %" PRIu64 "\n", seek_loc);
	fprintf(stderr, "[mkdir  ]  %" PRIu64 "\n", mkdir_loc);
	fprintf(stderr, "[rmdir  ]  %" PRIu64 "\n", rmdir_loc);
	fprintf(stderr, "[rename ]  %" PRIu64 "\n", rename_loc);
	fprintf(stderr, "[mmap   ]  %" PRIu64 "\n", mmap_loc);

	op_sum = read_loc + write_loc + open_loc + stat_loc + opendir_loc + readdir_loc +
		 unlink_loc + seek_loc + mkdir_loc + rmdir_loc + rename_loc + mmap_loc;
	fprintf(stderr, "\n");
	fprintf(stderr, "[op_sum ]  %" PRIu64 "\n", op_sum);
}

static void
close_all_fd(void)
{
	int i;

	for (i = 0; i < next_free_fd; i++) {
		if (file_list[i])
			free_fd(i);
	}
}

static void
close_all_dirfd(void)
{
	int i;

	for (i = 0; i < next_free_dirfd; i++) {
		if (dir_list[i])
			free_dirfd(i);
	}
}

static __attribute__((destructor)) void
finalize_myhook(void)
{
	int rc;

	close_all_duped_fd();
	close_all_fd();
	close_all_dirfd();
	finalize_dfs();

	rc = pthread_mutex_destroy(&lock_dfs);
	D_ASSERT(rc == 0);
	rc = pthread_mutex_destroy(&lock_dirfd);
	D_ASSERT(rc == 0);
	rc = pthread_mutex_destroy(&lock_fd);
	D_ASSERT(rc == 0);
	rc = pthread_mutex_destroy(&lock_mmap);
	D_ASSERT(rc == 0);
	rc = pthread_mutex_destroy(&lock_fd_dup2ed);
	D_ASSERT(rc == 0);

	uninstall_hook();
}

static int
init_dfs(int idx)
{
	int rc, rc2;

	/* TODO: Need to check the permission of mount point first!!! */
	rc =
	    daos_pool_connect(dfs_list[idx].pool, NULL, DAOS_PC_RW, &dfs_list[idx].poh,
			      NULL, NULL);
	if (rc != 0) {
		D_ERROR("failed to connect pool: "DF_RC"\nQuit\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	rc = daos_cont_open(dfs_list[idx].poh, dfs_list[idx].cont, DAOS_COO_RW, &dfs_list[idx].coh,
			    NULL, NULL);
	if (rc != 0) {
		D_ERROR("failed to open container: "DF_RC"\nQuit\n", DP_RC(rc));
		D_GOTO(out_err_cont_open, rc);
	}
	rc = dfs_mount(dfs_list[idx].poh, dfs_list[idx].coh, O_RDWR, &dfs_list[idx].dfs);
	if (rc != 0) {
		D_ERROR("failed to mount dfs: %s\nQuit\n", strerror(rc));
		D_GOTO(out_err_mt, rc);
	}
	rc = d_hash_table_create(D_HASH_FT_EPHEMERAL | D_HASH_FT_MUTEX | D_HASH_FT_LRU, 6, NULL,
				 &hdl_hash_ops, &dfs_list[idx].dfs_dir_hash);
	if (rc != 0) {
		D_ERROR("failed to create hash table: "DF_RC"\nQuit\n", DP_RC(rc));
		D_GOTO(out_err_ht, rc = daos_der2errno(rc));
	}

	return 0;

out_err_ht:
	rc2 = dfs_umount(dfs_list[idx].dfs);
	if (rc2 != 0)
		D_ERROR("error in dfs_umount() for %s\n", dfs_list[idx].fs_root);

out_err_mt:
	rc2 = daos_cont_close(dfs_list[idx].coh, NULL);
	if (rc2 != 0)
		D_ERROR("error in daos_cont_close() for %s\n", dfs_list[idx].fs_root);

out_err_cont_open:
	rc2 = daos_pool_disconnect(dfs_list[idx].poh, NULL);
	if (rc2 != 0)
		D_ERROR("error in daos_pool_disconnect() for %s\n", dfs_list[idx].fs_root);

	return rc;
}

static void
finalize_dfs(void)
{
	int       rc, i;
	d_list_t *rlink = NULL;

	for (i = 0; i < num_dfs; i++) {
		if (dfs_list[i].dfs_dir_hash == NULL)
			continue;

		while (1) {
			rlink = d_hash_rec_first(dfs_list[i].dfs_dir_hash);
			if (rlink == NULL)
				break;
			d_hash_rec_decref(dfs_list[i].dfs_dir_hash, rlink);
		}

		rc = d_hash_table_destroy(dfs_list[i].dfs_dir_hash, false);
		if (rc != 0) {
			D_ERROR("error in d_hash_table_destroy() for %s\n", dfs_list[i].fs_root);
			continue;
		}
		rc = dfs_umount(dfs_list[i].dfs);
		if (rc != 0) {
			D_ERROR("error in dfs_umount() for %s\n", dfs_list[i].fs_root);
			continue;
		}
		rc = daos_cont_close(dfs_list[i].coh, NULL);
		if (rc != 0) {
			D_ERROR("error in daos_cont_close() for %s\n", dfs_list[i].fs_root);
			continue;
		}
		rc = daos_pool_disconnect(dfs_list[i].poh, NULL);
		if (rc != 0) {
			D_ERROR("error in daos_pool_disconnect() for %s\n",
				dfs_list[i].fs_root);
			continue;
		}
	}

	if (daos_inited) {
		uint32_t init_cnt, j;

		init_cnt = atomic_load_relaxed(&daos_init_cnt);
		for (j = 0; j < init_cnt; j++) {
			rc = daos_fini();
			if (rc != 0) {
				/* D_ERROR may be not available */
				fprintf(stderr, "Error> finalize_dfs(): error in daos_fini()\n");
			}
		}
	}
}
