#include <stdio.h>
#include <assert.h>
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

#include <gurt/list.h>
#include <gurt/common.h>
#include <gurt/hash.h>
#include <daos.h>
#include <daos_fs.h>

#include "hook.h"

/* Use very large synthetic FD to distinguish regular FD from Kernel */
#define FD_FILE_BASE		(0x20000000)
#define FD_DIR_BASE		(0x40000000)
#define DUMMY_FD_DIR		(0x50000000)

#define READ_DIR_BATCH_SIZE	(24)
#define MAX_FILE_NAME_LEN	(256)
#define MAX_FD_DUP2ED		(8)

#define MAX_OPENED_FILE		(2048)
#define MAX_OPENED_FILE_M1	((MAX_OPENED_FILE) - 1)
#define MAX_OPENED_DIR		(512)
#define MAX_OPENED_DIR_M1	((MAX_OPENED_DIR) - 1)

/* Create a fake st_ino in stat for a path */
#define FAKE_ST_INO(path)	(d_hash_string_u32(path, strlen(path)))

/* structure allocated for a FD for a file */
struct FILESTATUS {
	dfs_obj_t	*file_obj;
	dfs_obj_t	*parent;
	int		open_flag;
	int		ref_count;
	int		fd_dup_pre;
	int		fd_dup_next;
	unsigned int	st_ino;
	off_t		offset;
	char item_name[MAX_FILE_NAME_LEN];
};

/* structure allocated for a FD for a dir */
struct DIRSTATUS {
	int		fd;
	uint32_t	num_ents;
	dfs_obj_t	*dir_obj;
	long int	offset;
	int		ref_count;
	int		fd_dup_pre;
	int		fd_dup_next;
	int		open_flag;
	daos_anchor_t	anchor;
	char		path[MAX_FILE_NAME_LEN];
	struct dirent	ents[READ_DIR_BATCH_SIZE];
};

struct FD_DUP2ED {
	int fd_src, fd_dest;
};

/* working dir of current process */
static char	cur_dir[MAX_FILE_NAME_LEN] = "";

/* the flag to indicate whether initlization is finished or not */
static int		inited;
static pthread_mutex_t	lock_fd;
static pthread_mutex_t	lock_dirfd;

/* store ! umask to apply on mode when creating file to honor system umask */
static mode_t	mode_not_umask;

static daos_handle_t	poh;
static daos_handle_t	coh;
static dfs_t		*dfs;
static struct d_hash_table *dfs_dir_hash;
static char *fs_root;
static int len_fs_root;
static int fd_stdin = -1, fd_stdout = -1, fd_stderr = -1;


static void init_dfs(void);
static void finalize_dfs(void);
static void update_cwd(void);

/* start copied from https://github.com/hpc/ior/blob/main/src/aiori-DFS.c */
struct dir_hdl {
	d_list_t	entry;
	dfs_obj_t	*oh;
	char		name[MAX_FILE_NAME_LEN];
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

	return (strcmp(hdl->name, (const char *)key) == 0);
}

static void
rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	int		rc;
	struct dir_hdl	*hdl = hdl_obj(rlink);

	rc = dfs_release(hdl->oh);
	assert(rc == 0);
	free(hdl);
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

	return d_hash_string_u32(hdl->name, strlen(hdl->name));
}

static d_hash_table_ops_t hdl_hash_ops = {
	.hop_key_cmp	= key_cmp,
	.hop_rec_decref	= rec_decref,
	.hop_rec_free	= rec_free,
	.hop_rec_hash	= rec_hash
};

static dfs_obj_t *
lookup_insert_dir(const char *name, mode_t *mode)
{
	struct dir_hdl	*hdl;
	dfs_obj_t	*oh;
	d_list_t	*rlink;
	size_t		len = strlen(name);
	int		rc;

	rlink = d_hash_rec_find(dfs_dir_hash, name, len);
	if (rlink != NULL) {
		hdl = hdl_obj(rlink);
		return hdl->oh;
	}

	rc = dfs_lookup(dfs, name, O_RDWR, &oh, mode, NULL);
	if (rc) {
		errno = rc;
		return NULL;
	}

	if (mode && !S_ISDIR(*mode))
		return oh;

	hdl = calloc(1, sizeof(struct dir_hdl));
	if (hdl == NULL)
		return NULL;

	strncpy(hdl->name, name, len);
	hdl->oh = oh;

	rc = d_hash_rec_insert(dfs_dir_hash, hdl->name, len, &hdl->entry, false);
	if (rc) {
		fprintf(stderr, "Failed to insert dir handle in hashtable\n");
		rc = dfs_release(hdl->oh);
		assert(rc == 0);
		free(hdl);
		return NULL;
	}

	return hdl->oh;
}
/* end   copied from https://github.com/hpc/ior/blob/main/src/aiori-DFS.c */

static int (*real_open_ld)(const char *pathname, int oflags, ...);
static int (*real_open_libc)(const char *pathname, int oflags, ...);
static int (*real_open_pthread)(const char *pathname, int oflags, ...);

static int (*real_close_nocancel)(int fd);

static int (*real_close_libc)(int fd);
static int (*real_close_pthread)(int fd);

static ssize_t (*real_read_libc)(int fd, void *buf, size_t count);
static ssize_t (*real_read_pthread)(int fd, void *buf, size_t count);

static ssize_t (*real_pread)(int fd, void *buf, size_t size, off_t offset);

static ssize_t (*real_write_libc)(int fd, const void *buf, size_t count);
static ssize_t (*real_write_pthread)(int fd, const void *buf, size_t count);

static ssize_t (*real_pwrite)(int fd, const void *buf, size_t size, off_t offset);

static off_t (*real_lseek_libc)(int fd, off_t offset, int whence);
static off_t (*real_lseek_pthread)(int fd, off_t offset, int whence);

static int (*real_fxstat)(int vers, int fd, struct stat *buf);

static int (*real_statfs)(const char *pathname, struct statfs *buf);

static int (*real_statvfs)(const char *pathname, struct statvfs *buf);

static DIR * (*real_opendir)(const char *name);

static DIR * (*real_fdopendir)(int fd);

static int (*real_closedir)(DIR *dirp);

static struct dirent * (*real_readdir)(DIR *dirp);

static int (*real_mkdir)(const char *path, mode_t mode);

static int (*real_mkdirat)(int dirfd, const char *pathname, mode_t mode);

static int (*real_xstat)(int ver, const char *path, struct stat *stat_buf);

static int (*real_lxstat)(int ver, const char *path, struct stat *stat_buf);

static int (*real_fxstatat)(int ver, int dirfd, const char *path, struct stat *stat_buf,
	int flags);

static int (*real_statx)(int dirfd, const char *path, int flags, unsigned int mask,
			 struct statx *statx_buf);

static int (*real_isatty)(int fd);

static int (*real_access)(const char *pathname, int mode);

static int (*real_faccessat)(int dirfd, const char *pathname, int mode, int flags);

static int (*real_chdir)(const char *path);

static int (*real_fchdir)(int fd);

static int (*real_rmdir)(const char *path);

static int (*real_rename)(const char *old_name, const char *new_name);

static char * (*real_getcwd)(char *buf, size_t size);

static int (*real_unlink)(const char *path);

static int (*real_unlinkat)(int dirfd, const char *path, int flags);

static int (*real_fsync)(int fd);

static int (*real_truncate)(const char *path, off_t length);

static int (*real_ftruncate)(int fd, off_t length);

static int (*real_chmod)(const char *path, mode_t mode);

static int (*real_fchmod)(int fd, mode_t mode);

static int (*real_fchmodat)(int dirfd, const char *path, mode_t mode, int flags);

static int (*real_utime)(const char *path, const struct utimbuf *times);

static int (*real_utimes)(const char *path, const struct timeval times[2]);

static int (*real_futimens)(int fd, const struct timespec times[2]);

static int (*real_utimensat)(int dirfd, const char *path, const struct timespec times[2],
			     int flags);

static int (*real_openat)(int dirfd, const char *pathname, int flags, ...);

static int (*real_openat_2)(int dirfd, const char *pathname, int flags);

static int (*real_fcntl)(int fd, int cmd, ...);

static int (*real_ioctl)(int fd, unsigned long request, ...);

static int (*real_dup)(int oldfd);

static int (*real_dup2)(int oldfd, int newfd);

/* typedef int (*org_dup3)(int oldfd, int newfd, int flags); */
/* static org_dup3 real_dup3=NULL; */

/**
 *static int (*real_execve)(const char *filename, char *const argv[], char *const envp[]);
 * static int (*real_execvp)(const char *filename, char *const argv[]);
 * static int (*real_execv)(const char *filename, char *const argv[]);
 * static pid_t (*real_fork)();
 */

/* start NOT supported by DAOS */
static int (*real_posix_fadvise)(int fd, off_t offset, off_t len, int advice);
static int (*real_flock)(int fd, int operation);
static int (*real_fallocate)(int fd, int mode, off_t offset, off_t len);
static int (*real_posix_fallocate)(int fd, off_t offset, off_t len);
static int (*real_posix_fallocate64)(int fd, off64_t offset, off64_t len);
static int (*real_tcgetattr)(int fd, void *termios_p);
/* end NOT supported by DAOS */

/* to do!! */
/**
 * static char * (*org_realpath)(const char *pathname, char *resolved_path);
 * org_realpath real_realpath=NULL;
 */

static void remove_dot_dot(char szPath[]);
static int remove_dot(char szPath[]);

static struct FILESTATUS *file_list;
static struct DIRSTATUS *dir_list;

/* last_fd==-1 means the list is empty. No active fd in list. */
static int next_free_fd, last_fd = -1, num_fd;
static int next_free_dirfd, last_dirfd = -1, num_dirfd;

static int find_next_available_fd(void);
static int find_next_available_dirfd(void);
static void free_fd(int idx);
static void free_dirfd(int idx);

static int num_fd_dup2ed;
struct FD_DUP2ED fd_dup2_list[MAX_FD_DUP2ED];

static void init_fd_dup2_list(void);
/* return dest fd */
static int query_fd_forward_dest(int fd_src);
/* static int Query_Fd_Forward_Src(int fd_dest, int *Closed);	// return src fd */
/* static void Close_Duped_All_Fd(void); */

static int new_close_common(int (*real_close)(int fd), int fd);

/* standarlize and determine whether a path is a target path or not */
static int
parse_path(const char *szInput, int *is_target_path, dfs_obj_t **parent, char *item_name,
	   char *parent_dir, char full_path[])
{
	char full_path_loc[MAX_FILE_NAME_LEN+4];
	int pos, len, rc = 0;
	mode_t mode;

	/* absolute path */
	if (strncmp(szInput, ".", 2) == 0) {
		strcpy(full_path_loc, cur_dir);
	} else if (szInput[0] == '/')	{
		strcpy(full_path_loc, szInput);
	} else {
		/* relative path */
		snprintf(full_path_loc, MAX_FILE_NAME_LEN+1, "%s/%s", cur_dir, szInput);
	}

	remove_dot_dot(full_path_loc);
	len = remove_dot(full_path_loc);

	if (strncmp(full_path_loc, fs_root, len_fs_root) == 0)	{
		*is_target_path = 1;

		if (full_path)
			strcpy(full_path, full_path_loc + len_fs_root);

		/* root dir */
		if (full_path_loc[len_fs_root] == 0) {
			*parent = NULL;
			parent_dir[0] = '\0';
			item_name[0] = '/';
			item_name[1] = '\0';
			if (full_path)
				strcpy(full_path, "/");
		} else {
			for (pos = len-1; pos >= len_fs_root; pos--) {
				if (full_path_loc[pos] == '/')
					break;
			}
			strcpy(item_name, full_path_loc + pos + 1);
			/* the item under root directory */
			if (pos == len_fs_root) {
				*parent = NULL;
				parent_dir[0] = '/';
				parent_dir[1] = '\0';
				return 0;
			}
			/* Need to look up the parent directory */
			full_path_loc[pos] = 0;
			strcpy(parent_dir, full_path_loc + len_fs_root);
			*parent = lookup_insert_dir(parent_dir, &mode);
			if (*parent == NULL) {
				/* parent dir does not exist or something wrong */
				printf("Dir %s does not exist or error to query. %s\n",
					full_path_loc, strerror(errno));
				rc = 1;
			}
		}
	} else {
		*is_target_path = 0;
		*parent = NULL;
		item_name[0] = '\0';
		return 0;
	}

	return rc;
}

