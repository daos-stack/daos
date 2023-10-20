package api

import (
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"syscall"
	"time"

	"github.com/pkg/errors"
)

/*
#include <sys/stat.h>
#include <fcntl.h>

#include <daos.h>
#include <daos_fs.h>
*/
import "C"

var (
	_ fs.FS         = (*Filesystem)(nil)
	_ fs.ReadDirFS  = (*Filesystem)(nil)
	_ fs.ReadFileFS = (*Filesystem)(nil)
	_ fs.StatFS     = (*Filesystem)(nil)
	_ fs.SubFS      = (*Filesystem)(nil)
	_ fs.File       = (*FilesystemFile)(nil)
	//_ io.ReadWriteCloser = (*FilesystemFile)(nil)
	_ io.WriterAt = (*FilesystemFile)(nil)
	_ io.ReaderAt = (*FilesystemFile)(nil)
)

type AccessFlag uint

const (
	AccessFlagReadOnly  AccessFlag = C.O_RDONLY
	AccessFlagReadWrite AccessFlag = C.O_RDWR
	AccessFlagExclusive AccessFlag = C.O_EXCL
	AccessFlagCreate    AccessFlag = C.O_CREAT
	AccessFlagTruncate  AccessFlag = C.O_TRUNC
)

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

type dfsObject struct {
	dfs  *C.dfs_t
	obj  *C.dfs_obj_t
	name string
}

func (o *dfsObject) Close() error {
	return dfsError(C.dfs_release(o.obj))
}

func (o *dfsObject) Stat() (fs.FileInfo, error) {
	var stat dfsStat_t

	if err := dfsError(C.dfs_ostat(o.dfs, o.obj, &stat.cst)); err != nil {
		return nil, err
	}
	stat.name = o.name
	stat.fillMode()

	return &stat, nil
}

type FilesystemFile struct {
	dfsObject
}

func (f *FilesystemFile) Read(buf []byte) (int, error) {
	return 0, errors.New("unimplemented")
}

func (f *FilesystemFile) ReadAt(buf []byte, off int64) (int, error) {
	sgl, err := NewSGList(RequestBuffers{buf})
	if err != nil {
		return 0, err
	}
	var readSize C.daos_size_t

	err = dfsError(C.dfs_read(f.dfs, f.obj, &sgl.sgl, C.daos_off_t(off), &readSize, nil))
	if err != nil {
		return 0, err
	}
	return int(readSize), nil
}

func (f *FilesystemFile) Write(buf []byte) (int, error) {
	return 0, errors.New("unimplemented")
}

func (f *FilesystemFile) WriteAt(buf []byte, off int64) (int, error) {
	sgl, err := NewSGList(RequestBuffers{buf})
	if err != nil {
		return 0, err
	}
	err = dfsError(C.dfs_write(f.dfs, f.obj, &sgl.sgl, C.daos_off_t(off), nil))
	if err != nil {
		return 0, err
	}
	return len(buf), nil
}

type FilesystemAttribute struct{}
type FilesystemAttributes []*FilesystemAttribute

type Filesystem struct {
	dfs *C.dfs_t
}

func MountFilesystem(pool *PoolHandle, cont *ContainerHandle, flags ...AccessFlag) (*Filesystem, error) {
	if len(flags) == 0 {
		flags = append(flags, AccessFlagReadOnly)
	}
	var cFlags C.int
	for _, flag := range flags {
		cFlags |= C.int(flag)
	}

	var fs Filesystem
	if err := daosError(C.dfs_mount(pool.daosHandle, cont.daosHandle, cFlags, &fs.dfs)); err != nil {
		return nil, err
	}

	return &fs, nil
}

func (f *Filesystem) Unmount() error {
	return daosError(C.dfs_umount(f.dfs))
}

func (f *Filesystem) Query() (FilesystemAttributes, error) {
	var attrs C.dfs_attr_t

	if err := daosError(C.dfs_query(f.dfs, &attrs)); err != nil {
		return nil, err
	}

	return nil, nil
}

type dfsOpenArgs struct {
	parent       *C.dfs_obj_t
	oclassID     C.daos_oclass_id_t
	mode         C.mode_t
	flags        C.int
	chunkSize    C.daos_size_t
	symlinkValue *C.char
}

type dfsOpenOption interface {
	apply(*dfsOpenArgs)
}

type openParentOpt dfsObject

func (o *openParentOpt) apply(args *dfsOpenArgs) {
	args.parent = o.obj
}

func WithOpenParent(parent *dfsObject) *openParentOpt {
	return &openParentOpt{
		obj: parent.obj,
	}
}

type openObjectClass ObjectClass

func (o openObjectClass) apply(args *dfsOpenArgs) {
	args.oclassID = C.daos_oclass_id_t(o)
}

func WithOpenObjectClass(class ObjectClass) openObjectClass {
	return openObjectClass(class)
}

type openChunkSize C.daos_size_t

func (o openChunkSize) apply(args *dfsOpenArgs) {
	args.chunkSize = C.daos_size_t(o)
}

func WithOpenChunkSize(size uint64) openChunkSize {
	return openChunkSize(size)
}

type openMode C.mode_t

func (o openMode) apply(args *dfsOpenArgs) {
	args.mode = C.mode_t(o)
}

func WithOpenMode(mode os.FileMode) openMode {
	return openMode(mode)
}

type openFlags C.int

func (o openFlags) apply(args *dfsOpenArgs) {
	args.flags = C.int(o)
}

