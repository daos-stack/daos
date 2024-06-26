//
// (C) Copyright 2018-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package auth

import (
	"errors"
	"os/user"
	"syscall"
	"testing"

	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/security"
)

// Helpers for the unit tests below

func expectAuthSysErrorForToken(t *testing.T, badToken *Token, expectedErrorMessage string) {
	t.Helper()
	authSys, err := AuthSysFromAuthToken(badToken)

	if authSys != nil {
		t.Error("Expected a nil AuthSys")
	}

	test.CmpErr(t, errors.New(expectedErrorMessage), err)
}

// AuthSysFromAuthToken tests
func TestAuthSysFromAuthToken_ErrorsWithNilAuthToken(t *testing.T) {
	expectAuthSysErrorForToken(t, nil,
		"Attempting to convert an invalid AuthSys Token")
}

func TestAuthSysFromAuthToken_ErrorsWithWrongAuthTokenFlavor(t *testing.T) {
	badFlavorToken := Token{Flavor: Flavor_AUTH_NONE}
	expectAuthSysErrorForToken(t, &badFlavorToken,
		"Attempting to convert an invalid AuthSys Token")
}

func TestAuthSysFromAuthToken_ErrorsIfTokenCannotBeUnmarshaled(t *testing.T) {
	zeroArray := make([]byte, 16)
	badToken := Token{Flavor: Flavor_AUTH_SYS,
		Data: zeroArray}
	expectAuthSysErrorForToken(t, &badToken,
		"unmarshaling AUTH_SYS:")
}

