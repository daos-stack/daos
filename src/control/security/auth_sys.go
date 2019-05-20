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

package security

import (
	"crypto/sha512"
	"errors"
	"fmt"
	"os"
	"os/user"

	"github.com/daos-stack/daos/src/control/security/auth"

	"github.com/golang/protobuf/proto"
)

// User is an interface wrapping a representation of a specific system user
type User interface {
	Username() string
	GroupIDs() ([]uint32, error)
}

// UserExt is an interface that wraps system user-related external functions
type UserExt interface {
	LookupUserID(uid uint32) (User, error)
	LookupGroupID(gid uint32) (*user.Group, error)
}

// HashFromToken will return a SHA512 hash of the token data
func HashFromToken(token *auth.Token) ([]byte, error) {
	// Generate our hash (not signed yet just a hash)
	hash := sha512.New()

	tokenBytes, err := proto.Marshal(token)
	if err != nil {
		fmt.Errorf("Unable to marshal AuthToken (%s)", err)
		return nil, err
	}

	hash.Write(tokenBytes)
	hashBytes := hash.Sum(nil)
	return hashBytes, nil
}

func sysNameToPrincipalName(name string) string {
	return name + "@"
}

// AuthSysRequestFromCreds takes the domain info credentials gathered
// during the gRPC handshake and creates an AuthSys security request to obtain
// a handle from the management service.
func AuthSysRequestFromCreds(ext UserExt, creds *DomainInfo) (*auth.Credential, error) {
	if creds == nil {
		return nil, errors.New("No credentials supplied")
	}

	userInfo, _ := ext.LookupUserID(creds.creds.Uid)
	groupInfo, _ := ext.LookupGroupID(creds.creds.Gid)
	groups, _ := userInfo.GroupIDs()

	name, err := os.Hostname()
	if err != nil {
		name = "unavailable"
	}

	var groupList = []string{}

	// Convert groups to gids
	for _, gid := range groups {
		gInfo, err := ext.LookupGroupID(gid)
		if err != nil {
			// Skip this group
			continue
		}
		groupList = append(groupList, sysNameToPrincipalName(gInfo.Name))
	}

	// Craft AuthToken
	sys := auth.Sys{
		Stamp:       0,
		Machinename: name,
		User:        sysNameToPrincipalName(userInfo.Username()),
		Group:       sysNameToPrincipalName(groupInfo.Name),
		Groups:      groupList,
		Secctx:      creds.ctx}

	// Marshal our AuthSys token into a byte array
	tokenBytes, err := proto.Marshal(&sys)
	if err != nil {
		fmt.Errorf("Unable to marshal AuthSys token (%s)", err)
		return nil, err
	}
	token := auth.Token{
		Flavor: auth.Flavor_AUTH_SYS,
		Data:   tokenBytes}

	verifier, err := HashFromToken(&token)
	if err != nil {
		fmt.Errorf("Unable to generate verifier (%s)", err)
		return nil, err
	}

	verifierToken := auth.Token{
		Flavor: auth.Flavor_AUTH_SYS,
		Data:   verifier}

	credential := auth.Credential{
		Token:    &token,
		Verifier: &verifierToken}

	return &credential, nil
}

// AuthSysFromAuthToken takes an opaque AuthToken and turns it into a
// concrete AuthSys data structure.
func AuthSysFromAuthToken(authToken *auth.Token) (*auth.Sys, error) {
	if authToken.GetFlavor() != auth.Flavor_AUTH_SYS {
		return nil, errors.New("Attempting to convert an invalid AuthSys Token")
	}

	sysToken := &auth.Sys{}
	err := proto.Unmarshal(authToken.GetData(), sysToken)
	if err != nil {
		return nil, fmt.Errorf("unmarshaling %s: %v", authToken.GetFlavor(), err)
	}
	return sysToken, nil
}