func WithOpenFlags(inFlags ...AccessFlag) openFlags {
	var outFlags openFlags
	for _, flag := range inFlags {
		outFlags |= openFlags(flag)
	}
	return outFlags
}

func (f *Filesystem) OpenDFS(name string, opts ...dfsOpenOption) (*dfsObject, error) {
	log.Debugf("Open(%s%s)", name, func() string {
		if len(opts) == 0 {
			return ""
		}
		return fmt.Sprintf(":%+v", opts)
	}())

	var fsObj dfsObject
	var args dfsOpenArgs
	for _, opt := range opts {
		opt.apply(&args)
	}

	if args.parent == nil {
		dir, file := filepath.Split(name)
		if file != name {
			name = file

			parent, err := f.Lookup(filepath.Clean(dir), AccessFlagReadWrite)
			if err != nil {
				return nil, errors.Wrapf(err, "failed to look up parent of %s", name)
			}
			args.parent = parent.obj
		}
	}
	if args.mode == 0 {
		args.mode = C.S_IFREG | C.S_IWUSR | C.S_IRUSR
	}
	if args.flags == 0 {
		args.flags = C.O_RDWR | C.O_CREAT | C.O_EXCL
	}

	cName := C.CString(name)
	defer freeString(cName)

	if err := dfsError(C.dfs_open(f.dfs, args.parent, cName, args.mode,
		args.flags, args.oclassID, args.chunkSize, args.symlinkValue, &fsObj.obj)); err != nil {
		return nil, errors.Wrapf(err, "failed to open %s", name)
	}
	fsObj.name = name

	return &fsObj, nil
}

func (f *Filesystem) Lookup(path string, flags AccessFlag) (*dfsObject, error) {
	log.Debugf("Lookup(%s)", path)

	var fsObj dfsObject

	cPath := C.CString(path)
	defer freeString(cPath)
	if err := dfsError(C.dfs_lookup(f.dfs, cPath, C.int(flags), &fsObj.obj, nil, nil)); err != nil {
		return nil, &fs.PathError{Op: "lookup", Path: path, Err: err}
	}
	fsObj.name = filepath.Base(path)

	return &fsObj, nil
}

// NB: Filesystem methods below implement the stdlib io/fs.FS interface and
// follow the patterns in os/file.go.

// OpenFile is the generalized Open call; most users will use Open or Create instead.
func (f *Filesystem) OpenFile(name string, flag int, perm fs.FileMode) (*FilesystemFile, error) {
	obj, err := f.OpenDFS(name, WithOpenFlags(AccessFlag(flag)), WithOpenMode(perm|C.S_IFREG))
	if err != nil {
		return nil, &fs.PathError{Op: "open", Path: name, Err: err}
	}

	return &FilesystemFile{
		dfsObject: dfsObject{
			dfs: f.dfs,
			obj: obj.obj,
		},
	}, nil
}

// Open opens the named file for reading.
func (f *Filesystem) Open(name string) (fs.File, error) {
	return f.OpenFile(name, os.O_RDONLY, 0)
}

// Create creates or truncates the named file.
func (f *Filesystem) Create(name string) (*FilesystemFile, error) {
	return f.OpenFile(name, os.O_RDWR|os.O_CREATE|os.O_TRUNC, 0666)
}

func (f *Filesystem) ReadDir(name string) ([]fs.DirEntry, error) {
	return nil, errors.New("unimplemented")
}

func (f *Filesystem) ReadFile(name string) ([]byte, error) {
	return nil, errors.New("unimplemented")
}

func (f *Filesystem) Stat(name string) (fs.FileInfo, error) {
	parent, base := filepath.Split(name)

	parentObj, err := f.Lookup(parent, AccessFlagReadOnly)
	if err != nil && !errors.Is(err, fs.ErrNotExist) {
		return nil, err
	}

	cName := C.CString(base)
	defer freeString(cName)

	var stat dfsStat_t
	if err := dfsError(C.dfs_stat(f.dfs, parentObj.obj, cName, &stat.cst)); err != nil {
		return nil, err
	}
	stat.name = base
	stat.fillMode()

	return &stat, nil
}

func (f *Filesystem) Sub(name string) (fs.FS, error) {
	return nil, errors.New("unimplemented")
}

func (f *Filesystem) Mkdir(name string, perm fs.FileMode) error {
	if name == "" {
		return &fs.PathError{Op: "mkdir", Err: fs.ErrNotExist}
	}

	var parentObj *dfsObject
	parent, dir := filepath.Split(name)
	if parent != "" {
		var err error
		parentObj, err = f.Lookup(parent, AccessFlagReadWrite)
		if err != nil {
			return err
		}
	}

	_, err := f.OpenDFS(dir, WithOpenFlags(AccessFlagReadWrite|AccessFlagCreate), WithOpenMode(perm|C.S_IFDIR), WithOpenParent(parentObj))
	if err != nil {
		var errnoErr *ErrnoErr
		if errors.As(err, errnoErr) && errnoErr.Errno == C.EEXIST {
			err = fs.ErrExist
		}
		return &fs.PathError{Op: "mkdir", Path: name, Err: err}
	}

	return nil
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

func (f *Filesystem) Remove(name string) error {
	var parentObj *dfsObject
	parent, base := filepath.Split(name)
	if parent != "" {
		var err error
		parentObj, err = f.Lookup(parent, AccessFlagReadWrite)
		if err != nil {
			return err
		}
	}

	cName := C.CString(base)
	defer freeString(cName)
	return dfsError(C.dfs_remove(f.dfs, parentObj.obj, cName, false, nil))
}
