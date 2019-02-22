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
	"strconv"

	pb "github.com/daos-stack/daos/src/control/security/proto"

	"github.com/golang/protobuf/proto"
)

// HashFromToken will return a SHA512 hash of the token data
func HashFromToken(token *pb.AuthToken) ([]byte, error) {
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

// AuthSysRequestFromCreds takes the domain info credentials gathered
// during the gRPC handshake and creates an AuthSys security request to obtain
// a handle from the management service.
func AuthSysRequestFromCreds(creds *DomainInfo) (*pb.SecurityCredential, error) {
	if creds == nil {
		return nil, errors.New("No credentials supplied")
	}

	uid := strconv.FormatUint(uint64(creds.creds.Uid), 10)
	userInfo, _ := user.LookupId(uid)
	groups, _ := userInfo.GroupIds()

	name, err := os.Hostname()
	if err != nil {
		name = "unavailable"
	}

	var gids = []uint32{}

	// Convert groups to gids
	for _, gstr := range groups {
		gid, err := strconv.Atoi(gstr)
		if err != nil {
			// Skip this group
			continue
		}
		gids = append(gids, uint32(gid))
	}

	// Craft AuthToken
	sys := pb.AuthSys{
		Stamp:       0,
		Machinename: name,
		Uid:         creds.creds.Uid,
		Gid:         creds.creds.Gid,
		Gids:        gids,
		Secctx:      creds.ctx}

	// Marshal our AuthSys token into a byte array
	tokenBytes, err := proto.Marshal(&sys)
	if err != nil {
		fmt.Errorf("Unable to marshal AuthSys token (%s)", err)
		return nil, err
	}
	token := pb.AuthToken{
		Flavor: pb.AuthFlavor_AUTH_SYS,
		Data:   tokenBytes}

	verifier, err := HashFromToken(&token)
	if err != nil {
		fmt.Errorf("Unable to generate verifier (%s)", err)
		return nil, err
	}

	verifierToken := pb.AuthToken{
		Flavor: pb.AuthFlavor_AUTH_SYS,
		Data:   verifier}

	credential := pb.SecurityCredential{
		Token:    &token,
		Verifier: &verifierToken}

	return &credential, nil
}

// AuthSysFromAuthToken takes an opaque AuthToken and turns it into a
// concrete AuthSys data structure.
func AuthSysFromAuthToken(authToken *pb.AuthToken) (*pb.AuthSys, error) {
	if authToken.GetFlavor() != pb.AuthFlavor_AUTH_SYS {
		return nil, errors.New("Attempting to convert an invalid AuthSys Token")
	}

	sysToken := &pb.AuthSys{}
	err := proto.Unmarshal(authToken.GetData(), sysToken)
	if err != nil {
		return nil, fmt.Errorf("unmarshaling %s: %v", authToken.GetFlavor(), err)
	}
	return sysToken, nil
}