static void
remove_dot_dot(char szPath[])
{
	char *p_Offset_2Dots, *p_Back, *pTmp, *pMax, *pNewStr;
	int i, nLen, nNonZero = 0;

	nLen = strlen(szPath);

	p_Offset_2Dots = strstr(szPath, "..");
	if (p_Offset_2Dots == (szPath+1))	{
		printf("Must be something wrong in path: %s\n", szPath);
		return;
	}

	while (p_Offset_2Dots > 0)	{
		pMax = p_Offset_2Dots + 2;
		for (p_Back = p_Offset_2Dots-2; p_Back >= szPath; p_Back--) {
			if (*p_Back == '/')	{
				for (pTmp = p_Back; pTmp < pMax; pTmp++)
					*pTmp = 0;
				break;
			}
		}
		p_Offset_2Dots = strstr(p_Offset_2Dots + 2, "..");
		if (p_Offset_2Dots == NULL)
			break;
	}

	pNewStr = szPath;
	for (i = 0; i < nLen; i++)	{
		if (szPath[i])	{
			pNewStr[nNonZero] = szPath[i];
			nNonZero++;
		}
	}
	pNewStr[nNonZero] = 0;
}

static int
remove_dot(char szPath[])
{
	char *p_Offset_Dots, *p_Offset_Slash, *pNewStr;
	int i, nLen, nNonZero = 0;

	nLen = strlen(szPath);

	p_Offset_Dots = strstr(szPath, "/./");

	while (p_Offset_Dots > 0)	{
		p_Offset_Dots[0] = 0;
		p_Offset_Dots[1] = 0;
		p_Offset_Dots = strstr(p_Offset_Dots + 2, "/./");
		if (p_Offset_Dots == NULL)
			break;
	}

	/* replace "//" with "/" */
	p_Offset_Slash = strstr(szPath, "//");
	while (p_Offset_Slash > 0)	{
		p_Offset_Slash[0] = 0;
		p_Offset_Slash = strstr(p_Offset_Slash + 1, "//");
		if (p_Offset_Slash == NULL)
			break;
	}

	pNewStr = szPath;
	for (i = 0; i < nLen; i++)	{
		if (szPath[i])	{
			pNewStr[nNonZero] = szPath[i];
			nNonZero++;
		}
	}
	/* remove "/" at the end of path */
	pNewStr[nNonZero] = 0;
	for (i = nNonZero-1; i >= 0; i--)	{
		if (pNewStr[i] == '/')	{
			pNewStr[i] = 0;
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
	int i;

	if (pthread_mutex_init(&lock_fd, NULL) != 0) {
		printf("\n mutex create_new_lock lock_fd init failed\n");
		exit(1);
	}
	if (pthread_mutex_init(&lock_dirfd, NULL) != 0) {
		printf("\n mutex create_new_lock lock_dirfd init failed\n");
		exit(1);
	}

	file_list = (struct FILESTATUS *)malloc(sizeof(struct FILESTATUS)*MAX_OPENED_FILE);
	dir_list = (struct DIRSTATUS *)malloc(sizeof(struct DIRSTATUS)*MAX_OPENED_DIR);
	memset(file_list, 0, sizeof(struct FILESTATUS)*MAX_OPENED_FILE);
	memset(dir_list, 0, sizeof(struct DIRSTATUS)*MAX_OPENED_DIR);

	for (i = 0; i < MAX_OPENED_FILE; i++)
		file_list[i].file_obj = NULL;
	for (i = 0; i < MAX_OPENED_DIR; i++) {
		dir_list[i].fd = -1;
		dir_list[i].dir_obj = NULL;
	}
	next_free_fd = 0;
	last_fd = -1;
	next_free_dirfd = 0;
	last_dirfd = -1;
	num_fd = num_dirfd = 0;
}

static int
find_next_available_fd(void)
{
	int i, idx = -1;

	pthread_mutex_lock(&lock_fd);
	if (next_free_fd < 0)	{
		pthread_mutex_unlock(&lock_fd);
		return next_free_fd;
	}
	idx = next_free_fd;
	if (next_free_fd > last_fd)
		last_fd = next_free_fd;
	next_free_fd = -1;

	for (i = idx+1; i < MAX_OPENED_FILE; i++) {
		if (file_list[i].file_obj == NULL) {
			/* available, then update next_free_fd */
			next_free_fd = i;
			break;
		}
	}
	if (next_free_fd < 0)
		printf("WARNING> All space for file_list are used.\n");

	num_fd++;
	pthread_mutex_unlock(&lock_fd);

	return idx;
}

static int
find_next_available_dirfd(void)
{
	int i, idx = -1;

	pthread_mutex_lock(&lock_dirfd);
	if (next_free_dirfd < 0) {
		pthread_mutex_unlock(&lock_dirfd);
		return next_free_dirfd;
	}
	idx = next_free_dirfd;
	if (next_free_dirfd > last_dirfd)
		last_dirfd = next_free_dirfd;

	next_free_dirfd = -1;

	for (i = idx+1; i < MAX_OPENED_DIR; i++) {
		if (dir_list[i].dir_obj == NULL) {
			/* available, then update next_free_dirfd */
			next_free_dirfd = i;
			break;
		}
	}
	if (next_free_dirfd < 0)
		printf("WARNING> All space for dir_list are used.\n");

	num_dirfd++;
	pthread_mutex_unlock(&lock_dirfd);

	return idx;
}

/* May need to support duplicated fd as duplicated dirfd too. */
static void
free_fd(int idx)
{
	int i;

	pthread_mutex_lock(&lock_fd);
	file_list[idx].file_obj = NULL;

	if (idx < next_free_fd)
		next_free_fd = idx;

	if (idx == last_fd)	{
		for (i = idx-1; i >= 0; i--)	{
			if (file_list[i].file_obj)	{
				last_fd = i;
				break;
			}
		}
	}

	num_fd--;
	pthread_mutex_unlock(&lock_fd);
}

static void
free_dirfd(int idx)
{
	int		i, fd_dup_pre, fd_dup_next, ready_to_release = 0, rc;
	dfs_obj_t	*dir_obj = NULL;

	pthread_mutex_lock(&lock_dirfd);

	dir_list[idx].ref_count--;
	if (dir_list[idx].ref_count > 0) {
		pthread_mutex_unlock(&lock_dirfd);
		return;
	}

	dir_obj = dir_list[idx].dir_obj;
	dir_list[idx].dir_obj = NULL;

	if (idx < next_free_dirfd)
		next_free_dirfd = idx;

	if (idx == last_dirfd) {
		for (i = idx-1; i >= 0; i--) {
			if (dir_list[i].dir_obj) {
				last_dirfd = i;
				break;
			}
		}
	}

	num_dirfd--;

	fd_dup_pre = dir_list[idx].fd_dup_pre;
	fd_dup_next = dir_list[idx].fd_dup_next;

	if (fd_dup_pre >= 0) {
		dir_list[fd_dup_pre].fd_dup_next = fd_dup_next;
		if (fd_dup_next >= 0)
			dir_list[fd_dup_next].fd_dup_pre = fd_dup_pre;
	}
	if ((fd_dup_pre == -1) && (fd_dup_next == -1))
		ready_to_release = 1;

	pthread_mutex_unlock(&lock_dirfd);

	if (ready_to_release) {
		rc = dfs_release(dir_obj);
		assert(rc == 0);
	}
}

static inline int
Get_Fd_Redirected(int fd)
{
	int i;

	for (i = 0; i < MAX_FD_DUP2ED; i++) {
		if (fd_dup2_list[i].fd_src == fd)
			return fd_dup2_list[i].fd_dest;
	}

	if (fd >= 3)	{
		return fd;
	} else if (fd == 0) {
		if (fd_stdin > 0)
			return fd_stdin;
	} else if (fd == 1) {
		if (fd_stdout > 0)
			return fd_stdout;
	} else if (fd == 2) {
		if (fd_stderr > 0)
			return fd_stderr;
	}

	return fd;
}

static void
init_fd_dup2_list(void)
{
	int i;

	for (i = 0; i < MAX_FD_DUP2ED; i++)	{
		fd_dup2_list[i].fd_src = -1;
		fd_dup2_list[i].fd_dest = -1;
	}
}

static void
free_fd_in_dup2_list(int fd)
{
	int i;

	for (i = 0; i < MAX_FD_DUP2ED; i++)	{
		if (fd_dup2_list[i].fd_src == fd) {
			fd_dup2_list[i].fd_src = -1;
			fd_dup2_list[i].fd_dest = -1;
		}
	}
}

static int
find_free_fd_dup2_list(void)
{
	int i;

	for (i = 0; i < MAX_FD_DUP2ED; i++)	{
		if (fd_dup2_list[i].fd_src == -1)
			return i;
	}
	printf("ERROR: num_fd_dup2ed >= MAX_FD_DUP2ED\n");
	errno = EMFILE;
	return (-1);
}
/**
 * static int
 * is_a_duped_fd(int fd)
 * {
 *	int i;
 *
 *	for (i = 0; i < MAX_FD_DUP2ED; i++)	{
 *		if (fd_dup2_list[i].fd_src == fd)	{
 *			return 1;
 *		}
 *	}
 *	return 0;
 * }
 */
static int
query_fd_forward_dest(int fd_src)
{
	int i;

	for (i = 0; i < MAX_FD_DUP2ED; i++)	{
		if (fd_src == fd_dup2_list[i].fd_src)
			return fd_dup2_list[i].fd_dest;
	}
	return -1;
}

static int
allocate_a_fd_from_kernel(void)
{
	struct timespec	times_loc;
	char		file_name[128];

	clock_gettime(CLOCK_REALTIME, &times_loc);
	snprintf(file_name, sizeof(file_name) - 1, "dummy_%ld_%ld", times_loc.tv_sec,
		 times_loc.tv_nsec);
	return memfd_create("dummy", 0);
}

static void
close_all_duped_fd(void)
{
	int i;

	for (i = 0; i < MAX_FD_DUP2ED; i++)	{
		if (fd_dup2_list[i].fd_src >= 0)	{
			fd_dup2_list[i].fd_src = -1;
			new_close_common(real_close_libc, fd_dup2_list[i].fd_dest);
		}
	}
}

/**
 *static int
 * Query_Fd_Forward_Src(int fd_dest, int *Closed)
 * {
 *	int i;
 *
 *	for (i = 0; i < MAX_FD_DUP2ED; i++)	{
 *		if (fd_dest == fd_dup2_list[i].fd_dest)	{
 *			return fd_dup2_list[i].fd_src;
 *		}
 *	}
 *	return -1;
 * }
 */
static int
open_common(int (*real_open)(const char *pathname, int oflags, ...), const char *szCallerName,
	    const char *pathname, int oflags, ...)
{
	unsigned int mode = 0664;
	int two_args = 1, rc, is_target_path, idx_fd, idx_dirfd;
	char item_name[MAX_FILE_NAME_LEN];
	char parent_dir[MAX_FILE_NAME_LEN];
	char full_path[MAX_FILE_NAME_LEN];
	dfs_obj_t *file_obj;
	dfs_obj_t *parent;
	mode_t mode_query = 0;

	if (oflags & O_CREAT)   {
		va_list arg;

		va_start(arg, oflags);
		mode = va_arg(arg, unsigned int);
		mode = mode & mode_not_umask;
		va_end(arg);
		two_args = 0;
	}

	parse_path(pathname, &is_target_path, &parent, item_name, parent_dir, full_path);
	if (is_target_path) {
		/* file/dir should be handled by our FS */
		if (oflags & O_CREAT) {
			rc = dfs_open(dfs, parent, item_name, mode | S_IFREG, oflags, 0, 0,
				      NULL, &file_obj);
			mode_query = S_IFREG;
		} else if (!parent && (strncmp(item_name, "/", 2) == 0)) {
			rc = dfs_lookup(dfs, "/", oflags, &file_obj, &mode_query, NULL);
		} else {
			rc = dfs_lookup_rel(dfs, parent, item_name, oflags, &file_obj, &mode_query,
					    NULL);
		}

		if (rc) {
			printf("open_common> Error: Fail to dfs_open/dfs_lookup_rel %s rc = %d\n",
			       pathname, rc);
			errno = rc;
			return (-1);
		}
		if (S_ISDIR(mode_query)) {
			idx_dirfd = find_next_available_dirfd();
			assert(idx_dirfd >= 0);

			dir_list[idx_dirfd].fd = idx_dirfd + FD_DIR_BASE;
			dir_list[idx_dirfd].offset = 0;
			dir_list[idx_dirfd].fd_dup_pre = -1;
			dir_list[idx_dirfd].fd_dup_next = -1;
			dir_list[idx_dirfd].ref_count = 1;
			dir_list[idx_dirfd].dir_obj = file_obj;
			dir_list[idx_dirfd].num_ents = 0;
			memset(&(dir_list[idx_dirfd].anchor), 0, sizeof(daos_anchor_t));
			sprintf(dir_list[idx_dirfd].path, "%s%s", fs_root, full_path);

			return (idx_dirfd+FD_DIR_BASE);
		}

		idx_fd = find_next_available_fd();
		assert(idx_fd >= 0);
		file_list[idx_fd].file_obj = file_obj;
		file_list[idx_fd].parent = parent;
		file_list[idx_fd].ref_count = 1;
		file_list[idx_fd].fd_dup_pre = -1;
		file_list[idx_fd].fd_dup_next = -1;
		file_list[idx_fd].st_ino = FAKE_ST_INO(full_path);
		file_list[idx_fd].open_flag = oflags;
		/* NEED to set at the end of file if O_APPEND!!!!!!!! */
		file_list[idx_fd].offset = 0;
		strcpy(file_list[idx_fd].item_name, item_name);
		return (idx_fd + FD_FILE_BASE);
	}

	if (two_args)
		rc = real_open(pathname, oflags);
	else
		rc = real_open(pathname, oflags, mode);

	return rc;
}

/* When the open() in ld.so is called, new_open_ld() will be executed. */

static int
new_open_ld(const char *pathname, int oflags, ...)
{
	unsigned int mode;
	int two_args = 1, rc;

	if (oflags & O_CREAT)	{
		va_list arg;

		va_start(arg, oflags);
		mode = va_arg(arg, unsigned int);
		va_end(arg);
		two_args = 0;
	}

	if (two_args)
		rc = open_common(real_open_ld, "new_open_ld", pathname, oflags);
	else
		rc = open_common(real_open_ld, "new_open_ld", pathname, oflags, mode);

	return rc;
}

/* When the open() in libc.so is called, new_open_libc() will be executed. */
static int
new_open_libc(const char *pathname, int oflags, ...)
{
	unsigned int mode;
	int two_args = 1, rc;

	if (oflags & O_CREAT)	{
		va_list arg;

		va_start(arg, oflags);
		mode = va_arg(arg, unsigned int);
		va_end(arg);
		two_args = 0;
	}

	if (two_args)
		rc = open_common(real_open_libc, "new_open_libc", pathname, oflags);
	else
		rc = open_common(real_open_libc, "new_open_libc", pathname, oflags, mode);

	return rc;
}

/* When the open() in libpthread.so is called, new_open_pthread() will be executed. */
static int
new_open_pthread(const char *pathname, int oflags, ...)
{
	unsigned int mode;
	int two_args = 1, rc;

	if (oflags & O_CREAT)	{
		va_list arg;

		va_start(arg, oflags);
		mode = va_arg(arg, unsigned int);
		va_end(arg);
		two_args = 0;
	}

	if (two_args)
		rc = open_common(real_open_pthread, "new_open_pthread", pathname, oflags);
	else
		rc = open_common(real_open_pthread, "new_open_pthread", pathname, oflags, mode);

	return rc;
}

static int
new_close_common(int (*real_close)(int fd), int fd)
{
	int rc, fd_Directed, idx;

	if (!inited)
		return real_close(fd);

	fd_Directed = Get_Fd_Redirected(fd);

	if (fd_Directed >= FD_DIR_BASE)	{
		/* directory */
		if (fd_Directed == DUMMY_FD_DIR)	{
			printf("ERROR> Unexpected fd == DUMMY_FD_DIR in close().\n");
			return 0;
		}
		free_dirfd(fd_Directed-FD_DIR_BASE);
		return 0;
	} else if (fd_Directed >= FD_FILE_BASE) {
		/* regular file */
		idx = fd_Directed - FD_FILE_BASE;
		file_list[idx].ref_count--;
		if (file_list[idx].ref_count == 0) {
			rc = dfs_release(file_list[idx].file_obj);
			if (rc)	{
				errno = rc;
				return (-1);
			}
			free_fd(idx);
			if (fd < FD_FILE_BASE) {
				real_close(fd);
				num_fd_dup2ed--;
				free_fd_in_dup2_list(fd);
			}
		}

		return 0;
	}

	return real_close(fd);
}

static int
new_close_libc(int fd)
{
	return new_close_common(real_close_libc, fd);
}

static int
new_close_pthread(int fd)
{
	return new_close_common(real_close_pthread, fd);
}

static int
new_close_nocancel(int fd)
{
	int rc;

	if (fd >= FD_DIR_BASE) {
		if (fd == DUMMY_FD_DIR)	{
			printf("ERROR> Unexpected fd == DUMMY_FD_DIR in close().\n");
			return 0;
		}
		free_dirfd(fd-FD_DIR_BASE);
		return 0;
	} else if (fd >= FD_FILE_BASE) {
		rc = dfs_release(file_list[fd-FD_FILE_BASE].file_obj);
		if (rc)	{
			errno = rc;
			return (-1);
		}
		free_fd(fd-FD_FILE_BASE);
		return 0;
	}

	return real_close_nocancel(fd);
}

static ssize_t
read_comm(ssize_t (*real_read)(int fd, void *buf, size_t size), int fd, void *buf, size_t size)
{
	ssize_t rc;

	if (fd >= FD_FILE_BASE) {
		rc = pread(fd, buf, size, file_list[fd-FD_FILE_BASE].offset);
		if (rc >= 0)
			file_list[fd-FD_FILE_BASE].offset += rc;
		return rc;
	} else {
		return real_read(fd, buf, size);
	}
}

static ssize_t
new_read_libc(int fd, void *buf, size_t size)
{
	return read_comm(real_read_libc, fd, buf, size);
}

static ssize_t
new_read_pthread(int fd, void *buf, size_t size)
{
	return read_comm(real_read_pthread, fd, buf, size);
}

ssize_t
pread(int fd, void *buf, size_t size, off_t offset)
{
	daos_size_t bytes_read;
	char *ptr = (char *)buf;
	int rc;
	d_iov_t iov;
	d_sg_list_t sgl;

	if (size == 0)
		return 0;

	if (real_pread == NULL)	{
		real_pread = dlsym(RTLD_NEXT, "pread64");
		assert(real_pread != NULL);
	}

	if (fd < FD_FILE_BASE)
		return real_pread(fd, buf, size, offset);

	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, (void *)ptr, size);
	sgl.sg_iovs = &iov;
	rc = dfs_read(dfs, file_list[fd-FD_FILE_BASE].file_obj, &sgl, offset, &bytes_read, NULL);
	if (rc) {
		printf("dfs_read(%p, %zu) failed (%d): %s\n", (void *)ptr, size, rc, strerror(rc));
		errno = rc;
		bytes_read = -1;
	}

	return (ssize_t)bytes_read;
}
ssize_t pread64(int fd, void *buf, size_t size, off_t offset) __attribute__ ((alias("pread")));
ssize_t __pread64(int fd, void *buf, size_t size, off_t offset) __attribute__ ((alias("pread")));

ssize_t
write_comm(ssize_t (*real_write)(int fd, const void *buf, size_t size), int fd, const void *buf,
	   size_t size)
{
	ssize_t rc;

	if (fd >= FD_FILE_BASE) {
		rc = pwrite(fd, buf, size, file_list[fd-FD_FILE_BASE].offset);
		if (rc >= 0)
			file_list[fd-FD_FILE_BASE].offset += rc;
		return rc;
	} else {
		return real_write(fd, buf, size);
	}
}

static ssize_t
new_write_libc(int fd, const void *buf, size_t size)
{
	return write_comm(real_write_libc, fd, buf, size);
}

static ssize_t
new_write_pthread(int fd, const void *buf, size_t size)
{
	return write_comm(real_write_pthread, fd, buf, size);
}

ssize_t
pwrite(int fd, const void *buf, size_t size, off_t offset)
{
	char *ptr = (char *)buf;
	int rc;
	d_iov_t iov;
	d_sg_list_t sgl;

	if (size == 0)
		return 0;

	if (real_pwrite == NULL)	{
		real_pwrite = dlsym(RTLD_NEXT, "pwrite64");
		assert(real_pwrite != NULL);
	}

	if (fd < FD_FILE_BASE)
		return real_pwrite(fd, buf, size, offset);

	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, (void *)ptr, size);
	sgl.sg_iovs = &iov;
	rc = dfs_write(dfs, file_list[fd-FD_FILE_BASE].file_obj, &sgl, offset, NULL);
	if (rc) {
		printf("dfs_write(%p, %zu) failed (%d): %s\n",
		       (void *)ptr, size, rc, strerror(rc));
		errno = rc;
		return (-1);
	}

	return size;
}
ssize_t pwrite64(int fd, const void *buf, size_t size, off_t offset)
	__attribute__ ((alias("pwrite")));
