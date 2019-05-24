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
	"os/user"
	"syscall"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/security/auth"

	"github.com/golang/protobuf/proto"
)

// Mocks

type mockUser struct {
	username   string
	groupIDs   []uint32
	groupIDErr error
}

func (u *mockUser) Username() string {
	return u.username
}

func (u *mockUser) GroupIDs() ([]uint32, error) {
	return u.groupIDs, u.groupIDErr
}

type mockExt struct {
	lookupUserIDUid        uint32
	lookupUserIDResult     User
	lookupUserIDErr        error
	lookupGroupIDGid       uint32
	lookupGroupIDResults   []*user.Group
	lookupGroupIDCallCount uint32
	lookupGroupIDErr       error
}

func (e *mockExt) LookupUserID(uid uint32) (User, error) {
	e.lookupUserIDUid = uid
	return e.lookupUserIDResult, e.lookupUserIDErr
}

func (e *mockExt) LookupGroupID(gid uint32) (*user.Group, error) {
	e.lookupGroupIDGid = gid
	result := e.lookupGroupIDResults[e.lookupGroupIDCallCount]
	e.lookupGroupIDCallCount++
	return result, e.lookupGroupIDErr
}

// Helpers for the unit tests below

func expectAuthSysErrorForToken(t *testing.T, badToken *auth.Token, expectedErrorMessage string) {
	authSys, err := AuthSysFromAuthToken(badToken)

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
	badFlavorToken := auth.Token{Flavor: auth.Flavor_AUTH_NONE}
	expectAuthSysErrorForToken(t, &badFlavorToken,
		"Attempting to convert an invalid AuthSys Token")
}

func TestAuthSysFromAuthToken_ErrorsIfTokenCannotBeUnmarshaled(t *testing.T) {
	zeroArray := make([]byte, 16)
	badToken := auth.Token{Flavor: auth.Flavor_AUTH_SYS,
		Data: zeroArray}
	expectAuthSysErrorForToken(t, &badToken,
		"unmarshaling AUTH_SYS: proto: auth.Sys: illegal tag 0 (wire type 0)")
}

func TestAuthSysFromAuthToken_SucceedsWithGoodToken(t *testing.T) {
	originalAuthSys := auth.Sys{
		Stamp:       0,
		Machinename: "something",
		User:        "niceuser",
		Group:       "nicegroup",
		Groups:      []string{"grp1", "grp2", "grp3"},
		Secctx:      "nothing",
	}

	marshaledToken, err := proto.Marshal(&originalAuthSys)
	if err != nil {
		t.Fatalf("Couldn't marshal during setup: %s", err)
	}

	goodToken := auth.Token{
		Flavor: auth.Flavor_AUTH_SYS,
		Data:   marshaledToken,
	}

	authSys, err := AuthSysFromAuthToken(&goodToken)

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
	AssertEqual(t, authSys.GetUser(), originalAuthSys.GetUser(),
		"Owners don't match")
	AssertEqual(t, authSys.GetGroup(), originalAuthSys.GetGroup(),
		"Groups don't match")
	AssertEqual(t, len(authSys.GetGroups()), len(originalAuthSys.GetGroups()),
		"Group lists aren't the same length")
	AssertEqual(t, authSys.GetSecctx(), originalAuthSys.GetSecctx(),
		"Secctx don't match")
}

// AuthSysRequestFromCreds tests

func TestAuthSysRequestFromCreds_failsIfDomainInfoNil(t *testing.T) {
	result, err := AuthSysRequestFromCreds(&mockExt{}, nil)

	if result != nil {
		t.Error("Expected a nil request")
	}

	ExpectError(t, err, "No credentials supplied", "")
}

func TestAuthSysRequestFromCreds_returnsAuthSys(t *testing.T) {
	ext := &mockExt{}
	uid := uint32(15)
	gid := uint32(2001)
	gids := []uint32{1, 2, 3}
	expectedUser := "myuser"
	expectedGroup := "mygroup"
	expectedGroupList := []string{"group1", "group2", "group3"}
	creds := &DomainInfo{
		creds: &syscall.Ucred{
			Uid: uid,
			Gid: gid,
		},
	}

	ext.lookupUserIDResult = &mockUser{
		username: expectedUser,
		groupIDs: gids,
	}
	ext.lookupGroupIDResults = []*user.Group{
		&user.Group{
			Name: expectedGroup,
		},
	}

	for _, grp := range expectedGroupList {
		ext.lookupGroupIDResults = append(ext.lookupGroupIDResults,
			&user.Group{
				Name: grp,
			})
	}

	result, err := AuthSysRequestFromCreds(ext, creds)

	if err != nil {
		t.Errorf("Unexpected error: %v", err)
	}

	if result == nil {
		t.Fatal("Credential was nil")
	}

	token := result.GetToken()
	if token == nil {
		t.Fatal("Token was nil")
	}

	if token.GetFlavor() != auth.Flavor_AUTH_SYS {
		t.Fatalf("Bad auth flavor: %v", token.GetFlavor())
	}

	authsys := &auth.Sys{}
	err = proto.Unmarshal(token.GetData(), authsys)
	if err != nil {
		t.Fatal("Failed to unmarshal token data")
	}

	if authsys.GetUser() != expectedUser+"@" {
		t.Errorf("AuthSys had bad username: %v", authsys.GetUser())
	}

	if authsys.GetGroup() != expectedGroup+"@" {
		t.Errorf("AuthSys had bad group name: %v", authsys.GetGroup())
	}

	for i, group := range authsys.GetGroups() {
		if group != expectedGroupList[i]+"@" {
			t.Errorf("AuthSys had bad group in list (idx %v): %v", i, group)
		}
	}
}
