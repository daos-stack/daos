package dfs

import (
	"io"
	"io/fs"
	"sync"
	"unsafe"

	"github.com/pkg/errors"
)

/*
#include <daos.h>
#include <daos_fs.h>
#include <daos_fs_sys.h>
*/
import "C"

const (
	// readerBufSize is 1MB as this is what DAOS likes
	readerBufSize = 1 << 20
	// writerBufSize is 1MB as this is what DAOS likes
	writerBufSize = 1 << 20
)

var (
	_ fs.File            = (*File)(nil)
	_ io.ReadWriteCloser = (*File)(nil)
	_ io.WriterTo        = (*File)(nil)
	_ io.WriterAt        = (*File)(nil)
	_ io.ReaderFrom      = (*File)(nil)
	_ io.ReaderAt        = (*File)(nil)
	_ io.Seeker          = (*File)(nil)
)

type File struct {
	sync.Mutex
	dfsObject

	openAppend bool
	position   int64
}

func (f *File) read(buf []byte, off int64) (int, error) {
	readSize := C.daos_size_t(len(buf))

	err := dfsError(C.dfs_sys_read(f.dfs, f.obj, unsafe.Pointer(&buf[0]), C.daos_off_t(off), &readSize, nil))
	if err != nil {
		return 0, err
	}

	if int(readSize) < len(buf) {
		err = errors.Wrapf(io.EOF, "read %d bytes, expected %d", readSize, len(buf))
	}

	f.position = off + int64(readSize)
	return int(readSize), err
}

func (f *File) ReadFrom(r io.Reader) (total int64, _ error) {
	f.Lock()
	defer f.Unlock()

	var err error
	var n int
	buf := make([]byte, writerBufSize)

	for {
		n, err = r.Read(buf)
		total += int64(n)

		if n > 0 {
			_, err = f.write(buf[:n], f.position)
			if err != nil {
				return total, err
			}
		}

		if err != nil {
			if errors.Is(err, io.EOF) {
				err = nil
			}
			return total, err
		}
	}
}

func (f *File) Read(buf []byte) (int, error) {
	f.Lock()
	defer f.Unlock()

	return f.read(buf, f.position)
}

func (f *File) ReadAt(buf []byte, off int64) (int, error) {
	f.Lock()
	defer f.Unlock()

	if off < 0 {
		return 0, &fs.PathError{Op: "ReadAt", Path: f.name, Err: errors.New("negative offset")}
	}

	return f.read(buf, off)
}

func (f *File) write(buf []byte, off int64) (int, error) {
	var writeSize C.daos_size_t = C.daos_size_t(len(buf))

	err := dfsError(C.dfs_sys_write(f.dfs, f.obj, unsafe.Pointer(&buf[0]), C.daos_off_t(off), &writeSize, nil))
	if err != nil {
		return 0, err
	}

	if int(writeSize) < len(buf) {
		err = errors.Wrapf(io.ErrShortWrite, "wrote %d bytes, expected %d", writeSize, len(buf))
	}

	f.position = off + int64(writeSize)
	return int(writeSize), err
}

func (f *File) WriteTo(w io.Writer) (int64, error) {
	f.Lock()
	defer f.Unlock()

	var err error
	var n int
	buf := make([]byte, readerBufSize)

	for {
		n, err = f.read(buf, f.position)
		if err != nil {
			if errors.Is(err, io.EOF) {
				err = nil
			}
			return int64(n), err
		}

		_, err = w.Write(buf[:n])
		if err != nil {
			return int64(n), err
		}
	}
}

func (f *File) Write(buf []byte) (int, error) {
	f.Lock()
	defer f.Unlock()

	return f.write(buf, f.position)
}

func (f *File) WriteAt(buf []byte, off int64) (int, error) {
	f.Lock()
	defer f.Unlock()

	if f.openAppend {
		return 0, errors.New("cannot WriteAt to append-only file")
	}
	if off < 0 {
		return 0, &fs.PathError{Op: "WriteAt", Path: f.name, Err: errors.New("negative offset")}
	}

	return f.write(buf, off)
}

func (f *File) Seek(offset int64, whence int) (int64, error) {
	f.Lock()
	defer f.Unlock()

	switch whence {
	case io.SeekStart:
		f.position = offset
	case io.SeekCurrent:
		f.position += offset
	case io.SeekEnd:
		// end whence not supported for now
		return 0, errors.New("end whence not supported")
	default:
		return 0, errors.New("invalid whence")
	}
	return f.position, nil
}