ssize_t __pwrite64(int fd, const void *buf, size_t size, off_t offset)
	__attribute__ ((alias("pwrite")));

static int
new_fxstat(int vers, int fd, struct stat *buf)
{
	int rc;

	if (fd < FD_FILE_BASE) {
		return real_fxstat(vers, fd, buf);
	} else if (fd < FD_DIR_BASE) {
		rc = dfs_ostat(dfs, file_list[fd-FD_FILE_BASE].file_obj, buf);
		buf->st_ino = file_list[fd-FD_FILE_BASE].st_ino;
	} else {
		rc = dfs_ostat(dfs, dir_list[fd-FD_DIR_BASE].dir_obj, buf);
		buf->st_ino = FAKE_ST_INO(dir_list[fd-FD_DIR_BASE].path);
	}

	if (rc) {
		printf("Failed to call dfs_ostat. %s\n", strerror(rc));
		errno = rc;
		rc = -1;
	}

	return 0;
}

static int
new_xstat(int ver, const char *path, struct stat *stat_buf)
{
	int is_target_path, rc;
	dfs_obj_t *parent;
	char item_name[MAX_FILE_NAME_LEN];
	char parent_dir[MAX_FILE_NAME_LEN];
	char full_path[MAX_FILE_NAME_LEN];

	parse_path(path, &is_target_path, &parent, item_name, parent_dir, full_path);
	if (!is_target_path)
		return real_xstat(ver, path, stat_buf);

	if (!parent && (strncmp(item_name, "/", 2) == 0))
		rc = dfs_stat(dfs, NULL, NULL, stat_buf);
	else
		rc = dfs_stat(dfs, parent, item_name, stat_buf);
	if (rc) {
		errno = rc;
		return (-1);
	}

	stat_buf->st_ino = FAKE_ST_INO(full_path);

	return 0;
}

