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

package security

import (
	"errors"
	"fmt"
	"log"
	"os"
	"os/user"
	"strconv"

	pb "modules/security/proto"

	"github.com/golang/protobuf/proto"
)

// AuthSysRequestFromCreds takes the domain info credentials gathered
// during the gRPC handshake and creates an AuthSys security request to obtain
// a handle from the management service.
func AuthSysRequestFromCreds(creds *DomainInfo) (*pb.SecurityRequest, error) {
	uid := strconv.FormatUint(uint64(creds.creds.Uid), 10)
	userInfo, _ := user.LookupId(uid)
	groups, _ := userInfo.GroupIds()

	name, err := os.Hostname()
	if err != nil {
		name = "unavailable"
		fmt.Println(err)
	}

	var gids = []uint32{}

	// Convert groups to gids
	for _, gstr := range groups {
		gid, err := strconv.Atoi(gstr)
		if err != nil {
			log.Printf("Was unable to convert %s to an integer\n", gstr)
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
		log.Println("Unable to marshal AuthSys token", err)
		return nil, err
	}
	token := pb.AuthToken{
		Flavor: pb.AuthFlavor_AUTH_SYS,
		Token:  tokenBytes}

	action := pb.SecurityRequest{
		Action: pb.AuthAction_AUTH_INIT,
		Host:   name,
		Data:   &pb.SecurityRequest_Token{&token}}

	return &action, nil
}

// AuthSysFromAuthToken takes an opaque AuthToken and turns it into a
// concrete AuthSys data structure.
func AuthSysFromAuthToken(authToken *pb.AuthToken) (*pb.AuthSys, error) {
	if authToken.GetFlavor() != pb.AuthFlavor_AUTH_SYS {
		return nil, errors.New("Attempting to convert an invalid AuthSys Token")
	}

	sysToken := &pb.AuthSys{}
	err := proto.Unmarshal(authToken.GetToken(), sysToken)
	if err != nil {
		return nil, fmt.Errorf("unmarshaling %s: %v", authToken.GetFlavor(), err)
	}
	return sysToken, nil
}
