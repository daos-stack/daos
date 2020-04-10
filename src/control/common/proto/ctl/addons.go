//
// (C) Copyright 2020 Intel Corporation.
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
package ctl

import (
	"errors"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	"github.com/daos-stack/daos/src/control/system"
)

// SetSystemRanks is a convenience method to convert a slice of
// system ranks to a slice of uint32 ranks for this request.
func (m *SystemQueryReq) SetSystemRanks(sysRanks []system.Rank) error {
	if m == nil {
		return errors.New("nil request")
	}
	return convert.Types(sysRanks, &m.Ranks)
}

// GetSystemRanks is a convenience method to convert this request's
// slice of uint32 ranks to a slice of system ranks.
func (m *SystemQueryReq) GetSystemRanks() []system.Rank {
	if m != nil {
		var sysRanks []system.Rank
		if err := convert.Types(m.GetRanks(), &sysRanks); err == nil {
			return sysRanks
		}
	}
	return nil
}

// SetSystemRanks is a convenience method to convert a slice of
// system ranks to a slice of uint32 ranks for this request.
func (m *SystemStopReq) SetSystemRanks(sysRanks []system.Rank) error {
	if m == nil {
		return errors.New("nil request")
	}
	return convert.Types(sysRanks, &m.Ranks)
}

// GetSystemRanks is a convenience method to convert this request's
// slice of uint32 ranks to a slice of system ranks.
func (m *SystemStopReq) GetSystemRanks() []system.Rank {
	if m != nil {
		var sysRanks []system.Rank
		if err := convert.Types(m.GetRanks(), &sysRanks); err == nil {
			return sysRanks
		}
	}
	return nil
}

// SetSystemRanks is a convenience method to convert a slice of
// system ranks to a slice of uint32 ranks for this request.
func (m *SystemStartReq) SetSystemRanks(sysRanks []system.Rank) error {
	if m == nil {
		return errors.New("nil request")
	}
	return convert.Types(sysRanks, &m.Ranks)
}

// GetSystemRanks is a convenience method to convert this request's
// slice of uint32 ranks to a slice of system ranks.
func (m *SystemStartReq) GetSystemRanks() []system.Rank {
	if m != nil {
		var sysRanks []system.Rank
		if err := convert.Types(m.GetRanks(), &sysRanks); err == nil {
			return sysRanks
		}
	}
	return nil
}