static int
new_fxstatat(int ver, int dirfd, const char *path, struct stat *stat_buf, int flags)
{
	char full_path[MAX_FILE_NAME_LEN+4];

	if (path[0] == '/')
		/* absolute path, dirfd is ignored */
		return new_xstat(1, path, stat_buf);

	if (dirfd >= FD_DIR_BASE) {
		sprintf(full_path, "%s/%s", dir_list[dirfd - FD_DIR_BASE].path, path);
		return new_xstat(1, full_path, stat_buf);
	} else if (dirfd == AT_FDCWD) {
		sprintf(full_path, "%s/%s", cur_dir, path);
		return new_xstat(1, full_path, stat_buf);
	}

	return real_fxstatat(ver, dirfd, path, stat_buf, flags);
}

static void
copy_stat_to_statx(const struct stat *stat_buf, struct statx *statx_buf)
{
	memset(statx_buf, 0, sizeof(struct statx));
	statx_buf->stx_blksize = stat_buf->st_blksize;
	statx_buf->stx_nlink = stat_buf->st_nlink;
	statx_buf->stx_uid = stat_buf->st_uid;
	statx_buf->stx_gid = stat_buf->st_gid;
	statx_buf->stx_mode = stat_buf->st_mode;
	statx_buf->stx_ino = stat_buf->st_ino;
	statx_buf->stx_size = stat_buf->st_size;
	statx_buf->stx_blocks = stat_buf->st_blocks;

	statx_buf->stx_atime.tv_sec = stat_buf->st_atim.tv_sec;
	statx_buf->stx_atime.tv_nsec = stat_buf->st_atim.tv_nsec;

	statx_buf->stx_btime.tv_sec = stat_buf->st_mtim.tv_sec;
	statx_buf->stx_btime.tv_nsec = stat_buf->st_mtim.tv_nsec;

	statx_buf->stx_ctime.tv_sec = stat_buf->st_ctim.tv_sec;
	statx_buf->stx_ctime.tv_nsec = stat_buf->st_ctim.tv_nsec;

	statx_buf->stx_mtime.tv_sec = stat_buf->st_mtim.tv_sec;
	statx_buf->stx_mtime.tv_nsec = stat_buf->st_mtim.tv_nsec;
}

int
statx(int dirfd, const char *path, int flags, unsigned int mask, struct statx *statx_buf)
{
	char full_path[MAX_FILE_NAME_LEN+4];
	struct stat stat_buf;
	int rc;

	if (real_statx == NULL)	{
		real_statx = dlsym(RTLD_NEXT, "statx");
		assert(real_statx != NULL);
	}
	if (!inited)
		return real_statx(dirfd, path, flags, mask, statx_buf);

	if (path[0] == '/') {
		/* absolute path, dirfd is ignored */
		rc = new_xstat(1, path, &stat_buf);
		copy_stat_to_statx(&stat_buf, statx_buf);
		return rc;
	}

	if (dirfd >= FD_DIR_BASE) {
		sprintf(full_path, "%s/%s", dir_list[dirfd - FD_DIR_BASE].path, path);
		rc = new_xstat(1, full_path, &stat_buf);
		copy_stat_to_statx(&stat_buf, statx_buf);
		return rc;
	} else if (dirfd == AT_FDCWD) {
		sprintf(full_path, "%s/%s", cur_dir, path);
		rc = new_xstat(1, full_path, &stat_buf);
		copy_stat_to_statx(&stat_buf, statx_buf);
		return rc;
	}

	return real_statx(dirfd, path, flags, mask, statx_buf);
}

static int
new_lxstat(int ver, const char *path, struct stat *stat_buf)
{
	int is_target_path, rc;
	dfs_obj_t *parent;
	char item_name[MAX_FILE_NAME_LEN];
	char parent_dir[MAX_FILE_NAME_LEN];
	char full_path[MAX_FILE_NAME_LEN];

	parse_path(path, &is_target_path, &parent, item_name, parent_dir, full_path);
	if (!is_target_path)
		return real_xstat(ver, path, stat_buf);

	/* Need to compare with using dfs_lookup!!! */
	if (!parent && (strncmp(item_name, "/", 2) == 0))
		rc = dfs_stat(dfs, NULL, NULL, stat_buf);
	else
		rc = dfs_stat(dfs, parent, item_name, stat_buf);
	if (rc) {
		errno = rc;
		return (-1);
	}

	stat_buf->st_ino = FAKE_ST_INO(full_path);
	return 0;
}

