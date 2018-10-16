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

package mgmt

import (
	"common/log"

	"go-spdk/spdk"

	pb "modules/mgmt/proto"
)

// Storage interface represents a persistent storage that can
// support relevant operations.
type Storage interface {
	Init() error
	Discover() interface{}
	Update(interface{}) interface{}
	// this assumes that the output is to be executed on Shell
	// (cmdName, cmdArgs, envStr, err)
	BurnIn(interface{}) (string, []string, string, error)
	Teardown() error
}

// NsMap is a type alias
type NsMap map[int32]*pb.NVMeNamespace

// CtrlrMap is a type alias
type CtrlrMap map[int32]*pb.NVMeController

// NvmeStorage is an implementation of the Storage interface.
type NvmeStorage struct {
	Logger *log.Logger
	Env    *spdk.Env  // SPDK ENV interface implementation
	Nvme   *spdk.Nvme // SPDK NVMe interface implementation
}

// NVMeReturn struct contains return values for NvmeStorage
// Discover and Update methods.
type NVMeReturn struct {
	Ctrlrs []spdk.Controller
	Nss    []spdk.Namespace
	Err    error
}

// UpdateParams struct contains input parameters for NvmeStorage
// Update implementation.
type UpdateParams struct {
	CtrlrID int32
	Path    string
	Slot    int32
}

// BurnInParams struct contains input parameters for NvmeStorage
// BurnIn implementation.
type BurnInParams struct {
	PciAddr    string
	NsID       int32
	ConfigPath string
}
