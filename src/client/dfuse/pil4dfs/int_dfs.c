/**
 * (C) Copyright 2022-2024 Intel Corporation.
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
#include <stdint.h>
#include <inttypes.h>
#include <sys/ucontext.h>
#include <sys/user.h>
#include <linux/binfmts.h>

#ifdef __aarch64__
#ifndef PAGE_SIZE
#define PAGE_SIZE 0x1000
#endif
#endif

#include <stdatomic.h>
#include <stdbool.h>

#include <daos/debug.h>
#include <gurt/list.h>
#include <gurt/common.h>
#include <daos.h>
#include <daos_fs.h>
#include <daos_uns.h>
#include <dfuse_ioctl.h>
#include <daos/event.h>
#include <daos_prop.h>
#include <daos/common.h>
#include <daos/dfs_lib_int.h>

#include "hook.h"
#include "pil4dfs_int.h"

/* useful in strncmp() and strndup() */
#define STR_AND_SIZE(s)    s, sizeof(s)
/* useful in strncmp() to check whether a string start with a target string. Not including \0 */
#define STR_AND_SIZE_M1(s) s, sizeof(s) - 1

/* D_ALLOC and D_FREE can not be used in query_path(). It causes dead lock during daos_init(). */
#define FREE(ptr)	do {free(ptr); (ptr) = NULL; } while (0)

/* The max number of mount points for DAOS mounted simultaneously */
#define MAX_DAOS_MT         (8)

#define READ_DIR_BATCH_SIZE (96)
#define MAX_FD_DUP2ED       (16)

/* The buffer size used for reading/writing in rename() */
#define FILE_BUFFER_SIZE    (64 * 1024 * 1024)

/* Create a fake st_ino in stat for a path */
#define FAKE_ST_INO(path)   (d_hash_string_u32(path, strnlen(path, DFS_MAX_PATH)))

/* the default min fd that will be used by DAOS */
#define DAOS_MIN_FD         10
/* a dummy fd that will be used to reserve low fd with dup2(). This is introduced to make sure low
 * fds are reserved as much as possible although applications (e.g., bash) may directly access
 * low fd. Once a low fd is freed and available, calls dup2(fd_dummy, fd) to reserve it.
 */
#define DAOS_DUMMY_FD       1001
/* fd_dummy actually reserved by pil4dfs returned by fcntl() */
static int                    fd_dummy = -1;

/* Default power2(bits) size of dir-cache */
#define DCACHE_SIZE_BITS      16
/* Default dir cache time-out in seconds */
#define DCACHE_REC_TIMEOUT    60
/* Default maximal number of dir cash entries to reclaim */
#define DCACHE_GC_RECLAIM_MAX 1000
/* Default dir cache garbage collector time-out in seconds */
#define DCACHE_GC_PERIOD      120

/* the number of low fd reserved */
static uint16_t               low_fd_count;
/* the list of low fd reserved */
static int                    low_fd_list[DAOS_MIN_FD];

/* flag whether fd 255 is reserved by pil4dfs */
static bool                   fd_255_reserved;

/* In case of fork(), only the parent process could destroy daos env. */
static bool                   context_reset;
static __thread daos_handle_t td_eqh;

static daos_handle_t          main_eqh;
static daos_handle_t          eq_list[MAX_EQ];
uint16_t                      d_eq_count_max;
uint16_t                      d_eq_count;
static uint16_t               eq_idx;

/* Configuration of the Garbage Collector */
static uint32_t               dcache_size_bits;
static uint32_t               dcache_rec_timeout;
static uint32_t               dcache_gc_reclaim_max;
static uint32_t               dcache_gc_period;

static _Atomic uint64_t        num_read;
static _Atomic uint64_t        num_write;
static _Atomic uint64_t        num_open;
static _Atomic uint64_t        num_stat;
static _Atomic uint64_t        num_opendir;
static _Atomic uint64_t        num_readdir;
static _Atomic uint64_t        num_link;
static _Atomic uint64_t        num_unlink;
static _Atomic uint64_t        num_rdlink;
static _Atomic uint64_t        num_seek;
static _Atomic uint64_t        num_mkdir;
static _Atomic uint64_t        num_rmdir;
static _Atomic uint64_t        num_rename;
static _Atomic uint64_t        num_mmap;

static _Atomic uint32_t        daos_init_cnt;

/* "report" is a bool to control whether a summary is printed or not when an application
 * finishes. Unsupported/unimplemented functions will output an error message with D_ERROR
 * too if "report" is true. Env variable "D_IL_REPORT=1" or "D_IL_REPORT=true" will set report
 * true.
 */
static bool             report;
/* current application is bash/sh or not */
static bool             is_bash;
/* "no_dcache_in_bash" is a flag to control whether turns off directory caching inside sh/bash
 * process. It is set true as default.
 */
static bool             no_dcache_in_bash = true;

/* "d_compatible_mode" is a bool to control whether passing open(), openat(), and opendir() to dfuse
 * all the time to avoid using fake fd. Env variable "D_IL_COMPATIBLE=1" will set it true. This
 * can increase the compatibility of libpil4dfs with degraded performance in open(), openat(),
 * and opendir().
 */
bool                    d_compatible_mode;
static long int         page_size;

#define DAOS_INIT_NOT_RUNNING 0
#define DAOS_INIT_RUNNING     1

static _Atomic uint64_t mpi_init_count;

static long int         daos_initing;
_Atomic bool            d_daos_inited;
static bool             daos_debug_inited;
static int              num_dfs;
static struct dfs_mt    dfs_list[MAX_DAOS_MT];

/* libpil4dfs include two scenarios / code paths, 1) interception disabled (let fuse to handle I/O)
 * and 2) interception enabled. The code path is chosen by determining whether application's name
 * is in bypass list (built-in and user supplied). Bypass is allowed as default.
 * An env "D_IL_NO_BYPASS" can be set 1 to disable bypass and always enable interception in CI
 * testing. Please be aware that this env is not documented for users since it is ONLY for testing.
 */
bool                    bypass_allowed = true;
/* bypass interception in current process. */
static bool             bypass;
/* base name of the current application name */
static char            *exe_short_name;
static char            *first_arg;

/* the list of apps provided by user including all child processes to be skipped for interception */
static char            *bypass_user_cmd_list;

static void
extract_exe_name_1st_arg(void);

static int
init_dfs(int idx);
static int
query_dfs_mount(const char *dfs_mount);

struct fd_dup2 {
	int fd_src, fd_dest;
};

/* Add the data structure for statx_timestamp and statx
 * since they are not defined under CentOS 7.9.
 * https://man7.org/linux/man-pages/man2/statx.2.html
 */
#ifndef __statx_defined
struct statx_timestamp {
	int64_t  tv_sec;
	uint32_t tv_nsec;
};

struct statx {
	uint32_t stx_mask;        /* Mask of bits indicating filled fields */
	uint32_t stx_blksize;     /* Block size for filesystem I/O */
	uint64_t stx_attributes;  /* Extra file attribute indicators */
	uint32_t stx_nlink;       /* Number of hard links */
	uint32_t stx_uid;         /* User ID of owner */
	uint32_t stx_gid;         /* Group ID of owner */
	uint32_t stx_mode;        /* File type and mode */
	uint64_t stx_ino;         /* Inode number */
	uint64_t stx_size;        /* Total size in bytes */
	uint64_t stx_blocks;      /* Number of 512B blocks allocated */
	uint64_t stx_attributes_mask; /* Mask to show what's supported in stx_attributes */

	/* The following fields are file timestamps */
	struct statx_timestamp stx_atime;  /* Last access */
	struct statx_timestamp stx_btime;  /* Creation */
	struct statx_timestamp stx_ctime;  /* Last status change */
	struct statx_timestamp stx_mtime;  /* Last modification */

	/* If this file represents a device, then the next two fields contain the ID of
	 * the device
	 */
	uint32_t stx_rdev_major;  /* Major ID */
	uint32_t stx_rdev_minor;  /* Minor ID */

	/* The next two fields contain the ID of the device
	 * containing the filesystem where the file resides
	 */
	uint32_t stx_dev_major;   /* Major ID */
	uint32_t stx_dev_minor;   /* Minor ID */
	uint64_t stx_mnt_id;      /* Mount ID */
};
#endif

/* working dir of current process */
static char             cur_dir[DFS_MAX_PATH] = "";
static bool             segv_handler_inited;
/* Old segv handler */
struct sigaction        old_segv;

/* the flag to indicate whether initlization is finished or not */
bool                    d_hook_enabled;
static bool             hook_enabled_bak;
static pthread_mutex_t  lock_reserve_fd;
static pthread_mutex_t  lock_dfs;
static pthread_mutex_t  lock_fd;
static pthread_mutex_t  lock_dirfd;
static pthread_mutex_t  lock_mmap;
static pthread_rwlock_t lock_fd_dup2ed;
static pthread_mutex_t  lock_eqh;

/* store ! umask to apply on mode when creating file to honor system umask */
static mode_t           mode_not_umask;

static void
finalize_dfs(void);
static void
update_cwd(void);
static int
get_eqh(daos_handle_t *eqh);
static void
destroy_all_eqs(void);

static void
free_fd(int idx, bool closing_dup_fd);
static void
free_dirfd(int idx);

/* Hash table entry for kernel fd.
 */

/* The hash table to store kernel_fd - fake_fd in compatible mode */
struct d_hash_table *fd_hash;

struct ht_fd {
	d_list_t entry;
	int      real_fd;
	int      fake_fd;
};

static inline struct ht_fd *
fd_obj(d_list_t *rlink)
{
	return container_of(rlink, struct ht_fd, entry);
}

static bool
fd_key_cmp(struct d_hash_table *htable, d_list_t *rlink, const void *key, unsigned int ksize)
{
	struct ht_fd *fd = fd_obj(rlink);

	return (fd->real_fd == *((int *)key));
}

static void
fd_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ht_fd *fd = fd_obj(rlink);

	/* close fake fd */
	if (fd->fake_fd >= FD_DIR_BASE)
		free_dirfd(fd->fake_fd - FD_DIR_BASE);
	else
		free_fd(fd->fake_fd - FD_FILE_BASE, false);

	D_FREE(fd);
}

static bool
fd_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	return true;
}

static uint32_t
fd_rec_hash(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ht_fd *fd = fd_obj(rlink);

	return d_u32_hash((uint64_t)fd->real_fd & 0xFFFFFFFF, 6);
}

static d_hash_table_ops_t fd_hash_ops = {.hop_key_cmp    = fd_key_cmp,
					 .hop_rec_decref = fd_rec_decref,
					 .hop_rec_free   = fd_rec_free,
					 .hop_rec_hash   = fd_rec_hash};

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

static ssize_t (*next_readv)(int fd, const struct iovec *iov, int iovcnt);
static ssize_t (*next_writev)(int fd, const struct iovec *iov, int iovcnt);

static off_t (*libc_lseek)(int fd, off_t offset, int whence);
static off_t (*pthread_lseek)(int fd, off_t offset, int whence);

static int new_fxstat(int vers, int fd, struct stat *buf);

static int (*next_fxstat)(int vers, int fd, struct stat *buf);
static int (*next_fstat)(int fd, struct stat *buf);

static int (*next_statfs)(const char *pathname, struct statfs *buf);
static int (*next_fstatfs)(int fd, struct statfs *buf);

static int (*next_statvfs)(const char *pathname, struct statvfs *buf);

static DIR *(*next_opendir)(const char *name);

static DIR *(*next_fdopendir)(int fd);

static int (*next_closedir)(DIR *dirp);

static struct dirent *(*next_readdir)(DIR *dirp);

static long (*next_telldir)(DIR *dirp);
static void (*next_seekdir)(DIR *dirp, long loc);
static void (*next_rewinddir)(DIR *dirp);
static int (*next_scandirat)(int dirfd, const char *restrict path,
			     struct dirent ***restrict namelist,
			     int (*filter)(const struct dirent *),
			     int (*compar)(const struct dirent **, const struct dirent **));

static int (*next_mkdir)(const char *path, mode_t mode);

static int (*next_mkdirat)(int dirfd, const char *pathname, mode_t mode);

static int (*next_xstat)(int ver, const char *path, struct stat *stat_buf);

static int (*libc_lxstat)(int ver, const char *path, struct stat *stat_buf);

static int (*libc_fxstatat)(int ver, int dirfd, const char *path, struct stat *stat_buf, int flags);

static int (*libc_fstatat)(int dirfd, const char *path, struct stat *stat_buf, int flags);

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

static int (*next_fdatasync)(int fd);

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

static int (*libc_dup3)(int oldfd, int newfd, int flags);

static int (*next_symlink)(const char *symvalue, const char *path);

static int (*next_symlinkat)(const char *symvalue, int dirfd, const char *path);

static ssize_t (*libc_readlink)(const char *path, char *buf, size_t size);
static ssize_t (*next_readlinkat)(int dirfd, const char *path, char *buf, size_t size);

static void * (*next_mmap)(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
static int (*next_munmap)(void *addr, size_t length);

static void (*next_exit)(int rc);
static void (*next__exit)(int rc) __attribute__((__noreturn__));

/* typedef int (*org_dup3)(int oldfd, int newfd, int flags); */
/* static org_dup3 real_dup3=NULL; */

static int (*next_execve)(const char *filename, char *const argv[], char *const envp[]);
static int (*next_execv)(const char *filename, char *const argv[]);
static int (*next_execvp)(const char *filename, char *const argv[]);
static int (*next_execvpe)(const char *filename, char *const argv[], char *const envp[]);
static int (*next_fexecve)(int fd, char *const argv[], char *const envp[]);

static pid_t (*next_fork)(void);

/* start NOT supported by DAOS */
static int (*next_posix_fadvise)(int fd, off_t offset, off_t len, int advice);
static int (*next_flock)(int fd, int operation);
static int (*next_fallocate)(int fd, int mode, off_t offset, off_t len);
static int (*next_posix_fallocate)(int fd, off_t offset, off_t len);
static int (*next_posix_fallocate64)(int fd, off64_t offset, off64_t len);
static int (*next_tcgetattr)(int fd, void *termios_p);
/* end NOT supported by DAOS */

static int (*next_mpi_init)(int *argc, char ***argv);

/* to do!! */
/**
 * static char * (*org_realpath)(const char *pathname, char *resolved_path);
 * org_realpath real_realpath=NULL;
 */

static int
remove_dot_dot(char path[], int *len);
static int
remove_dot_and_cleanup(char szPath[], int len);

/* reference count of fake fd duplicated by real fd with dup2() */
static int                dup_ref_count[MAX_OPENED_FILE];
struct file_obj          *d_file_list[MAX_OPENED_FILE];
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
free_map(int idx);

static void
register_handler(int sig, struct sigaction *old_handler);

static void
print_summary(void);

static _Atomic uint32_t  num_fd_dup2ed;
struct fd_dup2           fd_dup2_list[MAX_FD_DUP2ED];

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

/* Discover fuse mount points from env D_IL_MOUNT_POINT.
 * Return 0 for success. A non-zero value means something wrong in setting
 * and the caller will call abort() to terminate current application.
 */
static int
discover_daos_mount_with_env(void)
{
	int    idx, rc;
	char  *fs_root   = NULL;
	char  *pool      = NULL;
	char  *container = NULL;
	size_t len_fs_root, len_pool, len_container;

	/* Add the mount if env D_IL_MOUNT_POINT is set. */
	rc = d_agetenv_str(&fs_root, "D_IL_MOUNT_POINT");
	if (fs_root == NULL)
		/* env D_IL_MOUNT_POINT is undefined, return success (0) */
		D_GOTO(out, rc = 0);

	if (num_dfs >= MAX_DAOS_MT) {
		D_FATAL("dfs_list[] is full already. Need to increase MAX_DAOS_MT.\n");
		D_GOTO(out, rc = EBUSY);
	}

	if (access(fs_root, R_OK)) {
		D_FATAL("no read permission for %s: %d (%s)\n", fs_root, errno,	strerror(errno));
		D_GOTO(out, rc = EACCES);
	}

	/* check whether fs_root exists in dfs_list[] already. "idx >= 0" means exists. */
	idx = query_dfs_mount(fs_root);
	if (idx >= 0)
		D_GOTO(out, rc = 0);

	/* Not found in existing list, then append this new mount point. */
	len_fs_root = strnlen(fs_root, DFS_MAX_PATH);
	if (len_fs_root >= DFS_MAX_PATH) {
		D_FATAL("D_IL_MOUNT_POINT is too long.\n");
		D_GOTO(out, rc = ENAMETOOLONG);
	}

	d_agetenv_str(&pool, "D_IL_POOL");
	if (pool == NULL) {
		D_FATAL("D_IL_POOL is not set.\n");
		D_GOTO(out, rc = EINVAL);
	}

	len_pool = strnlen(pool, DAOS_PROP_MAX_LABEL_BUF_LEN);
	if (len_pool >= DAOS_PROP_MAX_LABEL_BUF_LEN) {
		D_FATAL("D_IL_POOL is too long.\n");
		D_GOTO(out, rc = ENAMETOOLONG);
	}

	rc = d_agetenv_str(&container, "D_IL_CONTAINER");
	if (container == NULL) {
		D_FATAL("D_IL_CONTAINER is not set.\n");
		D_GOTO(out, rc = EINVAL);
	}

	len_container = strnlen(container, DAOS_PROP_MAX_LABEL_BUF_LEN);
	if (len_container >= DAOS_PROP_MAX_LABEL_BUF_LEN) {
		D_FATAL("D_IL_CONTAINER is too long.\n");
		D_GOTO(out, rc = ENAMETOOLONG);
	}

	D_STRNDUP(dfs_list[num_dfs].fs_root, fs_root, len_fs_root);
	if (dfs_list[num_dfs].fs_root == NULL)
		D_GOTO(out, rc = ENOMEM);

	D_STRNDUP(dfs_list[num_dfs].pool, pool, len_pool);
	if (dfs_list[num_dfs].pool == NULL)
		D_GOTO(free_fs_root, rc = ENOMEM);

	D_STRNDUP(dfs_list[num_dfs].cont, container, len_container);
	if (dfs_list[num_dfs].cont == NULL)
		D_GOTO(free_pool, rc = ENOMEM);

	dfs_list[num_dfs].dcache       = NULL;
	dfs_list[num_dfs].len_fs_root  = (int)len_fs_root;
	atomic_init(&dfs_list[num_dfs].inited, 0);
	num_dfs++;
	D_GOTO(out, rc = 0);

free_pool:
	D_FREE(dfs_list[num_dfs].pool);
free_fs_root:
	D_FREE(dfs_list[num_dfs].fs_root);
out:
	d_freeenv_str(&container);
	d_freeenv_str(&pool);
	d_freeenv_str(&fs_root);
	return rc;
}

#define MNT_TYPE_FUSE	"fuse.daos"
/* Discover fuse mount points from /proc/self/mounts. Return 0 for success. Otherwise
 * return Linux errno and disable function interception.
 */
static int
discover_dfuse_mounts(void)
{
	int            rc = 0;
	FILE          *fp;
	struct mntent *fs_entry;
	struct dfs_mt *pt_dfs_mt;

	num_dfs = 0;

	fp = setmntent("/proc/self/mounts", "r");

	if (fp == NULL) {
		rc = errno;
		DS_ERROR(errno, "failed to open /proc/self/mounts");
		return rc;
	}

	while ((fs_entry = getmntent(fp)) != NULL) {
		if (num_dfs >= MAX_DAOS_MT) {
			D_FATAL("dfs_list[] is full. Need to increase MAX_DAOS_MT.\n");
			abort();
		}
		pt_dfs_mt = &dfs_list[num_dfs];
		if (memcmp(fs_entry->mnt_type, STR_AND_SIZE(MNT_TYPE_FUSE)) == 0) {
			pt_dfs_mt->dcache      = NULL;
			pt_dfs_mt->len_fs_root = strnlen(fs_entry->mnt_dir, DFS_MAX_PATH);
			if (pt_dfs_mt->len_fs_root >= DFS_MAX_PATH) {
				D_DEBUG(DB_ANY, "mnt_dir[] is too long. Skip this entry.\n");
				D_GOTO(out, rc = ENAMETOOLONG);
			}
			if (access(fs_entry->mnt_dir, R_OK)) {
				D_DEBUG(DB_ANY, "no read permission for %s: %d (%s)\n",
					fs_entry->mnt_dir, errno, strerror(errno));
				continue;
			}

			atomic_init(&pt_dfs_mt->inited, 0);
			pt_dfs_mt->pool         = NULL;
			pt_dfs_mt->cont         = NULL;
			D_STRNDUP(pt_dfs_mt->fs_root, fs_entry->mnt_dir, pt_dfs_mt->len_fs_root);
			if (pt_dfs_mt->fs_root == NULL)
				D_GOTO(out, rc = ENOMEM);
			num_dfs++;
		}
	}

out:
	endmntent(fp);
	return rc;
}

static int
fetch_dfs_obj_handle(int fd, struct dfs_mt *dfs_mt, dfs_obj_t **obj)
{
	int                    cmd, rc;
	d_iov_t                iov	 = {};
	char                  *buff_obj = NULL;
	struct dfuse_hsd_reply hsd_reply;
	struct dfuse_il_reply  il_reply;

	rc = ioctl(fd, DFUSE_IOCTL_IL, &il_reply);
	if (rc != 0) {
		rc = errno;
		if (rc != ENOTTY)
			DS_WARN(rc, "ioctl call on %d failed", fd);
		D_GOTO(err, rc);
	}

	if (il_reply.fir_version != DFUSE_IOCTL_VERSION) {
		D_WARN("ioctl version mismatch (fd=%d): expected %d got %d\n", fd,
		       DFUSE_IOCTL_VERSION, il_reply.fir_version);
		D_GOTO(err, rc);
	}

	rc = ioctl(fd, DFUSE_IOCTL_IL_DSIZE, &hsd_reply);
	if (rc != 0) {
		rc = errno;
		D_GOTO(err, rc);
	}
	/* to query dfs object for files */
	D_ALLOC(buff_obj, hsd_reply.fsr_dobj_size);
	if (buff_obj == NULL)
		return ENOMEM;

	iov.iov_buf = buff_obj;
	cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_DOOH, hsd_reply.fsr_dobj_size);
	rc  = ioctl(fd, cmd, iov.iov_buf);
	if (rc != 0) {
		rc = errno;
		DS_WARN(rc, "ioctl call on %d failed", fd);
		D_GOTO(err, rc);
	}

	iov.iov_buf_len = hsd_reply.fsr_dobj_size;
	iov.iov_len     = iov.iov_buf_len;

	rc = dfs_obj_global2local(dfs_mt->dfs, 0, iov, obj);
	if (rc) {
		DS_WARN(rc, "failed to use dfs object handle");
		D_GOTO(err, rc);
	}

	D_FREE(buff_obj);
	return 0;

err:
	D_FREE(buff_obj);
	return rc;
}

#define NAME_LEN 128