static off_t
lseek_comm(off_t (*real_lseek)(int fd, off_t offset, int whence), int fd, off_t offset, int whence)
{
	int rc;
	off_t new_offset;
	struct stat fstat;

	if (fd < FD_FILE_BASE)
		return real_lseek(fd, offset, whence);

	switch (whence) {
	case SEEK_SET:
		new_offset = offset;
		break;
	case SEEK_CUR:
		new_offset = file_list[fd-FD_FILE_BASE].offset + offset;
		break;
	case SEEK_END:
		fstat.st_size = 0;
		rc = new_fxstat(1, fd, &fstat);
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

	file_list[fd-FD_FILE_BASE].offset = new_offset;
	return new_offset;
}

static off_t
new_lseek_libc(int fd, off_t offset, int whence)
{
	return lseek_comm(real_lseek_libc, fd, offset, whence);
}

static off_t
new_lseek_pthread(int fd, off_t offset, int whence)
{
	return lseek_comm(real_lseek_pthread, fd, offset, whence);
}

int
statfs(const char *pathname, struct statfs *sfs)
{
	daos_pool_info_t info = {.pi_bits = DPI_SPACE};
	dfs_obj_t *parent;
	int rc, is_target_path;
	char item_name[MAX_FILE_NAME_LEN];
	char parent_dir[MAX_FILE_NAME_LEN];

	if (real_statfs == NULL) {
		real_statfs = dlsym(RTLD_NEXT, "statfs");
		assert(real_statfs != NULL);
	}

	if (!inited)
		return real_statfs(pathname, sfs);

	parse_path(pathname, &is_target_path, &parent, item_name, parent_dir, NULL);
	if (!is_target_path)
		return real_statfs(pathname, sfs);

	rc = daos_pool_query(poh, NULL, &info, NULL, NULL);
	assert(rc == 0);

	sfs->f_blocks = info.pi_space.ps_space.s_total[DAOS_MEDIA_SCM]
			+ info.pi_space.ps_space.s_total[DAOS_MEDIA_NVME];
	sfs->f_bfree = info.pi_space.ps_space.s_free[DAOS_MEDIA_SCM]
			+ info.pi_space.ps_space.s_free[DAOS_MEDIA_NVME];
	sfs->f_bsize = 1;
	sfs->f_files = -1;
	sfs->f_ffree = -1;
	sfs->f_bavail = sfs->f_bfree;

	if (rc)
		rc = -1;
	return rc;
}
int statfs64(const char *pathname, struct statfs64 *sfs) __attribute__ ((alias("statfs")));
int __statfs(const char *pathname, struct statfs *sfs) __attribute__ ((alias("statfs")));

int
statvfs(const char *pathname, struct statvfs *svfs)
{
	daos_pool_info_t info = {.pi_bits = DPI_SPACE};
	dfs_obj_t *parent;
	int rc, is_target_path;
	char item_name[MAX_FILE_NAME_LEN];
	char parent_dir[MAX_FILE_NAME_LEN];

	if (real_statvfs == NULL)	{
		real_statvfs = dlsym(RTLD_NEXT, "statvfs");
		assert(real_statvfs != NULL);
	}

	if (!inited)
		return real_statvfs(pathname, svfs);

	parse_path(pathname, &is_target_path, &parent, item_name, parent_dir, NULL);
	if (!is_target_path)
		return real_statvfs(pathname, svfs);

	rc = daos_pool_query(poh, NULL, &info, NULL, NULL);
	assert(rc == 0);

	svfs->f_blocks = info.pi_space.ps_space.s_total[DAOS_MEDIA_SCM]
			+ info.pi_space.ps_space.s_total[DAOS_MEDIA_NVME];
	svfs->f_bfree = info.pi_space.ps_space.s_free[DAOS_MEDIA_SCM]
			+ info.pi_space.ps_space.s_free[DAOS_MEDIA_NVME];
	svfs->f_bsize = 1;
	svfs->f_files = -1;
	svfs->f_ffree = -1;
	svfs->f_bavail = svfs->f_bfree;

	if (rc) {
		errno = rc;
		rc = -1;
	}
	return rc;
}
int statvfs64 (const char *__restrict pathname, struct statvfs64 *__restrict svfs)
	__attribute__ ((alias("statvfs")));

DIR *
opendir(const char *path)
{
	int is_target_path, idx_dirfd, rc;
	char parent_dir[MAX_FILE_NAME_LEN];
	char item_name[MAX_FILE_NAME_LEN];
	char full_path[MAX_FILE_NAME_LEN];
	dfs_obj_t *parent, *dir_obj;
	mode_t mode;
/*	struct dirent *ent_list = NULL; */

	if (real_opendir == NULL)	{
		real_opendir = dlsym(RTLD_NEXT, "opendir");
		assert(real_opendir != NULL);
	}
	if (!inited)
		return real_opendir(path);

	parse_path(path, &is_target_path, &parent, item_name, parent_dir, full_path);
	if (!is_target_path)
		return real_opendir(path);

	if (!parent && (strncmp(item_name, "/", 2) == 0))
		rc = dfs_lookup(dfs, "/", O_RDWR, &dir_obj, &mode, NULL);
	else
		rc = dfs_open(dfs, parent, item_name, S_IFDIR, O_RDONLY, 0, 0, NULL, &dir_obj);

/*	rc = dfs_lookup(dfs, full_path, O_RDWR, &dir_obj, &mode, NULL); */

/*	rc = dfs_lookup(dfs, full_path, O_RDONLY | O_NOFOLLOW, &dir_obj, &mode, NULL); */
	if (rc) {
		errno = rc;
		return NULL;
	}

	idx_dirfd = find_next_available_dirfd();
	assert(idx_dirfd >= 0);

	dir_list[idx_dirfd].fd = idx_dirfd + FD_DIR_BASE;
	dir_list[idx_dirfd].offset = 0;
	dir_list[idx_dirfd].dir_obj = dir_obj;
	dir_list[idx_dirfd].num_ents = 0;
	memset(&(dir_list[idx_dirfd].anchor), 0, sizeof(daos_anchor_t));
	sprintf(dir_list[idx_dirfd].path, "%s%s", fs_root, full_path);
/**
 *	ent_list = dir_list[idx_dirfd].ents;
 *
 *	ent_list[0].d_ino = 0;
 *	ent_list[0].d_off = 0;
 *	ent_list[0].d_reclen = 0;
 *	ent_list[0].d_type = DT_DIR;
 *	strcpy(ent_list[0].d_name, ".");
 *
 *	ent_list[1].d_ino = 0;
 *	ent_list[1].d_off = 0;
 *	ent_list[1].d_reclen = 0;
 *	ent_list[1].d_type = DT_DIR;
 *	strcpy(ent_list[1].d_name, "..");
 */
	return (DIR *)(&(dir_list[idx_dirfd]));
}

DIR *
fdopendir(int fd)
{
	if (real_fdopendir == NULL)	{
		real_fdopendir = dlsym(RTLD_NEXT, "fdopendir");
		assert(real_fdopendir != NULL);
	}
	if (!inited)
		return real_fdopendir(fd);

	if (fd < FD_DIR_BASE)
		return real_fdopendir(fd);

	return (DIR *)(&(dir_list[fd - FD_DIR_BASE]));
}

int
openat(int dirfd, const char *pathname, int oflags, ...)
{
	unsigned int mode;
	int two_args = 1;
	char full_path[MAX_FILE_NAME_LEN+4];

	if (real_openat == NULL)	{
		real_openat = dlsym(RTLD_NEXT, "openat");
		assert(real_openat != NULL);
	}

	if (oflags & O_CREAT)	{
		va_list arg;

		va_start(arg, oflags);
		mode = va_arg(arg, unsigned int);
		va_end(arg);
		two_args = 0;
	}

	if (!inited)
		goto out;

	if (dirfd >= FD_DIR_BASE) {
		sprintf(full_path, "%s/%s", dir_list[dirfd - FD_DIR_BASE].path, pathname);
		if (two_args)
			return open_common(real_open_libc, "new_openat", full_path, oflags);
		else
			return open_common(real_open_libc, "new_openat", full_path, oflags, mode);
	} else if (dirfd == AT_FDCWD) {
		if (strncmp(pathname, fs_root, len_fs_root) == 0)	{
			if (two_args)
				return open_common(real_open_libc, "new_openat", pathname, oflags);
			else
				return open_common(real_open_libc, "new_openat", pathname, oflags,
						   mode);
		}
	}

out:
	if (two_args)
		return real_openat(dirfd, pathname, oflags);
	else
		return real_openat(dirfd, pathname, oflags, mode);
}
int openat64(int dirfd, const char *pathname, int oflags, ...) __attribute__ ((alias("openat")));

int
__openat_2(int dirfd, const char *pathname, int oflags)
{
	char full_path[MAX_FILE_NAME_LEN+4];

	if (real_openat_2 == NULL)	{
		real_openat_2 = dlsym(RTLD_NEXT, "__openat_2");
		assert(real_openat_2 != NULL);
	}
	if (!inited)
		return real_openat_2(dirfd, pathname, oflags);

	if (dirfd >= FD_DIR_BASE) {
		sprintf(full_path, "%s/%s", dir_list[dirfd - FD_DIR_BASE].path, pathname);
		return open_common(real_open_libc, "__openat_2", full_path, oflags);
	} else if (dirfd == AT_FDCWD) {
		if (strncmp(pathname, fs_root, len_fs_root) == 0)
			return open_common(real_open_libc, "__openat_2", pathname, oflags);
	}

	return real_openat(dirfd, pathname, oflags);
}

int
closedir(DIR *dirp)
{
	int fd;

	if (real_closedir == NULL)	{
		real_closedir = dlsym(RTLD_NEXT, "closedir");
		assert(real_closedir != NULL);
	}
	if (!inited)
		return real_closedir(dirp);

	if (!dirp)	{
		printf("dirp == NULL in closedir().\nQuit\n");
		errno = EINVAL;
		return (-1);
	}

	fd = dirfd(dirp);
	if (fd >= FD_DIR_BASE)	{
		free_dirfd(dirfd(dirp) - FD_DIR_BASE);
		return 0;
	} else {
		return real_closedir(dirp);
	}
}

static struct dirent *
new_readdir(DIR *dirp)
{
	int rc = 0;
	struct DIRSTATUS *mydir = (struct DIRSTATUS *)dirp;

	if (mydir->fd < FD_FILE_BASE) {
		return real_readdir(dirp);
	} else if (mydir->fd < FD_DIR_BASE) {
		printf("Error: invalid fd in readdir.\n");
		errno = EINVAL;
		return NULL;
	}

	if (mydir->num_ents)
		goto out_readdir;

	mydir->num_ents = READ_DIR_BATCH_SIZE;
	while (!daos_anchor_is_eof(&mydir->anchor)) {
		rc = dfs_readdir(dfs, mydir->dir_obj, &mydir->anchor, &mydir->num_ents,
				 mydir->ents);
		if (rc != 0)
			goto out_null_readdir;

		/* We have an entry, so return it */
		if (mydir->num_ents != 0)
			goto out_readdir;
	}

out_null_readdir:
	mydir->num_ents = 0;
	errno = rc;
	return NULL;
out_readdir:
	mydir->num_ents--;
	mydir->offset++;
/*	mydir->ents[mydir->num_ents].d_ino = mydir->offset; */
	return &mydir->ents[mydir->num_ents];
}

/**
 *char* envp_lib[] = {
 *	"LD_PRELOAD=/scratch/leihuan1/tickets/12142/latest/daos/install/lib64/libpil4dfs.so",
 *	"DAOS_POOL=testpool",
 *	"DAOS_CONTAINER=testcont",
 *	"DAOS_MOUNT_POINT=/dfs", NULL };
 *
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
	int is_target_path, rc;
	char item_name[MAX_FILE_NAME_LEN];
	char parent_dir[MAX_FILE_NAME_LEN];
	dfs_obj_t *parent;

	if (real_mkdir == NULL)	{
		real_mkdir = dlsym(RTLD_NEXT, "mkdir");
		assert(real_mkdir != NULL);
	}
	if (!inited)
		return real_mkdir(path, mode);

	parse_path(path, &is_target_path, &parent, item_name, parent_dir, NULL);
	if (!is_target_path)
		return real_mkdir(path, mode);

	rc = dfs_mkdir(dfs, parent, item_name, mode & mode_not_umask, 0);
	if (rc) {
		errno = rc;
		return (-1);
	} else {
		return 0;
	}
}

int mkdirat(int dirfd, const char *path, mode_t mode)
{
	int rc;

	if (real_mkdirat == NULL)	{
		real_mkdirat = dlsym(RTLD_NEXT, "mkdirat");
		assert(real_mkdirat != NULL);
	}
	if (!inited)
		return real_mkdirat(dirfd, path, mode);

	if (dirfd < FD_DIR_BASE)
		return real_mkdirat(dirfd, path, mode);

	rc = dfs_mkdir(dfs, dir_list[dirfd - FD_DIR_BASE].dir_obj, path, mode, 0);
	if (rc) {
		errno = rc;
		return (-1);
	} else {
		return 0;
	}
}

int rmdir(const char *path)
{
	int is_target_path, rc;
	char item_name[MAX_FILE_NAME_LEN];
	char parent_dir[MAX_FILE_NAME_LEN];
	dfs_obj_t *parent;

	if (real_rmdir == NULL)	{
		real_rmdir = dlsym(RTLD_NEXT, "rmdir");
		assert(real_rmdir != NULL);
	}
	if (!inited)
		return real_rmdir(path);

	parse_path(path, &is_target_path, &parent, item_name, parent_dir, NULL);
	if (!is_target_path)
		return real_rmdir(path);

	rc = dfs_remove(dfs, parent, item_name, false, NULL);
	if (rc) {
		errno = rc;
		return (-1);
	} else {
		return 0;
	}
}

int rename(const char *old_name, const char *new_name)
{
	int is_target_path, rc;
	char item_name_old[MAX_FILE_NAME_LEN], item_name_new[MAX_FILE_NAME_LEN];
	char parent_dir_old[MAX_FILE_NAME_LEN], parent_dir_new[MAX_FILE_NAME_LEN];
	dfs_obj_t *parent_old, *parent_new;

	if (real_rename == NULL)	{
		real_rename = dlsym(RTLD_NEXT, "rename");
		assert(real_rename != NULL);
	}
	if (!inited)
		return real_rename(old_name, new_name);

	parse_path(old_name, &is_target_path, &parent_old, item_name_old, parent_dir_old, NULL);
	if (!is_target_path)
		return real_rename(old_name, new_name);

	if (parent_old == NULL) {
		printf("rename(): Failed to lookup parent: %s\n", old_name);
		/* NEED to double check !!! */
		errno = ENOTDIR;
		return (-1);
	}

	parse_path(new_name, &is_target_path, &parent_new, item_name_new, parent_dir_new, NULL);
	if (!is_target_path)
		return real_rename(old_name, new_name);

	if (parent_new == NULL) {
		printf("rename(): Failed to lookup parent: %s\n", new_name);
		/* NEED to double check !!! */
		errno = ENOTDIR;
		return (-1);
	}

	/* assume both src and dest are in the same container */
	rc = dfs_move(dfs, parent_old, item_name_old, parent_new, item_name_new, NULL);
	if (rc) {
		errno = rc;
		return (-1);
	}
	return 0;
}

char
*getcwd(char *buf, size_t size)
{
	if (real_getcwd == NULL) {
		real_getcwd = dlsym(RTLD_NEXT, "getcwd");
		assert(real_getcwd != NULL);
	}

	if (!inited)
		return real_getcwd(buf, size);

	if (cur_dir[0] != '/')
		update_cwd();

	if (strncmp(cur_dir, fs_root, len_fs_root) != 0)
		return real_getcwd(buf, size);

	if (buf == NULL) {
		char *szPath = NULL;

		szPath = (char *)malloc(strlen(cur_dir) + 256);
		if (szPath == NULL)      {
			printf("Fail to allocate memory for szPath in getcwd().\nQuit\n");
			exit(1);
		}
		strcpy(szPath, cur_dir);
		return szPath;
	}

	strcpy(buf, cur_dir);
	return buf;
}

int
isatty(int fd)
{
	if (real_isatty == NULL)	{
		real_isatty = dlsym(RTLD_NEXT, "isatty");
		assert(real_isatty != NULL);
	}
	if (!inited)
		return real_isatty(fd);

	if (fd >= FD_FILE_BASE)	{
		/* non-terminal */
		return 0;
	} else {
		return real_isatty(fd);
	}
}
int __isatty(int fd) __attribute__ ((alias("isatty")));

int
access(const char *path, int mode)
{
	char full_path[MAX_FILE_NAME_LEN];
	char item_name[MAX_FILE_NAME_LEN];
	char parent_dir[MAX_FILE_NAME_LEN];
	dfs_obj_t *parent;
	int rc, is_target_path;

	if (real_access == NULL)	{
		real_access = dlsym(RTLD_NEXT, "access");
		assert(real_access != NULL);
	}
	if (!inited)
		return real_access(path, mode);

	parse_path(path, &is_target_path, &parent, item_name, parent_dir, full_path);

	if (is_target_path) {
		rc = dfs_access(dfs, parent, item_name, mode);
		if (rc) {
			errno = rc;
			return (-1);
		}
		return 0;
	} else {
		return real_access(path, mode);
	}
}

