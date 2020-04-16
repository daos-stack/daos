//
// (C) Copyright 2018-2020 Intel Corporation.
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

package client

import (
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
)

const (
	msgStreamRecv = "%T recv() failed"
	msgTypeAssert = "type assertion failed, wanted %T got %T"
)

// StoragePrepare returns details of nonvolatile storage devices attached to each
// remote server. Data received over channel from requests running in parallel.
func (c *connList) StoragePrepare(req *ctlpb.StoragePrepareReq) ResultMap {
	// NB: This method is a stub to indicate that it has been replaced
	// by functionality in the control API, and will be removed when
	// the client package is removed.
	return nil
}

// StorageScan returns details of nonvolatile storage devices attached to each
// remote server. Critical storage device health information is also returned
// for all NVMe SSDs discovered. Data received over channel from requests
// in parallel. If health param is true, stringer repr will include stats.
func (c *connList) StorageScan(p *StorageScanReq) *StorageScanResp {
	// NB: This method is a stub to indicate that it has been replaced
	// by functionality in the control API, and will be removed when
	// the client package is removed.
	return nil
}

// StorageFormatRequest attempts to format nonvolatile storage devices on a
// remote server over gRPC.
//
// Calls control StorageFormat routine which activates StorageFormat service rpc
// and returns an open stream handle. Receive on stream and send ClientResult
// over channel for each.
func StorageFormatRequest(mc Control, parms interface{}, ch chan ClientResult) {
	// NB: This method is a stub to indicate that it has been replaced
	// by functionality in the control API, and will be removed when
	// the client package is removed.
}

// StorageFormat prepares nonvolatile storage devices attached to each
// remote server in the connection list for use with DAOS.
func (c *connList) StorageFormat(reformat bool) StorageFormatResults {
	// NB: This method is a stub to indicate that it has been replaced
	// by functionality in the control API, and will be removed when
	// the client package is removed.
	return nil
}
