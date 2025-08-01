//
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package drpc

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/pkg/errors"
)

func genTestData(len int) []byte {
	data := make([]byte, len)
	char := 'a'
	for i := 0; i < len; i++ {
		data[i] = byte(char)
		if char == 'z' {
			char = 'a'
		} else {
			char++
		}
	}
	return data
}

func genTestBuf(i, numChunks, totalData uint, data []byte) []byte {
	chunk := newChunk(data)
	chunk.setIndex(i)
	chunk.setTotalChunks(numChunks)
	chunk.setTotalDataSize(totalData)

	return chunk.buf
}

func TestDrpc_newChunk(t *testing.T) {
	for name, tc := range map[string]struct {
		data    []byte
		expData []byte
	}{
		"nil payload": {},
		"empty payload": {
			data: []byte{},
		},
		"zeroes": {
			data:    make([]byte, 100),
			expData: make([]byte, 100),
		},
		"small": {
			data:    []byte("supercalifragilisticexpialadocious"),
			expData: []byte("supercalifragilisticexpialadocious"),
		},
		"big": {
			data:    genTestData(maxDataSize),
			expData: genTestData(maxDataSize),
		},
	} {
		t.Run(name, func(t *testing.T) {
			chunk := newChunk(tc.data)

			test.AssertEqual(t, uint(len(tc.data)), chunk.dataSize(), "data length")
			test.CmpAny(t, "chunk data", tc.expData, chunk.dataSlice())
		})
	}
}

func TestDrpc_getChunkFromBuf(t *testing.T) {
	for name, tc := range map[string]struct {
		buf              []byte
		expErr           error
		expIdx           uint
		expTotalChunks   uint
		expDataSize      uint
		expTotalDataSize uint
		expData          []byte
	}{
		"nil buf": {
			expErr: errors.New("nil"),
		},
		"empty buf": {
			buf:    make([]byte, 0),
			expErr: errors.New("smaller than minimum header size"),
		},
		"too small": {
			buf:    make([]byte, headerSize-1),
			expErr: errors.New("smaller than minimum header size"),
		},
		"header only, empty buf": {
			buf: make([]byte, headerSize),
		},
		"data": {
			buf:              genTestBuf(5, 42, 1<<20, genTestData(26)),
			expIdx:           5,
			expTotalChunks:   42,
			expTotalDataSize: 1 << 20,
			expDataSize:      26,
			expData:          genTestData(26),
		},
	} {
		t.Run(name, func(t *testing.T) {
			chunk, err := getChunkFromBuf(tc.buf)

			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				if chunk != nil {
					t.Fatalf("expected nil chunk, got %+v", chunk)
				}
				return
			}

			test.AssertEqual(t, tc.expIdx, chunk.index(), "")
			test.AssertEqual(t, tc.expTotalChunks, chunk.totalChunks(), "")
			test.AssertEqual(t, tc.expDataSize, chunk.dataSize(), "")
			test.AssertEqual(t, tc.expTotalDataSize, chunk.totalDataSize(), "")
			test.CmpAny(t, "compare data slice", tc.expData, chunk.dataSlice())
		})
	}
}

func testChunks(dataChunks ...[]byte) [][]byte {
	chunks := make([][]byte, 0, len(dataChunks))

	totalSize := 0
	for _, data := range dataChunks {
		totalSize += len(data)
	}

	for i, data := range dataChunks {
		chunks = append(chunks, genTestBuf(uint(i), uint(len(dataChunks)), uint(totalSize), data))
	}
	return chunks
}

