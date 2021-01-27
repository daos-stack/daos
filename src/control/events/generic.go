//
// (C) Copyright 2020-2021 Intel Corporation.
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

package events

import (
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
)

// StrInfo contains opaque blob of type string to hold custom details for a
// generic RAS event to be forwarded through the control-plane from the
// data-plane to an external consumer e.g. syslog.
type StrInfo string

func (si *StrInfo) isExtendedInfo() {}

// GetStrInfo returns extended info if of type StrInfo.
func (evt *RASEvent) GetStrInfo() *StrInfo {
	if ei, ok := evt.ExtendedInfo.(*StrInfo); ok {
		return ei
	}

	return nil
}

// StrInfoFromProto converts event info from proto to native format.
func StrInfoFromProto(pbInfo *sharedpb.RASEvent_StrInfo) (*StrInfo, error) {
	si := StrInfo(pbInfo.StrInfo)

	return &si, nil
}

// StrInfoToProto converts event info from native to proto format.
func StrInfoToProto(si *StrInfo) (*sharedpb.RASEvent_StrInfo, error) {
	pbInfo := &sharedpb.RASEvent_StrInfo{
		StrInfo: string(*si),
	}

	return pbInfo, nil
}
