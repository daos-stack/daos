package dfs

import (
	"io/fs"
	"syscall"
	"time"
)

/*
#include <sys/stat.h>
*/
import "C"

type dfsStat_t struct {
	name string
	mode fs.FileMode
	cst  C.struct_stat
}

func (st *dfsStat_t) fillMode() {
	st.mode = fs.FileMode(st.cst.st_mode & 0777)
	switch st.cst.st_mode & syscall.S_IFMT {
	case syscall.S_IFBLK:
		st.mode |= fs.ModeDevice
	case syscall.S_IFCHR:
		st.mode |= fs.ModeDevice | fs.ModeCharDevice
	case syscall.S_IFDIR:
		st.mode |= fs.ModeDir
	case syscall.S_IFIFO:
		st.mode |= fs.ModeNamedPipe
	case syscall.S_IFLNK:
		st.mode |= fs.ModeSymlink
	case syscall.S_IFSOCK:
		st.mode |= fs.ModeSocket
	}
	if st.cst.st_mode&syscall.S_ISGID != 0 {
		st.mode |= fs.ModeSetgid
	}
	if st.cst.st_mode&syscall.S_ISUID != 0 {
		st.mode |= fs.ModeSetuid
	}
	if st.cst.st_mode&syscall.S_ISVTX != 0 {
		st.mode |= fs.ModeSticky
	}
}

func (st *dfsStat_t) Mode() fs.FileMode {
	return st.mode
}

func (st *dfsStat_t) IsDir() bool {
	return st.Mode().IsDir()
}

func (st *dfsStat_t) Size() int64 {
	return int64(st.cst.st_size)
}

func (st *dfsStat_t) ModTime() time.Time {
	return time.Unix(int64(st.cst.st_mtim.tv_sec), int64(st.cst.st_mtim.tv_nsec))
}

func (st *dfsStat_t) Name() string {
	return st.name
}

func (st *dfsStat_t) Sys() interface{} {
	return nil
}
