//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"crypto"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/sha512"
	"crypto/x509"
	"encoding/binary"
	"fmt"
	"time"

	"github.com/google/uuid"
)

const PoPPayloadLen = 40

// popSigDomain separates PoP signatures from any other signing operation
// that might reuse a node cert key.
const popSigDomain = "DAOS-NODE-POP-V1\x00"

// BuildPoPPayload assembles a fresh proof-of-possession payload.
func BuildPoPPayload(poolUUID uuid.UUID, handleUUID []byte) []byte {
	payload := make([]byte, PoPPayloadLen)
	copy(payload[0:16], poolUUID[:])
	copy(payload[16:32], handleUUID)
	binary.BigEndian.PutUint64(payload[32:40], uint64(time.Now().Unix()))
	return payload
}

// SignPoP signs payload with key, using a hash chosen for the key type.
func SignPoP(key crypto.PrivateKey, cert *x509.Certificate, payload []byte) ([]byte, error) {
	signInput := append([]byte(popSigDomain), payload...)
	switch k := key.(type) {
	case *rsa.PrivateKey:
		h := sha512.Sum512(signInput)
		return rsa.SignPSS(rand.Reader, k, crypto.SHA512, h[:], &rsa.PSSOptions{
			SaltLength: rsa.PSSSaltLengthEqualsHash,
			Hash:       crypto.SHA512,
		})
	case *ecdsa.PrivateKey:
		var hash []byte
		switch k.Curve {
		case elliptic.P256():
			h := sha256.Sum256(signInput)
			hash = h[:]
		case elliptic.P384():
			h := sha512.Sum384(signInput)
			hash = h[:]
		default:
			return nil, fmt.Errorf("unsupported ECDSA curve: %v", k.Curve.Params().Name)
		}
		return ecdsa.SignASN1(rand.Reader, k, hash)
	default:
		return nil, fmt.Errorf("unsupported key type: %T", key)
	}
}