func TestAuthSysFromAuthToken_SucceedsWithGoodToken(t *testing.T) {
	originalAuthSys := Sys{
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

	goodToken := Token{
		Flavor: Flavor_AUTH_SYS,
		Data:   marshaledToken,
	}

	authSys, err := AuthSysFromAuthToken(&goodToken)

	if err != nil {
		t.Fatalf("Expected no error, got: %s", err)
	}

	if authSys == nil {
		t.Fatal("Got a nil AuthSys")
	}

	test.AssertEqual(t, authSys.GetStamp(), originalAuthSys.GetStamp(),
		"Stamps don't match")
	test.AssertEqual(t, authSys.GetMachinename(), originalAuthSys.GetMachinename(),
		"Machinenames don't match")
	test.AssertEqual(t, authSys.GetUser(), originalAuthSys.GetUser(),
		"Owners don't match")
	test.AssertEqual(t, authSys.GetGroup(), originalAuthSys.GetGroup(),
		"Groups don't match")
	test.AssertEqual(t, len(authSys.GetGroups()), len(originalAuthSys.GetGroups()),
		"Group lists aren't the same length")
	test.AssertEqual(t, authSys.GetSecctx(), originalAuthSys.GetSecctx(),
		"Secctx don't match")
}

func testHostnameFn(expErr error, hostname string) getHostnameFn {
	return func() (string, error) {
		if expErr != nil {
			return "", expErr
		}
		return hostname, nil
	}
}

func testUserFn(expErr error, userName string) getUserFn {
	return func(uid string) (*user.User, error) {
		if expErr != nil {
			return nil, expErr
		}
		return &user.User{
			Uid:      uid,
			Gid:      uid,
			Username: userName,
		}, nil
	}
}

func testGroupFn(expErr error, groupName string) getGroupFn {
	return func(gid string) (*user.Group, error) {
		if expErr != nil {
			return nil, expErr
		}
		return &user.Group{
			Gid:  gid,
			Name: groupName,
		}, nil
	}
}

func testGroupIdsFn(expErr error, groupNames ...string) getGroupIdsFn {
	return func(*CredentialRequest) ([]string, error) {
		if expErr != nil {
			return nil, expErr
		}
		return groupNames, nil
	}
}

func testGroupNamesFn(expErr error, groupNames ...string) getGroupNamesFn {
	return func(*CredentialRequest) ([]string, error) {
		if expErr != nil {
			return nil, expErr
		}
		return groupNames, nil
	}
}

func getTestCreds(uid uint32, gid uint32) *security.DomainInfo {
	creds := &syscall.Ucred{
		Uid: uid,
		Gid: gid,
	}
	return security.InitDomainInfo(creds, "test")
}

func verifyCredential(t *testing.T, cred *Credential, expHostname, expUserPrinc, expGroupPrinc string, expGroupPrincs ...string) {
	t.Helper()

	token := cred.GetToken()
	if token == nil {
		t.Fatal("Token was nil")
	}

	if token.GetFlavor() != Flavor_AUTH_SYS {
		t.Fatalf("Bad auth flavor: %v", token.GetFlavor())
	}

	authsys := &Sys{}
	err := proto.Unmarshal(token.GetData(), authsys)
	if err != nil {
		t.Fatal("Failed to unmarshal token data")
	}

	if authsys.GetMachinename() != expHostname {
		t.Errorf("AuthSys had bad hostname: %v", authsys.GetMachinename())
	}

	if authsys.GetUser() != expUserPrinc {
		t.Errorf("AuthSys had bad username: %v", authsys.GetUser())
	}

	if authsys.GetGroup() != expGroupPrinc {
		t.Errorf("AuthSys had bad group name: %v", authsys.GetGroup())
	}

	for i, group := range authsys.GetGroups() {
		if group != expGroupPrincs[i] {
			t.Errorf("AuthSys had bad group in list (idx %v): %v", i, group)
		}
	}
}

func TestAuth_GetSignedCred(t *testing.T) {
	testHostname := "test-host.domain.foo"
	testUsername := "test-user"
	testGroup := "test-group"
	testGroupList := []string{"group1", "group2", "group3"}

	expectedHostname := "test-host"
	expectedUser := testUsername + "@"
	expectedGroup := testGroup + "@"
	expectedGroupList := make([]string, len(testGroupList))
	for i, group := range testGroupList {
		expectedGroupList[i] = group + "@"
	}

	for name, tc := range map[string]struct {
		req    *CredentialRequest
		expErr error
	}{
		"nil request": {
			req:    nil,
			expErr: errors.New("is nil"),
		},
		"nil DomainInfo": {
			req:    &CredentialRequest{},
			expErr: errors.New("No domain info supplied"),
		},
		"bad hostname": {
			req: func() *CredentialRequest {
				req := NewCredentialRequest(getTestCreds(1, 2), nil)
				req.getHostname = testHostnameFn(errors.New("bad hostname"), "")
				return req
			}(),
			expErr: errors.New("bad hostname"),
		},
		"bad uid": {
			req: func() *CredentialRequest {
				req := NewCredentialRequest(getTestCreds(1, 2), nil)
				req.getUser = testUserFn(errors.New("bad uid"), "")
				return req
			}(),
			expErr: errors.New("bad uid"),
		},
		"bad gid": {
			req: func() *CredentialRequest {
				req := NewCredentialRequest(getTestCreds(1, 2), nil)
				req.getGroup = testGroupFn(errors.New("bad gid"), "")
				return req
			}(),
			expErr: errors.New("bad gid"),
		},
		"bad group IDs": {
			req: func() *CredentialRequest {
				req := NewCredentialRequest(getTestCreds(1, 2), nil)
				req.getGroupIds = testGroupIdsFn(errors.New("bad group IDs"))
				return req
			}(),
			expErr: errors.New("bad group IDs"),
		},
		"bad group names": {
			req: func() *CredentialRequest {
				req := NewCredentialRequest(getTestCreds(1, 2), nil)
				req.getGroupNames = testGroupNamesFn(errors.New("bad group names"))
				return req
			}(),
			expErr: errors.New("bad group names"),
		},
		"valid": {
			req: func() *CredentialRequest {
				req := NewCredentialRequest(getTestCreds(1, 2), nil)
				req.getHostname = testHostnameFn(nil, testHostname)
				req.getUser = testUserFn(nil, testUsername)
				req.getGroup = testGroupFn(nil, testGroup)
				req.getGroupNames = testGroupNamesFn(nil, testGroupList...)
				return req
			}(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			cred, gotErr := GetSignedCredential(tc.req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			verifyCredential(t, cred, expectedHostname, expectedUser, expectedGroup, expectedGroupList...)
		})
	}
}

func TestAuth_CredentialRequestOverrides(t *testing.T) {
	req := NewCredentialRequest(getTestCreds(1, 2), nil)
	req.getHostname = testHostnameFn(nil, "test-host")
	req.WithUserAndGroup("test-user", "test-group", "test-secondary")

	cred, err := GetSignedCredential(req)
	if err != nil {
		t.Fatalf("Failed to get credential: %s", err)
	}

	verifyCredential(t, cred, "test-host", "test-user@", "test-group@", "test-secondary@")
}