static int
retrieve_handles_from_fuse(int idx)
{
	struct dfuse_hs_reply hs_reply;
	int                   fd, cmd, rc, errno_saved, read_size;
	d_iov_t               iov = {};
	char                  *buff = NULL;
	size_t                buff_size;

	fd = libc_open(dfs_list[idx].fs_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (fd < 0) {
		errno_saved = errno;
		D_DEBUG(DB_ANY, "failed to open dir %s: %d (%s)\n", dfs_list[idx].fs_root,
			errno_saved, strerror(errno_saved));
		errno = errno_saved;
		return (-1);
	}

	cmd = _IOR(DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_SIZE, struct dfuse_hs_reply);
	rc  = ioctl(fd, cmd, &hs_reply);
	if (rc != 0) {
		errno_saved = errno;
		D_DEBUG(DB_ANY, "failed to query size info from dfuse with ioctl(): %d (%s)\n",
			errno_saved, strerror(errno_saved));
		goto err;
	}

	/* To determine the size of buffer we need to accommodate the data from fuse */
	buff_size = max(hs_reply.fsr_pool_size, hs_reply.fsr_cont_size);
	buff_size = max(buff_size, hs_reply.fsr_dfs_size);
	D_ALLOC(buff, buff_size);
	if (buff == NULL) {
		errno_saved = ENOMEM;
		goto err;
	}

	iov.iov_buf = buff;

	/* Max size of ioctl is 16k */
	if (hs_reply.fsr_pool_size < (16 * 1024)) {
		cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_POH,
			   hs_reply.fsr_pool_size);
		rc  = ioctl(fd, cmd, iov.iov_buf);
		if (rc != 0) {
			errno_saved = errno;
			D_DEBUG(DB_ANY,
				"failed to query pool handle from dfuse with ioctl(): %d (%s)\n",
				errno_saved, strerror(errno_saved));
			goto err;
		}
	} else {
		char fname[NAME_LEN];
		FILE *tmp_file;

		cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_PFILE, NAME_LEN);
		errno = 0;
		rc = ioctl(fd, cmd, fname);
		if (rc != 0) {
			errno_saved = errno;
			D_DEBUG(DB_ANY, "ioctl call on %d failed: %d (%s)\n", fd, errno_saved,
				strerror(errno_saved));
			goto err;
		}
		errno = 0;
		tmp_file = fopen(fname, "rb");
		if (tmp_file == NULL) {
			errno_saved = errno;
			D_DEBUG(DB_ANY, "fopen(%s) failed: %d (%s)\n", fname, errno_saved,
				strerror(errno_saved));
			goto err;
		}
		read_size = fread(iov.iov_buf, 1, hs_reply.fsr_pool_size, tmp_file);
		fclose(tmp_file);
		unlink(fname);
		if (read_size != hs_reply.fsr_pool_size) {
			errno_saved = EAGAIN;
			D_DEBUG(DB_ANY, "fread expected %zu bytes, read %d bytes : %d (%s)\n",
				hs_reply.fsr_pool_size, read_size, errno_saved,
				strerror(errno_saved));
			goto err;
		}
	}

	iov.iov_buf_len = hs_reply.fsr_pool_size;
	iov.iov_len     = iov.iov_buf_len;
	rc              = daos_pool_global2local(iov, &dfs_list[idx].poh);
	if (rc != 0) {
		errno_saved = daos_der2errno(rc);
		D_DEBUG(DB_ANY,
			"failed to create pool handle in daos_pool_global2local(): %d (%s)\n",
			errno_saved, strerror(errno_saved));
		goto err;
	}

	cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_COH, hs_reply.fsr_cont_size);
	rc  = ioctl(fd, cmd, iov.iov_buf);
	if (rc != 0) {
		errno_saved = errno;
		D_DEBUG(DB_ANY,
			"failed to query container handle from dfuse with ioctl(): %d (%s)\n",
			errno_saved, strerror(errno_saved));
		goto err;
	}
	iov.iov_buf_len = hs_reply.fsr_cont_size;
	iov.iov_len     = iov.iov_buf_len;
	rc              = daos_cont_global2local(dfs_list[idx].poh, iov, &dfs_list[idx].coh);
	if (rc != 0) {
		errno_saved = daos_der2errno(rc);
		D_DEBUG(DB_ANY,
			"failed to create container handle in daos_pool_global2local(): %d (%s)\n",
			errno_saved, strerror(errno_saved));
		goto err;
	}

	cmd = _IOC(_IOC_READ, DFUSE_IOCTL_TYPE, DFUSE_IOCTL_REPLY_DOH, hs_reply.fsr_dfs_size);
	rc  = ioctl(fd, cmd, iov.iov_buf);
	if (rc != 0) {
		errno_saved = errno;
		D_DEBUG(DB_ANY, "failed to query DFS handle from dfuse with ioctl(): %d (%s)\n",
			errno_saved, strerror(errno_saved));
		goto err;
	}
	iov.iov_buf_len = hs_reply.fsr_dfs_size;
	iov.iov_len     = iov.iov_buf_len;
	rc = dfs_global2local(dfs_list[idx].poh, dfs_list[idx].coh, 0, iov, &dfs_list[idx].dfs);
	if (rc != 0) {
		errno_saved = daos_der2errno(rc);
		D_DEBUG(DB_ANY,
			"failed to create DFS handle in daos_pool_global2local(): %d (%s)\n",
			errno_saved, strerror(errno_saved));
		goto err;
	}

	rc = dcache_create(dfs_list[idx].dfs, dcache_size_bits, dcache_rec_timeout,
			   dcache_gc_period, dcache_gc_reclaim_max, &dfs_list[idx].dcache);
	if (rc != 0) {
		errno_saved = daos_der2errno(rc);
		D_DEBUG(DB_ANY,
			"failed to initialize DFS directory cache in "
			"daos_pool_global2local(): %d (%s)\n",
			errno_saved, strerror(errno_saved));
		goto err;
	}

	D_FREE(buff);

	return 0;

err:
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

	if (strncasecmp(path, "daos://", 7) != 0)
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

static void
child_hdlr(void)
{
	int rc;

	/* daos is not initialized yet */
	if (atomic_load_relaxed(&d_daos_inited) == false)
		return;

	rc = daos_reinit();
	if (rc)
		DL_WARN(rc, "daos_reinit() failed in child process");
	td_eqh = main_eqh = DAOS_HDL_INVAL;
	context_reset = true;
	d_eq_count    = 0;
}

/* only free the reserved low fds when application exits or encounters error */
static void
free_reserved_low_fd(void)
{
	int i;

	for (i = 0; i < low_fd_count; i++)
		libc_close(low_fd_list[i]);
	low_fd_count = 0;
}

/* some applications especially bash scripts use specific low fds directly.
 * It would be safer to avoid using such low fds (fd < DAOS_MIN_FD) in daos.
 * We consume such low fds before any daos calls and close them only when
 * application exits or encounters error.
 */

static int
consume_low_fd(void)
{
	int rc = 0;
	int fd_dup;

	if (atomic_load_relaxed(&d_daos_inited) == true)
		return 0;

	D_MUTEX_LOCK(&lock_reserve_fd);
	low_fd_count              = 0;
	low_fd_list[low_fd_count] = libc_open("/", O_PATH | O_DIRECTORY);
	while (1) {
		if (low_fd_list[low_fd_count] < 0) {
			DS_ERROR(errno, "failed to reserve a low fd");
			goto err;
		} else if (low_fd_list[low_fd_count] >= DAOS_MIN_FD) {
			if (low_fd_count > 0)
				libc_close(low_fd_list[low_fd_count]);
			/* low_fd_list[0] will be used and closed later if low_fd_count is 0. */
			break;
		} else {
			low_fd_count++;
		}
		low_fd_list[low_fd_count] = libc_open("/", O_RDONLY);
	}

	/* reserve fd 255 too if unused. 255 is commonly used by bash. */
	fd_dup = fcntl(low_fd_list[0], F_DUPFD, 255);
	if (fd_dup == -1) {
		DS_ERROR(errno, "fcntl() failed");
		goto err;
	}
	/* If fd 255 is used, lowest available fd is assigned to fd_dup. */
	if (fd_dup >= 0 && fd_dup != 255)
		libc_close(fd_dup);
	if (fd_dup == 255)
		fd_255_reserved = true;

	/* reserve fd DAOS_DUMMY_FD which will be used later by dup2(). */
	fd_dummy = fcntl(low_fd_list[0], F_DUPFD, DAOS_DUMMY_FD);
	if (fd_dummy == -1) {
		DS_ERROR(errno, "fcntl() failed");
		goto err;
	}

	if (low_fd_count == 0 && low_fd_list[low_fd_count] >= DAOS_MIN_FD)
		libc_close(low_fd_list[0]);

	D_MUTEX_UNLOCK(&lock_reserve_fd);
	return 0;

err:
	rc = errno;
	free_reserved_low_fd();
	D_MUTEX_UNLOCK(&lock_reserve_fd);

	return rc;
}

int
MPI_Init(int *argc, char ***argv)
{
	int rc;

	if (next_mpi_init == NULL) {
		next_mpi_init = dlsym(RTLD_NEXT, "MPI_Init");
		D_ASSERT(next_mpi_init != NULL);
	}

	atomic_fetch_add_relaxed(&mpi_init_count, 1);
	rc = next_mpi_init(argc, argv);
	atomic_fetch_add_relaxed(&mpi_init_count, -1);
	return rc;
}

/** determine whether a path (both relative and absolute) is on DAOS or not. If yes,
 *  returns parent object, item name, full path of parent dir, full absolute path, and
 *  the pointer to struct dfs_mt.
 *  Dynamically allocate 2 * DFS_MAX_PATH for *parent_dir and *full_path in one malloc().
 */
