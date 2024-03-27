package client

import (
	"unsafe"

	"github.com/pkg/errors"
)

/*
#include <daos.h>
#include <daos/common.h>
*/
import "C"

type (
	// RequestBuffers is a slice of byte slices.
	RequestBuffers [][]byte

	// SGList wraps a C.daos_sg_list_t.
	SGList struct {
		complete bool
		buffers  RequestBuffers
		sgl      C.d_sg_list_t
	}
)

// NewSGList initializes a DAOS scatter-gather list with the
// provided RequestBuffers slices.
func NewSGList(bufs RequestBuffers) (*SGList, error) {
	if len(bufs) == 0 {
		return nil, errors.New("empty RequestBuffers")
	}

	bufNr := C.size_t(len(bufs))
	iovPtr := (*C.d_iov_t)(C.calloc(bufNr, C.sizeof_d_iov_t))

	iovs := unsafe.Slice(iovPtr, bufNr)
	for i := range iovs {
		if len(bufs[i]) == 0 {
			return nil, errors.Errorf("buffer %d is zero-length", i)
		}
		iovs[i].iov_len = C.daos_size_t(len(bufs[i]))
		iovs[i].iov_buf_len = C.daos_size_t(cap(bufs[i]))
		iovs[i].iov_buf = unsafe.Pointer(&bufs[i][0]) // zero copy
	}

	return &SGList{
		buffers: bufs,
		sgl: C.d_sg_list_t{
			sg_nr:   C.uint32_t(bufNr),
			sg_iovs: iovPtr,
		},
	}, nil
}
