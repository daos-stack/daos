//
// (C) Copyright 2019 Intel Corporation.
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
	"crypto"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha512"
	"io"

	"github.com/pkg/errors"
)

//UnsupportedKeyError is a structured error used to indicate that the PublicKey
//or PrivateKey interface passed in represents a key type we do not support.
type UnsupportedKeyError struct{}

//Error is the implementation of the error interface.
func (err *UnsupportedKeyError) Error() string {
	return "key contains an unsupported key type"
}

//TokenSigner serves to encapsulate the functionality needed
//to sign and verify auth token signatures.
type TokenSigner struct {
	randPool io.Reader
}

//DefaultTokenSigner creates a TokenSigner with an instantiated entropy pool.
func DefaultTokenSigner() *TokenSigner {
	return &TokenSigner{
		randPool: rand.Reader,
	}
}

//Hash returns the SHA-512 hash of the byte array passed in.
func (s *TokenSigner) Hash(data []byte) ([]byte, error) {
	hash := sha512.New()
	if _, err := hash.Write(data); err != nil {
		return nil, errors.New("hash failed to write")
	}
	return hash.Sum(nil), nil
}

//Sign takes an unhashed set of bytes and hashes and signs the result with the
//key passed in.
func (s *TokenSigner) Sign(key crypto.PrivateKey, data []byte) ([]byte, error) {
	digest, err := s.Hash(data)
	if err != nil {
		return nil, err
	}

	if s.randPool == nil {
		s.randPool = rand.Reader
	}
	switch signingKey := key.(type) {
	// TODO: Support key types other than RSA
	case *rsa.PrivateKey:
		return rsa.SignPSS(s.randPool, signingKey, crypto.SHA512, digest, nil)
	default:
		return nil, &UnsupportedKeyError{}
	}
}

//Verify takes an unhashed set of bytes and hashes the data and verifies the
//signature against the hash and the publickey passed in.
func (s *TokenSigner) Verify(key crypto.PublicKey, data []byte, sig []byte) error {
	digest, err := s.Hash(data)
	if err != nil {
		return err
	}

	switch signingKey := key.(type) {
	// TODO: Support key types other than RSA
	case *rsa.PublicKey:
		return rsa.VerifyPSS(signingKey, crypto.SHA512, digest, sig, nil)
	default:
		return &UnsupportedKeyError{}
	}
}