static int
query_path(const char *szInput, int *is_target_path, struct dcache_rec **parent, char *item_name,
	   char **parent_dir, char **full_path, struct dfs_mt **dfs_mt)
{
	int    pos, len;
	bool   with_daos_prefix;
	char   pool[DAOS_PROP_MAX_LABEL_BUF_LEN + 1];
	char   cont[DAOS_PROP_MAX_LABEL_BUF_LEN + 1];
	char  *rel_path = NULL;
	int    idx_dfs, rc;
	char  *pt_end = NULL;
	char  *full_path_parse = NULL;

	*parent_dir = NULL;
	*parent     = NULL;

	/* determine whether the path starts with daos://pool/cont/. Need more work. */
	with_daos_prefix = is_path_start_with_daos(szInput, pool, cont, &rel_path);
	if (with_daos_prefix) {
		/* TO DO. Need more work!!! query_dfs_pool_cont(pool, cont)*/
		*is_target_path = 0;
		return 0;
	}

	/* handle special cases. Needed to work with git. */
	if ((memcmp(szInput, STR_AND_SIZE_M1("http://")) == 0) ||
	    (memcmp(szInput, STR_AND_SIZE_M1("https://")) == 0) ||
	    (memcmp(szInput, STR_AND_SIZE_M1("git://")) == 0)) {
		*is_target_path = 0;
		return 0;
	}

	*full_path  = NULL;
	*parent_dir = calloc(2, DFS_MAX_PATH);
	if (*parent_dir == NULL)
		goto out_oom;
	/* allocate both for *parent_dir and *full_path above */
	*full_path = *parent_dir + DFS_MAX_PATH;

	full_path_parse = malloc(DFS_MAX_PATH + 4);
	if (full_path_parse == NULL)
		goto out_oom;

	if (strncmp(szInput, ".", 2) == 0) {
		/* special case for current work directory */
		pt_end = stpncpy(full_path_parse, cur_dir, DFS_MAX_PATH + 1);
		len = (int)(pt_end - full_path_parse);
		if (len >= DFS_MAX_PATH) {
			D_DEBUG(DB_ANY, "full_path_parse[] is not large enough: %d (%s)\n",
				ENAMETOOLONG, strerror(ENAMETOOLONG));
			D_GOTO(out_err, rc = ENAMETOOLONG);
		}
	} else if (szInput[0] == '/') {
		/* absolute path */
		pt_end = stpncpy(full_path_parse, szInput, DFS_MAX_PATH);
		len = (int)(pt_end - full_path_parse);
		if (len >= DFS_MAX_PATH) {
			D_DEBUG(DB_ANY, "full_path_parse[] is not large enough: %d (%s)\n",
				ENAMETOOLONG, strerror(ENAMETOOLONG));
			D_GOTO(out_err, rc = ENAMETOOLONG);
		}
	} else {
		/* relative path */
		len = snprintf(full_path_parse, DFS_MAX_PATH, "%s/%s", cur_dir, szInput);
		if (len >= DFS_MAX_PATH) {
			D_DEBUG(DB_ANY, "The length of path is too long: %d (%s)\n", ENAMETOOLONG,
				strerror(ENAMETOOLONG));
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
		if (atomic_load_relaxed(&d_daos_inited) == false) {
			uint64_t status_old = DAOS_INIT_NOT_RUNNING;
			bool     rc_cmp_swap;

			/* Check whether MPI_Init() is running. If yes, pass to the original
			 * libc functions. Avoid possible zeInit reentrancy/nested call.
			 */

			if (atomic_load_relaxed(&mpi_init_count) > 0) {
				*is_target_path = 0;
				goto out_normal;
			}

			/* daos_init() is expensive to call. We call it only when necessary. */

			/* Check whether daos_init() is running. If yes, pass to the original
			 * libc functions. Avoid possible daos_init reentrancy/nested call.
			 */

			if (!atomic_compare_exchange_weak(&daos_initing, &status_old,
			    DAOS_INIT_RUNNING)) {
				*is_target_path = 0;
				goto out_normal;
			}

			rc = consume_low_fd();
			if (rc) {
				DS_ERROR(rc, "consume_low_fd() failed");
				*is_target_path = 0;
				goto out_normal;
			}

			rc = daos_init();
			/* This message is used by ftest "il_whitelist.py" */
			if (rc) {
				DL_ERROR(rc, "daos_init() failed");
				*is_target_path = 0;
				goto out_normal;
			}

			if (d_eq_count_max) {
				D_MUTEX_LOCK(&lock_eqh);
				if (daos_handle_is_inval(main_eqh)) {
					rc = daos_eq_create(&td_eqh);
					if (rc)
						DL_WARN(rc, "daos_eq_create() failed");
					main_eqh = td_eqh;
				}
				D_MUTEX_UNLOCK(&lock_eqh);
			}

			atomic_store_relaxed(&d_daos_inited, true);
			atomic_fetch_add_relaxed(&daos_init_cnt, 1);

			status_old = DAOS_INIT_RUNNING;
			rc_cmp_swap = atomic_compare_exchange_weak(&daos_initing, &status_old,
				DAOS_INIT_NOT_RUNNING);
			D_ASSERT(rc_cmp_swap);
		}

		/* dfs info can be set up after daos has been initialized. */
		*dfs_mt = &dfs_list[idx_dfs];
		/* trying to avoid lock as much as possible. Need to make sure whether
		 * it is safe enough or not!
		 */
		if (atomic_load_relaxed(&((*dfs_mt)->inited)) == 0) {
			D_MUTEX_LOCK(&lock_dfs);
			if (atomic_load_relaxed(&((*dfs_mt)->inited)) == 0) {
				if ((*dfs_mt)->pool == NULL) {
					/* retrieve dfs, pool and container handles from dfuse */
					rc = retrieve_handles_from_fuse(idx_dfs);
					if (rc) {
						/* Let the original functions to handle it. */
						*is_target_path = 0;
						D_MUTEX_UNLOCK(&lock_dfs);
						goto out_normal;
					}
				} else {
					rc = init_dfs(idx_dfs);
					if (rc) {
						/* Let the original functions to handle it. */
						*is_target_path = 0;
						D_MUTEX_UNLOCK(&lock_dfs);
						goto out_normal;
					}
				}
				atomic_store_relaxed(&((*dfs_mt)->inited), 1);
			}
			D_MUTEX_UNLOCK(&lock_dfs);
		}
		*is_target_path = 1;

		/* root dir */
		if (full_path_parse[(*dfs_mt)->len_fs_root] == 0) {
			item_name[0]  = '/';
			item_name[1]  = '\0';
			strncpy(*full_path, "/", 2);
		} else {
			size_t len_item_name;
			size_t parent_dir_len;

			/* full_path holds the full path inside dfs container */
			strncpy(*full_path, full_path_parse + (*dfs_mt)->len_fs_root, len + 1);
			for (pos = len - 1; pos >= (*dfs_mt)->len_fs_root; pos--) {
				if (full_path_parse[pos] == '/')
					break;
			}

			len_item_name = strnlen(full_path_parse + pos + 1, len);
			if (len_item_name >= DFS_MAX_NAME) {
				D_DEBUG(DB_ANY, "item_name[] is not large enough: %d (%s)\n",
					ENAMETOOLONG, strerror(ENAMETOOLONG));
				D_GOTO(out_err, rc = ENAMETOOLONG);
			}
			strncpy(item_name, full_path_parse + pos + 1, len_item_name + 1);

			/* the item under root directory */
			if (pos == (*dfs_mt)->len_fs_root) {
				(*parent_dir)[0] = '/';
				parent_dir_len   = 1;
			} else {
				char *parent_path;

				full_path_parse[pos] = '\0';
				parent_path          = full_path_parse + (*dfs_mt)->len_fs_root;
				/* parent_dir_len is length of the string, without termination */
				parent_dir_len = pos - (*dfs_mt)->len_fs_root;
				/* Need to look up the parent directory */
				strncpy(*parent_dir, parent_path, parent_dir_len);
			}
			/* look up the dfs object from hash table for the parent dir */
			rc = dcache_find_insert((*dfs_mt)->dcache, *parent_dir, parent_dir_len,
						parent);
			/* parent dir does not exist or something wrong */
			if (rc)
				D_GOTO(out_err, rc = daos_der2errno(rc));
		}
	} else {
		strncpy(*full_path, full_path_parse, len + 1);
		*is_target_path = 0;
		item_name[0]    = '\0';
	}

out_normal:
	FREE(full_path_parse);
	return 0;

out_err:
	FREE(full_path_parse);
	drec_del_at((*dfs_mt)->dcache, *parent);
	*parent = NULL;
	FREE(*parent_dir);
	return rc;

out_oom:
	FREE(full_path_parse);
	drec_del_at((*dfs_mt)->dcache, *parent);
	*parent = NULL;
	FREE(*parent_dir);
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
		D_DEBUG(DB_ANY, "wrong path %s: %d (%s)\n", path, EINVAL, strerror(EINVAL));
		return EINVAL;
	}

	while (p_Offset_2Dots != NULL) {
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
	p_Offset_2Dots = strstr(path, "/..");
	if (p_Offset_2Dots && p_Offset_2Dots[3] == '\0')
		/* /.. at the very end of the path. */
		goto again;

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
	while ((p_Offset_Dots != NULL)) {
		p_Offset_Dots[0] = 0;
		p_Offset_Dots[1] = 0;
		p_Offset_Dots    = strstr(p_Offset_Dots + 2, "/./");
		if (p_Offset_Dots == NULL)
			break;
	}

	/* replace "//" with "/" */
	p_Offset_Slash = strstr(path, "//");
	while (p_Offset_Slash != NULL) {
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

static int
init_fd_list(void)
{
	int rc;

	rc = D_MUTEX_INIT(&lock_fd, NULL);
	if (rc)
		return 1;
	rc = D_MUTEX_INIT(&lock_dirfd, NULL);
	if (rc)
		return 1;
	rc = D_MUTEX_INIT(&lock_mmap, NULL);
	if (rc)
		return 1;
	rc = D_RWLOCK_INIT(&lock_fd_dup2ed, NULL);
	if (rc)
		return 1;

	/* fatal error above: failure to create mutexes. */

	memset(d_file_list, 0, sizeof(struct file_obj *) * MAX_OPENED_FILE);
	memset(dir_list, 0, sizeof(struct dir_obj *) * MAX_OPENED_DIR);
	memset(mmap_list, 0, sizeof(struct mmap_obj) * MAX_MMAP_BLOCK);

	next_free_fd    = 0;
	last_fd         = -1;
	next_free_dirfd = 0;
	last_dirfd      = -1;
	next_free_map   = 0;
	last_map        = -1;
	num_fd = num_dirfd = num_map = 0;

	/* success without error */
	return 0;
}

static int
find_next_available_fd(struct file_obj *obj, int *new_fd)
{
	bool	allocated = false;
	int	i, idx = -1;
	struct file_obj *new_obj = NULL;

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

	D_MUTEX_LOCK(&lock_fd);
	if (next_free_fd < 0) {
		D_MUTEX_UNLOCK(&lock_fd);
		if (allocated)
			D_FREE(new_obj);
		DS_ERROR(EMFILE, "failed to allocate fd");
		return EMFILE;
	}
	idx = next_free_fd;
	if (idx >= 0) {
		new_obj->ref_count++;
		d_file_list[idx] = new_obj;
	}
	dup_ref_count[idx] = 0;
	if (next_free_fd > last_fd)
		last_fd = next_free_fd;
	next_free_fd = -1;

	for (i = idx + 1; i < MAX_OPENED_FILE; i++) {
		if (d_file_list[i] == NULL) {
			/* available, then update next_free_fd */
			next_free_fd = i;
			break;
		}
	}

	num_fd++;
	D_MUTEX_UNLOCK(&lock_fd);

	*new_fd = idx;
	return 0;
}

static void
inc_dup_ref_count(int fd)
{
	D_MUTEX_LOCK(&lock_fd);
	dup_ref_count[fd - FD_FILE_BASE]++;
	d_file_list[fd - FD_FILE_BASE]->ref_count++;
	D_MUTEX_UNLOCK(&lock_fd);
}

static void
dec_dup_ref_count(int fd)
{
	D_MUTEX_LOCK(&lock_fd);
	dup_ref_count[fd - FD_FILE_BASE]--;
	d_file_list[fd - FD_FILE_BASE]->ref_count--;
	D_MUTEX_UNLOCK(&lock_fd);
}

static int
find_next_available_dirfd(struct dir_obj *obj, int *new_dir_fd)
{
	bool	allocated = false;
	int	i, idx	= -1;
	struct dir_obj *new_obj;

	if (obj == NULL) {
		D_ALLOC_PTR(new_obj);
		if (new_obj == NULL)
			return ENOMEM;
		new_obj->dir       = NULL;
		new_obj->ref_count = 0;
		allocated          = true;
	} else {
		new_obj            = obj;
	}

	D_MUTEX_LOCK(&lock_dirfd);
	if (next_free_dirfd < 0) {
		D_MUTEX_UNLOCK(&lock_dirfd);
		if (allocated)
			D_FREE(new_obj);
		DS_ERROR(EMFILE, "Failed to allocate dirfd");
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
	D_MUTEX_UNLOCK(&lock_dirfd);

	*new_dir_fd = idx;
	return 0;
}

static int
find_next_available_map(int *idx)
{
	int i;

	*idx = -1;
	D_MUTEX_LOCK(&lock_mmap);
	if (next_free_map < 0) {
		D_MUTEX_UNLOCK(&lock_mmap);
		DS_ERROR(EMFILE, "Failed to allocate space from mmap_list[]");
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
	D_MUTEX_UNLOCK(&lock_mmap);

	return 0;
}

/* May need to support duplicated fd as duplicated dirfd too. */
static void
free_fd(int idx, bool closing_dup_fd)
{
	int              i, rc;
	struct file_obj *saved_obj = NULL;

	D_MUTEX_LOCK(&lock_fd);
	if (d_file_list[idx]->idx_mmap >= 0 && d_file_list[idx]->idx_mmap < MAX_MMAP_BLOCK) {
		/* d_file_list[idx].idx_mmap >= MAX_MMAP_BLOCK means fd needs closed after munmap()
		 */
		d_file_list[idx]->idx_mmap += MAX_MMAP_BLOCK;
		D_MUTEX_UNLOCK(&lock_fd);
		return;
	}

	if (closing_dup_fd)
		dup_ref_count[idx]--;
	d_file_list[idx]->ref_count--;
	if (d_file_list[idx]->ref_count == 0)
		saved_obj = d_file_list[idx];
	if (dup_ref_count[idx] > 0) {
		D_MUTEX_UNLOCK(&lock_fd);
		return;
	}
	d_file_list[idx] = NULL;

	if (idx < next_free_fd)
		next_free_fd = idx;

	if (idx == last_fd) {
		for (i = idx - 1; i >= 0; i--) {
			if (d_file_list[i]) {
				last_fd = i;
				break;
			}
		}
	}

	num_fd--;
	D_MUTEX_UNLOCK(&lock_fd);

	if (saved_obj) {
		/* Decrement the refcounter get in open_common() */
		drec_decref(saved_obj->dfs_mt->dcache, saved_obj->parent);
		rc = dfs_release(saved_obj->file);
		if (rc)
			DS_ERROR(rc, "dfs_release() failed");
		/** This memset() is not necessary. It is left here intended. In case of duplicated
		 *  fd exists, multiple fd could point to same struct file_obj. struct file_obj is
		 *  supposed to be freed only when reference count reaches zero. With zeroing out
		 *  the memory, we could observe issues immediately if something is wrong (e.g.,
		 *  one fd is still points to this struct). We can remove this later after we
		 *  make sure the code about adding and decreasing the reference to the struct
		 *  working as expected.
		 */
		D_FREE(saved_obj->path);
		memset(saved_obj, 0, sizeof(struct file_obj));
		D_FREE(saved_obj);
	}
}

static void
free_dirfd(int idx)
{
	int             i, rc;
	struct dir_obj *saved_obj = NULL;

	D_MUTEX_LOCK(&lock_dirfd);

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
	D_MUTEX_UNLOCK(&lock_dirfd);

	if (saved_obj) {
		/* free memory for path and ents. */
		D_FREE(saved_obj->path);
		D_FREE(saved_obj->ents);
		rc = dfs_release(saved_obj->dir);
		if (rc)
			DS_ERROR(rc, "dfs_release() failed");
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
	int i;

	D_MUTEX_LOCK(&lock_mmap);
	mmap_list[idx].addr = NULL;
	/* Need to call free_fd(). */
	if (d_file_list[mmap_list[idx].fd - FD_FILE_BASE]->idx_mmap >= MAX_MMAP_BLOCK)
		free_fd(mmap_list[idx].fd - FD_FILE_BASE, false);
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
	D_MUTEX_UNLOCK(&lock_mmap);
}

int
d_get_fd_redirected(int fd)
{
	int i, rc, fd_ret = fd;

	if (atomic_load_relaxed(&d_daos_inited) == false)
		return fd;

	if (fd >= FD_FILE_BASE)
		return fd;

	if (d_compatible_mode) {
		d_list_t     *rlink;
		int           fd_kernel = fd;
		struct ht_fd *fd_ht_obj;

		rlink = d_hash_rec_find(fd_hash, &fd_kernel, sizeof(int));
		if (rlink != NULL) {
			fd_ht_obj = fd_obj(rlink);
			return fd_ht_obj->fake_fd;
		}
	}

	if (atomic_load_relaxed(&num_fd_dup2ed) > 0) {
		rc = pthread_rwlock_rdlock(&lock_fd_dup2ed);
		if (rc != 0) {
			DS_ERROR(rc, "pthread_rwlock_rdlock() failed");
			return fd_ret;
		}
		for (i = 0; i < MAX_FD_DUP2ED; i++) {
			if (fd_dup2_list[i].fd_src == fd) {
				fd_ret = fd_dup2_list[i].fd_dest;
				break;
			}
		}
		rc = pthread_rwlock_unlock(&lock_fd_dup2ed);
		if (rc != 0) {
			DS_ERROR(rc, "pthread_rwlock_unlock() failed");
			return fd_ret;
		}
	}

	return fd_ret;
}

/* This fd is a fd from kernel and it is associated with a fake fd.
 * Need to 1) close(fd) 2) remove the entry in fd_dup2_list[] 3) decrease
 * the dup reference count of the fake fd.
 */

static int
close_dup_fd(int (*next_close)(int fd), int fd, bool close_fd)
{
	int i, rc, idx_dup = -1, fd_dest = -1;

	if (close_fd) {
		/* close the fd from kernel */
		assert(fd < FD_FILE_BASE);
		rc = next_close(fd);
		if (rc != 0)
			return (-1);
	}

	/* remove the fd_dup entry */
	if (atomic_load_relaxed(&num_fd_dup2ed) > 0) {
		D_RWLOCK_WRLOCK(&lock_fd_dup2ed);
		for (i = 0; i < MAX_FD_DUP2ED; i++) {
			if (fd_dup2_list[i].fd_src == fd) {
				idx_dup = i;
				fd_dest = fd_dup2_list[i].fd_dest;
				/* clear the value to free */
				fd_dup2_list[i].fd_src  = -1;
				fd_dup2_list[i].fd_dest = -1;
				atomic_fetch_add_relaxed(&num_fd_dup2ed, -1);
				break;
			}
		}
		D_RWLOCK_UNLOCK(&lock_fd_dup2ed);
	}

	if (idx_dup < 0) {
		D_DEBUG(DB_ANY, "failed to find fd %d in fd_dup2_list[]: %d (%s)\n", fd, EINVAL,
			strerror(EINVAL));
		errno = EINVAL;
		return (-1);
	}
	free_fd(fd_dest - FD_FILE_BASE, true);

	return 0;
}

static void
init_fd_dup2_list(void)
{
	int i;

	D_RWLOCK_WRLOCK(&lock_fd_dup2ed);
	for (i = 0; i < MAX_FD_DUP2ED; i++) {
		fd_dup2_list[i].fd_src  = -1;
		fd_dup2_list[i].fd_dest = -1;
	}
	D_RWLOCK_UNLOCK(&lock_fd_dup2ed);
}

static int
allocate_dup2ed_fd(const int fd_src, const int fd_dest)
{
	int i;

	/* increase reference count of the fake fd */
	inc_dup_ref_count(fd_dest);

	/* Not many applications use dup2(). Normally the number of fd duped is small. */
	if (atomic_load_relaxed(&num_fd_dup2ed) < MAX_FD_DUP2ED) {
		D_RWLOCK_WRLOCK(&lock_fd_dup2ed);
		for (i = 0; i < MAX_FD_DUP2ED; i++) {
			if (fd_dup2_list[i].fd_src == -1) {
				fd_dup2_list[i].fd_src  = fd_src;
				fd_dup2_list[i].fd_dest = fd_dest;
				atomic_fetch_add_relaxed(&num_fd_dup2ed, 1);
				D_RWLOCK_UNLOCK(&lock_fd_dup2ed);
				return i;
			}
		}
		D_RWLOCK_UNLOCK(&lock_fd_dup2ed);
	}

	/* decrease dup reference count in error */
	dec_dup_ref_count(fd_dest);
	DS_ERROR(EMFILE, "fd_dup2_list[] is out of space");
	errno = EMFILE;
	return (-1);
}

static int
query_fd_forward_dest(int fd_src)
{
	int i, fd_dest = -1;

	if (atomic_load_relaxed(&num_fd_dup2ed) > 0) {
		D_RWLOCK_RDLOCK(&lock_fd_dup2ed);
		for (i = 0; i < MAX_FD_DUP2ED; i++) {
			if (fd_src == fd_dup2_list[i].fd_src) {
				fd_dest = fd_dup2_list[i].fd_dest;
				D_RWLOCK_UNLOCK(&lock_fd_dup2ed);
				return fd_dest;
			}
		}
		D_RWLOCK_UNLOCK(&lock_fd_dup2ed);
	}
	return -1;
}

static int
allocate_a_fd_from_kernel(void)
{
	/* Just use open() to allocate a fd from kernel. Not going to read the file. */
	return open("/proc/self/maps", O_RDONLY);
}

static void
close_all_duped_fd(void)
{
	int i;

	if (atomic_load_relaxed(&num_fd_dup2ed) == 0)
		return;
	/* Only the main thread will call this function in the destruction phase */
	for (i = 0; i < MAX_FD_DUP2ED; i++) {
		if (fd_dup2_list[i].fd_src >= 0)
			close_dup_fd(libc_close, fd_dup2_list[i].fd_src, true);
	}
	atomic_store_relaxed(&num_fd_dup2ed, 0);
}

static int
check_path_with_dirfd(int dirfd, char **full_path_out, const char *rel_path, int *error)
{
	int len_str, dirfd_directed;

	*error = 0;
	*full_path_out = NULL;
	dirfd_directed = d_get_fd_redirected(dirfd);

	if (dirfd_directed >= FD_DIR_BASE) {
		len_str = asprintf(full_path_out, "%s/%s",
				   dir_list[dirfd_directed - FD_DIR_BASE]->path, rel_path);
		if (len_str >= DFS_MAX_PATH)
			goto out_toolong;
		else if (len_str < 0)
			goto out_oom;
	} else if (dirfd_directed == AT_FDCWD) {
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
	if (*full_path_out) {
		free(*full_path_out);
		*full_path_out = NULL;
	}
	D_DEBUG(DB_ANY, "path is too long: %d (%s)\n", ENAMETOOLONG, strerror(ENAMETOOLONG));
	*error = ENAMETOOLONG;
	return (-1);

out_oom:
	*error = ENOMEM;
	return (-1);

out_readlink:
	*error = errno;
	if (*full_path_out) {
		free(*full_path_out);
		*full_path_out = NULL;
	}
	D_DEBUG(DB_ANY, "readlink() failed: %d (%s)\n", errno, strerror(errno));
	return (-1);
}

static int
open_common(int (*real_open)(const char *pathname, int oflags, ...), const char *caller_name,
	    const char *pathname, int oflags, ...)
{
	unsigned int       mode     = 0664;
	int                two_args = 1, rc, is_target_path, idx_fd, idx_dirfd, fd_kernel;
	dfs_obj_t         *dfs_obj  = NULL;
	dfs_obj_t         *parent_dfs;
	mode_t             mode_query = 0, mode_parent = 0;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

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

	if (!d_hook_enabled)
		goto org_func;

	rc = query_path(pathname, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc == ENOENT)
		D_GOTO(out_error, rc = ENOENT);
	parent_dfs = NULL;
	if (parent != NULL)
		parent_dfs = drec2obj(parent);

	if (!is_target_path)
		goto org_func;
	if (oflags & O_CREAT && (oflags & O_DIRECTORY || oflags & O_PATH)) {
		/* Create a dir is not supported. */
		errno = ENOENT;
		return (-1);
	}

	/* Always rely on fuse for open() to avoid a fake fd */
	if (d_compatible_mode) {
		struct ht_fd *fd_ht_obj = NULL;
		int           fd_fake   = 0;

		if (two_args)
			fd_kernel = real_open(pathname, oflags);
		else
			fd_kernel = real_open(pathname, oflags, mode);
		/* dfuse fails for some reason */
		if (fd_kernel < 0)
			goto out_compatible;

		/* query file object through ioctl() */
		rc = fetch_dfs_obj_handle(fd_kernel, dfs_mt, &dfs_obj);
		if (rc != 0) {
			DS_WARN(rc, "fetch_dfs_obj_handle() failed");
			goto out_compatible;
		}

		/* Need to create a fake fd and associate with fd_kernel */
		atomic_fetch_add_relaxed(&num_open, 1);
		dfs_get_mode(dfs_obj, &mode_query);

		/* regular file */
		if (S_ISREG(mode_query)) {
			rc = find_next_available_fd(NULL, &idx_fd);
			if (rc)
				goto out_compatible_release;
			d_file_list[idx_fd]->dfs_mt    = dfs_mt;
			d_file_list[idx_fd]->file      = dfs_obj;
			d_file_list[idx_fd]->parent    = parent;
			d_file_list[idx_fd]->st_ino    = FAKE_ST_INO(full_path);
			d_file_list[idx_fd]->idx_mmap  = -1;
			d_file_list[idx_fd]->open_flag = oflags;
			d_file_list[idx_fd]->offset    = 0;
			if (strncmp(full_path, "/", 2) == 0)
				D_STRNDUP(d_file_list[idx_fd]->path, dfs_mt->fs_root, DFS_MAX_PATH);
			else
				D_ASPRINTF(d_file_list[idx_fd]->path, "%s%s", dfs_mt->fs_root,
					   full_path);
			if (d_file_list[idx_fd]->path == NULL) {
				free_fd(idx_fd, false);
				/* free_fd() already called drec_decref(). set dfs_mt NULL to avoid
				 * calling drec_decref() again.
				 */
				dfs_mt = NULL;
				goto out_compatible;
			}
			strncpy(d_file_list[idx_fd]->item_name, item_name, DFS_MAX_NAME);
			fd_fake = idx_fd + FD_FILE_BASE;
		} else if (S_ISDIR(mode_query)) {
			/* directory */
			rc = find_next_available_dirfd(NULL, &idx_dirfd);
			if (rc)
				goto out_compatible_release;
			dir_list[idx_dirfd]->dfs_mt   = dfs_mt;
			dir_list[idx_dirfd]->fd       = idx_dirfd + FD_DIR_BASE;
			dir_list[idx_dirfd]->offset   = 0;
			dir_list[idx_dirfd]->dir      = dfs_obj;
			dir_list[idx_dirfd]->num_ents = 0;
			dir_list[idx_dirfd]->st_ino   = FAKE_ST_INO(full_path);
			memset(&dir_list[idx_dirfd]->anchor, 0, sizeof(daos_anchor_t));
			dir_list[idx_dirfd]->path = NULL;
			dir_list[idx_dirfd]->ents = NULL;
			if (strncmp(full_path, "/", 2) == 0)
				full_path[0] = 0;

			/* allocate memory for ents[]. */
			D_ALLOC_ARRAY(dir_list[idx_dirfd]->ents, READ_DIR_BATCH_SIZE);
			if (dir_list[idx_dirfd]->ents == NULL) {
				free_dirfd(idx_dirfd);
				goto out_compatible;
			}
			D_ASPRINTF(dir_list[idx_dirfd]->path, "%s%s", dfs_mt->fs_root, full_path);
			if (dir_list[idx_dirfd]->path == NULL) {
				free_dirfd(idx_dirfd);
				goto out_compatible;
			}
			if (strnlen(dir_list[idx_dirfd]->path, DFS_MAX_PATH) >= DFS_MAX_PATH) {
				DS_WARN(ENAMETOOLONG, "path is longer than DFS_MAX_PATH");
				free_dirfd(idx_dirfd);
				goto out_compatible;
			}
			fd_fake = idx_dirfd + FD_DIR_BASE;
			drec_decref(dfs_mt->dcache, parent);
		} else {
			/* not supposed to be here. If the object is neither a dir or a regular
			 * file, fetch_dfs_obj_handle() should fail already. */
			D_ASSERT(0);
		}

		/* add fd_kernel to hash table */
		D_ALLOC_PTR(fd_ht_obj);
		if (fd_ht_obj == NULL) {
			if (fd_fake >= FD_DIR_BASE)
				free_dirfd(idx_dirfd);
			else
				free_fd(idx_fd, false);
			dfs_mt = NULL;
			goto out_compatible;
		}
		fd_ht_obj->real_fd = fd_kernel;
		fd_ht_obj->fake_fd = fd_fake;
		rc = d_hash_rec_insert(fd_hash, &fd_ht_obj->real_fd, sizeof(int), &fd_ht_obj->entry,
				       false);
		D_ASSERT(rc == 0);
		FREE(parent_dir);
		return fd_kernel;
	}

	if (oflags & __O_TMPFILE) {
		if (!parent && (strncmp(item_name, "/", 2) == 0))
			rc = dfs_access(dfs_mt->dfs, NULL, NULL, X_OK | W_OK);
		else
			rc = dfs_access(dfs_mt->dfs, parent_dfs, item_name, X_OK | W_OK);
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
			rc = dfs_get_mode(parent_dfs, &mode_parent);
			if (rc)
				D_GOTO(out_error, rc);
		}
		if ((S_IXUSR & mode_parent) == 0 || (S_IWUSR & mode_parent) == 0)
			D_GOTO(out_error, rc = EACCES);
	}
	/* file handled by DFS */
	if (oflags & O_CREAT) {
		/* clear the bits for types first. mode in open() only contains permission info. */
		rc = dfs_open(dfs_mt->dfs, parent_dfs, item_name, (mode & (~S_IFMT)) | S_IFREG,
			      oflags & (~O_APPEND), 0, 0, NULL, &dfs_obj);
		mode_query = S_IFREG;
	} else if (!parent && (strncmp(item_name, "/", 2) == 0)) {
		rc =
		    dfs_lookup(dfs_mt->dfs, "/", oflags & (~O_APPEND), &dfs_obj, &mode_query, NULL);
	} else {
		rc = dfs_lookup_rel(dfs_mt->dfs, parent_dfs, item_name, oflags & (~O_APPEND),
				    &dfs_obj, &mode_query, NULL);
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
			D_DEBUG(DB_ANY, "path is longer than DFS_MAX_PATH: %d (%s)\n", ENAMETOOLONG,
				strerror(ENAMETOOLONG));
			free_dirfd(idx_dirfd);
			D_GOTO(out_error, rc = ENAMETOOLONG);
		}

		drec_decref(dfs_mt->dcache, parent);
		FREE(parent_dir);

		return (idx_dirfd + FD_DIR_BASE);
	}
	atomic_fetch_add_relaxed(&num_open, 1);

	rc = find_next_available_fd(NULL, &idx_fd);
	if (rc)
		D_GOTO(out_error, rc);

	d_file_list[idx_fd]->dfs_mt      = dfs_mt;
	d_file_list[idx_fd]->file        = dfs_obj;
	/* Note drec_decref() will be called in free_fd() */
	d_file_list[idx_fd]->parent      = parent;
	d_file_list[idx_fd]->st_ino      = FAKE_ST_INO(full_path);
	d_file_list[idx_fd]->idx_mmap    = -1;
	d_file_list[idx_fd]->open_flag   = oflags;
	/* NEED to set at the end of file if O_APPEND!!!!!!!! */
	d_file_list[idx_fd]->offset = 0;
	if (strncmp(full_path, "/", 2) == 0)
		D_STRNDUP(d_file_list[idx_fd]->path, dfs_mt->fs_root, DFS_MAX_PATH);
	else
		D_ASPRINTF(d_file_list[idx_fd]->path, "%s%s", dfs_mt->fs_root, full_path);
	if (d_file_list[idx_fd]->path == NULL) {
		free_fd(idx_fd, false);
		D_GOTO(out_error, rc = ENOMEM);
	}
	strncpy(d_file_list[idx_fd]->item_name, item_name, DFS_MAX_NAME);

	FREE(parent_dir);

	if (oflags & O_APPEND) {
		struct stat fstat;

		rc = new_fxstat(1, idx_fd + FD_FILE_BASE, &fstat);
		if (rc != 0)
			return (-1);
		d_file_list[idx_fd]->offset = fstat.st_size;
	}

	return (idx_fd + FD_FILE_BASE);

org_func:
	if (dfs_mt != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	if (two_args)
		return real_open(pathname, oflags);
	else
		return real_open(pathname, oflags, mode);

out_error:
	if (dfs_mt != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	errno = rc;
	return (-1);

out_compatible_release:
	if (dfs_obj)
		dfs_release(dfs_obj);

out_compatible:
	if (dfs_mt != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return fd_kernel;
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

/* Search a fd in fd hash table. Remove it in case it is found. Also free the fake fd.
 * Return true if fd is found. Return false if fd is not found.
 */
static bool
remove_fd_compatible(int real_fd)
{
	d_list_t     *rlink;

	rlink = d_hash_rec_find(fd_hash, &real_fd, sizeof(int));
	if (rlink == NULL)
		return false;

	/* remove fd from hash table */
	d_hash_rec_decref(fd_hash, rlink);
	return true;
}

static int
new_close_common(int (*next_close)(int fd), int fd)
{
	int fd_directed, rc;

	if (!d_hook_enabled)
		return next_close(fd);

	if (d_compatible_mode && fd < FD_FILE_BASE) {
		remove_fd_compatible(fd);
		if (fd < DAOS_MIN_FD && d_daos_inited && (fd_dummy >= 0)) {
			rc = dup2(fd_dummy, fd);
			if (rc != -1)
				return 0;

			return (-1);
		}
		return next_close(fd);
	}

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed >= FD_DIR_BASE) {
		/* directory */
		free_dirfd(fd_directed - FD_DIR_BASE);
		return 0;
	} else if (fd_directed >= FD_FILE_BASE) {
		/* This fd is a kernel fd. There was a duplicate fd created. */
		if (fd < FD_FILE_BASE)
			return close_dup_fd(next_close, fd, true);

		/* This fd is a fake fd. There exists a associated kernel fd with dup2. */
		free_fd(fd - FD_FILE_BASE, false);
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
new_close_nocancel_libc(int fd)
{
	return new_close_common(libc_close_nocancel, fd);
}

static ssize_t
pread_over_dfs(int fd, void *buf, size_t size, off_t offset)
{
	int           rc, rc2;
	d_iov_t       iov;
	d_sg_list_t   sgl;
	daos_size_t   bytes_read;
	daos_event_t  ev;
	daos_handle_t eqh;

	atomic_fetch_add_relaxed(&num_read, 1);
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, buf, size);
	sgl.sg_iovs = &iov;

	rc = get_eqh(&eqh);
	if (rc == 0) {
		bool flag = false;

		rc = daos_event_init(&ev, eqh, NULL);
		if (rc) {
			DL_ERROR(rc, "daos_event_init() failed");
			D_GOTO(err, rc = daos_der2errno(rc));
		}

		rc = dfs_read(d_file_list[fd]->dfs_mt->dfs, d_file_list[fd]->file, &sgl, offset,
			      &bytes_read, &ev);
		if (rc)
			D_GOTO(err_ev, rc);

		while (1) {
			rc = daos_event_test(&ev, DAOS_EQ_NOWAIT, &flag);
			if (rc) {
				DL_ERROR(rc, "daos_event_test() failed");
				D_GOTO(err_ev, rc = daos_der2errno(rc));
			}
			if (flag)
				break;
			sched_yield();
		}
		rc = ev.ev_error;

		rc2 = daos_event_fini(&ev);
		if (rc2)
			DL_ERROR(rc2, "daos_event_fini() failed");
	} else {
		rc = dfs_read(d_file_list[fd]->dfs_mt->dfs, d_file_list[fd]->file, &sgl, offset,
			      &bytes_read, NULL);
	}

	if (rc)
		D_GOTO(err, rc);

	/* not passing the error of daos_event_fini() to read() */
	return (ssize_t)bytes_read;

err_ev:
	rc2 = daos_event_fini(&ev);
	if (rc2)
		DL_ERROR(rc2, "daos_event_fini() failed");

err:
	DS_ERROR(rc, "dfs_read(%p, %zu) failed", buf, size);
	errno = rc;
	return (-1);
}

static ssize_t
read_comm(ssize_t (*next_read)(int fd, void *buf, size_t size), int fd, void *buf, size_t size)
{
	ssize_t rc;
	int	fd_directed;

	if (!d_hook_enabled)
		return next_read(fd, buf, size);

	if (is_bash && fd <= 2 && d_compatible_mode)
		/* special cases to handle bash/sh */
		return next_read(fd, buf, size);

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed >= FD_FILE_BASE) {
		rc = pread_over_dfs(fd_directed - FD_FILE_BASE, buf, size,
				    d_file_list[fd_directed - FD_FILE_BASE]->offset);
		if (rc >= 0)
			d_file_list[fd_directed - FD_FILE_BASE]->offset += rc;
		return rc;
	} else {
		return next_read(fd_directed, buf, size);
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
	int fd_directed;

	if (size == 0)
		return 0;

	if (next_pread == NULL) {
		next_pread = dlsym(RTLD_NEXT, "pread64");
		D_ASSERT(next_pread != NULL);
	}
	if (!d_hook_enabled)
		return next_pread(fd, buf, size, offset);

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_pread(fd, buf, size, offset);

	return pread_over_dfs(fd_directed - FD_FILE_BASE, buf, size, offset);
}

ssize_t
pread64(int fd, void *buf, size_t size, off_t offset) __attribute__((alias("pread")));

ssize_t
__pread64(int fd, void *buf, size_t size, off_t offset) __attribute__((alias("pread")));

extern void __chk_fail(void) __attribute__ ((__noreturn__));

ssize_t
__pread64_chk(int fd, void *buf, size_t size, off_t offset, size_t buflen)
{
	if (size > buflen)
		__chk_fail();

	return pread(fd, buf, size, offset);
}

ssize_t
__read_chk(int fd, void *buf, size_t size, size_t buflen)
{
	if (size > buflen)
		__chk_fail();

	return read(fd, buf, size);
}

static ssize_t
pwrite_over_dfs(int fd, const void *buf, size_t size, off_t offset)
{
	int           rc, rc2;
	d_iov_t       iov;
	d_sg_list_t   sgl;
	daos_event_t  ev;
	daos_handle_t eqh;

	atomic_fetch_add_relaxed(&num_write, 1);
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, (void *)buf, size);
	sgl.sg_iovs = &iov;

	rc = get_eqh(&eqh);
	if (rc == 0) {
		bool flag = false;

		rc = daos_event_init(&ev, eqh, NULL);
		if (rc) {
			DL_ERROR(rc, "daos_event_init() failed");
			D_GOTO(err, rc = daos_der2errno(rc));
		}

		rc = dfs_write(d_file_list[fd]->dfs_mt->dfs, d_file_list[fd]->file, &sgl, offset,
			       &ev);
		if (rc)
			D_GOTO(err_ev, rc);

		while (1) {
			rc = daos_event_test(&ev, DAOS_EQ_NOWAIT, &flag);
			if (rc) {
				DL_ERROR(rc, "daos_event_test() failed");
				D_GOTO(err_ev, rc = daos_der2errno(rc));
			}
			if (flag)
				break;
			sched_yield();
		}
		rc = ev.ev_error;

		rc2 = daos_event_fini(&ev);
		if (rc2)
			DL_ERROR(rc2, "daos_event_fini() failed");
	} else {
		rc = dfs_write(d_file_list[fd]->dfs_mt->dfs, d_file_list[fd]->file, &sgl, offset,
			       NULL);
	}

	if (rc)
		D_GOTO(err, rc);

	/* not passing the error of daos_event_fini() to read() */
	return size;

err_ev:
	rc2 = daos_event_fini(&ev);
	if (rc2)
		DL_ERROR(rc2, "daos_event_fini() failed");

err:
	DS_ERROR(rc, "dfs_write(%p, %zu) failed", (void *)buf, size);
	errno = rc;
	return (-1);
}

ssize_t
write_comm(ssize_t (*next_write)(int fd, const void *buf, size_t size), int fd, const void *buf,
	   size_t size)
{
	ssize_t	rc;
	int	fd_directed;

	if (!d_hook_enabled)
		return next_write(fd, buf, size);

	if (is_bash && fd <= 2 && d_compatible_mode)
		/* special cases to handle bash/sh */
		return next_write(fd, buf, size);

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed >= FD_FILE_BASE) {
		rc = pwrite_over_dfs(fd_directed - FD_FILE_BASE, buf, size,
				     d_file_list[fd_directed - FD_FILE_BASE]->offset);
		if (rc >= 0)
			d_file_list[fd_directed - FD_FILE_BASE]->offset += rc;
		return rc;
	} else {
		return next_write(fd_directed, buf, size);
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
	int fd_directed;

	if (size == 0)
		return 0;

	if (next_pwrite == NULL) {
		next_pwrite = dlsym(RTLD_NEXT, "pwrite64");
		D_ASSERT(next_pwrite != NULL);
	}
	if (!d_hook_enabled)
		return next_pwrite(fd, buf, size, offset);

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_pwrite(fd, buf, size, offset);

	return pwrite_over_dfs(fd_directed - FD_FILE_BASE, buf, size, offset);
}

ssize_t
pwrite64(int fd, const void *buf, size_t size, off_t offset) __attribute__((alias("pwrite")));

ssize_t
__pwrite64(int fd, const void *buf, size_t size, off_t offset) __attribute__((alias("pwrite")));

static ssize_t
readv_over_dfs(int fd, const struct iovec *iov, int iovcnt)
{
	int           rc, rc2, i, ii;
	daos_size_t   bytes_read;
	daos_event_t  ev;
	daos_handle_t eqh;
	d_sg_list_t   sgl      = {0};
	ssize_t       size_sum = 0;

	atomic_fetch_add_relaxed(&num_read, 1);

	D_ALLOC_ARRAY(sgl.sg_iovs, iovcnt);
	if (sgl.sg_iovs == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	/* need to skip zero size length. ii will be the real value for iovcnt */
	ii = 0;
	for (i = 0; i < iovcnt; i++) {
		if (iov[i].iov_len == 0)
			continue;
		d_iov_set(&sgl.sg_iovs[ii], iov[i].iov_base, iov[i].iov_len);
		size_sum += iov[i].iov_len;
		ii++;
	}
	sgl.sg_nr = ii;
	if (size_sum == 0) {
		D_FREE(sgl.sg_iovs);
		return size_sum;
	}

	rc = get_eqh(&eqh);
	if (rc == 0) {
		bool flag = false;

		rc = daos_event_init(&ev, eqh, NULL);
		if (rc) {
			DL_ERROR(rc, "daos_event_init() failed");
			D_GOTO(err, rc = daos_der2errno(rc));
		}

		rc = dfs_read(d_file_list[fd]->dfs_mt->dfs, d_file_list[fd]->file, &sgl,
			      d_file_list[fd]->offset, &bytes_read, &ev);
		if (rc)
			D_GOTO(err_ev, rc);

		while (1) {
			rc = daos_event_test(&ev, DAOS_EQ_NOWAIT, &flag);
			if (rc) {
				DL_ERROR(rc, "daos_event_test() failed");
				D_GOTO(err_ev, rc = daos_der2errno(rc));
			}
			if (flag)
				break;
			sched_yield();
		}
		rc = ev.ev_error;

		rc2 = daos_event_fini(&ev);
		if (rc2)
			DL_ERROR(rc2, "daos_event_fini() failed");
	} else {
		rc = dfs_read(d_file_list[fd]->dfs_mt->dfs, d_file_list[fd]->file, &sgl,
			      d_file_list[fd]->offset, &bytes_read, NULL);
	}

	if (rc)
		D_GOTO(err, rc);

	D_FREE(sgl.sg_iovs);
	return (ssize_t)bytes_read;

err_ev:
	rc2 = daos_event_fini(&ev);
	if (rc2)
		DL_ERROR(rc2, "daos_event_fini() failed");

err:
	D_FREE(sgl.sg_iovs);
	DS_ERROR(rc, "readv_over_dfs failed");
	errno = rc;
	return (-1);
}

static ssize_t
writev_over_dfs(int fd, const struct iovec *iov, int iovcnt)
{
	int           rc, rc2, i, ii;
	daos_event_t  ev;
	daos_handle_t eqh;
	d_sg_list_t   sgl      = {0};
	ssize_t       size_sum = 0;

	atomic_fetch_add_relaxed(&num_write, 1);

	D_ALLOC_ARRAY(sgl.sg_iovs, iovcnt);
	if (sgl.sg_iovs == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	/* need to skip zero size length. ii will be the real value for iovcnt */
	ii = 0;
	for (i = 0; i < iovcnt; i++) {
		if (iov[i].iov_len == 0)
			continue;
		d_iov_set(&sgl.sg_iovs[ii], iov[i].iov_base, iov[i].iov_len);
		size_sum += iov[i].iov_len;
		ii++;
	}
	sgl.sg_nr = ii;
	if (size_sum == 0) {
		D_FREE(sgl.sg_iovs);
		return size_sum;
	}

	rc = get_eqh(&eqh);
	if (rc == 0) {
		bool flag = false;

		rc = daos_event_init(&ev, eqh, NULL);
		if (rc) {
			DL_ERROR(rc, "daos_event_init() failed");
			D_GOTO(err, rc = daos_der2errno(rc));
		}

		rc = dfs_write(d_file_list[fd]->dfs_mt->dfs, d_file_list[fd]->file, &sgl,
			       d_file_list[fd]->offset, &ev);
		if (rc)
			D_GOTO(err_ev, rc);

		while (1) {
			rc = daos_event_test(&ev, DAOS_EQ_NOWAIT, &flag);
			if (rc) {
				DL_ERROR(rc, "daos_event_test() failed");
				D_GOTO(err_ev, rc = daos_der2errno(rc));
			}
			if (flag)
				break;
			sched_yield();
		}
		rc = ev.ev_error;

		rc2 = daos_event_fini(&ev);
		if (rc2)
			DL_ERROR(rc2, "daos_event_fini() failed");
	} else {
		rc = dfs_write(d_file_list[fd]->dfs_mt->dfs, d_file_list[fd]->file, &sgl,
			       d_file_list[fd]->offset, NULL);
	}

	if (rc)
		D_GOTO(err, rc);

	D_FREE(sgl.sg_iovs);
	return size_sum;

err_ev:
	rc2 = daos_event_fini(&ev);
	if (rc2)
		DL_ERROR(rc2, "daos_event_fini() failed");

err:
	D_FREE(sgl.sg_iovs);
	DS_ERROR(rc, "writev_over_dfs failed");
	errno = rc;
	return (-1);
}

ssize_t
readv(int fd, const struct iovec *iov, int iovcnt)
{
	int     fd_directed;
	ssize_t size_sum;

	if (next_readv == NULL) {
		next_readv = dlsym(RTLD_NEXT, "readv");
		D_ASSERT(next_readv != NULL);
	}
	if (!d_hook_enabled)
		return next_readv(fd, iov, iovcnt);

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_readv(fd, iov, iovcnt);

	size_sum = readv_over_dfs(fd_directed - FD_FILE_BASE, iov, iovcnt);
	if (size_sum < 0)
		return size_sum;
	d_file_list[fd_directed - FD_FILE_BASE]->offset += size_sum;

	return size_sum;
}

ssize_t
writev(int fd, const struct iovec *iov, int iovcnt)
{
	int     fd_directed;
	ssize_t size_sum;

	if (next_writev == NULL) {
		next_writev = dlsym(RTLD_NEXT, "writev");
		D_ASSERT(next_writev != NULL);
	}
	if (!d_hook_enabled)
		return next_writev(fd, iov, iovcnt);

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_writev(fd, iov, iovcnt);

	size_sum = writev_over_dfs(fd_directed - FD_FILE_BASE, iov, iovcnt);
	if (size_sum < 0)
		return size_sum;
	d_file_list[fd_directed - FD_FILE_BASE]->offset += size_sum;

	return size_sum;
}

static int
new_fxstat(int vers, int fd, struct stat *buf)
{
	int rc, fd_directed;

	if (!d_hook_enabled)
		return next_fxstat(vers, fd, buf);

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_fxstat(vers, fd, buf);

	if (fd_directed < FD_DIR_BASE) {
		rc          = dfs_ostat(d_file_list[fd_directed - FD_FILE_BASE]->dfs_mt->dfs,
					d_file_list[fd_directed - FD_FILE_BASE]->file, buf);
		buf->st_ino = d_file_list[fd_directed - FD_FILE_BASE]->st_ino;
	} else {
		rc          = dfs_ostat(dir_list[fd_directed - FD_DIR_BASE]->dfs_mt->dfs,
					dir_list[fd_directed - FD_DIR_BASE]->dir, buf);
		buf->st_ino = dir_list[fd_directed - FD_DIR_BASE]->st_ino;
	}

	if (rc) {
		DS_ERROR(rc, "dfs_ostat() failed");
		errno = rc;
		rc    = -1;
	}

	atomic_fetch_add_relaxed(&num_stat, 1);

	return rc;
}

int
fstat(int fd, struct stat *buf)
{
	int rc, fd_directed;

	if (next_fstat == NULL) {
		next_fstat = dlsym(RTLD_NEXT, "fstat");
		D_ASSERT(next_fstat != NULL);
	}
	if (!d_hook_enabled)
		return next_fstat(fd, buf);

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_fstat(fd, buf);

	if (fd_directed < FD_DIR_BASE) {
		rc          = dfs_ostat(d_file_list[fd_directed - FD_FILE_BASE]->dfs_mt->dfs,
					d_file_list[fd_directed - FD_FILE_BASE]->file, buf);
		buf->st_ino = d_file_list[fd_directed - FD_FILE_BASE]->st_ino;
	} else {
		rc          = dfs_ostat(dir_list[fd_directed - FD_DIR_BASE]->dfs_mt->dfs,
					dir_list[fd_directed - FD_DIR_BASE]->dir, buf);
		buf->st_ino = dir_list[fd_directed - FD_DIR_BASE]->st_ino;
	}

	if (rc) {
		DS_ERROR(rc, "dfs_ostat() failed");
		errno = rc;
		rc    = -1;
	}

	atomic_fetch_add_relaxed(&num_stat, 1);

	return rc;
}

int
fstat64(int fd, struct stat64 *buf) __attribute__((alias("fstat"), leaf, nonnull, nothrow));

int
__fstat64(int fd, struct stat64 *buf) __attribute__((alias("fstat"), leaf, nonnull, nothrow));

static int
new_xstat(int ver, const char *path, struct stat *stat_buf)
{
	int                is_target_path, rc;
	dfs_obj_t         *obj;
	mode_t             mode;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

	if (!d_hook_enabled)
		return next_xstat(ver, path, stat_buf);
	if (path[0] == 0) {
		errno = ENOENT;
		return (-1);
	}

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;
	atomic_fetch_add_relaxed(&num_stat, 1);

	if (!parent && (strncmp(item_name, "/", 2) == 0)) {
		rc = dfs_lookup(dfs_mt->dfs, "/", O_RDONLY, &obj, &mode, stat_buf);
	} else {
		rc = dfs_lookup_rel(dfs_mt->dfs, drec2obj(parent), item_name, O_RDONLY, &obj, &mode,
				    stat_buf);
	}
	if ((rc == ENOTSUP || rc == EIO) && d_compatible_mode)
		goto out_org;

	stat_buf->st_mode = mode;
	if (rc)
		D_GOTO(out_err, rc);

	stat_buf->st_ino = FAKE_ST_INO(full_path);
	dfs_release(obj);
	drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);

	return 0;

out_org:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return next_xstat(ver, path, stat_buf);

out_err:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	if ((rc == EIO || rc == EINVAL) && d_compatible_mode)
		return next_xstat(ver, path, stat_buf);
	errno = rc;
	return (-1);
}

static int
new_lxstat(int ver, const char *path, struct stat *stat_buf)
{
	int                is_target_path, rc;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

	if (!d_hook_enabled)
		return libc_lxstat(ver, path, stat_buf);
	if (path[0] == 0) {
		errno = ENOENT;
		return (-1);
	}

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;
	atomic_fetch_add_relaxed(&num_stat, 1);

	if (!parent && (strncmp(item_name, "/", 2) == 0))
		rc = dfs_stat(dfs_mt->dfs, NULL, NULL, stat_buf);
	else
		rc = dfs_stat(dfs_mt->dfs, drec2obj(parent), item_name, stat_buf);
	if (rc)
		goto out_err;
	stat_buf->st_ino = FAKE_ST_INO(full_path);
	drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return 0;

out_org:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return libc_lxstat(ver, path, stat_buf);

out_err:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	if ((rc == EIO || rc == EINVAL) && d_compatible_mode)
		return libc_lxstat(ver, path, stat_buf);
	errno = rc;
	return (-1);
}

static int
new_fxstatat(int ver, int dirfd, const char *path, struct stat *stat_buf, int flags)
{
	int  idx_dfs, error = 0, rc;
	char *full_path = NULL;

	if (!d_hook_enabled)
		return libc_fxstatat(ver, dirfd, path, stat_buf, flags);
	if (path[0] == 0 && ((flags & AT_EMPTY_PATH) == 0)) {
		errno = ENOENT;
		return (-1);
	}

	if (path[0] == '/') {
		/* Absolute path, dirfd is ignored */
		if (flags & AT_SYMLINK_NOFOLLOW)
			return new_lxstat(1, path, stat_buf);
		else
			return new_xstat(1, path, stat_buf);
	}

	if (dirfd >= FD_FILE_BASE && dirfd < FD_DIR_BASE) {
		if (path[0] == 0 && flags & AT_EMPTY_PATH)
			/* same as fstat for a file. May need further work to handle flags */
			return new_fxstat(ver, dirfd, stat_buf);
		else if (path[0] == 0)
			error = ENOENT;
		else
			error = ENOTDIR;
		goto out_err;
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
		rc = libc_fxstatat(ver, dirfd, path, stat_buf, flags);
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

int
new_fstatat(int dirfd, const char *__restrict path, struct stat *__restrict stat_buf, int flags)
{
	int  idx_dfs, error = 0, rc;
	char *full_path = NULL;

	if (!d_hook_enabled)
		return libc_fstatat(dirfd, path, stat_buf, flags);

	if (path[0] == 0 && ((flags & AT_EMPTY_PATH) == 0)) {
		errno = ENOENT;
		return (-1);
	}

	if (path[0] == '/') {
		/* Absolute path, dirfd is ignored */
		if (flags & AT_SYMLINK_NOFOLLOW)
			return new_lxstat(1, path, stat_buf);
		else
			return new_xstat(1, path, stat_buf);
	}

	if (dirfd >= FD_FILE_BASE && dirfd < FD_DIR_BASE) {
		if (path[0] == 0 && flags & AT_EMPTY_PATH)
			/* same as fstat for a file. May need further work to handle flags */
			return fstat(dirfd, stat_buf);
		else if (path[0] == 0)
			error = ENOENT;
		else
			error = ENOTDIR;
		goto out_err;
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
		rc = libc_fstatat(dirfd, path, stat_buf, flags);
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
	if (path[0] == 0 && ((flags & AT_EMPTY_PATH) == 0)) {
		errno = ENOENT;
		return (-1);
	}

	if (!d_hook_enabled)
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
	int         fd_directed;
	off_t       new_offset;
	struct stat fstat;

	if (!d_hook_enabled)
		return next_lseek(fd, offset, whence);

	if (is_bash && fd <= 2 && d_compatible_mode)
		/* special cases to handle bash/sh */
		return next_lseek(fd, offset, whence);

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_lseek(fd, offset, whence);
	/* seekdir() will handle by the following. */
	if (fd < FD_FILE_BASE && fd_directed >= FD_DIR_BASE && d_compatible_mode)
		return next_lseek(fd, offset, whence);

	atomic_fetch_add_relaxed(&num_seek, 1);

	switch (whence) {
	case SEEK_SET:
		new_offset = offset;
		break;
	case SEEK_CUR:
		new_offset = d_file_list[fd_directed - FD_FILE_BASE]->offset + offset;
		break;
	case SEEK_END:
		fstat.st_size = 0;
		rc            = new_fxstat(1, fd_directed, &fstat);
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

	d_file_list[fd_directed - FD_FILE_BASE]->offset = new_offset;
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
	daos_pool_info_t   info = {.pi_bits = DPI_SPACE};
	int                rc, is_target_path;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

	if (next_statfs == NULL) {
		next_statfs = dlsym(RTLD_NEXT, "statfs");
		D_ASSERT(next_statfs != NULL);
	}

	if (!d_hook_enabled)
		return next_statfs(pathname, sfs);

	rc = query_path(pathname, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;

	rc = daos_pool_query(dfs_mt->poh, NULL, &info, NULL, NULL);
	if (rc != 0)
		D_GOTO(out_err, rc = daos_der2errno(rc));

	sfs->f_blocks = info.pi_space.ps_space.s_total[DAOS_MEDIA_SCM] +
			info.pi_space.ps_space.s_total[DAOS_MEDIA_NVME];
	sfs->f_bfree = info.pi_space.ps_space.s_free[DAOS_MEDIA_SCM] +
		       info.pi_space.ps_space.s_free[DAOS_MEDIA_NVME];
	sfs->f_bsize  = 1;
	sfs->f_files  = -1;
	sfs->f_ffree  = -1;
	sfs->f_bavail = sfs->f_bfree;

	drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return 0;

out_org:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return next_statfs(pathname, sfs);

out_err:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

int
fstatfs(int fd, struct statfs *sfs)
{
	int              rc;
	int              fd_directed;
	struct dfs_mt   *dfs_mt;
	daos_pool_info_t info = {.pi_bits = DPI_SPACE};

	if (next_fstatfs == NULL) {
		next_fstatfs = dlsym(RTLD_NEXT, "fstatfs");
		D_ASSERT(next_fstatfs != NULL);
	}

	if (!d_hook_enabled)
		return next_fstatfs(fd, sfs);

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_fstatfs(fd, sfs);

	if (fd_directed < FD_DIR_BASE)
		dfs_mt = d_file_list[fd_directed - FD_FILE_BASE]->dfs_mt;
	else
		dfs_mt = dir_list[fd_directed - FD_DIR_BASE]->dfs_mt;
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
	daos_pool_info_t   info = {.pi_bits = DPI_SPACE};
	int                rc, is_target_path;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

	if (next_statvfs == NULL) {
		next_statvfs = dlsym(RTLD_NEXT, "statvfs");
		D_ASSERT(next_statvfs != NULL);
	}

	if (!d_hook_enabled)
		return next_statvfs(pathname, svfs);

	rc = query_path(pathname, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;

	rc = daos_pool_query(dfs_mt->poh, NULL, &info, NULL, NULL);
	if (rc) {
		DL_ERROR(rc, "failed to query pool");
		D_GOTO(out_err, rc = daos_der2errno(rc));
	}

	svfs->f_blocks = info.pi_space.ps_space.s_total[DAOS_MEDIA_SCM] +
			 info.pi_space.ps_space.s_total[DAOS_MEDIA_NVME];
	svfs->f_bfree  = info.pi_space.ps_space.s_free[DAOS_MEDIA_SCM] +
			 info.pi_space.ps_space.s_free[DAOS_MEDIA_NVME];
	svfs->f_bsize  = 1;
	svfs->f_files  = -1;
	svfs->f_ffree  = -1;
	svfs->f_bavail = svfs->f_bfree;

	drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return 0;

out_org:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return next_statvfs(pathname, svfs);

out_err:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
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
	int                is_target_path, idx_dirfd, rc;
	dfs_obj_t         *dir_obj;
	mode_t             mode;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt      = NULL;
	struct dcache_rec *parent      = NULL;
	char              *parent_dir  = NULL;
	char              *full_path   = NULL;
	DIR               *dirp_kernel = NULL;
	struct ht_fd      *fd_ht_obj   = NULL;

	if (next_opendir == NULL) {
		next_opendir = dlsym(RTLD_NEXT, "opendir");
		D_ASSERT(next_opendir != NULL);
	}
	if (!d_hook_enabled)
		return next_opendir(path);

	rc =
	    query_path(path, &is_target_path, &parent, item_name, &parent_dir, &full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err_ret, rc);
	if (!is_target_path) {
		FREE(parent_dir);
		return next_opendir(path);
	}

	/* Always rely on fuse for opendir() to avoid a fake dir fd */
	if (d_compatible_mode) {
		dirp_kernel = next_opendir(path);
		if (dirp_kernel == NULL)
			D_GOTO(out_err_ret, rc = errno);
	}

	atomic_fetch_add_relaxed(&num_opendir, 1);

	if (!parent && (strncmp(item_name, "/", 2) == 0)) {
		/* dfs_lookup() is needed for root dir */
		rc = dfs_lookup(dfs_mt->dfs, "/", O_RDONLY, &dir_obj, &mode, NULL);
		if (rc)
			D_GOTO(out_err_ret, rc);
	} else {
		rc = dfs_open(dfs_mt->dfs, drec2obj(parent), item_name, S_IFDIR, O_RDONLY, 0, 0,
			      NULL, &dir_obj);
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
	dir_list[idx_dirfd]->st_ino   = FAKE_ST_INO(full_path);
	memset(&dir_list[idx_dirfd]->anchor, 0, sizeof(daos_anchor_t));
	dir_list[idx_dirfd]->path = NULL;
	dir_list[idx_dirfd]->ents = NULL;
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
		D_DEBUG(DB_ANY, "path is longer than DFS_MAX_PATH: %d (%s)\n", ENAMETOOLONG,
			strerror(ENAMETOOLONG));
		free_dirfd(idx_dirfd);
		D_GOTO(out_err, rc = ENAMETOOLONG);
	}

	if (d_compatible_mode) {
		/* add fd kernel to hash table */
		D_ALLOC_PTR(fd_ht_obj);
		if (fd_ht_obj == NULL) {
			/* not returning ENOMEM since dirp_kernel is valid */
			free_dirfd(idx_dirfd);
			goto out_compatible;
		}
		fd_ht_obj->real_fd = dirfd(dirp_kernel);
		D_ASSERT(fd_ht_obj->real_fd >= 0);
		fd_ht_obj->fake_fd = idx_dirfd + FD_DIR_BASE;
		rc = d_hash_rec_insert(fd_hash, &fd_ht_obj->real_fd, sizeof(int), &fd_ht_obj->entry,
				       false);
		D_ASSERT(rc == 0);
		goto out_compatible;
	}

	drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);

	return (DIR *)(dir_list[idx_dirfd]);

out_err:
	dfs_release(dir_obj);

out_err_ret:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	errno = rc;
	return NULL;

out_compatible:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return dirp_kernel;
}

DIR *
fdopendir(int fd)
{
	int fd_directed;

	if (next_fdopendir == NULL) {
		next_fdopendir = dlsym(RTLD_NEXT, "fdopendir");
		D_ASSERT(next_fdopendir != NULL);
	}
	if (!d_hook_enabled)
		return next_fdopendir(fd);
	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_DIR_BASE)
		return next_fdopendir(fd_directed);
	if (fd < FD_FILE_BASE && fd_directed >= FD_DIR_BASE && d_compatible_mode)
		return next_fdopendir(fd);

	atomic_fetch_add_relaxed(&num_opendir, 1);

	return (DIR *)(dir_list[fd_directed - FD_DIR_BASE]);
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

	if (!d_hook_enabled)
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

	_Pragma("GCC diagnostic push")
	_Pragma("GCC diagnostic ignored \"-Wnonnull-compare\"")
	/* Check whether path is NULL or not since application provides dirp */
	if (path == NULL) {
		errno = EFAULT;
		return (-1);
	}
	_Pragma("GCC diagnostic pop")

	if (next_openat_2 == NULL) {
		next_openat_2 = dlsym(RTLD_NEXT, "__openat_2");
		D_ASSERT(next_openat_2 != NULL);
	}
	if (!d_hook_enabled)
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
	if (!d_hook_enabled)
		return next_closedir(dirp);

	_Pragma("GCC diagnostic push")
	_Pragma("GCC diagnostic ignored \"-Wnonnull-compare\"")
	/* Check whether dirp is NULL or not since application provides dirp */
	if (!dirp) {
		D_DEBUG(DB_ANY, "dirp is NULL in closedir(): %d (%s)\n", EINVAL, strerror(EINVAL));
		errno = EINVAL;
		return (-1);
	}
	_Pragma("GCC diagnostic pop")

	fd = dirfd(dirp);

	if (d_compatible_mode && fd < FD_FILE_BASE) {
		d_list_t     *rlink;
		int           real_fd = fd;

		rlink = d_hash_rec_find(fd_hash, &real_fd, sizeof(int));
		if (rlink != NULL) {
			/* remove fd from hash table */
			d_hash_rec_decref(fd_hash, rlink);
			return next_closedir(dirp);
		}
	}

	if (fd >= FD_DIR_BASE) {
		free_dirfd(fd - FD_DIR_BASE);
		return 0;
	} else {
		return next_closedir(dirp);
	}
}

long
telldir(DIR *dirp)
{
	int fd;

	if (next_telldir == NULL) {
		next_telldir = dlsym(RTLD_NEXT, "telldir");
		D_ASSERT(next_telldir != NULL);
	}
	if (!d_hook_enabled)
		return next_telldir(dirp);

	fd = dirfd(dirp);
	if (fd < FD_DIR_BASE)
		return next_telldir(dirp);

	return dir_list[fd - FD_DIR_BASE]->offset;
}

void
rewinddir(DIR *dirp)
{
	int fd, idx;

	if (next_rewinddir == NULL) {
		next_rewinddir = dlsym(RTLD_NEXT, "rewinddir");
		D_ASSERT(next_rewinddir != NULL);
	}
	if (!d_hook_enabled)
		return next_rewinddir(dirp);

	fd = dirfd(dirp);
	if (fd < FD_DIR_BASE)
		return next_rewinddir(dirp);

	idx                     = fd - FD_DIR_BASE;
	dir_list[idx]->offset   = 0;
	dir_list[idx]->num_ents = 0;
	memset(&dir_list[idx]->anchor, 0, sizeof(daos_anchor_t));

	return;
}

/* Offset of the first entry, allow two entries for . and .. */
#define OFFSET_BASE 2

void
seekdir(DIR *dirp, long loc)
{
	int      fd, idx, rc;
	long     num_entry;
	uint32_t num_to_read;

	if (next_seekdir == NULL) {
		next_seekdir = dlsym(RTLD_NEXT, "seekdir");
		D_ASSERT(next_seekdir != NULL);
	}
	if (!d_hook_enabled)
		return next_seekdir(dirp, loc);

	fd = dirfd(dirp);
	if (fd < FD_DIR_BASE)
		return next_seekdir(dirp, loc);

	idx = fd - FD_DIR_BASE;

	/* need to compare loc with current offset & the number of cached entries */
	if (loc <= OFFSET_BASE) {
		dir_list[idx]->offset   = loc;
		dir_list[idx]->num_ents = 0;
		memset(&dir_list[idx]->anchor, 0, sizeof(daos_anchor_t));
		return;
	}
	if (dir_list[idx]->offset <= OFFSET_BASE) {
		/* no buffered entry */
		dir_list[idx]->offset   = OFFSET_BASE;
		dir_list[idx]->num_ents = 0;
		num_entry               = loc - OFFSET_BASE;
	} else if (loc < dir_list[idx]->offset) {
		/* rewind and read entries from the beginning */
		dir_list[idx]->offset   = OFFSET_BASE;
		dir_list[idx]->num_ents = 0;
		memset(&dir_list[idx]->anchor, 0, sizeof(daos_anchor_t));
		num_entry = loc - OFFSET_BASE;
	} else if (loc >= (dir_list[idx]->offset + dir_list[idx]->num_ents)) {
		/* need to read more entries from current offset */
		dir_list[idx]->offset   = dir_list[idx]->offset + dir_list[idx]->num_ents;
		dir_list[idx]->num_ents = 0;
		num_entry               = loc - dir_list[idx]->offset;
	} else if (loc >= dir_list[idx]->offset) {
		/* in the cached entries */
		dir_list[idx]->num_ents -= (loc - dir_list[idx]->offset);
		dir_list[idx]->offset = loc;
		return;
	}

	while (num_entry) {
		num_to_read = min(READ_DIR_BATCH_SIZE, num_entry);

		rc = dfs_iterate(dir_list[idx]->dfs_mt->dfs, dir_list[idx]->dir,
				 &dir_list[idx]->anchor, &num_to_read, DFS_MAX_NAME * num_to_read,
				 NULL, NULL);
		if (rc)
			D_GOTO(out_rewind, rc);
		if (daos_anchor_is_eof(&dir_list[idx]->anchor))
			D_GOTO(out_rewind, rc);
		dir_list[idx]->offset += num_to_read;
		dir_list[idx]->num_ents = 0;
		num_entry               = loc - dir_list[idx]->offset;
		num_to_read             = READ_DIR_BATCH_SIZE;
	}

	return;

out_rewind:
	dir_list[idx]->offset   = 0;
	dir_list[idx]->num_ents = 0;
	memset(&dir_list[idx]->anchor, 0, sizeof(daos_anchor_t));
	return;
}

int
scandirat(int dirfd, const char *restrict path, struct dirent ***restrict namelist,
	  int (*filter)(const struct dirent *),
	  int (*compar)(const struct dirent **, const struct dirent **))
{
	int   rc;
	int   error     = 0;
	char *full_path = NULL;

	if (next_scandirat == NULL) {
		next_scandirat = dlsym(RTLD_NEXT, "scandirat");
		D_ASSERT(next_scandirat != NULL);
	}
	if (!d_hook_enabled)
		return next_scandirat(dirfd, path, namelist, filter, compar);

	if (dirfd < FD_DIR_BASE)
		return next_scandirat(dirfd, path, namelist, filter, compar);

	if (path[0] == '/')
		return scandir(path, namelist, filter, compar);

	check_path_with_dirfd(dirfd, &full_path, path, &error);
	if (error)
		goto out_err;

	rc = scandir(full_path, namelist, filter, compar);

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

static struct dirent *
new_readdir(DIR *dirp)
{
	int               rc        = 0;
	int               len_str   = 0;
	struct dir_obj   *mydir     = (struct dir_obj *)dirp;
	char             *full_path = NULL;
	int               fd_directed;

	if (!d_hook_enabled)
		return next_readdir(dirp);
	fd_directed = d_get_fd_redirected(dirfd(dirp));
	if (fd_directed < FD_FILE_BASE)
		return next_readdir(dirp);

	if (fd_directed < FD_DIR_BASE) {
		/* Not suppose to be here */
		D_DEBUG(DB_ANY, "readdir() failed: %d (%s)\n", EINVAL, strerror(EINVAL));
		errno = EINVAL;
		return NULL;
	}
	if (d_compatible_mode)
		/* dirp is from Linux kernel */
		mydir = dir_list[fd_directed - FD_DIR_BASE];
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
	if (mydir->offset <= OFFSET_BASE)
		mydir->offset = OFFSET_BASE + 1;
	else
		mydir->offset++;
	len_str = asprintf(&full_path, "%s/%s",
			   dir_list[mydir->fd - FD_DIR_BASE]->path +
			   dir_list[mydir->fd - FD_DIR_BASE]->dfs_mt->len_fs_root,
			   mydir->ents[mydir->num_ents].d_name);
	if (len_str >= DFS_MAX_PATH) {
		D_DEBUG(DB_ANY, "path is too long: %d (%s)\n", ENAMETOOLONG,
			strerror(ENAMETOOLONG));
		free(full_path);
		mydir->num_ents = 0;
		errno = ENAMETOOLONG;
		return NULL;
	} else if (len_str < 0) {
		D_DEBUG(DB_ANY, "asprintf() failed: %d (%s)\n", errno, strerror(errno));
		mydir->num_ents = 0;
		errno           = ENOMEM;
		return NULL;
	}
	mydir->ents[mydir->num_ents].d_ino = FAKE_ST_INO(full_path);
	free(full_path);
	return &mydir->ents[mydir->num_ents];
}

/* Bypass is allowed by default. Env "D_IL_NO_BYPASS" is ONLY used for testing purpose.
 * "D_IL_NO_BYPASS=1" enforces that interception by libpil4dfs is on in current process and
 * children processes. This is needed to thoroughly test interception related code in CI.
 */
static char env_str_no_bypass_on[]  = "D_IL_NO_BYPASS=1";
static char env_str_no_bypass_off[] = "D_IL_NO_BYPASS=0";

/* This is the number of environmental variables that would be forced to set in child process.
 * "LD_PRELOAD" is a special case and it is not included in the list.
 */
static char *env_list[] = {"D_IL_REPORT",
			   "D_IL_MOUNT_POINT",
			   "D_IL_POOL",
			   "D_IL_CONTAINER",
			   "D_IL_MAX_EQ",
			   "D_LOG_FILE",
			   "DD_MASK",
			   "DD_SUBSYS",
			   "D_LOG_MASK",
			   "D_IL_COMPATIBLE",
			   "D_IL_NO_DCACHE_BASH",
			   "D_IL_BYPASS_LIST"};

/* Environmental variables could be cleared in some applications. To make sure all libpil4dfs
 * related env properly set, we intercept execve and its variants to check envp[] and append our
 * env when a stripped env list is provided.
 * pre_envp() scans provided envp to look for libpil4dfs related envs (env_list[]) and sets a new
 * envp with missing libpil4dfs related envs appended. Non-zero return value carries error code.
 * envp could be NULL, so *new_envp could be NULL too.
 * The fd for daos logging may be not valid here, so fprintf() is used in this function.
 */
static int
pre_envp(char *const envp[], char ***new_envp)
{
	int    i, j, rc = 0;
	int    len, len2, len_total;
	int    num_env_append;
	char  *pil4df_path;
	char  *new_preload_str;
	int    num_entry       = 0;
	int    num_entry_found = 0;
	int    idx_preload     = -1;
	/* whether pil4dfs set in LD_PRELOAD in environ */
	bool   pil4dfs_set_preload                = false;
	bool   preload_included                   = false;
	/* whether pil4dfs in LD_PRELOAD in envp[] */
	bool   pil4dfs_in_preload                 = false;
	bool   no_bypass_included                 = false;
	/* whether env_list entry exists in envp[] */
	bool   env_found[ARRAY_SIZE(env_list)]    = {false};
	/* whether env_list entry exists in environ */
	bool   env_set[ARRAY_SIZE(env_list)]      = {false};
	/* the buffer allocated for the env string to append */
	char  *env_buf_list[ARRAY_SIZE(env_list)] = {NULL};
	char  *env_value                          = NULL;

	*new_envp = (char**)envp;

	/* simply return for environ. append pil4dfs env only for stripped env list. */
	if (envp == environ)
		return 0;

	/* the number of env in env_list[] that are set in environ */
	num_env_append = 0;

	/* check whether "LD_PRELOAD" exists in environ */
	rc = d_agetenv_str(&env_value, "LD_PRELOAD");
	if (rc == -DER_NONEXIST) {
		return 0;
	} else if (rc == -DER_NOMEM) {
		rc = ENOMEM;
		goto err_out0;
	}
	if (strstr(env_value, "libpil4dfs.so")) {
		pil4dfs_set_preload = true;
		num_env_append++;
	}
	d_freeenv_str(&env_value);
	/* libpil4dfs.so is not in LD_PRELOAD, do not append env. */
	if (!pil4dfs_set_preload)
		return 0;
	if (bypass_allowed == false)
		/* "D_IL_NO_BYPASS" needs to be appended. */
		num_env_append++;

	/* check whether env_list entries exist in environ */
	for (i = 0; i < ARRAY_SIZE(env_list); i++) {
		rc = d_agetenv_str(&env_value, env_list[i]);
		if (rc == -DER_NONEXIST) {
			/* Do nothing if env does not exist*/
			continue;
		} else if (rc == -DER_NOMEM) {
			rc = ENOMEM;
			goto err_out0;
		}
		/* In case d_agetenv_str() returns -DER_SUCCESS */
		d_freeenv_str(&env_value);
		env_set[i] = true;
		num_env_append++;
	}

	if (envp == NULL) {
		num_entry = 0;
	} else if (envp[0] == NULL) {
		num_entry = 0;
	} else {
		while (envp[num_entry]) {
			/* scan the env in env_list[] to check whether they exist or not */
			if (memcmp(envp[num_entry], STR_AND_SIZE_M1("LD_PRELOAD")) == 0 &&
			    preload_included == false) {
				preload_included = true;
				idx_preload      = num_entry;
				num_entry_found++;
				if (strstr(envp[num_entry], "libpil4dfs.so"))
					pil4dfs_in_preload = true;
			} else if (memcmp(envp[num_entry], STR_AND_SIZE_M1("D_IL_NO_BYPASS")) ==
				   0 && no_bypass_included == false) {
				no_bypass_included = true;
				num_entry_found++;
			}
			/* The list of env is not too long. We use a simple loop to lookup for
			 * simplicity. This function is not performance critical.
			 */
			for (i = 0; i < ARRAY_SIZE(env_list); i++) {
				/* env is not set in environ, then no need to append. */
				if (!env_set[i])
					continue;
				if (!env_found[i]) {
					if (memcmp(envp[num_entry], env_list[i],
						   strlen(env_list[i])) == 0) {
						env_found[i] = true;
						num_entry_found++;
					}
				}
			}
			num_entry++;
		}
	}

	/* All required env are found and pil4dfs is in LD_PRELOAD. No need to create a new envp. */
	if (num_entry_found == num_env_append && pil4dfs_in_preload == true)
		return 0;

	/* the new envp holds the existing envs & the envs forced to append plus NULL at the end */
	*new_envp = calloc(num_entry + num_env_append + 1, sizeof(char *));
	if (*new_envp == NULL) {
		rc = ENOMEM;
		goto err_out0;
	}

	/* Copy all existing entries to the new envp[] */
	for (i = 0; i < num_entry; i++) {
		(*new_envp)[i] = envp[i];
	}

	/* LD_PRELOAD is a special case. If LD_PRELOAD is found but libpil4dfs.so is not in
	 * LD_PRELOAD env, then allocate a buffer for the concatenation of existing LD_PRELOAD
	 * env and the full path of libpil4dfs.so.
	 */
	pil4df_path = query_pil4dfs_path();
	len2        = strnlen(pil4df_path, PATH_MAX);
	if (preload_included == true && pil4dfs_in_preload == false) {
		/* need to replace the existing string */
		len = strnlen(envp[idx_preload], MAX_ARG_STRLEN);
		/* 2 extra bytes for ':' and '\0' */
		len_total = len + len2 + 2;
		if (len_total > MAX_ARG_STRLEN) {
			fprintf(stderr, "Error: env for LD_PRELOAD is too long.\n");
			rc = E2BIG;
			goto err_out1;
		}
		rc = asprintf(&new_preload_str, "%s:%s", envp[idx_preload], pil4df_path);
		if (rc < 0) {
			rc = ENOMEM;
			goto err_out1;
		}
		(*new_envp)[idx_preload] = new_preload_str;
	}
	/* append LD_PRELOAD env */
	if (!preload_included) {
		rc = asprintf(&new_preload_str, "LD_PRELOAD=%s", pil4df_path);
		if (rc < 0) {
			rc = ENOMEM;
			goto err_out1;
		}
		(*new_envp)[i] = new_preload_str;
		i++;
	}
	/* append D_IL_NO_BYPASS env */
	if (!no_bypass_included) {
		if (bypass_allowed == false)
			(*new_envp)[i] = env_str_no_bypass_on;
		else
			(*new_envp)[i] = env_str_no_bypass_off;
		i++;
	}

	for (j = 0; j < ARRAY_SIZE(env_list); j++) {
		/* env is not set in environ. */
		if (!env_set[j])
			continue;
		/* env is not found in envp[]. Need to be appended if present in current process. */
		if (!env_found[j]) {
			rc = d_agetenv_str(&env_value, env_list[j]);
			if (rc == -DER_NONEXIST) {
				/* Do nothing if env does not exist. Not suppose to be here. */
				continue;
			} else if (rc == -DER_NOMEM) {
				rc = ENOMEM;
				goto err_out2;
			}
			/* In case d_agetenv_str() returns -DER_SUCCESS, append the env */
			rc = asprintf(&env_buf_list[j], "%s=%s", env_list[j], env_value);
			if (rc < 0) {
				rc = ENOMEM;
				goto err_out2;
			}
			(*new_envp)[i] = env_buf_list[j];
			i++;
			/* free the buffer allocated by d_agetenv_str() */
			d_freeenv_str(&env_value);
		}
	}

	return 0;

err_out2:
	/* free the memory allocated for all env buffer */
	for (j = 0; j < ARRAY_SIZE(env_list); j++) {
		if (env_buf_list[j])
			free(env_buf_list[j]);
	}
	/* free the buffer returned from d_agetenv_str */
	if (env_value)
		d_freeenv_str(&env_value);
	free(new_preload_str);
err_out1:
	free(*new_envp);
err_out0:
	return rc;
}

/* check whether fd 0, 1, and 2 are located on DFS mount or not. If yes, reopen files and
 * set offset.
 */
static int
setup_fd_0_1_2(void)
{
	int   i, fd, idx, fd_tmp, fd_new, open_flag, error_save;
	off_t offset;

	if (atomic_load_relaxed(&num_fd_dup2ed) == 0)
		return 0;

	D_RWLOCK_RDLOCK(&lock_fd_dup2ed);
	for (i = 0; i < MAX_FD_DUP2ED; i++) {
		/* only check fd 0, 1, and 2 */
		if (fd_dup2_list[i].fd_src >= 0 && fd_dup2_list[i].fd_src <= 2) {
			fd        = fd_dup2_list[i].fd_src;
			idx       = fd_dup2_list[i].fd_dest - FD_FILE_BASE;
			offset    = d_file_list[idx]->offset;
			open_flag = d_file_list[idx]->open_flag;

			/* get a real fd from kernel */
			fd_tmp = libc_open(d_file_list[idx]->path, open_flag);
			if (fd_tmp < 0) {
				error_save = errno;
				fprintf(stderr, "Error: open %s failed. %d (%s)\n",
					d_file_list[idx]->path, errno, strerror(errno));
				D_GOTO(err, error_save);
			}
			/* using dup2() to make sure we get desired fd */
			fd_new = dup2(fd_tmp, fd);
			if (fd_new < 0 || fd_new != fd) {
				error_save = errno;
				fprintf(stderr, "Error: dup2 failed. %d (%s)\n", errno,
					strerror(errno));
				libc_close(fd_tmp);
				D_GOTO(err, error_save);
			}
			libc_close(fd_tmp);
			if (libc_lseek(fd, offset, SEEK_SET) == -1) {
				error_save = errno;
				fprintf(stderr, "Error: lseek failed to set offset. %d (%s)\n",
					errno, strerror(errno));
				libc_close(fd);
				D_GOTO(err, error_save);
			}
		}
	}
	D_RWLOCK_UNLOCK(&lock_fd_dup2ed);
	return 0;

err:
	D_RWLOCK_UNLOCK(&lock_fd_dup2ed);
	return error_save;
}

static int
reset_daos_env_before_exec(void)
{
	int rc;

	/* bash does fork(), then close opened files before exec(),
	 * so the fd for log file probably is invalid now.
	 */
	d_log_disable_logging();

	/* close fd 255 and fd_dummy before exec(). */
	if (fd_255_reserved) {
		libc_close(255);
		fd_255_reserved = false;
	}
	if (fd_dummy >= 0) {
		libc_close(fd_dummy);
		fd_dummy = -1;
	}

	rc = setup_fd_0_1_2();
	if (rc)
		return rc;

	if (context_reset) {
		destroy_all_eqs();
		daos_eq_lib_fini();
		d_daos_inited     = false;
		daos_debug_inited = false;
		context_reset     = false;
		/* all IO requests go through dfuse */
		d_hook_enabled    = false;
	}
	return 0;
}

/* Now we always make sure important pil4dfs env variables are appended in new child processes. */
int
execve(const char *filename, char *const argv[], char *const envp[])
{
	char **new_envp;
	int    rc;

	if (next_execve == NULL) {
		next_execve = dlsym(RTLD_NEXT, "execve");
		D_ASSERT(next_execve != NULL);
	}

	rc = pre_envp(envp, &new_envp);
	if (rc)
		goto err;

	if (bypass)
		return next_execve(filename, argv, new_envp);

	rc = reset_daos_env_before_exec();
	if (rc)
		goto err;

	rc = next_execve(filename, argv, new_envp);
	if (rc == -1)
		/* d_hook_enabled could be set false in reset_daos_env_before_exec(). Need to be
		 * restored if exec() failed for some reason.
		 */
		d_hook_enabled = true;
	return rc;

err:
	errno = rc;
	return (-1);
}

int
execvpe(const char *filename, char *const argv[], char *const envp[])
{
	char **new_envp;
	int    rc;

	if (next_execvpe == NULL) {
		next_execvpe = dlsym(RTLD_NEXT, "execvpe");
		D_ASSERT(next_execvpe != NULL);
	}

	rc = pre_envp(envp, &new_envp);
	if (rc)
		goto err;
	if (bypass)
		return next_execvpe(filename, argv, new_envp);

	rc = reset_daos_env_before_exec();
	if (rc)
		goto err;

	rc = next_execvpe(filename, argv, new_envp);
	if (rc == -1)
		d_hook_enabled = true;
	return rc;

err:
	errno = rc;
	return (-1);
}

int
execv(const char *filename, char *const argv[])
{
	int    rc;

	if (next_execv == NULL) {
		next_execv = dlsym(RTLD_NEXT, "execv");
		D_ASSERT(next_execv != NULL);
	}
	if (!d_hook_enabled)
		return next_execv(filename, argv);

	rc = reset_daos_env_before_exec();
	if (rc) {
		errno = rc;
		return (-1);
	}

	rc = next_execv(filename, argv);
	if (rc == -1)
		d_hook_enabled = true;
	return rc;
}

int
execvp(const char *filename, char *const argv[])
{
	int    rc;

	if (next_execvp == NULL) {
		next_execvp = dlsym(RTLD_NEXT, "execvp");
		D_ASSERT(next_execvp != NULL);
	}
	if (!d_hook_enabled)
		return next_execvp(filename, argv);

	rc = reset_daos_env_before_exec();
	if (rc) {
		errno = rc;
		return (-1);
	}

	rc = next_execvp(filename, argv);
	if (rc == -1)
		d_hook_enabled = true;
	return rc;
}

int
fexecve(int fd, char *const argv[], char *const envp[])
{
	char **new_envp;
	int    rc;

	if (next_fexecve == NULL) {
		next_fexecve = dlsym(RTLD_NEXT, "fexecve");
		D_ASSERT(next_fexecve != NULL);
	}

	rc = pre_envp(envp, &new_envp);
	if (rc)
		goto err;
	if (bypass)
		return next_fexecve(fd, argv, new_envp);

	rc = reset_daos_env_before_exec();
	if (rc)
		goto err;

	rc = next_fexecve(fd, argv, new_envp);
	if (rc == -1)
		d_hook_enabled = true;
	return rc;

err:
	errno = rc;
	return (-1);
}

pid_t
fork(void)
{
	pid_t pid;

	if (next_fork == NULL) {
		next_fork = dlsym(RTLD_NEXT, "fork");
		D_ASSERT(next_fork != NULL);
	}
	if (!d_hook_enabled)
		return next_fork();

	pid = next_fork();

	if (pid) {
		/* parent process: do nothing */
		return pid;
	} else {
		child_hdlr();
		return pid;
	}
}

int
mkdir(const char *path, mode_t mode)
{
	int                is_target_path, rc;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

	if (next_mkdir == NULL) {
		next_mkdir = dlsym(RTLD_NEXT, "mkdir");
		D_ASSERT(next_mkdir != NULL);
	}
	if (!d_hook_enabled)
		return next_mkdir(path, mode);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;
	atomic_fetch_add_relaxed(&num_mkdir, 1);

	if (!parent && (strncmp(item_name, "/", 2) == 0))
		D_GOTO(out_err, rc = EEXIST);

	rc = dfs_mkdir(dfs_mt->dfs, drec2obj(parent), item_name, mode & mode_not_umask, 0);
	if (rc)
		D_GOTO(out_err, rc);

	drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return 0;

out_org:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return next_mkdir(path, mode);

out_err:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
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
	if (!d_hook_enabled)
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
	int                is_target_path, rc;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

	if (next_rmdir == NULL) {
		next_rmdir = dlsym(RTLD_NEXT, "rmdir");
		D_ASSERT(next_rmdir != NULL);
	}
	if (!d_hook_enabled)
		return next_rmdir(path);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;
	atomic_fetch_add_relaxed(&num_rmdir, 1);

	rc = dfs_remove(dfs_mt->dfs, drec2obj(parent), item_name, false, NULL);
	if (rc)
		D_GOTO(out_err, rc);

	if (parent != NULL) {
		rc = drec_del(dfs_mt->dcache, full_path, parent);
		if (rc != -DER_SUCCESS && rc != -DER_NONEXIST)
			DL_ERROR(rc, "DAOS directory cache cleanup failed");
	}

	drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return 0;

out_org:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return next_rmdir(path);

out_err:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

int
symlink(const char *symvalue, const char *path)
{
	int                is_target_path, rc;
	dfs_obj_t         *obj;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

	if (next_symlink == NULL) {
		next_symlink = dlsym(RTLD_NEXT, "symlink");
		D_ASSERT(next_symlink != NULL);
	}
	if (!d_hook_enabled)
		return next_symlink(symvalue, path);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;

	rc = dfs_open(dfs_mt->dfs, drec2obj(parent), item_name, S_IFLNK, O_CREAT | O_EXCL, 0, 0,
		      symvalue, &obj);
	if (rc)
		goto out_err;
	rc = dfs_release(obj);
	if (rc)
		goto out_err;
	atomic_fetch_add_relaxed(&num_link, 1);

	drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return 0;

out_org:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return next_symlink(symvalue, path);

out_err:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
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
	if (!d_hook_enabled)
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
new_readlink(const char *path, char *buf, size_t size)
{
	int                is_target_path, rc, rc2;
	dfs_obj_t         *obj;
	daos_size_t        str_len = size;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

	if (!d_hook_enabled)
		return libc_readlink(path, buf, size);

	rc =
	    query_path(path, &is_target_path, &parent, item_name, &parent_dir, &full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;

	atomic_fetch_add_relaxed(&num_rdlink, 1);
	rc = dfs_lookup_rel(dfs_mt->dfs, drec2obj(parent), item_name, O_RDONLY | O_NOFOLLOW, &obj,
			    NULL, NULL);
	if (rc)
		goto out_err;
	rc = dfs_get_symlink_value(obj, buf, &str_len);
	if (rc)
		goto out_release;
	rc = dfs_release(obj);
	if (rc)
		goto out_err;
	drec_decref(dfs_mt->dcache, parent);
	/* The NULL at the end should not be included in the length */
	FREE(parent_dir);
	return (str_len - 1);

out_org:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return libc_readlink(path, buf, size);

out_release:
	rc2 = dfs_release(obj);
	if (rc2)
		DS_ERROR(rc2, "dfs_release() failed");

out_err:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
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
	if (!d_hook_enabled)
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

int
rename(const char *old_name, const char *new_name)
{
	int                is_target_path1, is_target_path2, rc = -1;
	char               item_name_old[DFS_MAX_NAME], item_name_new[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt1        = NULL;
	struct dfs_mt     *dfs_mt2        = NULL;
	struct dcache_rec *parent_old     = NULL;
	char              *parent_dir_old = NULL;
	char              *full_path_old  = NULL;
	struct dcache_rec *parent_new     = NULL;
	char              *parent_dir_new = NULL;
	char              *full_path_new  = NULL;

	if (next_rename == NULL) {
		next_rename = dlsym(RTLD_NEXT, "rename");
		D_ASSERT(next_rename != NULL);
	}
	if (!d_hook_enabled)
		D_GOTO(out_org, rc);

	rc = query_path(old_name, &is_target_path1, &parent_old, item_name_old,
			&parent_dir_old, &full_path_old, &dfs_mt1);
	if (rc)
		D_GOTO(out_err, rc);

	rc = query_path(new_name, &is_target_path2, &parent_new, item_name_new,
			&parent_dir_new, &full_path_new, &dfs_mt2);
	if (rc)
		D_GOTO(out_err, rc);

	if (is_target_path1 == 0 && is_target_path2 == 0)
		D_GOTO(out_org, rc);

	if (is_target_path1 != is_target_path2 || dfs_mt1 != dfs_mt2)
		D_GOTO(out_err, rc = EXDEV);

	atomic_fetch_add_relaxed(&num_rename, 1);

	/* Both old and new are on DAOS */
	rc = dfs_move(dfs_mt1->dfs, drec2obj(parent_old), item_name_old, drec2obj(parent_new),
		      item_name_new, NULL);
	if (rc)
		D_GOTO(out_err, rc);

	if (parent_old != NULL) {
		rc = drec_del(dfs_mt1->dcache, full_path_old, parent_old);
		if (rc != -DER_SUCCESS && rc != -DER_NONEXIST)
			DL_ERROR(rc, "DAOS directory cache cleanup failed");
	}

	drec_decref(dfs_mt1->dcache, parent_old);
	FREE(parent_dir_old);
	drec_decref(dfs_mt2->dcache, parent_new);
	FREE(parent_dir_new);
	return 0;

out_org:
	if (parent_old != NULL)
		drec_decref(dfs_mt1->dcache, parent_old);
	FREE(parent_dir_old);
	if (parent_new != NULL)
		drec_decref(dfs_mt2->dcache, parent_new);
	FREE(parent_dir_new);
	return next_rename(old_name, new_name);

out_err:
	if (parent_old != NULL)
		drec_decref(dfs_mt1->dcache, parent_old);
	FREE(parent_dir_old);
	if (parent_new != NULL)
		drec_decref(dfs_mt2->dcache, parent_new);
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

	if (!d_hook_enabled)
		return next_getcwd(buf, size);

	if (cur_dir[0] != '/')
		update_cwd();

	if (query_dfs_mount(cur_dir) < 0)
		return next_getcwd(buf, size);

	if (buf == NULL) {
		size_t len;

		if (size == 0)
			size = PATH_MAX;
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
	int fd_directed;

	if (next_isatty == NULL) {
		next_isatty = dlsym(RTLD_NEXT, "isatty");
		D_ASSERT(next_isatty != NULL);
	}
	if (!d_hook_enabled)
		return next_isatty(fd);
	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed >= FD_FILE_BASE) {
		/* non-terminal */
		errno = ENOTTY;
		return 0;
	} else {
		return next_isatty(fd);
	}
}

int
__isatty(int fd) __attribute__((alias("isatty"), leaf, nothrow));

int
access(const char *path, int mode)
{
	int                rc, is_target_path;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

	if (next_access == NULL) {
		next_access = dlsym(RTLD_NEXT, "access");
		D_ASSERT(next_access != NULL);
	}
	if (!d_hook_enabled)
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
		rc = dfs_access(dfs_mt->dfs, drec2obj(parent), item_name, mode);
	if (rc)
		D_GOTO(out_err, rc);

	drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return 0;

out_org:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return next_access(path, mode);

out_err:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
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
	if (!d_hook_enabled)
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
	int                is_target_path, rc, len_str;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

	if (next_chdir == NULL) {
		next_chdir = dlsym(RTLD_NEXT, "chdir");
		D_ASSERT(next_chdir != NULL);
	}
	if (!d_hook_enabled)
		return next_chdir(path);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);

	rc = next_chdir(path);
	if (rc)
		D_GOTO(out_err, rc = errno);

	if (!is_target_path) {
		len_str = snprintf(cur_dir, DFS_MAX_PATH, "%s", full_path);
		if (len_str >= DFS_MAX_PATH) {
			D_DEBUG(DB_ANY, "path is too long: %d (%s)\n", ENAMETOOLONG,
				strerror(ENAMETOOLONG));
			D_GOTO(out_err, rc = ENAMETOOLONG);
		}
		D_GOTO(out, rc);
	}

	/* assuming the path exists and it is backed by dfuse */
	len_str = snprintf(cur_dir, DFS_MAX_PATH, "%s%s", dfs_mt->fs_root, full_path);
	if (len_str >= DFS_MAX_PATH) {
		D_DEBUG(DB_ANY, "path is too long: %d (%s)\n", ENAMETOOLONG,
			strerror(ENAMETOOLONG));
		D_GOTO(out_err, rc = ENAMETOOLONG);
	}

out:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return 0;

out_err:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

int
fchdir(int dirfd)
{
	int   rc, fd_directed;
	char *pt_end = NULL;

	if (next_fchdir == NULL) {
		next_fchdir = dlsym(RTLD_NEXT, "fchdir");
		D_ASSERT(next_fchdir != NULL);
	}
	if (!d_hook_enabled)
		return next_fchdir(dirfd);

	fd_directed = d_get_fd_redirected(dirfd);
	if (fd_directed < FD_DIR_BASE)
		return next_fchdir(dirfd);

	/* assume dfuse is running. call chdir() to update cwd. */
	if (next_chdir == NULL) {
		next_chdir = dlsym(RTLD_NEXT, "chdir");
		D_ASSERT(next_chdir != NULL);
	}
	rc = next_chdir(dir_list[fd_directed - FD_DIR_BASE]->path);
	if (rc)
		return rc;

	pt_end = stpncpy(cur_dir, dir_list[fd_directed - FD_DIR_BASE]->path, DFS_MAX_PATH - 1);
	if ((long int)(pt_end - cur_dir) >= DFS_MAX_PATH - 1) {
		D_DEBUG(DB_ANY, "path is too long: %d (%s)\n", ENAMETOOLONG,
			strerror(ENAMETOOLONG));
		errno = ENAMETOOLONG;
		return (-1);
	}
	return 0;
}

static int
new_unlink(const char *path)
{
	int                is_target_path, rc;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

	if (!d_hook_enabled)
		return libc_unlink(path);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;

	atomic_fetch_add_relaxed(&num_unlink, 1);

	rc = dfs_remove(dfs_mt->dfs, drec2obj(parent), item_name, false, NULL);
	if (rc)
		D_GOTO(out_err, rc);

	if (parent != NULL) {
		rc = drec_del(dfs_mt->dcache, full_path, parent);
		if (rc != -DER_SUCCESS && rc != -DER_NONEXIST)
			DL_ERROR(rc, "DAOS directory cache cleanup failed");
	}

	drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return 0;

out_org:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return libc_unlink(path);

out_err:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

int
unlinkat(int dirfd, const char *path, int flags)
{
	int                is_target_path, rc, error = 0;
	int                idx_dfs;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt          = NULL;
	char              *full_path       = NULL;
	struct dcache_rec *parent          = NULL;
	char              *parent_dir      = NULL;
	char              *full_path_dummy = NULL;

	if (next_unlinkat == NULL) {
		next_unlinkat = dlsym(RTLD_NEXT, "unlinkat");
		D_ASSERT(next_unlinkat != NULL);
	}
	if (!d_hook_enabled)
		return next_unlinkat(dirfd, path, flags);

	if (path[0] == '/') {
		/* absolute path, dirfd is ignored */
		rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
				&full_path_dummy, &dfs_mt);
		if (rc)
			D_GOTO(out_err, rc);
		if (!is_target_path)
			goto out_org;
		atomic_fetch_add_relaxed(&num_unlink, 1);

		rc = dfs_remove(dfs_mt->dfs, drec2obj(parent), item_name, false, NULL);
		if (rc)
			D_GOTO(out_err_abs, rc);

		if (parent != NULL) {
			rc = drec_del(dfs_mt->dcache, full_path_dummy, parent);
			if (rc != -DER_SUCCESS && rc != -DER_NONEXIST)
				DL_ERROR(rc, "DAOS directory cache cleanup failed");
		}

		drec_decref(dfs_mt->dcache, parent);
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
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return next_unlinkat(dirfd, path, flags);

out_err_abs:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
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
	int fd_directed;

	if (next_fsync == NULL) {
		next_fsync = dlsym(RTLD_NEXT, "fsync");
		D_ASSERT(next_fsync != NULL);
	}
	if (!d_hook_enabled)
		return next_fsync(fd);

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_fsync(fd);

	if (fd < FD_DIR_BASE && d_compatible_mode)
		return next_fsync(fd);

	/* errno = ENOTSUP;
	 * return (-1);
	 */
	return 0;
}

int
fdatasync(int fd)
{
	int fd_directed;

	if (next_fdatasync == NULL) {
		next_fdatasync = dlsym(RTLD_NEXT, "fdatasync");
		D_ASSERT(next_fdatasync != NULL);
	}
	if (!d_hook_enabled)
		return next_fdatasync(fd);

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_fdatasync(fd);

	if (fd < FD_DIR_BASE && d_compatible_mode)
		return next_fdatasync(fd);

	return 0;
}

int
ftruncate(int fd, off_t length)
{
	int rc, fd_directed;

	if (next_ftruncate == NULL) {
		next_ftruncate = dlsym(RTLD_NEXT, "ftruncate");
		D_ASSERT(next_ftruncate != NULL);
	}
	if (!d_hook_enabled)
		return next_ftruncate(fd, length);
	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_ftruncate(fd, length);

	if (fd_directed >= FD_DIR_BASE) {
		errno = EINVAL;
		return (-1);
	}

	rc = dfs_punch(d_file_list[fd_directed - FD_FILE_BASE]->dfs_mt->dfs,
		       d_file_list[fd_directed - FD_FILE_BASE]->file, length, DFS_MAX_FSIZE);
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
	int                is_target_path, rc, rc2;
	dfs_obj_t         *file_obj;
	mode_t             mode;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

	if (next_truncate == NULL) {
		next_truncate = dlsym(RTLD_NEXT, "truncate");
		D_ASSERT(next_truncate != NULL);
	}
	if (!d_hook_enabled)
		return next_truncate(path, length);

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path)
		goto out_org;

	rc = dfs_open(dfs_mt->dfs, drec2obj(parent), item_name, S_IFREG, O_RDWR, 0, 0, NULL,
		      &file_obj);
	if (rc)
		D_GOTO(out_err, rc);
	if (!S_ISREG(mode)) {
		D_DEBUG(DB_ANY, "%s is not a regular file: %d (%s)\n", path, EISDIR,
			strerror(EISDIR));
		D_GOTO(out_err, rc = EISDIR);
	}
	rc = dfs_punch(dfs_mt->dfs, file_obj, length, DFS_MAX_FSIZE);
	rc2 = dfs_release(file_obj);
	if (rc)
		D_GOTO(out_err, rc);
	if (rc2)
		D_GOTO(out_err, rc = rc2);

	drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return 0;

out_org:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return next_truncate(path, length);

out_err:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

static int
chmod_with_flag(const char *path, mode_t mode, int flag)
{
	int                rc, is_target_path;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

	if (next_chmod == NULL) {
		next_chmod = dlsym(RTLD_NEXT, "chmod");
		D_ASSERT(next_chmod != NULL);
	}

	if (!d_hook_enabled)
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
		rc = dfs_chmod(dfs_mt->dfs, drec2obj(parent), item_name, mode);
	if (rc)
		D_GOTO(out_err, rc);

	drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return 0;

out_org:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return next_chmod(path, mode);

out_err:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
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

	if (!d_hook_enabled)
		return next_chmod(path, mode);

	return chmod_with_flag(path, mode, 0);
}

int
fchmod(int fd, mode_t mode)
{
	int rc, fd_directed;

	if (next_fchmod == NULL) {
		next_fchmod = dlsym(RTLD_NEXT, "fchmod");
		D_ASSERT(next_fchmod != NULL);
	}

	if (!d_hook_enabled)
		return next_fchmod(fd, mode);
	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_fchmod(fd, mode);

	if (fd_directed >= FD_DIR_BASE) {
		errno = EINVAL;
		return (-1);
	}

	rc = dfs_chmod(d_file_list[fd_directed - FD_FILE_BASE]->dfs_mt->dfs,
		       drec2obj(d_file_list[fd_directed - FD_FILE_BASE]->parent),
		       d_file_list[fd_directed - FD_FILE_BASE]->item_name, mode);
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

	if (!d_hook_enabled)
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
	int                is_target_path, rc;
	dfs_obj_t         *obj;
	mode_t             mode;
	struct stat        stbuf;
	struct timespec    times_loc;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

	if (next_utime == NULL) {
		next_utime = dlsym(RTLD_NEXT, "utime");
		D_ASSERT(next_utime != NULL);
	}
	if (!d_hook_enabled)
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
		rc = dfs_lookup_rel(dfs_mt->dfs, drec2obj(parent), item_name, O_RDWR, &obj, &mode,
				    &stbuf);
	if (rc) {
		DS_ERROR(rc, "fail to lookup %s", full_path);
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

	rc = dfs_osetattr(dfs_mt->dfs, obj, &stbuf, DFS_SET_ATTR_MTIME);
	if (rc) {
		dfs_release(obj);
		D_GOTO(out_err, rc);
	}

	rc = dfs_release(obj);
	if (rc)
		D_GOTO(out_err, rc);

	drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return 0;

out_org:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return next_utime(path, times);

out_err:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

int
utimes(const char *path, const struct timeval times[2])
{
	int                is_target_path, rc;
	dfs_obj_t         *obj;
	mode_t             mode;
	struct stat        stbuf;
	struct timespec    times_loc;
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

	if (next_utimes == NULL) {
		next_utimes = dlsym(RTLD_NEXT, "utimes");
		D_ASSERT(next_utimes != NULL);
	}
	if (!d_hook_enabled)
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
		rc = dfs_lookup_rel(dfs_mt->dfs, drec2obj(parent), item_name, O_RDWR, &obj, &mode,
				    &stbuf);
	if (rc) {
		DS_ERROR(rc, "fail to lookup %s", full_path);
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

	rc = dfs_osetattr(dfs_mt->dfs, obj, &stbuf, DFS_SET_ATTR_MTIME);
	if (rc) {
		DS_ERROR(rc, "dfs_osetattr() failed");
		dfs_release(obj);
		D_GOTO(out_err, rc);
	}

	rc = dfs_release(obj);
	if (rc)
		D_GOTO(out_err, rc);

	drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return 0;

out_org:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return next_utimes(path, times);

out_err:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	errno = rc;
	return (-1);
}

static int
utimens_timespec(const char *path, const struct timespec times[2], int flags)
{
	int                is_target_path, rc;
	dfs_obj_t         *obj;
	mode_t             mode;
	struct stat        stbuf;
	struct timespec    times_loc;
	struct timeval     times_us[2];
	char               item_name[DFS_MAX_NAME];
	struct dfs_mt     *dfs_mt     = NULL;
	struct dcache_rec *parent     = NULL;
	char              *parent_dir = NULL;
	char              *full_path  = NULL;

	rc = query_path(path, &is_target_path, &parent, item_name, &parent_dir,
			&full_path, &dfs_mt);
	if (rc)
		D_GOTO(out_err, rc);
	if (!is_target_path) {
		if (next_utimes == NULL) {
			next_utimes = dlsym(RTLD_NEXT, "utimes");
			D_ASSERT(next_utimes != NULL);
		}
		times_us[0].tv_sec  = times[0].tv_sec;
		times_us[0].tv_usec = times[0].tv_nsec / 1000;
		times_us[1].tv_sec  = times[1].tv_sec;
		times_us[1].tv_usec = times[1].tv_nsec / 1000;
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
		rc = dfs_lookup_rel(dfs_mt->dfs, drec2obj(parent), item_name, flags, &obj, &mode,
				    &stbuf);
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

	rc = dfs_osetattr(dfs_mt->dfs, obj, &stbuf, DFS_SET_ATTR_MTIME);
	if (rc) {
		dfs_release(obj);
		D_GOTO(out_err, rc);
	}

	rc = dfs_release(obj);
	if (rc)
		D_GOTO(out_err, rc);

	drec_decref(dfs_mt->dcache, parent);
	FREE(parent_dir);
	return 0;

out_err:
	if (parent != NULL)
		drec_decref(dfs_mt->dcache, parent);
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
	if (!d_hook_enabled)
		return next_utimensat(dirfd, path, times, flags);

	_Pragma("GCC diagnostic push")
	_Pragma("GCC diagnostic ignored \"-Wnonnull-compare\"")
	/* check path is NULL or not. path could be NULL since it is from application */
	if (path == NULL) {
		errno = EFAULT;
		return -1;
	}
	_Pragma("GCC diagnostic pop")

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
	int             fd_directed;
	struct timespec times_loc;
	struct stat     stbuf;

	if (next_futimens == NULL) {
		next_futimens = dlsym(RTLD_NEXT, "futimens");
		D_ASSERT(next_futimens != NULL);
	}
	if (!d_hook_enabled)
		return next_futimens(fd, times);
	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
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

	rc = dfs_osetattr(d_file_list[fd_directed - FD_FILE_BASE]->dfs_mt->dfs,
			  d_file_list[fd_directed - FD_FILE_BASE]->file, &stbuf,
			  DFS_SET_ATTR_MTIME);
	if (rc) {
		errno = rc;
		return (-1);
	}

	return 0;
}

/* The macro was added to fix the compiling issue on CentOS 7.9.
 * Those issues could be resolved by adding -D_FILE_OFFSET_BITS=64, however
 * this flag causes other issues too.
 */
#ifndef F_ADD_SEALS
#define F_OFD_GETLK	36
#define F_OFD_SETLK	37
#define F_OFD_SETLKW	38
#define F_ADD_SEALS	1033
#endif

static int
new_fcntl(int fd, int cmd, ...)
{
	int     fd_directed, param, OrgFunc = 1;
	int     next_dirfd, next_fd, rc;
	va_list arg;

	va_start(arg, cmd);
	param = va_arg(arg, int);
	va_end(arg);

	if (fd < FD_FILE_BASE && d_compatible_mode)
		return libc_fcntl(fd, cmd, param);

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
		fd_directed = d_get_fd_redirected(fd);

		if (!d_hook_enabled)
			return libc_fcntl(fd, cmd, param);

		if (cmd == F_GETFL) {
			if (fd_directed >= FD_DIR_BASE)
				return dir_list[fd_directed - FD_DIR_BASE]->open_flag;
			else if (fd_directed >= FD_FILE_BASE)
				return d_file_list[fd_directed - FD_FILE_BASE]->open_flag;
			else
				return libc_fcntl(fd, cmd);
		}

		if (fd_directed >= FD_FILE_BASE) {
			if (cmd == F_SETFD)
				return 0;
		}

		if (fd_directed >= FD_FILE_BASE)
			OrgFunc = 0;

		if ((cmd == F_DUPFD) || (cmd == F_DUPFD_CLOEXEC)) {
			if (fd_directed >= FD_DIR_BASE) {
				rc = find_next_available_dirfd(
					dir_list[fd_directed - FD_DIR_BASE], &next_dirfd);
				if (rc) {
					errno = rc;
					return (-1);
				}
				return (next_dirfd + FD_DIR_BASE);
			} else if (fd_directed >= FD_FILE_BASE) {
				rc = find_next_available_fd(
					d_file_list[fd_directed - FD_FILE_BASE], &next_fd);
				if (rc) {
					errno = rc;
					return (-1);
				}
				return (next_fd + FD_FILE_BASE);
			}
		} else if ((cmd == F_GETFD) || (cmd == F_SETFD)) {
			if (OrgFunc == 0)
				return 0;
		}
		return libc_fcntl(fd, cmd, param);
	case F_SETLK:
	case F_SETLKW:
	case F_GETLK:
	case F_OFD_SETLK:
	case F_OFD_SETLKW:
	case F_OFD_GETLK:
	case F_GETOWN_EX:
	case F_SETOWN_EX:
		if (!d_hook_enabled)
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
	int                             fd_directed;

	va_start(arg, request);
	param = va_arg(arg, void *);
	va_end(arg);

	if (next_ioctl == NULL) {
		next_ioctl = dlsym(RTLD_NEXT, "ioctl");
		D_ASSERT(next_ioctl != NULL);
	}
	if (!d_hook_enabled)
		return next_ioctl(fd, request, param);

	/* To pass existing test of ioctl() with DFUSE_IOCTL_DFUSE_USER */
	/* Provided to pass dfuse_test                                  */
	if ((request & 0xFFFFFFFF) == 0x8008A3cA) {
		reply = (struct dfuse_user_reply *)param;
		reply->uid = getuid();
		reply->gid = getgid();
		return 0;
	}

	if (fd < FD_FILE_BASE && d_compatible_mode)
		return next_ioctl(fd, request, param);

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_ioctl(fd, request, param);

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
	if (!d_hook_enabled)
		return next_dup(oldfd);
	fd_directed = d_get_fd_redirected(oldfd);
	if (fd_directed >= FD_FILE_BASE)
		return new_fcntl(oldfd, F_DUPFD, 0);

	return next_dup(fd_directed);
}

int
dup2(int oldfd, int newfd)
{
	int fd_kernel, oldfd_directed, newfd_directed, fd_directed, next_fd, next_dirfd;
	int idx, rc, errno_save;

	/* Need more work later. */
	if (next_dup2 == NULL) {
		next_dup2 = dlsym(RTLD_NEXT, "dup2");
		D_ASSERT(next_dup2 != NULL);
	}
	if (!d_hook_enabled)
		return next_dup2(oldfd, newfd);

	if (d_compatible_mode) {
		struct ht_fd *fd_ht_obj = NULL;
		int           fd_fake   = -1;

		D_ASSERT(oldfd < FD_FILE_BASE && newfd < FD_FILE_BASE);
		/* next_dup2(oldfd, newfd) will close newfd, so we need to check whether
		 * newfd is in fd_hash. Remove it in case existing. */
		remove_fd_compatible(newfd);
		fd_kernel = next_dup2(oldfd, newfd);
		if (fd_kernel < 0)
			return (-1);
		fd_directed = d_get_fd_redirected(oldfd);
		if (fd_directed < FD_FILE_BASE) {
			return fd_kernel;
		} else if (fd_directed < FD_DIR_BASE) {
			rc = find_next_available_fd(d_file_list[fd_directed - FD_FILE_BASE],
						    &next_fd);
			if (rc)
				/* still return the fd dup from kernel in compatible mode */
				return fd_kernel;
			fd_fake = next_fd + FD_FILE_BASE;
		} else {
			rc = find_next_available_dirfd(dir_list[fd_directed - FD_DIR_BASE],
						       &next_dirfd);
			if (rc)
				/* still return the fd dup from kernel in
				 * compatible mode
				 */
				return fd_kernel;
			fd_fake = next_dirfd + FD_DIR_BASE;
		}
		if (fd_fake < 0)
			return fd_kernel;
		/* add fd_kernel to hash table */
		D_ALLOC_PTR(fd_ht_obj);
		if (fd_ht_obj == NULL) {
			if (fd_fake >= FD_DIR_BASE)
				free_dirfd(next_dirfd);
			else
				free_fd(next_fd, false);
			return fd_kernel;
		}
		fd_ht_obj->real_fd = fd_kernel;
		fd_ht_obj->fake_fd = fd_fake;
		rc = d_hash_rec_insert(fd_hash, &fd_ht_obj->real_fd, sizeof(int), &fd_ht_obj->entry,
				       false);
		D_ASSERT(rc == 0);
		return fd_kernel;
	}

	if (oldfd == newfd) {
		if (oldfd < FD_FILE_BASE)
			return next_dup2(oldfd, newfd);
		else
			return newfd;
	}
	oldfd_directed = query_fd_forward_dest(oldfd);
	newfd_directed = query_fd_forward_dest(newfd);
	if ((oldfd_directed < FD_FILE_BASE) && (oldfd < FD_FILE_BASE) &&
	    (newfd_directed < FD_FILE_BASE) && (newfd < FD_FILE_BASE))
		return next_dup2(oldfd, newfd);

	if (oldfd_directed >= FD_FILE_BASE && oldfd < FD_FILE_BASE)
		oldfd = oldfd_directed;

	if (newfd >= FD_FILE_BASE) {
		DS_ERROR(ENOTSUP, "unimplemented yet for newfd >= FD_FILE_BASE");
		errno = ENOTSUP;
		return -1;
	}
	fd_directed = query_fd_forward_dest(newfd);
	if (fd_directed >= FD_FILE_BASE && newfd < FD_FILE_BASE && oldfd_directed < FD_FILE_BASE &&
	    oldfd < FD_FILE_BASE) {
		/* need to remove newfd from forward list and decrease refcount in d_file_list[] */
		close_dup_fd(libc_close, newfd, false);
		return next_dup2(oldfd, newfd);
	} else if (fd_directed >= FD_FILE_BASE) {
		DS_ERROR(ENOTSUP, "unimplemented yet for fd_directed >= FD_FILE_BASE");
		errno = ENOTSUP;
		return -1;
	}

	if (oldfd >= FD_FILE_BASE)
		fd_directed = oldfd;
	else
		fd_directed = query_fd_forward_dest(oldfd);
	if (fd_directed >= FD_FILE_BASE) {
		int fd_tmp;

		fd_tmp = allocate_a_fd_from_kernel();
		if (fd_tmp < 0) {
			/* failed to allocate an fd from kernel */
			errno_save = errno;
			DS_ERROR(errno_save, "failed to get a fd from kernel");
			errno = errno_save;
			return (-1);
		}
		/* rely on dup2() to get the desired fd */
		fd_kernel = next_dup2(fd_tmp, newfd);
		if (fd_kernel < 0) {
			/* failed to allocate an fd from kernel */
			errno_save = errno;
			close(fd_tmp);
			DS_ERROR(errno_save, "failed to get a fd from kernel");
			errno = errno_save;
			return (-1);
		} else if (fd_kernel != newfd) {
			close(fd_kernel);
			DS_ERROR(EBUSY, "failed to get the desired fd in dup2()");
			errno = EBUSY;
			return (-1);
		}
		rc = libc_close(fd_tmp);
		if (rc != 0)
			return -1;
		idx = allocate_dup2ed_fd(fd_kernel, fd_directed);
		if (idx >= 0)
			return fd_kernel;
		else
			return idx;
	}
	return -1;
}

int
__dup2(int oldfd, int newfd) __attribute__((alias("dup2"), leaf, nothrow));

int
new_dup3(int oldfd, int newfd, int flags)
{
	if (oldfd == newfd) {
		errno = EINVAL;
		return (-1);
	}

	if (!d_hook_enabled)
		return libc_dup3(oldfd, newfd, flags);

	if (d_get_fd_redirected(oldfd) < FD_FILE_BASE && d_get_fd_redirected(newfd) < FD_FILE_BASE)
		return libc_dup3(oldfd, newfd, flags);

	/* Ignore flags now. Need more work later to handle flags, e.g., O_CLOEXEC */
	return dup2(oldfd, newfd);
}

void *
new_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	int             rc, idx_map, fd_directed;
	struct stat     stat_buf;
	void            *addr_ret;

	if (!d_hook_enabled)
		return next_mmap(addr, length, prot, flags, fd, offset);

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_mmap(addr, length, prot, flags, fd, offset);

	atomic_fetch_add_relaxed(&num_mmap, 1);

	addr_ret = next_mmap(addr, length, prot, flags | MAP_ANONYMOUS, -1, offset);
	if (addr_ret == MAP_FAILED)
		return MAP_FAILED;

	rc = dfs_ostat(d_file_list[fd_directed - FD_FILE_BASE]->dfs_mt->dfs,
		       d_file_list[fd_directed - FD_FILE_BASE]->file, &stat_buf);
	if (rc) {
		errno = rc;
		return MAP_FAILED;
	}

	rc = find_next_available_map(&idx_map);
	if (rc) {
		DS_ERROR(rc, "mmap_list is out of space");
		errno = rc;
		return MAP_FAILED;
	}

	d_file_list[fd_directed - FD_FILE_BASE]->idx_mmap = idx_map;
	mmap_list[idx_map].addr = (char *)addr_ret;
	mmap_list[idx_map].length = length;
	mmap_list[idx_map].file_size = stat_buf.st_size;
	mmap_list[idx_map].prot = prot;
	mmap_list[idx_map].flags = flags;
	mmap_list[idx_map].fd = fd_directed;
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
		D_MUTEX_LOCK(&lock_mmap);
		register_handler(SIGSEGV, &old_segv);
		segv_handler_inited = true;
		D_MUTEX_UNLOCK(&lock_mmap);
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
		rc          = dfs_write(d_file_list[idx_file]->dfs_mt->dfs,
					d_file_list[idx_file]->file, &sgl, addr_min, NULL);
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

	if (!d_hook_enabled)
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
	int fd_directed;

	if (next_posix_fadvise == NULL) {
		next_posix_fadvise = dlsym(RTLD_NEXT, "posix_fadvise");
		D_ASSERT(next_posix_fadvise != NULL);
	}
	if (!d_hook_enabled)
		return next_posix_fadvise(fd, offset, len, advice);
	if (fd < FD_FILE_BASE && d_compatible_mode)
		return next_posix_fadvise(fd, offset, len, advice);

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_posix_fadvise(fd, offset, len, advice);

	/* Hint to turn off caching. */
	if (advice == POSIX_FADV_DONTNEED)
		return 0;
	/**
	 *	if (report)
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
	int fd_directed;

	if (next_flock == NULL) {
		next_flock = dlsym(RTLD_NEXT, "flock");
		D_ASSERT(next_flock != NULL);
	}
	if (!d_hook_enabled)
		return next_flock(fd, operation);

	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_flock(fd, operation);
	if (d_compatible_mode && fd < FD_FILE_BASE)
		return next_flock(fd, operation);

	/* We output the message only if env "D_IL_REPORT" is set. */
	if (report)
		DS_ERROR(ENOTSUP, "flock() is not implemented yet");
	errno = ENOTSUP;
	return -1;
}

int
fallocate(int fd, int mode, off_t offset, off_t len)
{
	int fd_directed;

	if (next_fallocate == NULL) {
		next_fallocate = dlsym(RTLD_NEXT, "fallocate");
		D_ASSERT(next_fallocate != NULL);
	}
	if (!d_hook_enabled)
		return next_fallocate(fd, mode, offset, len);
	if (fd < FD_FILE_BASE && d_compatible_mode)
		return next_fallocate(fd, mode, offset, len);
	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_fallocate(fd, mode, offset, len);

	/* We output the message only if env "D_IL_REPORT" is set. */
	if (report)
		DS_ERROR(ENOTSUP, "fallocate() is not implemented yet");
	errno = ENOTSUP;
	return -1;
}

int
posix_fallocate(int fd, off_t offset, off_t len)
{
	int fd_directed;

	if (next_posix_fallocate == NULL) {
		next_posix_fallocate = dlsym(RTLD_NEXT, "posix_fallocate");
		D_ASSERT(next_posix_fallocate != NULL);
	}
	if (!d_hook_enabled)
		return next_posix_fallocate(fd, offset, len);
	if (fd < FD_FILE_BASE && d_compatible_mode)
		return next_posix_fallocate(fd, offset, len);
	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_posix_fallocate(fd, offset, len);

	/* We output the message only if env "D_IL_REPORT" is set. */
	if (report)
		DS_ERROR(ENOTSUP, "posix_fallocate() is not implemented yet");
	errno = ENOTSUP;
	return -1;
}

int
posix_fallocate64(int fd, off64_t offset, off64_t len)
{
	int fd_directed;

	if (next_posix_fallocate64 == NULL) {
		next_posix_fallocate64 = dlsym(RTLD_NEXT, "posix_fallocate64");
		D_ASSERT(next_posix_fallocate64 != NULL);
	}
	if (!d_hook_enabled)
		return next_posix_fallocate64(fd, offset, len);
	if (fd < FD_FILE_BASE && d_compatible_mode)
		return next_posix_fallocate64(fd, offset, len);
	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_posix_fallocate64(fd, offset, len);

	/* We output the message only if env "D_IL_REPORT" is set. */
	if (report)
		DS_ERROR(ENOTSUP, "posix_fallocate64() is not implemented yet");
	errno = ENOTSUP;
	return -1;
}

int
tcgetattr(int fd, void *termios_p)
{
	int fd_directed;

	if (next_tcgetattr == NULL) {
		next_tcgetattr = dlsym(RTLD_NEXT, "tcgetattr");
		D_ASSERT(next_tcgetattr != NULL);
	}
	if (!d_hook_enabled)
		return next_tcgetattr(fd, termios_p);
	if (fd < FD_FILE_BASE && d_compatible_mode)
		return next_tcgetattr(fd, termios_p);
	fd_directed = d_get_fd_redirected(fd);
	if (fd_directed < FD_FILE_BASE)
		return next_tcgetattr(fd, termios_p);

	/* We output the message only if env "D_IL_REPORT" is set. */
	if (report)
		DS_ERROR(ENOTSUP, "tcgetattr() is not implemented yet");
	errno = ENOTSUP;
	return -1;
}

static void
new_exit(int rc)
{
	if (!d_hook_enabled)
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
		D_FATAL("fatal error to get CWD with get_current_dir_name(): %d (%s)\n", errno,
			strerror(errno));
		abort();
	} else {
		pt_end = stpncpy(cur_dir, cwd, DFS_MAX_PATH - 1);
		if ((long int)(pt_end - cur_dir) >= DFS_MAX_PATH - 1) {
			D_FATAL("fatal error, cwd path is too long:  %d (%s)\n", ENAMETOOLONG,
				strerror(ENAMETOOLONG));
			abort();
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
	ucontext_t             *context = ctx;

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
	}

	rc          = dfs_read(d_file_list[fd]->dfs_mt->dfs,
			       d_file_list[fd]->file, &sgl,
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
		D_FATAL("sigaction() failed: %d (%s)\n", errno, strerror(errno));
		abort();
	}
}

/* Check whether current executable is sh/bash or not. Flag is_bash is set. */
static inline void
check_exe_sh_bash(void)
{
	if (memcmp(exe_short_name, "bash", 5) == 0 || memcmp(exe_short_name, "sh", 3) == 0)
		is_bash = true;
}

#define CMDLINE_BUF_SIZE (2 * DFS_MAX_PATH + 2)

static void
extract_exe_name_1st_arg(void)
{
	FILE *f;
	int   readsize;
	int   count = 0;
	char *buf, *p, *end;

	f = fopen("/proc/self/cmdline", "r");
	if (f == NULL) {
		fprintf(stderr, "Fail to open file: /proc/self/cmdline. %d (%s)\n", errno,
			strerror(errno));
		exit(1);
	}
	buf = malloc(CMDLINE_BUF_SIZE);
	if (buf == NULL) {
		fprintf(stderr, "Fail to allocate memory for buf %d (%s)\n", errno,
			strerror(errno));
		exit(1);
	}
	readsize = fread(buf, 1, CMDLINE_BUF_SIZE, f);
	if (readsize <= 0) {
		fprintf(stderr, "Fail to read /proc/self/cmdline %d (%s)\n", errno,
			strerror(errno));
		fclose(f);
		exit(1);
	}

	fclose(f);

	exe_short_name = basename(buf);
	if (exe_short_name == NULL) {
		fprintf(stderr, "Fail to determine exe_short_name %d (%s)\n", errno,
			strerror(errno));
		exit(1);
	}
	exe_short_name = strndup(exe_short_name, DFS_MAX_NAME);
	if (exe_short_name == NULL) {
		printf("Fail to allocate exe_short_name %d (%s)\n", errno, strerror(errno));
		exit(1);
	}
	first_arg = NULL;
	end       = buf + readsize;

	for (p = buf; p < end;) {
		if (count == 1) {
			if (p[0] == '/' || memcmp(p, "./", 2) == 0 || memcmp(p, "../", 3) == 0) {
				/* Extract the first argument in command line */
				first_arg = basename(p);
				if (first_arg == NULL) {
					fprintf(stderr, "Fail to determine first_arg %d (%s)\n",
						errno, strerror(errno));
					exit(1);
				}
				first_arg = strndup(first_arg, DFS_MAX_NAME);
				if (first_arg == NULL) {
					fprintf(stderr, "Fail to allocate first_arg %d (%s)\n",
						errno, strerror(errno));
					exit(1);
				}
			}
			break;
		}
		count++;
		while (*p++)
			;
	}
	free(buf);
	/* We allocated buffers for exe_short_name and first_arg. Now buf can be deallocated. */
}

static char *bypass_bash_cmd_list[] = {"autoconf", "configure", "libtool", "libtoolize",
				       "lsb_release"};

static char *bypass_python3_cmd_list[] = {"scons", "scons-3", "dnf", "dnf-3", "meson"};

static char *bypass_app_list[] = {"arch", "as", "awk", "basename", "bc", "cal", "cat", "chmod",
				      "chown", "clang", "clear", "cmake", "cmake3", "cp", "cpp",
				      "daos", "daos_agent", "daos_engine", "daos_server", "df",
				      "dfuse", "dmg", "expr", "f77", "f90", "f95", "file", "gawk",
				      "gcc", "gfortran", "gmake", "go", "gofmt", "grep", "g++",
				      "head", "link", "ln", "ls", "kill", "m4", "make", "mkdir",
				      "mktemp", "mv", "nasm", "yasm", "nm",  "numactl", "patchelf",
				      "ping", "pkg-config", "ps", "pwd", "ranlib", "readelf",
				      "readlink", "rename", "rm", "rmdir", "rpm", "sed", "seq",
				      "size", "sleep", "sort", "ssh", "stat", "strace", "strip",
				      "su", "sudo", "tail", "tee", "telnet", "time", "top", "touch",
				      "tr", "truncate", "uname", "vi", "vim", "whoami", "yes"};

static void
check_bypasslist(void)
{
	int   i;
	char *saveptr, *str, *token;

	d_agetenv_str(&bypass_user_cmd_list, "D_IL_BYPASS_LIST");

	/* Normally the list of app is not very long. strncmp() is used for simplicity. */

	if (is_bash && first_arg != NULL) {
		/* built-in list of bash scripts to skip */
		for (i = 0; i < ARRAY_SIZE(bypass_bash_cmd_list); i++) {
			if (strncmp(first_arg, bypass_bash_cmd_list[i],
				    strlen(bypass_bash_cmd_list[i]) + 1) == 0)
				goto set_bypass;
		}
		/* user provided list to skip */
		if (bypass_user_cmd_list) {
			for (str = bypass_user_cmd_list;; str = NULL) {
				token = strtok_r(str, ":", &saveptr);
				if (token == NULL)
					break;
				if (strncmp(first_arg, token, strlen(token) + 1) == 0)
					goto set_bypass;
			}
		}
	}

	if ((memcmp(exe_short_name, STR_AND_SIZE("python")) == 0 ||
	     memcmp(exe_short_name, STR_AND_SIZE("python3")) == 0) &&
	    first_arg) {
		/* built-in list of python scripts to skip */
		for (i = 0; i < ARRAY_SIZE(bypass_python3_cmd_list); i++) {
			if (strncmp(first_arg, bypass_python3_cmd_list[i],
				    strlen(bypass_python3_cmd_list[i]) + 1) == 0)
				goto set_bypass;
		}
		/* user provided list to skip */
		if (bypass_user_cmd_list) {
			for (str = bypass_user_cmd_list;; str = NULL) {
				token = strtok_r(str, ":", &saveptr);
				if (token == NULL)
					break;
				if (strncmp(first_arg, token, strlen(token) + 1) == 0)
					goto set_bypass;
			}
		}
	}

	for (i = 0; i < ARRAY_SIZE(bypass_app_list); i++) {
		if (strncmp(exe_short_name, bypass_app_list[i], strlen(bypass_app_list[i]) + 1) ==
		    0)
			goto set_bypass;
	}

	if (bypass_user_cmd_list) {
		for (str = bypass_user_cmd_list;; str = NULL) {
			token = strtok_r(str, ":", &saveptr);
			if (token == NULL)
				break;
			if (strncmp(exe_short_name, token, strlen(token) + 1) == 0)
				goto set_bypass;
		}
	}

	if (bypass_user_cmd_list)
		d_freeenv_str(&bypass_user_cmd_list);
	return;

set_bypass:
	bypass = true;
	if (bypass_user_cmd_list)
		d_freeenv_str(&bypass_user_cmd_list);
	return;
}

static __attribute__((constructor)) void
init_myhook(void)
{
	mode_t   umask_old;
	char    *env_log;
	char    *env_no_bypass;
	int      rc;
	uint64_t eq_count_loc = 0;

	/* D_IL_NO_BYPASS is ONLY for testing. It always keeps function interception enabled in
	 * current process and children processes. This is needed to thoroughly test interception
	 * related code in CI. The code related to interception disabled is tested by a few tests in
	 * "test_bashcmd_pil4dfs" and "test_whitelist_pil4dfs". Most tests in CI run with
	 * "D_IL_NO_BYPASS=1" to test the code with interception enabled.
	 */
	d_agetenv_str(&env_no_bypass, "D_IL_NO_BYPASS");
	if (env_no_bypass) {
		if (strncmp(env_no_bypass, "1", 2) == 0) {
			bypass_allowed = false;
			bypass         = false;
		}
		d_freeenv_str(&env_no_bypass);
	}

	rc = d_agetenv_str(&env_log, "D_IL_REPORT");
	if (env_log) {
		report = true;
		if (strncmp(env_log, "0", 2) == 0 || strncasecmp(env_log, "false", 6) == 0)
			report = false;
		d_freeenv_str(&env_log);
	}

	extract_exe_name_1st_arg();

	/* Need to check whether current process is bash or not under regular & compatible modes.*/
	check_exe_sh_bash();

	if (bypass_allowed)
		check_bypasslist();
	if (report)
		fprintf(stderr, "app %s interception %s\n", exe_short_name, bypass ? "OFF" : "ON");
	if (bypass)
		return;

	umask_old = umask(0);
	umask(umask_old);
	mode_not_umask = ~umask_old;
	page_size = sysconf(_SC_PAGESIZE);

	rc = daos_debug_init(NULL);
	if (rc != 0)
		fprintf(stderr, "Error> daos_debug_init() failed: %d (%s)\n", daos_der2errno(rc),
			strerror(daos_der2errno(rc)));
	else
		daos_debug_inited = true;

	d_compatible_mode = false;
	d_getenv_bool("D_IL_COMPATIBLE", &d_compatible_mode);

	d_getenv_bool("D_IL_NO_DCACHE_BASH", &no_dcache_in_bash);

	if (d_compatible_mode) {
		rc = d_hash_table_create(D_HASH_FT_EPHEMERAL | D_HASH_FT_MUTEX |
					 D_HASH_FT_LRU, 6, NULL, &fd_hash_ops, &fd_hash);
		if (rc != 0) {
			DL_ERROR(rc, "failed to create fd hash table");
			return;
		}
		D_INFO("pil4dfs compatible mode is ON.\n");
	}

	/* Find dfuse mounts from /proc/mounts */
	rc = discover_dfuse_mounts();
	if (rc) {
		/* Do not enable interception if discover_dfuse_mounts() failed. */
		D_DEBUG(DB_ANY, "discover_dfuse_mounts() failed: %d (%s)\n", rc, strerror(rc));
		return;
	}

	rc = discover_daos_mount_with_env();
	if (rc) {
		D_FATAL("discover_daos_mount_with_env() failed: %d (%s)", rc, strerror(rc));
		abort();
	}
	if (num_dfs == 0) {
		/* Do not enable interception if no DFS mounts found. */
		D_DEBUG(DB_ANY, "No DFS mount points found.\n");
		return;
	}

	update_cwd();
	rc = D_MUTEX_INIT(&lock_reserve_fd, NULL);
	if (rc)
		return;

	rc = D_MUTEX_INIT(&lock_dfs, NULL);
	if (rc)
		return;

	rc = init_fd_list();
	if (rc)
		return;

	rc = D_MUTEX_INIT(&lock_eqh, NULL);
	if (rc)
		return;
	rc = d_getenv_uint64_t("D_IL_MAX_EQ", &eq_count_loc);
	if (rc != -DER_NONEXIST) {
		if (eq_count_loc > MAX_EQ) {
			D_WARN("Max EQ count (%" PRIu64 ") should not exceed: %d", eq_count_loc,
			       MAX_EQ);
			eq_count_loc = MAX_EQ;
		}
		d_eq_count_max = (uint16_t)eq_count_loc;
	} else {
		d_eq_count_max = MAX_EQ;
	}

	dcache_size_bits = DCACHE_SIZE_BITS;
	rc               = d_getenv_uint32_t("D_IL_DCACHE_SIZE_BITS", &dcache_size_bits);
	if (rc != -DER_SUCCESS && rc != -DER_NONEXIST)
		DL_WARN(rc, "'D_IL_DCACHE_SIZE_BITS' env variable could not be used");

	dcache_rec_timeout = DCACHE_REC_TIMEOUT;
	rc                 = d_getenv_uint32_t("D_IL_DCACHE_REC_TIMEOUT", &dcache_rec_timeout);
	if (rc != -DER_SUCCESS && rc != -DER_NONEXIST)
		DL_WARN(rc, "'D_IL_DCACHE_REC_TIMEOUT' env variable could not be used");

	dcache_gc_period = DCACHE_GC_PERIOD;
	rc               = d_getenv_uint32_t("D_IL_DCACHE_GC_PERIOD", &dcache_gc_period);
	if (rc != -DER_SUCCESS && rc != -DER_NONEXIST)
		DL_WARN(rc, "'D_IL_DCACHE_GC_PERIOD' env variable could not be used");

	dcache_gc_reclaim_max = DCACHE_GC_RECLAIM_MAX;
	rc = d_getenv_uint32_t("D_IL_DCACHE_GC_RECLAIM_MAX", &dcache_gc_reclaim_max);
	if (rc != -DER_SUCCESS && rc != -DER_NONEXIST)
		DL_WARN(rc, "'D_IL_DCACHE_GC_RECLAIM_MAX' env variable could not be used");
	if (dcache_gc_reclaim_max == 0) {
		D_WARN("'D_IL_DCACHE_GC_RECLAIM_MAX' env variable could not be used: value == 0.");
		dcache_gc_reclaim_max = DCACHE_GC_RECLAIM_MAX;
	}

	register_a_hook("libc", "open64", (void *)new_open_libc, (long int *)(&libc_open));
	register_a_hook("libpthread", "open64", (void *)new_open_pthread,
			(long int *)(&pthread_open));

	register_a_hook("libc", "__close", (void *)new_close_libc, (long int *)(&libc_close));
	register_a_hook("libpthread", "__close", (void *)new_close_pthread,
			(long int *)(&pthread_close));
	register_a_hook("libc", "__close_nocancel", (void *)new_close_nocancel_libc,
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
	register_a_hook("libc", "__fxstatat", (void *)new_fxstatat, (long int *)(&libc_fxstatat));
	register_a_hook("libc", "fstatat", (void *)new_fstatat, (long int *)(&libc_fstatat));
	register_a_hook("libc", "readdir", (void *)new_readdir, (long int *)(&next_readdir));

	register_a_hook("libc", "fcntl", (void *)new_fcntl, (long int *)(&libc_fcntl));

	if (d_compatible_mode == false) {
		register_a_hook("libc", "mmap", (void *)new_mmap, (long int *)(&next_mmap));
		register_a_hook("libc", "munmap", (void *)new_munmap, (long int *)(&next_munmap));
	}

	register_a_hook("libc", "exit", (void *)new_exit, (long int *)(&next_exit));
	register_a_hook("libc", "dup3", (void *)new_dup3, (long int *)(&libc_dup3));
	register_a_hook("libc", "readlink", (void *)new_readlink, (long int *)(&libc_readlink));

	init_fd_dup2_list();

	if (is_bash && no_dcache_in_bash)
		/* Disable directory caching inside bash. bash could remove a dir then recreate
		 * it which causes cache inconsistency. Observed such issue in "configure" in ucx.
		 */
		dcache_rec_timeout = 0;

	install_hook();
	d_hook_enabled   = 1;
	hook_enabled_bak = d_hook_enabled;
}

static void
print_summary(void)
{
	uint64_t op_sum = 0;
	uint64_t read_loc, write_loc, open_loc, stat_loc;
	uint64_t opendir_loc, readdir_loc, link_loc, unlink_loc, rdlink_loc, seek_loc;
	uint64_t mkdir_loc, rmdir_loc, rename_loc, mmap_loc;

	if (!report)
		return;

	read_loc    = atomic_load_relaxed(&num_read);
	write_loc   = atomic_load_relaxed(&num_write);
	open_loc    = atomic_load_relaxed(&num_open);
	stat_loc    = atomic_load_relaxed(&num_stat);
	opendir_loc = atomic_load_relaxed(&num_opendir);
	readdir_loc = atomic_load_relaxed(&num_readdir);
	link_loc    = atomic_load_relaxed(&num_link);
	unlink_loc  = atomic_load_relaxed(&num_unlink);
	rdlink_loc  = atomic_load_relaxed(&num_rdlink);
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
	fprintf(stderr, "[link   ]  %" PRIu64 "\n", link_loc);
	fprintf(stderr, "[unlink ]  %" PRIu64 "\n", unlink_loc);
	fprintf(stderr, "[rdlink ]  %" PRIu64 "\n", rdlink_loc);
	fprintf(stderr, "[seek   ]  %" PRIu64 "\n", seek_loc);
	fprintf(stderr, "[mkdir  ]  %" PRIu64 "\n", mkdir_loc);
	fprintf(stderr, "[rmdir  ]  %" PRIu64 "\n", rmdir_loc);
	fprintf(stderr, "[rename ]  %" PRIu64 "\n", rename_loc);
	fprintf(stderr, "[mmap   ]  %" PRIu64 "\n", mmap_loc);

	op_sum = read_loc + write_loc + open_loc + stat_loc + opendir_loc + readdir_loc + link_loc +
		 unlink_loc + rdlink_loc + seek_loc + mkdir_loc + rmdir_loc + rename_loc + mmap_loc;
	fprintf(stderr, "\n");
	fprintf(stderr, "[op_sum ]  %" PRIu64 "\n", op_sum);
}

static void
close_all_fd(void)
{
	int i;

	for (i = 0; i <= last_fd; i++) {
		if (d_file_list[i])
			free_fd(i, false);
	}
}

static void
close_all_dirfd(void)
{
	int i;

	for (i = 0; i <= last_dirfd; i++) {
		if (dir_list[i])
			free_dirfd(i);
	}
}

static void
destroy_all_eqs(void)
{
	int i, rc;

	/** destroy EQs created by threads */
	for (i = 0; i < d_eq_count; i++) {
		rc = daos_eq_destroy(eq_list[i], 0);
		if (rc)
			DL_ERROR(rc, "daos_eq_destroy() failed");
	}
	/** destroy main thread eq */
	if (daos_handle_is_valid(main_eqh)) {
		rc = daos_eq_destroy(main_eqh, 0);
		if (rc)
			DL_ERROR(rc, "daos_eq_destroy() failed");
	}
}

static __attribute__((destructor)) void
finalize_myhook(void)
{
	int       rc;
	d_list_t *rlink;

	if (bypass)
		return;

	if (context_reset) {
		/* child processes after fork() */
		destroy_all_eqs();
		daos_eq_lib_fini();
		return;
	} else {
		/* parent process */
		destroy_all_eqs();
	}

	if (d_compatible_mode) {
		/* Free record left in fd hash table then destroy it */
		while (1) {
			rlink = d_hash_rec_first(fd_hash);
			if (rlink == NULL)
				break;
			d_hash_rec_decref(fd_hash, rlink);
		}

		rc = d_hash_table_destroy(fd_hash, false);
		if (rc != 0) {
			DL_ERROR(rc, "error in d_hash_table_destroy(fd_hash)");
		}
	}

	if (num_dfs > 0) {
		close_all_duped_fd();
		close_all_fd();
		close_all_dirfd();

		finalize_dfs();

		D_MUTEX_DESTROY(&lock_eqh);
		D_MUTEX_DESTROY(&lock_reserve_fd);
		D_MUTEX_DESTROY(&lock_dfs);
		D_MUTEX_DESTROY(&lock_dirfd);
		D_MUTEX_DESTROY(&lock_fd);
		D_MUTEX_DESTROY(&lock_mmap);
		D_RWLOCK_DESTROY(&lock_fd_dup2ed);

		if (fd_255_reserved)
			libc_close(255);
		if (fd_dummy >= 0)
			libc_close(fd_dummy);

		if (hook_enabled_bak)
			uninstall_hook();
		else
			free_memory_in_hook();
	}

	if (daos_debug_inited)
		daos_debug_fini();
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
		DL_ERROR(rc, "failed to connect pool");
		return daos_der2errno(rc);
	}

	rc = daos_cont_open(dfs_list[idx].poh, dfs_list[idx].cont, DAOS_COO_RW, &dfs_list[idx].coh,
			    NULL, NULL);
	if (rc != 0) {
		DL_ERROR(rc, "failed to open container");
		D_GOTO(out_err_cont_open, rc);
	}
	rc = dfs_mount(dfs_list[idx].poh, dfs_list[idx].coh, O_RDWR, &dfs_list[idx].dfs);
	if (rc != 0) {
		DS_ERROR(rc, "failed to mount dfs");
		D_GOTO(out_err_mt, rc);
	}

	rc = dcache_create(dfs_list[idx].dfs, dcache_size_bits, dcache_rec_timeout,
			   dcache_gc_period, dcache_gc_reclaim_max, &dfs_list[idx].dcache);
	if (rc != 0) {
		DL_ERROR(rc, "failed to create DFS directory cache");
		D_GOTO(out_err_ht, rc = daos_der2errno(rc));
	}

	return 0;

out_err_ht:
	rc2 = dfs_umount(dfs_list[idx].dfs);
	if (rc2 != 0)
		DS_ERROR(rc2, "error in dfs_umount(%s)", dfs_list[idx].fs_root);

out_err_mt:
	rc2 = daos_cont_close(dfs_list[idx].coh, NULL);
	if (rc2 != 0)
		DL_ERROR(rc2, "error in daos_cont_close(%s)", dfs_list[idx].fs_root);

out_err_cont_open:
	rc2 = daos_pool_disconnect(dfs_list[idx].poh, NULL);
	if (rc2 != 0)
		DL_ERROR(rc2, "error in daos_pool_disconnect(%s)", dfs_list[idx].fs_root);

	return rc;
}

static void
finalize_dfs(void)
{
	int i;
	int rc;

	/* Disable interception */
	d_hook_enabled = 0;

	for (i = 0; i < num_dfs; i++) {
		if (atomic_load_relaxed(&(dfs_list[i].inited)) == 0) {
			D_ASSERT(dfs_list[i].dcache == NULL);
			D_FREE(dfs_list[i].fs_root);
			D_FREE(dfs_list[i].pool);
			D_FREE(dfs_list[i].cont);
			continue;
		}

		rc = dcache_destroy(dfs_list[i].dcache);
		if (rc != 0) {
			DL_ERROR(rc, "error in dcache_destroy(%s)", dfs_list[i].fs_root);
			continue;
		}
		rc = dfs_umount(dfs_list[i].dfs);
		if (rc != 0) {
			DS_ERROR(rc, "error in dfs_umount(%s)", dfs_list[i].fs_root);
			continue;
		}
		rc = daos_cont_close(dfs_list[i].coh, NULL);
		if (rc != 0) {
			DL_ERROR(rc, "error in daos_cont_close(%s)", dfs_list[i].fs_root);
			continue;
		}
		rc = daos_pool_disconnect(dfs_list[i].poh, NULL);
		if (rc != 0) {
			DL_ERROR(rc, "error in daos_pool_disconnect(%s)", dfs_list[i].fs_root);
			continue;
		}
		D_FREE(dfs_list[i].fs_root);
		D_FREE(dfs_list[i].pool);
		D_FREE(dfs_list[i].cont);
	}

	if (atomic_load_relaxed(&d_daos_inited)) {
		uint32_t init_cnt, j;

		free_reserved_low_fd();
		init_cnt = atomic_load_relaxed(&daos_init_cnt);
		for (j = 0; j < init_cnt; j++) {
			rc = daos_fini();
			if (rc != 0)
				DL_ERROR(rc, "daos_fini() failed");
		}
	}
}

void __attribute__ ((__noreturn__))
_exit(int rc)
{
	if (next__exit == NULL) {
		next__exit = dlsym(RTLD_NEXT, "_exit");
		D_ASSERT(next__exit != NULL);
	}
	if (context_reset) {
		destroy_all_eqs();
		daos_eq_lib_fini();
	}
	(*next__exit)(rc);
}

static int
get_eqh(daos_handle_t *eqh)
{
	int rc;

	if (daos_handle_is_valid(td_eqh)) {
		*eqh = td_eqh;
		return 0;
	}

	/** No EQ support requested */
	if (d_eq_count_max == 0)
		return -1;

	rc = pthread_mutex_lock(&lock_eqh);
	/** create a new EQ if the EQ pool is not full; otherwise round robin EQ use from pool */
	if (d_eq_count >= d_eq_count_max) {
		td_eqh = eq_list[eq_idx++];
		if (eq_idx == d_eq_count_max)
			eq_idx = 0;
	} else {
		rc = daos_eq_create(&td_eqh);
		if (rc) {
			pthread_mutex_unlock(&lock_eqh);
			return -1;
		}
		eq_list[d_eq_count] = td_eqh;
		d_eq_count++;
	}
	pthread_mutex_unlock(&lock_eqh);
	*eqh = td_eqh;
	return 0;
}
