//
// (C) Copyright 2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package main

import (
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
	. "github.com/inhies/go-bytesize"
)

// TestGetSize verifies the correct number of bytes are returned from input
// human readable strings
func TestGetSize(t *testing.T) {
	var tests = []struct {
		in     string
		out    uint64
		errMsg string
	}{
		{"", uint64(0), ""},
		{"0", uint64(0), ""},
		{"B", uint64(0), msgSizeNoNumber},
		{"0B", uint64(0), ""},
		{"2g", uint64(2 * GB), ""},
		{"2G", uint64(2 * GB), ""},
		{"2gb", uint64(2 * GB), ""},
		{"2Gb", uint64(2 * GB), ""},
		{"2gB", uint64(2 * GB), ""},
		{"2GB", uint64(2 * GB), ""},
		{"8000M", uint64(8000 * MB), ""},
		{"8000m", uint64(8000 * MB), ""},
		{"8000MB", uint64(8000 * MB), ""},
		{"8000mb", uint64(8000 * MB), ""},
		{"16t", uint64(16 * TB), ""},
		{"16T", uint64(16 * TB), ""},
		{"16tb", uint64(16 * TB), ""},
		{"16Tb", uint64(16 * TB), ""},
		{"16tB", uint64(16 * TB), ""},
		{"16TB", uint64(16 * TB), ""},
	}

	for _, tt := range tests {
		bytes, err := getSize(tt.in)
		if tt.errMsg != "" {
			ExpectError(t, err, tt.errMsg, "")
			continue
		}
		if err != nil {
			t.Fatal(err)
		}

		AssertEqual(t, uint64(bytes), tt.out, "bad output")
	}
}

// TestCalcStorage verifies the correct scm/nvme bytes are returned from input
func TestCalcStorage(t *testing.T) {
	var tests = []struct {
		scm     string
		nvme    string
		outScm  ByteSize
		outNvme ByteSize
		errMsg  string
		desc    string
	}{
		{"256M", "8G", 256 * MB, 8 * GB, "", "defaults"},
		{"256M", "", 256 * MB, 0, "", "no nvme specified"},
		{"99M", "1G", 99 * MB, 1 * GB, "", "bad ratio"}, // should issue ratio warning
		{"", "8G", 0, 0, msgSizeZeroScm, "no scm specified"},
		{"0", "8G", 0, 0, msgSizeZeroScm, "zero scm"},
		{"Z0", "Z8G", 0, 0, "illegal scm size: Z0: Unrecognized size suffix Z0B", "zero scm"},
	}

	for _, tt := range tests {
		scmBytes, nvmeBytes, err := calcStorage(tt.scm, tt.nvme)
		if tt.errMsg != "" {
			ExpectError(t, err, tt.errMsg, tt.desc)
			continue
		}
		if err != nil {
			t.Fatal(err)
		}

		AssertEqual(t, scmBytes, tt.outScm, "bad scm bytes, "+tt.desc)
		AssertEqual(t, nvmeBytes, tt.outNvme, "bad nvme bytes, "+tt.desc)
	}
}
