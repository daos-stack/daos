//
// (C) Copyright 2019-2020 Intel Corporation.
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

package storage

import (
	"fmt"
	"math/rand"
	"time"

	"github.com/daos-stack/daos/src/control/common"
)

func concat(base string, idx int32, altSep ...string) string {
	sep := "-"
	if len(altSep) == 1 {
		sep = altSep[0]
	}

	return fmt.Sprintf("%s%s%d", base, sep, idx)
}

func getRandIdx(n ...int32) int32 {
	rand.Seed(time.Now().UnixNano())
	if len(n) > 0 {
		return rand.Int31n(n[0])
	}
	return rand.Int31()
}

func MockNvmeDeviceHealth(varIdx ...int32) *NvmeDeviceHealth {
	idx := common.GetIndex(varIdx...)
	tWarn := false
	if idx > 0 {
		tWarn = true
	}
	return &NvmeDeviceHealth{
		Temperature:  uint32(getRandIdx(280)),
		PowerCycles:  uint64(getRandIdx()),
		PowerOnHours: uint64(getRandIdx()),
		TempWarn:     tWarn,
	}
}

func MockNvmeNamespace(varIdx ...int32) *NvmeNamespace {
	idx := common.GetIndex(varIdx...)
	return &NvmeNamespace{
		ID:   uint32(idx),
		Size: uint64(idx),
	}
}

func MockNvmeController(varIdx ...int32) *NvmeController {
	idx := common.GetIndex(varIdx...)

	return &NvmeController{
		Model:       concat("model", idx),
		Serial:      concat("serial", getRandIdx()),
		PciAddr:     concat("0000:80:00", idx, "."),
		FwRev:       concat("fwRev", idx),
		SocketID:    idx,
		HealthStats: MockNvmeDeviceHealth(idx),
		Namespaces:  []*NvmeNamespace{MockNvmeNamespace(idx)},
	}
}

func MockScmModule(varIdx ...int32) *ScmModule {
	idx := uint32(common.GetIndex(varIdx...))

	return &ScmModule{
		ChannelID:       idx,
		ChannelPosition: idx,
		ControllerID:    idx,
		SocketID:        idx,
		PhysicalID:      idx,
		Capacity:        uint64(idx),
		UID:             fmt.Sprintf("Device%d", idx),
	}
}

func MockScmNamespace(varIdx ...int32) *ScmNamespace {
	idx := common.GetIndex(varIdx...)

	return &ScmNamespace{
		UUID:        common.MockUUID(varIdx...),
		BlockDevice: fmt.Sprintf("/dev/pmem%d", idx),
		Name:        fmt.Sprintf("pmem%d", idx),
		NumaNode:    uint32(idx),
		Size:        uint64(idx),
	}
}
