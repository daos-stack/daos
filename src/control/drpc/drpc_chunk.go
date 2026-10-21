//
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package drpc

import (
	"context"
	"fmt"
	"net"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

/*
#cgo CFLAGS: -I${SRCDIR}/../../include
#include <daos/drpc_types.h>
*/
import "C"

const (
	// MaxChunkSize is the maximum drpc message chunk size that may be sent.
	// Using a packetsocket over the unix domain socket means that we receive
	// a whole message at a time without knowing its size.
	MaxChunkSize = C.UNIXCOMM_MAXMSGSIZE

	// headerSize is the length of the message header in bytes.
	headerSize = C.sizeof_struct_drpc_header

	maxDataSize = MaxChunkSize - headerSize
)

// chunk is a structure used to represent a dRPC message chunk.
type chunk struct {
	buf    []byte
	header *C.struct_drpc_header
	data   *byte
}

func (c *chunk) String() string {
	if c == nil {
		return "(nil)"
	}
	return fmt.Sprintf("chunk (size=%d)", len(c.buf))
}

func (c *chunk) index() uint {
	return uint(c.header.chunk_idx)
}

func (c *chunk) setIndex(i uint) {
	c.header.chunk_idx = C.uint32_t(i)
}

func (c *chunk) totalChunks() uint {
	return uint(c.header.total_chunks)
}

func (c *chunk) setTotalChunks(n uint) {
	c.header.total_chunks = C.uint32_t(n)
}

func (c *chunk) dataSize() uint {
	return uint(c.header.chunk_data_size)
}

func (c *chunk) setDataSize(size uint) {
	c.header.chunk_data_size = C.size_t(size)
}

func (c *chunk) totalDataSize() uint {
	return uint(c.header.total_data_size)
}

func (c *chunk) setTotalDataSize(size uint) {
	c.header.total_data_size = C.size_t(size)
}

func (c *chunk) dataSlice() []byte {
	return unsafe.Slice(c.data, c.dataSize())
}

func (c *chunk) setData(d []byte) error {
	if c.dataSize() != uint(len(d)) {
		return fmt.Errorf("unable to set data of size %d, expected %d", len(d), c.dataSize())
	}
	copy(unsafe.Slice(c.data, c.dataSize()), d)
	return nil
}

func getChunkFromBuf(buf []byte) (*chunk, error) {
	if buf == nil {
		return nil, errors.New("nil buffer")
	}
	if uint(len(buf)) < headerSize {
		return nil, fmt.Errorf("buffer size %d smaller than minimum header size %d", len(buf), headerSize)
	}
	c := &chunk{
		buf:    buf,
		header: (*C.struct_drpc_header)(unsafe.Pointer(&buf[0])),
	}

	if uint(len(buf)) > headerSize {
		// Data starts at the end of the header
		c.data = &buf[headerSize]
	}

	return c, nil
}

func newChunk(data []byte) *chunk {
	chunkSize := len(data) + headerSize
	buf := make([]byte, chunkSize)
	c, err := getChunkFromBuf(buf)
	if err != nil {
		panic(errors.Wrap(err, "initializing chunk"))
	}
	c.setDataSize(uint(len(data)))
	if err := c.setData(data); err != nil {
		panic(errors.Wrap(err, "set chunk data"))
	}
	return c
}

func getNumChunks(msgSize int) int {
	if msgSize == 0 {
		return 1
	}
	n := msgSize / maxDataSize
	if msgSize%maxDataSize != 0 {
		n++
	}
	return n
}

func sendMsg(ctx context.Context, conn net.Conn, buf []byte) error {
	log := logging.FromContext(ctx)

	msgSize := len(buf)
	numChunks := getNumChunks(msgSize)
	bytesSent := 0

	remaining := func() int {
		return len(buf) - bytesSent
	}

	log.Tracef("sending message size=%d in %d chunks", msgSize, numChunks)

	for i := 0; i < numChunks; i++ {
		dataSize := maxDataSize
		if remaining() < maxDataSize {
			dataSize = remaining()
		}

		log.Tracef("creating chunk %d, data size=%d", i, dataSize)
		chunk := newChunk(buf[bytesSent : bytesSent+dataSize])

		chunk.setIndex(uint(i))
		chunk.setTotalChunks(uint(numChunks))
		chunk.setTotalDataSize(uint(msgSize))

		log.Tracef("sending chunk %d, buffer size=%d", i, len(chunk.buf))
		_, err := conn.Write(chunk.buf)
		if err != nil {
			return errors.Wrapf(err, "sending chunk %d", i)
		}
		bytesSent += dataSize
	}

	return nil
}

func recvMsg(ctx context.Context, conn net.Conn) ([]byte, error) {
	log := logging.FromContext(ctx)
	chunkBuf := make([]byte, MaxChunkSize)
	numChunks := uint(1)
	bytesReceived := uint(0)
	expBytes := uint(0)

	var msgBuf []byte
	for chunkIdx := uint(0); chunkIdx < numChunks; chunkIdx++ {
		bytesRead, err := conn.Read(chunkBuf)
		if err != nil {
			return nil, errors.Wrapf(err, "dRPC recv chunk %d", chunkIdx)
		}

		log.Tracef("read chunk %d (size=%d)", chunkIdx, bytesRead)

		chunk, err := getChunkFromBuf(chunkBuf[:bytesRead])
		if err != nil {
			return nil, errors.Wrapf(err, "parse dRPC chunk")
		}

		if chunk.index() != chunkIdx {
			return nil, fmt.Errorf("possible lost chunk, got chunk index=%d, expected=%d", chunk.index(), chunkIdx)
		}

		if chunkIdx == 0 {
			numChunks = chunk.totalChunks()
			expBytes = chunk.totalDataSize()
			msgBuf = make([]byte, expBytes)
			log.Tracef("incoming message: total chunks=%d, total msg size=%d", numChunks, expBytes)
		} else {
			if numChunks != chunk.totalChunks() {
				return nil, fmt.Errorf("inconsistent total chunks noticed at chunk %d (total=%d, previous=%d)",
					chunkIdx, chunk.totalChunks(), numChunks)
			}

			if chunk.totalDataSize() != expBytes {
				return nil, fmt.Errorf("inconsistent data size noticed at chunk %d (total=%d, previous=%d)",
					chunkIdx, chunk.totalDataSize(), expBytes)
			}

			if bytesReceived+chunk.dataSize() > chunk.totalDataSize() {
				return nil, fmt.Errorf("data overflow at chunk %d (expected total=%d, received=%d, new chunk=%d)",
					chunkIdx, expBytes, len(msgBuf), chunk.dataSize())
			}
		}

		copy(msgBuf[bytesReceived:], chunk.dataSlice())
		bytesReceived += chunk.dataSize()
	}

	if bytesReceived != expBytes {
		return nil, fmt.Errorf("expected message of size %d, got %d", expBytes, bytesReceived)
	}

	return msgBuf, nil
}
