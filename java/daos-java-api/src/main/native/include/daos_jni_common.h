#include <stdio.h>
#include <daos.h>
#include <daos_fs.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <gurt/common.h>

#ifndef _INCLUDED_DAOS_JNI_COMMON
#define _INCLUDED_DAOS_JNI_COMMON

static jint JNI_VERSION = JNI_VERSION_1_8;

static const int READ_DIR_BATCH_SIZE = 10;
static const int READ_DIR_INITIAL_BUFFER_SIZE = 1024;
static const int CUSTOM_ERROR_CODE_BASE = -1000000;

static const int CUSTOM_ERR1 = -1000001; // scm size and nvme size no greater than 0
static const int CUSTOM_ERR2 = -1000002; // failed to parse service replics string
static const int CUSTOM_ERR3 = -1000003; // malloc or realloc buffer failed
static const int CUSTOM_ERR4 = -1000004; // value length greater than expected

#endif
