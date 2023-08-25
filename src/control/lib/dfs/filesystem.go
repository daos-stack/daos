package dfs

import (
	"io"
	"io/fs"
	"os"
	"syscall"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/daos/client"
)

/*
#include <fcntl.h>

#include <daos.h>
#include <daos_fs.h>
#include <daos_fs_sys.h>
*/
import "C"

var (
	_ fs.FS         = (*Filesystem)(nil)
	_ fs.ReadDirFS  = (*Filesystem)(nil)
	_ fs.ReadFileFS = (*Filesystem)(nil)
	_ fs.StatFS     = (*Filesystem)(nil)
	_ fs.SubFS      = (*Filesystem)(nil)
)

type Filesystem struct {
	dfs       *C.struct_dfs_sys
	connected atm.Bool
	mounted   atm.Bool
}

func cHandlePtr(ptrHdl interface{ Pointer() unsafe.Pointer }) (*C.daos_handle_t, error) {
	if ptrHdl == nil || ptrHdl.Pointer() == nil {
		return nil, errors.New("nil handle pointer")
	}

	return (*C.daos_handle_t)(ptrHdl.Pointer()), nil
}

func Connect(poolID, contID, sysID string, mFlags int, sFlags SysFlag) (*Filesystem, error) {
	if poolID == "" || contID == "" {
		return nil, errors.New("missing pool or container ID")
	}
	if sysID == "" {
		sysID = build.DefaultSystemName
	}
	if mFlags == 0 {
		mFlags = os.O_RDONLY
	}

	cPoolID := C.CString(poolID)
	defer freeString(cPoolID)
	cContID := C.CString(contID)
	defer freeString(cContID)
	cSysID := C.CString(sysID)
	defer freeString(cSysID)

	var f Filesystem
	if err := dfsError(C.dfs_sys_connect(cPoolID, cSysID, cContID, C.int(mFlags), C.int(sFlags), nil, &f.dfs)); err != nil {
		return nil, err
	}

	f.connected.SetTrue()
	return &f, nil
}

func (f *Filesystem) Disconnect() error {
	if f.connected.IsFalse() {
		return errors.New("not connected")
	}
	if err := dfsError(C.dfs_sys_disconnect(f.dfs)); err != nil {
		return err
	}

	f.connected.SetFalse()
	return nil
}

func Mount(pool *client.PoolHandle, cont *client.ContainerHandle, mFlags int, sFlags SysFlag) (*Filesystem, error) {
	if mFlags == 0 {
		mFlags = os.O_RDONLY
	}

	poh, err := cHandlePtr(pool)
	if err != nil {
		return nil, err
	}
	coh, err := cHandlePtr(cont)
	if err != nil {
		return nil, err
	}

	var f Filesystem
	if err := dfsError(C.dfs_sys_mount(*poh, *coh, C.int(mFlags), C.int(sFlags), &f.dfs)); err != nil {
		return nil, err
	}

	f.mounted.SetTrue()
	return &f, nil
}

func (f *Filesystem) Unmount() error {
	if f.mounted.IsFalse() {
		return errors.New("not mounted")
	}

	if err := dfsError(C.dfs_sys_umount(f.dfs)); err != nil {
		return err
	}

	f.mounted.SetFalse()
	return nil
}

type dfsOpenArgs struct {
	oclassID     C.daos_oclass_id_t
	chunkSize    C.daos_size_t
	symlinkValue *C.char
}

type dfsOpenOption interface {
	apply(*dfsOpenArgs)
}

type openObjectClass client.ObjectClass

func (o openObjectClass) apply(args *dfsOpenArgs) {
	args.oclassID = C.daos_oclass_id_t(o)
}

func WithOpenObjectClass(class client.ObjectClass) openObjectClass {
	return openObjectClass(class)
}

type openChunkSize C.daos_size_t

func (o openChunkSize) apply(args *dfsOpenArgs) {
	args.chunkSize = C.daos_size_t(o)
}

func WithOpenChunkSize(size uint64) openChunkSize {
	return openChunkSize(size)
}

