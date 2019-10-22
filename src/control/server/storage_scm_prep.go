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

package server

import (
	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// PrepScm interface provides capability to prepare SCM storage
//
// TODO: Update tests in this layer to use a mock scm.Provider
// implementation rather than requiring so much knowledge about
// low-level details.
type PrepScm interface {
	GetNamespaces() ([]scm.Namespace, error)
	GetState() (types.ScmState, error)
	Prep(types.ScmState) (bool, []scm.Namespace, error)
	PrepReset(types.ScmState) (bool, error)
}

// core scm prep code moved to storage/scm/ipmctl.go