int
faccessat(int dirfd, const char *path, int mode, int flags)
{
	char full_path[MAX_FILE_NAME_LEN+4];

	if (real_faccessat == NULL)	{
		real_faccessat = dlsym(RTLD_NEXT, "faccessat");
		assert(real_faccessat != NULL);
	}
	if (!inited)
		return real_faccessat(dirfd, path, mode, flags);

	if (path[0] == '/') {
		/* absolute path, dirfd is ignored */
		return access(path, mode);
	}

	if (dirfd >= FD_DIR_BASE) {
		sprintf(full_path, "%s/%s", dir_list[dirfd - FD_DIR_BASE].path, path);
		return access(full_path, mode);
	} else if (dirfd == AT_FDCWD) {
		sprintf(full_path, "%s/%s", cur_dir, path);
		return access(full_path, mode);
	}

	return real_access(path, mode);
}

int
chdir(const char *path)
{
	int is_target_path, rc;
	char full_path[MAX_FILE_NAME_LEN];
	char item_name[MAX_FILE_NAME_LEN];
	char parent_dir[MAX_FILE_NAME_LEN];
	dfs_obj_t *parent;
	struct stat stat_buf;

	if (real_chdir == NULL)  {
		real_chdir = dlsym(RTLD_NEXT, "chdir");
		assert(real_chdir != NULL);
	}
	if (!inited)
		return real_chdir(path);

	parse_path(path, &is_target_path, &parent, item_name, parent_dir, full_path);
	if (!is_target_path) {
		rc = real_chdir(path);
		if (rc == 0)
			update_cwd();
		return rc;
	}

	if (!parent && (strncmp(item_name, "/", 2) == 0))
		rc = dfs_stat(dfs, NULL, NULL, &stat_buf);
	else
		rc = dfs_stat(dfs, parent, item_name, &stat_buf);
	if (rc) {
		errno = rc;
		return (-1);
	}
	if (!S_ISDIR(stat_buf.st_mode)) {
		printf("chdir(): %s is not a directory.\n", path);
		errno = ENOTDIR;
		return (-1);
	}
	sprintf(cur_dir, "%s%s", fs_root, full_path);
	return 0;
}

int
fchdir(int dirfd)
{
	if (real_fchdir == NULL)  {
		real_fchdir = dlsym(RTLD_NEXT, "fchdir");
		assert(real_fchdir != NULL);
	}
	if (!inited)
		return real_fchdir(dirfd);

	if (dirfd < FD_DIR_BASE)
		return real_fchdir(dirfd);

	strcpy(cur_dir, dir_list[dirfd - FD_DIR_BASE].path);
	return 0;
}

static int
new_unlink(const char *path)
{
	int is_target_path, rc;
	char item_name[MAX_FILE_NAME_LEN];
	char parent_dir[MAX_FILE_NAME_LEN];
	dfs_obj_t *parent;

	parse_path(path, &is_target_path, &parent, item_name, parent_dir, NULL);
	if (!is_target_path)
		return real_unlink(path);

	rc = dfs_remove(dfs, parent, item_name, false, NULL);
	if (rc) {
		errno = rc;
		return (-1);
	} else {
		return 0;
	}
}

int
unlinkat(int dirfd, const char *path, int flags)
{
	char full_path[MAX_FILE_NAME_LEN+4];

	if (real_unlinkat == NULL)	{
		real_unlinkat = dlsym(RTLD_NEXT, "unlinkat");
		assert(real_unlinkat != NULL);
	}
	if (!inited)
		return real_unlinkat(dirfd, path, flags);

	if (path[0] == '/') {
		/* absolute path, dirfd is ignored */
		return new_unlink(path);
	}

	if (dirfd >= FD_DIR_BASE) {
		sprintf(full_path, "%s/%s", dir_list[dirfd - FD_DIR_BASE].path, path);
		return new_unlink(full_path);
	} else if (dirfd == AT_FDCWD) {
		sprintf(full_path, "%s/%s", cur_dir, path);
		return new_unlink(full_path);
	}

	return real_unlinkat(dirfd, path, flags);
}