func (f *Filesystem) OpenDFS(path string, mode fs.FileMode, flags int, opts ...dfsOpenOption) (*File, error) {
	var args dfsOpenArgs
	for _, opt := range opts {
		opt.apply(&args)
	}

	cPath := C.CString(path)
	defer freeString(cPath)

	var fsObj dfsObject
	err := dfsError(C.dfs_sys_open(f.dfs, cPath, C.uint(mode),
		C.int(flags), args.oclassID, args.chunkSize, args.symlinkValue, &fsObj.obj))
	if err != nil {
		return nil, &fs.PathError{Op: "open", Path: path, Err: err}
	}
	fsObj.name = path

	return &File{
		dfsObject: dfsObject{
			dfs: f.dfs,
			obj: fsObj.obj,
		},
		openAppend: flags&os.O_APPEND != 0,
	}, nil
}

// NB: Filesystem methods below implement the stdlib io/fs.FS interface and
// follow the patterns in os/file.go.

// OpenFile is the generalized Open call; most users will use Open or Create instead.
func (f *Filesystem) OpenFile(name string, flags int, perm os.FileMode) (*File, error) {
	return f.OpenDFS(name, perm|C.S_IFREG, flags)
}

// Open opens the named file for reading.
func (f *Filesystem) Open(name string) (fs.File, error) {
	return f.OpenFile(name, os.O_RDONLY, 0)
}

// Create creates or truncates the named file.
func (f *Filesystem) Create(name string) (*File, error) {
	return f.OpenFile(name, os.O_RDWR|os.O_CREATE|os.O_TRUNC, 0666)
}

func (f *Filesystem) ReadDir(name string) ([]fs.DirEntry, error) {
	return nil, errors.New("unimplemented")
}

func (f *Filesystem) ReadFile(name string) ([]byte, error) {
	rf, err := f.Open(name)
	if err != nil {
		return nil, err
	}
	defer rf.Close()

	var size int
	st, err := rf.Stat()
	if err != nil {
		return nil, err
	}
	size = int(st.Size())

	buf := make([]byte, size)
	read, err := rf.Read(buf)
	if err != nil {
		if err == io.EOF {
			err = nil
		}
	}
	if read < size {
		err = io.ErrUnexpectedEOF
	}
	return buf, err
}

func (f *Filesystem) Stat(path string) (fs.FileInfo, error) {
	cPath := C.CString(path)
	defer freeString(cPath)

	var stat dfsStat_t
	if err := dfsError(C.dfs_sys_stat(f.dfs, cPath, 0, &stat.cst)); err != nil {
		return nil, err
	}
	stat.name = path
	stat.fillMode()

	return &stat, nil
}

func (f *Filesystem) Sub(name string) (fs.FS, error) {
	return nil, errors.New("unimplemented")
}

func (f *Filesystem) MkdirDFS(path string, mode fs.FileMode, oclass client.ObjectClass) error {
	if path == "" {
		return &fs.PathError{Op: "mkdir", Err: fs.ErrNotExist}
	}

	cPath := C.CString(path)
	defer freeString(cPath)

	err := dfsError(C.dfs_sys_mkdir(f.dfs, cPath, C.mode_t(mode), C.daos_oclass_id_t(oclass)))
	if err != nil {
		var errnoErr *ErrnoErr
		if errors.As(err, errnoErr) && errnoErr.Errno == C.EEXIST {
			err = fs.ErrExist
		}
		return &fs.PathError{Op: "mkdir", Path: path, Err: err}
	}

	return nil
}

func (f *Filesystem) Mkdir(path string, mode fs.FileMode) error {
	return f.MkdirDFS(path, mode, 0)
}

func (f *Filesystem) MkdirAll(path string, perm fs.FileMode) error {
	// NB: This borrows heavily from the stdlib implemenation of os.MkdirAll().
	dir, err := f.Stat(path)
	if err == nil {
		if dir.IsDir() {
			return nil
		}
		return &fs.PathError{Op: "mkdir", Path: path, Err: syscall.ENOTDIR}
	}

	i := len(path)
	for i > 0 && os.IsPathSeparator(path[i-1]) {
		i--
	}

	j := i
	for j > 0 && !os.IsPathSeparator(path[j-1]) {
		j--
	}

	if j > 1 {
		if err = f.MkdirAll(path[:j-1], perm); err != nil {
			return err
		}
	}

	return f.Mkdir(path, perm)
}

func (f *Filesystem) Remove(path string) error {
	cPath := C.CString(path)
	defer freeString(cPath)

	return dfsError(C.dfs_sys_remove(f.dfs, cPath, false, nil))
}
