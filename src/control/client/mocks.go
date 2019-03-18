//
// (C) Copyright 2018 Intel Corporation.
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
	"fmt"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// NewClientFM provides a mock cFeatureMap for testing.
func NewClientFM(features []*pb.Feature, addrs Addresses) cFeatureMap {
	cf := make(cFeatureMap)
	for _, addr := range addrs {
		fMap := make(FeatureMap)
		for _, f := range features {
			fMap[f.Fname.Name] = fmt.Sprintf(
				"category %s, %s", f.Category.Category, f.Description)
		}
		cf[addr] = fMap
	}
	return cf
}

// NewClientNvme provides a mock cNvmeMap for testing.
func NewClientNvme(ctrlrs NvmeControllers, addrs Addresses) cNvmeMap {
	cMap := make(cNvmeMap)
	for _, addr := range addrs {
		cMap[addr] = ctrlrs
	}
	return cMap
}

// NewClientScm provides a mock cScmMap for testing.
func NewClientScm(mms ScmModules, addrs Addresses) cScmMap {
	cMap := make(cScmMap)
	for _, addr := range addrs {
		cMap[addr] = mms
	}
	return cMap
}
