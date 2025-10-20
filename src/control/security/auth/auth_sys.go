//
// (C) Copyright 2018-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package auth

import (
	"bytes"
	"context"
	"crypto"
	"fmt"
	"slices"

	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

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
// provided the token. It also confirms that the token is from an authentication
// source supported by the server.
func VerifyToken(key crypto.PublicKey, token *Token, sig []byte, validAuthFlavors []uint32) error {
	tokenBytes, err := proto.Marshal(token)
	if err != nil {
		return errors.Wrap(err, "unable to marshal Token")
	}

	if !slices.Contains(validAuthFlavors, uint32(token.Flavor)) {
		return errors.Errorf("token has authentication flavor not supported by server.")
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

func CredentialRequestGetSigned(ctx context.Context, log logging.Logger, req CredentialRequest) (*Credential, error) {
	return req.GetSignedCredential(log, ctx)
}

func ParseValidAuthFlavors(authStrings []string) ([]uint32, error) {
	validAuthFlavors := make([]uint32, len(authStrings))
	for i := 0; i < len(authStrings); i++ {
		flavor, ok := Flavor_value["AUTH_" + authStrings[i]]
		if !ok {
			return nil, errors.Errorf("auth string %s is not recognized", authStrings[i])
		}
		validAuthFlavors[i] = uint32(flavor)
	}

	return validAuthFlavors, nil
}

func generateAuthMap() AuthMap {
	var am = make(AuthMap)
	for _, req := range CredentialRequests {
		flavor := req.GetAuthFlavor()
		_, found := am[flavor]
		if found {
			panic(fmt.Errorf("multiple authentication methods with the flavor `%s` were found - confirm that all implementations of `GetAuthName` are unique", Flavor_name[int32(flavor)]))
		}
		am[flavor] = req
	}
	return am
}

type (
	AuthMap map[Flavor]CredentialRequestFactory

	CredentialRequestFactory interface {
		// Allocate, and return, an instance of the type that implements CredentialRequest interface.
		AllocCredentialRequest() CredentialRequest
		// Returns a unique 4 character identifier (AuthTag) that refers to the CredentialRequest implementation.
		GetAuthFlavor() Flavor
	}

	CredentialRequest interface {
		// Using information stored in the agent's security config, the session/socket, request body, and the agent's signing key,
		// initalize the state of the Credential Request so that it can sign a credential in the future. Return an error if
		// a problem occurs during initializiation.
		InitCredentialRequest(log logging.Logger, sec_cfg *security.CredentialConfig, session *drpc.Session, req_body []byte, key crypto.PrivateKey) error
		// Contact a source of authenticity (e.g. domain socket, access manager) using the initalized state of the CredentialRequest
		// to construct a Credential object.
		GetSignedCredential(log logging.Logger, ctx context.Context) (*Credential, error)
		// Returns a key, as a string, representing a unique identifer specific to the request. This key is used by the cache
		// to remember credentials. It is vital for security that this identifer cannot be forged or easily guessed by the client - 
		// otherwise cached credentials can be "stolen".
		CredReqKey() string
	}
)

// CredentialRequests is a list of authentication methods the agent can use.
// To implement a new type of authentication: satisfy the CredentialRequest and 
// CredentialRequestFactory interfaces, add a new flavor in auth.proto, ensure
// that your `GetAuthFlavor` method returns this new unique flavor and add your 
// interface to the `CredentialRequests` list below.
// The server must be configured to allow an authentication method when it is initalized.
// By default, only Unix authentication is enabled.

var CredentialRequests = []CredentialRequestFactory{&CredentialRequestUnix{}, &CredentialRequestAM{}}
var FlavorToFactory = generateAuthMap()
