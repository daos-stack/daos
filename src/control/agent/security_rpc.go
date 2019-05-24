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

package main

import (
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"os/user"
	"strconv"
)

// Module id for the Agent security module
const securityModuleID int32 = 1

const (
	methodRequestCredentials int32 = 101
)

// userInfo is an internal implementation of the security.User interface
type userInfo struct {
	info *user.User
}

func (u *userInfo) Username() string {
	return u.info.Username
}

func (u *userInfo) GroupIDs() ([]uint32, error) {
	gidStrs, err := u.info.GroupIds()
	if err != nil {
		return nil, err
	}

	gids := []uint32{}
	for _, gstr := range gidStrs {
		gid, err := strconv.Atoi(gstr)
		if err != nil {
			continue
		}
		gids = append(gids, uint32(gid))
	}

	return gids, nil
}

// external is an internal implementation of the UserExt interface
type external struct{}

// LookupUserId is a wrapper for user.LookupId
func (e *external) LookupUserID(uid uint32) (security.User, error) {
	uidStr := strconv.FormatUint(uint64(uid), 10)
	info, err := user.LookupId(uidStr)
	if err != nil {
		return nil, err
	}
	return &userInfo{
		info: info,
	}, nil
}

// LookupGroupId is a wrapper for user.LookupGroupId
func (e *external) LookupGroupID(gid uint32) (*user.Group, error) {
	gidStr := strconv.FormatUint(uint64(gid), 10)
	return user.LookupGroupId(gidStr)
}

// SecurityModule is the security drpc module struct
type SecurityModule struct {
	ext security.UserExt
}

// HandleCall is the handler for calls to the SecurityModule
func (m *SecurityModule) HandleCall(client *drpc.Client, method int32, body []byte) ([]byte, error) {
	if method != methodRequestCredentials {
		return nil, errors.Errorf("Attempt to call unregistered function")
	}

	info, err := security.DomainInfoFromUnixConn(client.Conn)
	if err != nil {
		return nil, errors.Errorf("Unable to get credentials for client socket")
	}

	response, err := security.AuthSysRequestFromCreds(m.ext, info)
	if err != nil {
		return nil, err
	}

	responseBytes, err := proto.Marshal(response)
	if err != nil {
		return nil, err
	}
	return responseBytes, nil
}

// InitModule initializes internal variables for the module
func (m *SecurityModule) InitModule(state drpc.ModuleState) {
	m.ext = &external{}
}

//ID will return Security module ID
func (m *SecurityModule) ID() int32 {
	return securityModuleID
}
