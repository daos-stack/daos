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
package storage_messages

const (
	MsgBdevNotFound = "controller at pci addr not found, check device exists " +
		"and can be discovered, you may need to run `sudo daos_server " +
		"storage prepare --nvme-only` to setup SPDK to access SSDs"
	MsgBdevAlreadyFormatted = "nvme storage has already been formatted and " +
		"reformat not implemented"
	MsgBdevNotInited          = "nvme storage not initialized"
	MsgBdevClassNotSupported  = "operation unsupported on bdev class"
	MsgSpdkInitFail           = "SPDK env init, has setup been run?"
	MsgSpdkDiscoverFail       = "SPDK controller discovery"
	MsgBdevFwrevStartMismatch = "controller fwrev unexpected before update"
	MsgBdevFwrevEndMismatch   = "controller fwrev unchanged after update"
	MsgBdevModelMismatch      = "controller model unexpected"
	MsgBdevNoDevs             = "no controllers specified"
	MsgBdevClassIsFile        = "nvme emulation initialized with backend file"
)
