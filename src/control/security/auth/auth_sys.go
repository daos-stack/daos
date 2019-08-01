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

package auth

import (
	"bytes"
	"crypto"
	"os"
	"os/user"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/security"
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

//VerifierFromToken will return a SHA512 hash of the token data. If a signing key
//is passed in it will additionally sign the hash of the token.
func VerifierFromToken(key crypto.PublicKey, token *Token) ([]byte, error) {
	var sig []byte
	tokenBytes, err := proto.Marshal(token)
	if err != nil {
		return nil, errors.Wrap(err, "unable to marshal Token")
	}

	signer := security.DefaultTokenSigner()

	if key == nil {
		return signer.Hash(tokenBytes)
	}
	sig, err = signer.Sign(key, tokenBytes)
	return sig, errors.Wrap(err, "signing verifier failed")
}

//VerifyToken takes the auth token and the signature bytes in the verifier and
//verifies it against the public key provided for the agent who claims to have
//provided the token.
func VerifyToken(key crypto.PublicKey, token *Token, sig []byte) error {
	tokenBytes, err := proto.Marshal(token)
	if err != nil {
		return errors.Wrap(err, "unable to marshal Token")
	}

	signer := security.DefaultTokenSigner()

	if key == nil {
		digest, err := signer.Hash(tokenBytes)
		if err != nil {
			return err
		}
		if bytes.Equal(digest, sig) {
			return nil
		}
		return errors.Errorf("unsigned hash failed to verify.")
	}

	err = signer.Verify(key, tokenBytes, sig)
	return errors.Wrap(err, "token verification Failed")
}

func sysNameToPrincipalName(name string) string {
	return name + "@"
}

// AuthSysRequestFromCreds takes the domain info credentials gathered
// during the dRPC request and creates an AuthSys security request to obtain
// a handle from the management service.
func AuthSysRequestFromCreds(ext UserExt, creds *security.DomainInfo, signing crypto.PrivateKey) (*Credential, error) {
	if creds == nil {
		return nil, errors.New("No credentials supplied")
	}

	userInfo, err := ext.LookupUserID(creds.Uid())
	if err != nil {
		return nil, errors.Wrapf(err, "Failed to lookup uid %v",
			creds.Uid())
	}

	groupInfo, err := ext.LookupGroupID(creds.Gid())
	if err != nil {
		return nil, errors.Wrapf(err, "Failed to lookup gid %v",
			creds.Gid())
	}

	groups, err := userInfo.GroupIDs()
	if err != nil {
		return nil, errors.Wrapf(err, "Failed to get group IDs for user %v",
			userInfo.Username())
	}

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
	sys := Sys{
		Stamp:       0,
		Machinename: name,
		User:        sysNameToPrincipalName(userInfo.Username()),
		Group:       sysNameToPrincipalName(groupInfo.Name),
		Groups:      groupList,
		Secctx:      creds.Ctx()}

	// Marshal our AuthSys token into a byte array
	tokenBytes, err := proto.Marshal(&sys)
	if err != nil {
		return nil, errors.Wrap(err, "Unable to marshal AuthSys token")
	}
	token := Token{
		Flavor: Flavor_AUTH_SYS,
		Data:   tokenBytes}

	verifier, err := VerifierFromToken(signing, &token)
	if err != nil {
		return nil, errors.WithMessage(err, "Unable to generate verifier")
	}

	verifierToken := Token{
		Flavor: Flavor_AUTH_SYS,
		Data:   verifier}

	credential := Credential{
		Token:    &token,
		Verifier: &verifierToken,
		Origin:   "agent"}

	return &credential, nil
}

// AuthSysFromAuthToken takes an opaque AuthToken and turns it into a
// concrete AuthSys data structure.
func AuthSysFromAuthToken(authToken *Token) (*Sys, error) {
	if authToken.GetFlavor() != Flavor_AUTH_SYS {
		return nil, errors.New("Attempting to convert an invalid AuthSys Token")
	}

	sysToken := &Sys{}
	err := proto.Unmarshal(authToken.GetData(), sysToken)
	if err != nil {
		return nil, errors.Wrapf(err, "unmarshaling %s", authToken.GetFlavor())
	}
	return sysToken, nil
}
