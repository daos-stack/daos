//
// (C) Copyright 2018-2019 Intel Corporation.
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

package server

import (
	"github.com/pkg/errors"
)

// SystemQuery retrieves system membership list.
func (cs *ControlService) SystemQuery() ([]string, error) {
	// TODO: return list of hosts registered through gRPC join requests
	return nil, errors.New("SystemQuery() not yet implemented")
}

// SystemStop sends multicast ServiceStop gRPC requests to system membership list.
func (cs *ControlService) SystemStop() error {
	// TODO: retrieve system membership list, inhibit rebuild on pool
	// services and distribute ServiceStop requests, then wait for returns.
	// TODO: investigate nonblocking approach.
	return errors.New("SystemStop() not yet implemented")
}
