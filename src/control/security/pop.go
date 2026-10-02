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
	"fmt"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"
)

// A proof-of-possession (PoP) ties the presenter of a cert to its private
// key: the client signs a payload binding pool, handle, and timestamp; the
// server checks the signature against the cert's public key.

// popSigDomain separates PoP signatures from any other signing operation
// that might reuse a node cert key.
const popSigDomain = "DAOS-NODE-POP-V1\x00"

// PoolID returns the pool UUID bound into the payload.
// Valid only after parsePoPPayload has validated field lengths.
func (p *PoPPayload) PoolID() uuid.UUID {
	var id uuid.UUID
	copy(id[:], p.PoolUuid)
	return id
}

// HandleID returns the pool handle UUID bound into the payload.
// Valid only after parsePoPPayload has validated field lengths.
func (p *PoPPayload) HandleID() uuid.UUID {
	var id uuid.UUID
	copy(id[:], p.HandleUuid)
	return id
}

// Time returns the payload creation timestamp.
func (p *PoPPayload) Time() time.Time {
	return time.Unix(p.Timestamp, 0)
}

// ReplayKey returns a stable identifier for this proof's (pool, handle)
// binding, for use in replay caches.
func (p *PoPPayload) ReplayKey() string {
	return p.PoolID().String() + ":" + p.HandleID().String()
}

// parsePoPPayload unmarshals raw payload bytes and validates field shape.
// Only call on bytes whose signature has already been verified.
func parsePoPPayload(raw []byte) (*PoPPayload, error) {
	p := &PoPPayload{}
	if err := proto.Unmarshal(raw, p); err != nil {
		return nil, errors.Wrap(err, "unmarshaling PoP payload")
	}
	uuidLen := len(uuid.UUID{})
	if len(p.PoolUuid) != uuidLen {
		return nil, fmt.Errorf("PoP payload pool UUID is %d bytes, want %d",
			len(p.PoolUuid), uuidLen)
	}
	if len(p.HandleUuid) != uuidLen {
		return nil, fmt.Errorf("PoP payload handle UUID is %d bytes, want %d",
			len(p.HandleUuid), uuidLen)
	}
	if p.Timestamp <= 0 {
		return nil, fmt.Errorf("PoP payload timestamp %d is not positive", p.Timestamp)
	}
	return p, nil
}

// buildPoPPayload assembles and marshals a fresh proof-of-possession payload.
func buildPoPPayload(poolUUID, handleUUID uuid.UUID) ([]byte, error) {
	return proto.Marshal(&PoPPayload{
		PoolUuid:   poolUUID[:],
		HandleUuid: handleUUID[:],
		Timestamp:  time.Now().Unix(),
	})
}

// signPoP signs payload with key, using a hash chosen for the key type.
func signPoP(key crypto.PrivateKey, payload []byte) ([]byte, error) {
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

// verifyPoP verifies a signPoP signature using the matching domain prefix.
func verifyPoP(pub crypto.PublicKey, payload, sig []byte) error {
	signInput := append([]byte(popSigDomain), payload...)
	switch k := pub.(type) {
	case *rsa.PublicKey:
		h := sha512.Sum512(signInput)
		return rsa.VerifyPSS(k, crypto.SHA512, h[:], sig, &rsa.PSSOptions{
			SaltLength: rsa.PSSSaltLengthEqualsHash,
			Hash:       crypto.SHA512,
		})
	case *ecdsa.PublicKey:
		var hash []byte
		switch k.Curve {
		case elliptic.P256():
			h := sha256.Sum256(signInput)
			hash = h[:]
		case elliptic.P384():
			h := sha512.Sum384(signInput)
			hash = h[:]
		default:
			return fmt.Errorf("unsupported ECDSA curve: %v", k.Curve.Params().Name)
		}
		if !ecdsa.VerifyASN1(k, hash, sig) {
			return fmt.Errorf("ECDSA signature verification failed")
		}
		return nil
	default:
		return fmt.Errorf("unsupported public key type: %T", pub)
	}
}
