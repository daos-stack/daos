//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package auth

import (
	"bytes"
	"crypto"
	"os"
	"os/user"
	"strconv"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/security"
)

// User is an interface wrapping a representation of a specific system user.
type User interface {
	Username() string
	GroupIDs() ([]uint32, error)
	Gid() (uint32, error)
}

// UserExt is an interface that wraps system user-related external functions.
type UserExt interface {
	Current() (User, error)
	LookupUserID(uid uint32) (User, error)
	LookupGroupID(gid uint32) (*user.Group, error)
}

// UserInfo is an exported implementation of the security.User interface.
type UserInfo struct {
	Info *user.User
}

// Username is a wrapper for user.Username.
func (u *UserInfo) Username() string {
	return u.Info.Username
}

// GroupIDs is a wrapper for user.GroupIds.
func (u *UserInfo) GroupIDs() ([]uint32, error) {
	gidStrs, err := u.Info.GroupIds()
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

// Gid is a wrapper for user.Gid.
func (u *UserInfo) Gid() (uint32, error) {
	gid, err := strconv.Atoi(u.Info.Gid)

	return uint32(gid), errors.Wrap(err, "user gid")
}

// External is an exported implementation of the UserExt interface.
type External struct{}

// LookupUserId is a wrapper for user.LookupId.
func (e *External) LookupUserID(uid uint32) (User, error) {
	uidStr := strconv.FormatUint(uint64(uid), 10)
	info, err := user.LookupId(uidStr)
	if err != nil {
		return nil, err
	}
	return &UserInfo{Info: info}, nil
}

// LookupGroupId is a wrapper for user.LookupGroupId.
func (e *External) LookupGroupID(gid uint32) (*user.Group, error) {
	gidStr := strconv.FormatUint(uint64(gid), 10)
	return user.LookupGroupId(gidStr)
}

// Current is a wrapper for user.Current.
func (e *External) Current() (User, error) {
	info, err := user.Current()
	if err != nil {
		return nil, err
	}
	return &UserInfo{Info: info}, nil
}

// VerifierFromToken will return a SHA512 hash of the token data. If a signing key
// is passed in it will additionally sign the hash of the token.
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

// VerifyToken takes the auth token and the signature bytes in the verifier and
// verifies it against the public key provided for the agent who claims to have
// provided the token.
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
