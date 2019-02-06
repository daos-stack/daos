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

package security_test

import (
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/security"
	pb "github.com/daos-stack/daos/src/control/security/proto"

	"github.com/golang/protobuf/proto"
)

// Helpers for the unit tests below

func expectAuthSysErrorForToken(t *testing.T, badToken *pb.AuthToken, expectedErrorMessage string) {
	authSys, err := security.AuthSysFromAuthToken(badToken)

	if authSys != nil {
		t.Error("Expected a nil AuthSys")
	}

	ExpectError(t, err, expectedErrorMessage, "")
}

// AuthSysFromAuthToken tests
func TestAuthSysFromAuthToken_ErrorsWithNilAuthToken(t *testing.T) {
	expectAuthSysErrorForToken(t, nil,
		"Attempting to convert an invalid AuthSys Token")
}

func TestAuthSysFromAuthToken_ErrorsWithWrongAuthTokenFlavor(t *testing.T) {
	badFlavorToken := pb.AuthToken{Flavor: pb.AuthFlavor_AUTH_NONE}
	expectAuthSysErrorForToken(t, &badFlavorToken,
		"Attempting to convert an invalid AuthSys Token")
}

func TestAuthSysFromAuthToken_ErrorsIfTokenCannotBeUnmarshaled(t *testing.T) {
	zeroArray := make([]byte, 16)
	badToken := pb.AuthToken{Flavor: pb.AuthFlavor_AUTH_SYS,
		Data: zeroArray}
	expectAuthSysErrorForToken(t, &badToken,
		"unmarshaling AUTH_SYS: proto: proto.AuthSys: illegal tag 0 (wire type 0)")
}

func TestAuthSysFromAuthToken_SucceedsWithGoodToken(t *testing.T) {
	originalAuthSys := pb.AuthSys{
		Stamp:       0,
		Machinename: "something",
		Uid:         50,
		Gid:         25,
		Gids:        []uint32{1, 2, 3},
		Secctx:      "nothing",
	}

	marshaledToken, err := proto.Marshal(&originalAuthSys)
	if err != nil {
		t.Fatalf("Couldn't marshal during setup: %s", err)
	}

	goodToken := pb.AuthToken{
		Flavor: pb.AuthFlavor_AUTH_SYS,
		Data:   marshaledToken,
	}

	authSys, err := security.AuthSysFromAuthToken(&goodToken)

	if err != nil {
		t.Fatalf("Expected no error, got: %s", err)
	}

	if authSys == nil {
		t.Fatal("Got a nil AuthSys")
	}

	AssertEqual(t, authSys.GetStamp(), originalAuthSys.GetStamp(),
		"Stamps don't match")
	AssertEqual(t, authSys.GetMachinename(), originalAuthSys.GetMachinename(),
		"Machinenames don't match")
	AssertEqual(t, authSys.GetUid(), originalAuthSys.GetUid(),
		"Uids don't match")
	AssertEqual(t, authSys.GetGid(), originalAuthSys.GetGid(),
		"Gids don't match")
	AssertEqual(t, len(authSys.GetGids()), len(originalAuthSys.GetGids()),
		"Gid lists aren't the same length")
	AssertEqual(t, authSys.GetSecctx(), originalAuthSys.GetSecctx(),
		"Secctx don't match")
}

// AuthSysRequestFromCreds tests

func TestAuthSysRequestFromCreds_failsIfDomainInfoNil(t *testing.T) {
	result, err := security.AuthSysRequestFromCreds(nil)

	if result != nil {
		t.Error("Expected a nil request")
	}

	ExpectError(t, err, "No credentials supplied", "")
}
