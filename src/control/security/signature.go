//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
