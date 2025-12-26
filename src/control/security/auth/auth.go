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
	"strings"

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

// ParseValidAuthFlavors takes in case-insensitive strings representing
// authentication methods the server will allow and returns a list
// of authentication flavors, as defined in the auth protobuf.
func ParseValidAuthFlavors(authStrings []string) ([]Flavor, error) {
	validAuthFlavors := make([]Flavor, len(authStrings))
	for i := 0; i < len(authStrings); i++ {
		// Uppercase avoids case sensitivity, we trim the AUTH_ prefix to enable raw authentication names.
		authString := strings.TrimPrefix(strings.ToUpper(authStrings[i]), "AUTH_")
		flavor, ok := Flavor_value["AUTH_"+authString]
		if !ok {
			return nil, errors.Errorf("auth string %s is not recognized", authStrings[i])
		}
		validAuthFlavors[i] = Flavor(flavor)
	}

	return validAuthFlavors, nil
}

type (
	AuthMap map[Flavor]CredentialRequestFactory

	// CredentialRequestIdentity interface {
	// 	// Returns the auth flavor that refers to the CredentialRequest implementation.
	// 	GetAuthFlavor() Flavor
	// }

	CredentialRequestFactory interface {
		// Using information stored in the agent's security config, the session/socket, request body, and the agent's signing key,
		// initalize the state of the Credential Request so that it can sign a credential in the future. Return an error if
		// a problem occurs during initializiation.
		Init(log logging.Logger, secCfg *security.CredentialConfig, session *drpc.Session, reqBody []byte, key crypto.PrivateKey) (CredentialRequest, error)
		// Returns the auth flavor that refers to the CredentialRequest implementation.
		GetAuthFlavor() Flavor
	}

	CredentialRequest interface {
		// Contact a source of authenticity (e.g. domain socket, access manager) using the initalized state of the CredentialRequest
		// to construct a Credential object.
		GetSignedCredential(log logging.Logger, ctx context.Context) (*Credential, error)
		// Returns a key, as a string, representing a unique identifer specific to the request. This key is used by the cache
		// to remember credentials. It is vital for security that this identifer cannot be forged or easily guessed by the client -
		// otherwise cached credentials can be "stolen".
		GetKey() string
		// Returns the auth flavor that refers to the CredentialRequest implementation.
		GetAuthFlavor() Flavor
	}
)

// CredentialRequests is a list of authentication methods the agent can use.
// To implement a new type of authentication: satisfy the CredentialRequest and
// CredentialRequestFactory interfaces, add a new flavor in auth.proto, ensure
// that your `GetAuthFlavor` method returns this new unique flavor and add your
// interface to the `CredentialRequests` list below.
// The server must be configured to allow an authentication method when it is initalized.
// By default, only Unix authentication is enabled.

// var CredentialRequests = []CredentialRequestFactory{&AuthSysCredentialFactory{}, &AuthAccManCredentialFactory{}}
var FlavorToFactory = map[Flavor]CredentialRequestFactory{
	AuthSysCredentialFactory{}.GetAuthFlavor():    &AuthSysCredentialFactory{},
	AuthAccManCredentialFactory{}.GetAuthFlavor(): &AuthAccManCredentialFactory{},
}