int
fsync(int fd)
{
	if (real_fsync == NULL)  {
		real_fsync = dlsym(RTLD_NEXT, "fsync");
		assert(real_fsync != NULL);
	}
	if (!inited)
		return real_fsync(fd);

	if (fd < FD_FILE_BASE) {
		return real_fsync(fd);
	} else if (fd >= FD_DIR_BASE) {
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

	if (real_ftruncate == NULL)  {
		real_ftruncate = dlsym(RTLD_NEXT, "ftruncate");
		assert(real_ftruncate != NULL);
	}
	if (!inited)
		return real_ftruncate(fd, length);

	if (fd < FD_FILE_BASE) {
		return real_ftruncate(fd, length);
	} else if (fd >= FD_DIR_BASE) {
		errno = EINVAL;
		return (-1);
	}

	rc = dfs_punch(dfs, file_list[fd-FD_FILE_BASE].file_obj, length, DFS_MAX_FSIZE);
	if (rc) {
		errno = rc;
		return (-1);
	}
	return 0;
}

int
truncate(const char *path, off_t length)
{
	int is_target_path, rc;
	char item_name[MAX_FILE_NAME_LEN];
	char parent_dir[MAX_FILE_NAME_LEN];
	dfs_obj_t *parent, *file_obj;
	mode_t mode;

	if (real_truncate == NULL)	{
		real_truncate = dlsym(RTLD_NEXT, "truncate");
		assert(real_truncate != NULL);
	}
	if (!inited)
		return real_truncate(path, length);

	parse_path(path, &is_target_path, &parent, item_name, parent_dir, NULL);
	if (!is_target_path)
		return real_truncate(path, length);

	/* rc = dfs_lookup(dfs, full_path, O_RDWR, &file_obj, &mode, NULL); */
	rc = dfs_open(dfs, parent, item_name, S_IFREG, O_RDWR, 0, 0, NULL, &file_obj);
	if (rc) {
		errno = rc;
		return (-1);
	}
	if (!S_ISREG(mode)) {
		printf("truncate(): %s is not a regular file.\n", path);
		errno = ENOTDIR;
		return (-1);
	}
	rc = dfs_punch(dfs, file_obj, length, DFS_MAX_FSIZE);
	dfs_release(file_obj);
	if (rc) {
		errno = rc;
		return (-1);
	}
	return 0;
}

int
chmod(const char *path, mode_t mode)
{
	int rc, is_target_path;
	char item_name[MAX_FILE_NAME_LEN];
	char parent_dir[MAX_FILE_NAME_LEN];
	char full_path[MAX_FILE_NAME_LEN];
	dfs_obj_t *parent;

	if (real_chmod == NULL)	{
		real_chmod = dlsym(RTLD_NEXT, "chmod");
		assert(real_chmod != NULL);
	}

	if (!inited)
		return real_chmod(path, mode);

	parse_path(path, &is_target_path, &parent, item_name, parent_dir, full_path);

	if (is_target_path) {
		rc = dfs_chmod(dfs, parent, item_name, mode);
		if (rc) {
			errno = rc;
			return (-1);
		}
	}

	return real_chmod(path, mode);
}

int
fchmod(int fd, mode_t mode)
{
	int rc;

	if (real_fchmod == NULL)	{
		real_fchmod = dlsym(RTLD_NEXT, "fchmod");
		assert(real_fchmod != NULL);
	}

	if (!inited)
		return real_fchmod(fd, mode);

	if (fd < FD_FILE_BASE) {
		return real_fchmod(fd, mode);
	} else if (fd >= FD_DIR_BASE) {
		errno = EINVAL;
		return (-1);
	}

	rc = dfs_chmod(dfs, file_list[fd-FD_FILE_BASE].parent, file_list[fd-FD_FILE_BASE].item_name,
		       mode);
	if (rc) {
		errno = rc;
		return (-1);
	}

	return 0;
}

int
fchmodat(int dirfd, const char *path, mode_t mode, int flag)
{
	if (real_fchmodat == NULL)	{
		real_fchmodat = dlsym(RTLD_NEXT, "fchmodat");
		assert(real_fchmodat != NULL);
	}

	if (!inited)
		return real_fchmodat(dirfd, path, mode, flag);

/**	if (path[0] == '/')	{
 *	}
 */
	if (dirfd == AT_FDCWD)	{
		if (strncmp(cur_dir, fs_root, len_fs_root) != 0)
			return real_fchmodat(dirfd, path, mode, flag);
	} else if (dirfd < FD_FILE_BASE) {
		return real_fchmodat(dirfd, path, mode, flag);
	} else if (dirfd < FD_DIR_BASE) {
		errno = EINVAL;
		return (-1);
	}

	/* Need more work!!! */
/**
 *	rc = dfs_chmod(dfs, dir_list[dirfd-FD_DIR_BASE].dir_obj, path, mode);
 *	if (rc) {
 *		errno = rc;
 *		return (-1);
 *	}
 */
	return 0;
}

int
utime(const char *path, const struct utimbuf *times)
{
	int is_target_path, rc;
	char item_name[MAX_FILE_NAME_LEN];
	char parent_dir[MAX_FILE_NAME_LEN];
	char full_path[MAX_FILE_NAME_LEN];
	dfs_obj_t *file_obj, *parent;
	struct stat	stbuf;
/*	mode_t mode_query; */
	struct timespec times_loc;

	if (real_utime == NULL)	{
		real_utime = dlsym(RTLD_NEXT, "utime");
		assert(real_utime != NULL);
	}
	if (!inited)
		return real_utime(path, times);

	parse_path(path, &is_target_path, &parent, item_name, parent_dir, full_path);
	if (!is_target_path)
		return real_utime(path, times);

	/* rc = dfs_lookup(dfs, full_path, S_IFREG, &file_obj, &mode_query, &stbuf); */
	rc = dfs_open(dfs, parent, item_name, S_IFREG, O_RDWR, 0, 0, NULL, &file_obj);
	if (rc) {
		printf("utime> Error: Fail to lookup %s. %s\n", full_path, strerror(rc));
		errno = rc;
		return (-1);
	}

	if (times == NULL) {
		clock_gettime(CLOCK_REALTIME, &times_loc);
		stbuf.st_atim.tv_sec = times_loc.tv_sec;
		stbuf.st_atim.tv_nsec = times_loc.tv_nsec;
		stbuf.st_mtim.tv_sec = times_loc.tv_sec;
		stbuf.st_mtim.tv_nsec = times_loc.tv_nsec;
	} else {
		stbuf.st_atim.tv_sec = times->actime;
		stbuf.st_atim.tv_nsec = 0;
		stbuf.st_mtim.tv_sec = times->modtime;
		stbuf.st_mtim.tv_nsec = 0;
	}

	rc = dfs_osetattr(dfs, file_obj, &stbuf, DFS_SET_ATTR_ATIME | DFS_SET_ATTR_MTIME);
	if (rc) {
		errno = rc;
		return (-1);
	}

	rc = dfs_release(file_obj);
	if (rc) {
		errno = rc;
		return (-1);
	}

	return 0;
}

int
utimes(const char *path, const struct timeval times[2])
{
	int is_target_path, rc;
	char item_name[MAX_FILE_NAME_LEN];
	char parent_dir[MAX_FILE_NAME_LEN];
	char full_path[MAX_FILE_NAME_LEN];
	dfs_obj_t *file_obj, *parent;
	struct stat	stbuf;
/*	mode_t mode_query; */
	struct timespec times_loc;

	if (real_utimes == NULL)	{
		real_utimes = dlsym(RTLD_NEXT, "utimes");
		assert(real_utimes != NULL);
	}
	if (!inited)
		return real_utimes(path, times);

	parse_path(path, &is_target_path, &parent, item_name, parent_dir, full_path);
	if (!is_target_path)
		return real_utimes(path, times);

	/* rc = dfs_lookup(dfs, full_path, S_IFREG, &file_obj, &mode_query, &stbuf); */
	rc = dfs_open(dfs, parent, item_name, S_IFREG, O_RDWR, 0, 0, NULL, &file_obj);
	if (rc) {
		printf("utime> Error: Fail to lookup %s. %s\n", full_path, strerror(rc));
		errno = rc;
		return (-1);
	}

	if (times == NULL) {
		clock_gettime(CLOCK_REALTIME, &times_loc);
		stbuf.st_atim.tv_sec = times_loc.tv_sec;
		stbuf.st_atim.tv_nsec = times_loc.tv_nsec;
		stbuf.st_mtim.tv_sec = times_loc.tv_sec;
		stbuf.st_mtim.tv_nsec = times_loc.tv_nsec;
	} else {
		stbuf.st_atim.tv_sec = times[0].tv_sec;
		stbuf.st_atim.tv_nsec = times[0].tv_usec * 1000;
		stbuf.st_mtim.tv_sec = times[1].tv_sec;
		stbuf.st_mtim.tv_nsec = times[1].tv_usec * 1000;
	}

	rc = dfs_osetattr(dfs, file_obj, &stbuf, DFS_SET_ATTR_ATIME | DFS_SET_ATTR_MTIME);
	if (rc) {
		errno = rc;
		return (-1);
	}

	rc = dfs_release(file_obj);
	if (rc) {
		errno = rc;
		return (-1);
	}

	return 0;
}

static int
new_utimens_timespec(const char *path, const struct timespec times[2])
{
	int is_target_path, rc;
	char item_name[MAX_FILE_NAME_LEN];
	char parent_dir[MAX_FILE_NAME_LEN];
	char full_path[MAX_FILE_NAME_LEN];
	dfs_obj_t *file_obj, *parent;
	struct stat	stbuf;
	struct timespec times_loc;
	struct timeval times_us[2];

	parse_path(path, &is_target_path, &parent, item_name, parent_dir, full_path);
	if (!is_target_path) {
		times_us[0].tv_sec = times[0].tv_sec;
		times_us[0].tv_usec = times[0].tv_nsec/100;
		times_us[1].tv_sec = times[1].tv_sec;
		times_us[1].tv_usec = times[1].tv_nsec/100;
		return real_utimes(path, times_us);
	}

	/* rc = dfs_lookup(dfs, full_path, S_IFREG, &file_obj, &mode_query, &stbuf); */
	rc = dfs_open(dfs, parent, item_name, S_IFREG, O_RDWR, 0, 0, NULL, &file_obj);
	if (rc) {
		printf("utime> Error: Fail to dfs_open %s. %s\n", full_path, strerror(rc));
		errno = rc;
		return (-1);
	}

	if (times == NULL) {
		clock_gettime(CLOCK_REALTIME, &times_loc);
		stbuf.st_atim.tv_sec = times_loc.tv_sec;
		stbuf.st_atim.tv_nsec = times_loc.tv_nsec;
		stbuf.st_mtim.tv_sec = times_loc.tv_sec;
		stbuf.st_mtim.tv_nsec = times_loc.tv_nsec;
	} else {
		stbuf.st_atim.tv_sec = times[0].tv_sec;
		stbuf.st_atim.tv_nsec = times[0].tv_nsec;
		stbuf.st_mtim.tv_sec = times[1].tv_sec;
		stbuf.st_mtim.tv_nsec = times[1].tv_nsec;
	}

	rc = dfs_osetattr(dfs, file_obj, &stbuf, DFS_SET_ATTR_ATIME | DFS_SET_ATTR_MTIME);
	if (rc) {
		errno = rc;
		return (-1);
	}

	rc = dfs_release(file_obj);
	if (rc) {
		errno = rc;
		return (-1);
	}

	return 0;
}

/**
 * TODO:
 *	The flags field is a bit mask that may be 0, or include the
 *	following constant, defined in <fcntl.h>:
 *
 *	AT_SYMLINK_NOFOLLOW
 *	If pathname specifies a symbolic link, then update the
 *	timestamps of the link, rather than the file to which it
 *	refers.
 */
int
utimensat(int dirfd, const char *path, const struct timespec times[2], int flags)
{
	char full_path[MAX_FILE_NAME_LEN+4];

	if (real_utimensat == NULL)	{
		real_utimensat = dlsym(RTLD_NEXT, "utimensat");
		assert(real_utimensat != NULL);
	}
	if (!inited)
		return real_utimensat(dirfd, path, times, flags);

	if (path == NULL) {
		errno = EINVAL;
		return -1;
	}

	if (path[0] == '/') {
		/* absolute path, dirfd is ignored */
		return new_utimens_timespec(path, times);
	}

	if (dirfd >= FD_DIR_BASE) {
		sprintf(full_path, "%s/%s", dir_list[dirfd - FD_DIR_BASE].path, path);
		return new_utimens_timespec(full_path, times);
	} else if (dirfd == AT_FDCWD) {
		sprintf(full_path, "%s/%s", cur_dir, path);
		return new_utimens_timespec(full_path, times);
	}

	return utimensat(dirfd, path, times, flags);
}

int
futimens(int fd, const struct timespec times[2])
{
	int rc;
	struct timespec times_loc;
	struct stat	stbuf;

	if (real_futimens == NULL)	{
		real_futimens = dlsym(RTLD_NEXT, "futimens");
		assert(real_futimens != NULL);
	}
	if (!inited)
		return real_futimens(fd, times);

	if (fd < FD_FILE_BASE)
		return real_futimens(fd, times);

	if (times == NULL) {
		clock_gettime(CLOCK_REALTIME, &times_loc);
		stbuf.st_atim.tv_sec = times_loc.tv_sec;
		stbuf.st_atim.tv_nsec = times_loc.tv_nsec;
		stbuf.st_mtim.tv_sec = times_loc.tv_sec;
		stbuf.st_mtim.tv_nsec = times_loc.tv_nsec;
	} else {
		stbuf.st_atim.tv_sec = times[0].tv_sec;
		stbuf.st_atim.tv_nsec = times[0].tv_nsec;
		stbuf.st_mtim.tv_sec = times[1].tv_sec;
		stbuf.st_mtim.tv_nsec = times[1].tv_nsec;
	}

	rc = dfs_osetattr(dfs, file_list[fd-FD_FILE_BASE].file_obj, &stbuf,
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
	int fd_save, fd_Directed, param, OrgFunc = 1, fd_dup2ed_Dest = -1, Next_Dirfd, Next_fd;
	int dup_next;
	va_list arg;

	fd_Directed = Get_Fd_Redirected(fd);
	fd_save = fd_Directed;

	switch (cmd)     {
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

		if (!inited)
			return real_fcntl(fd, cmd, param);

		if (cmd == F_GETFL)	{
			if (fd_Directed >= FD_DIR_BASE)
				return dir_list[fd_Directed - FD_DIR_BASE].open_flag;
			else if (fd_Directed >= FD_FILE_BASE)
				return file_list[fd_Directed - FD_FILE_BASE].open_flag;
			else
				return real_fcntl(fd_Directed, cmd);
		}

		fd_dup2ed_Dest = query_fd_forward_dest(fd_Directed);
		if (fd_dup2ed_Dest >= FD_FILE_BASE)	{
			if (cmd == F_SETFD)
				return 0;
			else if (cmd == F_GETFL)
				return file_list[fd_dup2ed_Dest - FD_FILE_BASE].open_flag;
		}

		if (fd_Directed >= FD_DIR_BASE) {
			fd_Directed -= FD_DIR_BASE;
			OrgFunc = 0;
		} else if (fd_Directed >= FD_FILE_BASE) {
			fd_Directed -= FD_FILE_BASE;
			OrgFunc = 0;
		}

		if ((cmd == F_DUPFD) || (cmd == F_DUPFD_CLOEXEC))	{
			if (fd_save >= FD_DIR_BASE)	{
				if (fd_save == DUMMY_FD_DIR)	{
					printf("ERROR> Unexpected fd == DUMMY_FD_DIR in \n"
						"fcntl(fd, F_DUPFD / F_DUPFD_CLOEXEC)\n");
					return (-1);
				}

				Next_Dirfd = find_next_available_dirfd();
				memcpy(&(dir_list[Next_Dirfd]), &(dir_list[fd_Directed]),
					sizeof(struct DIRSTATUS));
				dup_next = dir_list[fd_Directed].fd_dup_next;
				dir_list[fd_Directed].fd_dup_next = Next_Dirfd;
				dir_list[Next_Dirfd].fd_dup_pre = fd_Directed;
				dir_list[Next_Dirfd].fd_dup_next = dup_next;
				if (dup_next >= 0)
					dir_list[Next_Dirfd].fd_dup_pre = Next_Dirfd;
				dir_list[Next_Dirfd].ref_count = 1;
				return (Next_Dirfd + FD_DIR_BASE);
			} else if (fd_save >= FD_FILE_BASE) {
				Next_fd = find_next_available_fd();
				memcpy(&(file_list[Next_fd]), &(file_list[fd_Directed]),
					sizeof(struct FILESTATUS));
				dup_next = file_list[fd_Directed].fd_dup_next;
				file_list[fd_Directed].fd_dup_next = Next_fd;
				file_list[Next_fd].fd_dup_pre = fd_Directed;
				file_list[Next_fd].fd_dup_next = dup_next;
				if (dup_next >= 0)
					file_list[Next_fd].fd_dup_pre = Next_fd;
				file_list[Next_fd].ref_count = 1;
				return (Next_fd + FD_FILE_BASE);
			}
		} else if ((cmd == F_GETFD) || (cmd == F_SETFD)) {
			if (OrgFunc == 0)
				return 0;
		}
/**		else if (cmd == F_GETFL)	{
 *		}
*/
		return real_fcntl(fd, cmd, param);
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

		if (!inited)
			return real_fcntl(fd, cmd, param);

		return real_fcntl(fd, cmd, param);
	default:
		return real_fcntl(fd, cmd);
	}

	return real_fcntl(fd, cmd);
}


struct dfuse_user_reply {
	uid_t uid;
	gid_t gid;
};

int
ioctl(int fd, unsigned long request, ...)
{
	va_list arg;
	void *param;
	struct dfuse_user_reply *reply;

	va_start(arg, request);
	param = va_arg(arg, void *);
	va_end(arg);

	if (real_ioctl == NULL)	{
		real_ioctl = dlsym(RTLD_NEXT, "ioctl");
		assert(real_ioctl != NULL);
	}
	if (!inited)
		return real_ioctl(fd, request, param);

	if (fd < FD_FILE_BASE)
		return real_ioctl(fd, request, param);

	if (request == 0xffffffff8008a3ca) {
		/* DFUSE_IOCTL_DFUSE_USER */
		reply = (struct dfuse_user_reply *)param;
		reply->uid = getuid();
		reply->gid = getgid();
		return 0;
	}

	printf("Not implemented yet for ioctl().\n");
	errno = ENOTSUP;
	return -1;
}

int
dup(int oldfd)
{
	int fd_Directed, fd, idx;

	if (real_dup == NULL)	{
		real_dup = dlsym(RTLD_NEXT, "dup");
		assert(real_dup != NULL);
	}
	if (!inited)
		return real_dup(oldfd);

	if (oldfd >= FD_FILE_BASE) {
		fd = allocate_a_fd_from_kernel();
		idx = find_free_fd_dup2_list();
		if (idx >= 0) {
			fd_dup2_list[idx].fd_src = fd;
			fd_dup2_list[idx].fd_dest = oldfd;
			file_list[oldfd - FD_FILE_BASE].ref_count++;
			num_fd_dup2ed++;
			return fd;
		} else {
			return idx;
		}
	}

	fd_Directed = Get_Fd_Redirected(oldfd);

	if (fd_Directed >= FD_FILE_BASE) {
		fd = allocate_a_fd_from_kernel();
		idx = find_free_fd_dup2_list();
		if (idx >= 0) {
			fd_dup2_list[idx].fd_src = fd;
			fd_dup2_list[idx].fd_dest = fd_Directed;
			file_list[fd_Directed - FD_FILE_BASE].ref_count++;
			num_fd_dup2ed++;
			return fd;
		} else {
			return idx;
		}
	} else {
		return real_dup(oldfd);
	}

	return -1;
}

int
dup2(int oldfd, int newfd)
{
	int fd, fd_Directed, idx, rc;

	if (real_dup2 == NULL)	{
		real_dup2 = dlsym(RTLD_NEXT, "dup2");
		assert(real_dup2 != NULL);
	}
	if (!inited)
		return real_dup2(oldfd, newfd);
	if (oldfd == newfd) {
		if (oldfd < FD_FILE_BASE)
			return real_dup2(oldfd, newfd);
		else
			return newfd;
	}
	if ((oldfd < FD_FILE_BASE) && (newfd < FD_FILE_BASE))
		return real_dup2(oldfd, newfd);

	fd_Directed = query_fd_forward_dest(newfd);
	if (fd_Directed >= FD_FILE_BASE) {
		printf("Not implemented yet.\n");
		errno = ENOTSUP;
	} else {
		fd_Directed = query_fd_forward_dest(oldfd);
		if (oldfd >= FD_FILE_BASE)
			fd_Directed = oldfd;
		if (fd_Directed >= FD_FILE_BASE) {
			rc = close(newfd);
			if (rc != 0)
				return -1;
			fd = allocate_a_fd_from_kernel();
			if (fd != newfd) {
				printf("allocate_a_fd_from_kernel() failed to get the \n"
					"desired fd.\n");
				errno = EAGAIN;
				return (-1);
			}
			idx = find_free_fd_dup2_list();
			if (idx >= 0) {
				fd_dup2_list[idx].fd_src = fd;
				fd_dup2_list[idx].fd_dest = fd_Directed;
				file_list[fd_Directed - FD_FILE_BASE].ref_count++;
				num_fd_dup2ed++;
				return fd;
			} else {
				return idx;
			}
		} else {
			real_dup2(oldfd, newfd);
		}
	}
	return -1;
}
int __dup2(int oldfd, int newfd) __attribute__ ((alias("dup2")));
/**
 *int
 *dup3(int oldfd, int newfd, int flags)
 *{
 *	int i, fd_Directed;
 *
 *	if (real_dup3 == NULL)	{
 *		real_dup3 = dlsym(RTLD_NEXT, "dup3");
 *		assert(real_dup3 != NULL);
 *	}
 *	if (!inited)
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
int
posix_fadvise(int fd, off_t offset, off_t len, int advice)
{
	if (real_posix_fadvise == NULL)	{
		real_posix_fadvise = dlsym(RTLD_NEXT, "posix_fadvise");
		assert(real_posix_fadvise != NULL);
	}
	if (!inited)
		return real_posix_fadvise(fd, offset, len, advice);

	if (fd < FD_FILE_BASE)
		return real_posix_fadvise(fd, offset, len, advice);

	printf("Error: DAOS does not support posix_fadvise yet.\n");
	errno = ENOTSUP;
	return -1;
}
int posix_fadvise64(int fd, off_t offset, off_t len, int advice)
	__attribute__ ((alias("posix_fadvise")));

int
flock(int fd, int operation)
{
	if (real_flock == NULL)	{
		real_flock = dlsym(RTLD_NEXT, "flock");
		assert(real_flock != NULL);
	}
	if (!inited)
		return real_flock(fd, operation);

	if (fd < FD_FILE_BASE)
		return real_flock(fd, operation);

	printf("Error: DAOS does not support flock yet.\n");
	errno = ENOTSUP;
	return -1;
}

int
fallocate(int fd, int mode, off_t offset, off_t len)
{
	if (real_fallocate == NULL)	{
		real_fallocate = dlsym(RTLD_NEXT, "fallocate");
		assert(real_fallocate != NULL);
	}
	if (!inited)
		return real_fallocate(fd, mode, offset, len);

	if (fd < FD_FILE_BASE)
		return real_fallocate(fd, mode, offset, len);

	printf("Error: DAOS does not support fallocate yet.\n");
	errno = ENOTSUP;
	return -1;
}

int
posix_fallocate(int fd, off_t offset, off_t len)
{
	if (real_posix_fallocate == NULL)	{
		real_posix_fallocate = dlsym(RTLD_NEXT, "posix_fallocate");
		assert(real_posix_fallocate != NULL);
	}
	if (!inited)
		return real_posix_fallocate(fd, offset, len);

	if (fd < FD_FILE_BASE)
		return real_posix_fallocate(fd, offset, len);

	printf("Error: DAOS does not support posix_fallocate yet.\n");
	errno = ENOTSUP;
	return -1;
}

int
posix_fallocate64(int fd, off64_t offset, off64_t len)
{
	if (real_posix_fallocate64 == NULL)	{
		real_posix_fallocate64 = dlsym(RTLD_NEXT,
			"posix_fallocate64");
		assert(real_posix_fallocate64 != NULL);
	}
	if (!inited)
		return real_posix_fallocate64(fd, offset, len);

	if (fd < FD_FILE_BASE)
		return real_posix_fallocate64(fd, offset, len);

	printf("Error: DAOS does not support posix_fallocate64 yet.\n");
	errno = ENOTSUP;
	return -1;
}

int
tcgetattr(int fd, void *termios_p)
{
	if (real_tcgetattr == NULL)	{
		real_tcgetattr = dlsym(RTLD_NEXT, "tcgetattr");
		assert(real_tcgetattr != NULL);
	}

	if (fd < FD_FILE_BASE)
		return real_tcgetattr(fd, termios_p);

	printf("Error: DAOS does not support tcgetattr yet.\n");
	errno = ENOTSUP;
	return -1;
}

static void
update_cwd(void)
{
	char *cwd = NULL;

	cwd = get_current_dir_name();

	if (cwd == NULL) {
		printf("Fail to get CWD with get_current_dir_name().\nQuit\n");
		exit(1);
	} else {
		strcpy(cur_dir, cwd);
		free(cwd);
	}
}

static __attribute__((constructor)) void init_myhook(void)
{
	mode_t umask_old;

	umask_old = umask(0);
	umask(umask_old);
	mode_not_umask = ~umask_old;
	update_cwd();

	init_fd_list();

	register_a_hook("ld", "open64", (void *)new_open_ld, (long int *)(&real_open_ld));
	register_a_hook("libc", "open64", (void *)new_open_libc, (long int *)(&real_open_libc));
	register_a_hook("libpthread", "open64", (void *)new_open_pthread,
		(long int *)(&real_open_pthread));

	register_a_hook("libc", "__close", (void *)new_close_libc, (long int *)(&real_close_libc));
	register_a_hook("libpthread", "__close", (void *)new_close_pthread,
		(long int *)(&real_close_pthread));
	register_a_hook("libc", "__close_nocancel", (void *)new_close_nocancel,
		(long int *)(&real_close_nocancel));

	register_a_hook("libc", "__read", (void *)new_read_libc, (long int *)(&real_read_libc));
	register_a_hook("libpthread", "__read", (void *)new_read_pthread,
		(long int *)(&real_read_pthread));
	register_a_hook("libc", "__write", (void *)new_write_libc, (long int *)(&real_write_libc));
	register_a_hook("libpthread", "__write", (void *)new_write_pthread,
		(long int *)(&real_write_pthread));

	register_a_hook("libc", "lseek64", (void *)new_lseek_libc, (long int *)(&real_lseek_libc));
	register_a_hook("libpthread", "lseek64", (void *)new_lseek_pthread,
		(long int *)(&real_lseek_pthread));

	register_a_hook("libc", "unlink", (void *)new_unlink, (long int *)(&real_unlink));

	register_a_hook("libc", "__fxstat", (void *)new_fxstat, (long int *)(&real_fxstat));
	register_a_hook("libc", "__xstat", (void *)new_xstat, (long int *)(&real_xstat));
	/* Many variants for lxstat: _lxstat, __lxstat, ___lxstat, __lxstat64 */
	register_a_hook("libc", "__lxstat", (void *)new_lxstat, (long int *)(&real_lxstat));
	register_a_hook("libc", "__fxstatat", (void *)new_fxstatat, (long int *)(&real_fxstatat));
	register_a_hook("libc", "readdir", (void *)new_readdir, (long int *)(&real_readdir));

	register_a_hook("libc", "fcntl", (void *)new_fcntl, (long int *)(&real_fcntl));

/**	register_a_hook("libc", "execve", (void *)new_execve, (long int *)(&real_execve));
*	register_a_hook("libc", "execvp", (void *)new_execvp, (long int *)(&real_execvp));
*	register_a_hook("libc", "execv", (void *)new_execv, (long int *)(&real_execv));
*	register_a_hook("libc", "fork", (void *)new_fork, (long int *)(&real_fork));
 */

	init_dfs();
	install_hook();
	init_fd_dup2_list();

	inited = 1;
}

static __attribute__((destructor)) void finalize_myhook(void)
{
	close_all_duped_fd();

	uninstall_hook();
	finalize_dfs();

	pthread_mutex_destroy(&lock_dirfd);
	pthread_mutex_destroy(&lock_fd);

	free(file_list);
	free(dir_list);
}

static void
init_dfs(void)
{
	int rc;
	char *pool = NULL;
	char *container = NULL;

	pool = getenv("DAOS_POOL");
	if (pool == NULL) {
		printf("DAOS_POOL is not set.\n");
		exit(1);
	}

	container = getenv("DAOS_CONTAINER");
	if (container == NULL) {
		printf("DAOS_CONTAINER is not set.\n");
		exit(1);
	}

	fs_root = getenv("DAOS_MOUNT_POINT");
	if (fs_root == NULL) {
		printf("DAOS_MOUNT_POINT is not set.\n");
		exit(1);
	}
	len_fs_root = strlen(fs_root);

	rc = daos_init();
	assert(rc == 0);
	rc = daos_pool_connect(pool, NULL, DAOS_PC_RW, &poh, NULL, NULL);
	assert(rc == 0);
	rc = daos_cont_open(poh, container, DAOS_COO_RW, &coh, NULL, NULL);
	assert(rc == 0);
	rc = dfs_mount(poh, coh, O_RDWR, &dfs);
	assert(rc == 0);

	rc = d_hash_table_create(D_HASH_FT_EPHEMERAL | D_HASH_FT_NOLOCK | D_HASH_FT_LRU, 6, NULL,
				 &hdl_hash_ops, &dfs_dir_hash);
}

static void
finalize_dfs(void)
{
	int rc;

	while (1) {
		d_list_t *rlink = NULL;

		rlink = d_hash_rec_first(dfs_dir_hash);
		if (rlink == NULL)
			break;
		d_hash_rec_decref(dfs_dir_hash, rlink);
	}

	rc = d_hash_table_destroy(dfs_dir_hash, false);
	assert(rc == 0);

	rc = dfs_umount(dfs);
	assert(rc == 0);
	rc = daos_cont_close(coh, NULL);
	assert(rc == 0);
	rc = daos_pool_disconnect(poh, NULL);
	assert(rc == 0);
	rc = daos_fini();
	assert(rc == 0);
}