func TestDrpc_sendMsg(t *testing.T) {
	for name, tc := range map[string]struct {
		conn      *mockConn
		input     []byte
		expErr    error
		expChunks [][]byte
	}{
		"write fails": {
			conn: &mockConn{
				WriteOutputError: errors.New("mock write"),
			},
			input:     []byte("something"),
			expErr:    errors.New("mock write"),
			expChunks: testChunks([]byte("something")),
		},
		"empty message": {
			input:     []byte{},
			expChunks: testChunks([]byte{}),
		},
		"small message": {
			input:     []byte("Bilbo"),
			expChunks: testChunks([]byte("Bilbo")),
		},
		"largest single chunk": {
			input:     genTestData(maxDataSize),
			expChunks: testChunks(genTestData(maxDataSize)),
		},
		"multi chunk": {
			input:     genTestData(2 * maxDataSize),
			expChunks: testChunks(genTestData(maxDataSize), genTestData(2 * maxDataSize)[maxDataSize:]),
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx := test.MustLogContext(t)

			if tc.conn == nil {
				tc.conn = newMockConn()
			}

			err := sendMsg(ctx, tc.conn, tc.input)

			test.CmpErr(t, tc.expErr, err)
			test.CmpAny(t, "chunks sent", tc.expChunks, tc.conn.WriteInputBytes)
		})
	}
}

func TestDrpc_recvMsg(t *testing.T) {
	for name, tc := range map[string]struct {
		conn      *mockConn
		expErr    error
		expResult []byte
	}{
		"read fails": {
			conn: &mockConn{
				ReadOutputError: errors.New("mock read"),
			},
			expErr: errors.New("mock read"),
		},
		"too small": {
			conn: &mockConn{
				ReadOutputBytes: [][]byte{[]byte("junk")},
			},
			expErr: errors.New("smaller than minimum header size"),
		},
		"dropped first chunk": {
			conn: &mockConn{
				ReadOutputBytes: [][]byte{genTestBuf(1, 2, 0, nil)},
			},
			expErr: errors.New("lost chunk"),
		},
		"dropped later chunk": {
			conn: &mockConn{
				ReadOutputBytes: [][]byte{genTestBuf(0, 3, 28, genTestData(12)), genTestBuf(2, 3, 28, genTestData(2))},
			},
			expErr: errors.New("lost chunk"),
		},
		"num chunks changed": {
			conn: &mockConn{
				ReadOutputBytes: [][]byte{genTestBuf(0, 3, 28, genTestData(12)), genTestBuf(1, 4, 28, genTestData(2))},
			},
			expErr: errors.New("inconsistent total chunks"),
		},
		"total data size changed": {
			conn: &mockConn{
				ReadOutputBytes: [][]byte{genTestBuf(0, 2, 28, genTestData(12)), genTestBuf(1, 2, 32, genTestData(20))},
			},
			expErr: errors.New("inconsistent data size"),
		},
		"more data sent than expected": {
			conn: &mockConn{
				ReadOutputBytes: [][]byte{genTestBuf(0, 2, 28, genTestData(12)), genTestBuf(1, 2, 28, genTestData(20))},
			},
			expErr: errors.New("data overflow"),
		},
		"less data sent than expected": {
			conn: &mockConn{
				ReadOutputBytes: [][]byte{genTestBuf(0, 2, 28, genTestData(12)), genTestBuf(1, 2, 28, genTestData(3))},
			},
			expErr: errors.New("expected message of size 28"),
		},
		"empty message": {
			conn: &mockConn{
				ReadOutputBytes: testChunks([]byte{}),
			},
			expResult: []byte{},
		},
		"small message": {
			conn: &mockConn{
				ReadOutputBytes: testChunks([]byte("something")),
			},
			expResult: []byte("something"),
		},
		"largest single chunk": {
			conn: &mockConn{
				ReadOutputBytes: testChunks(genTestData(maxDataSize)),
			},
			expResult: genTestData(maxDataSize),
		},
		"multi chunk": {
			conn: &mockConn{
				ReadOutputBytes: testChunks(genTestData(maxDataSize), genTestData(MaxChunkSize)[maxDataSize:]),
			},
			expResult: genTestData(MaxChunkSize),
		},
	} {
		t.Run(name, func(t *testing.T) {
			ctx := test.MustLogContext(t)

			if tc.conn == nil {
				tc.conn = newMockConn()
			}

			result, err := recvMsg(ctx, tc.conn)

			test.CmpErr(t, tc.expErr, err)
			test.CmpAny(t, "message received", tc.expResult, result)
		})
	}
}
