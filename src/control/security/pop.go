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
	"crypto/x509"
	"fmt"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"
)

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

// popHash returns the hash function to use for signing and verifying a proof-of-possession payload.
func popHash(key crypto.PublicKey) (crypto.Hash, error) {
	switch k := key.(type) {
	case *ecdsa.PublicKey:
		switch k.Curve {
		case elliptic.P384():
			return crypto.SHA384, nil
		default:
			return 0, errors.Errorf("unsupported PoP key curve %s",
				k.Curve.Params().Name)
		}
	default:
		return 0, errors.Errorf("unsupported PoP key type %T", key)
	}
}

// CheckPoPKeyType refuses a certificate whose key the proof-of-possession
// code cannot sign or verify with.
func CheckPoPKeyType(cert *x509.Certificate) error {
	if _, err := popHash(cert.PublicKey); err != nil {
		return errors.Wrap(ErrCertInvalid, err.Error())
	}
	return nil
}

// hashPoP hashes payload with the domain prefix.
// The hash is used for signing and verifying the payload, proving possession
// of the private key.
func hashPoP(key crypto.PublicKey, payload []byte) ([]byte, error) {
	hashFunc, err := popHash(key)
	if err != nil {
		return nil, err
	}

	h := hashFunc.New()
	h.Write([]byte(popSigDomain))
	h.Write(payload)
	return h.Sum(nil), nil
}

// signPoP signs payload with key.
// Signing the payload proves possession of the private key, which is
// cryptographically validated on the receiving side by verifyPoP.
func signPoP(key crypto.PrivateKey, payload []byte) ([]byte, error) {
	switch k := key.(type) {
	case *ecdsa.PrivateKey:
		hash, err := hashPoP(&k.PublicKey, payload)
		if err != nil {
			return nil, errors.Wrap(err, "hashing PoP payload")
		}

		return ecdsa.SignASN1(rand.Reader, k, hash)
	default:
		return nil, errors.Errorf("unsupported PoP key type %T", key)
	}
}

// verifyPoP verifies that the payload was signed by the private key corresponding to pub.
func verifyPoP(pub crypto.PublicKey, payload, sig []byte) error {
	hash, err := hashPoP(pub, payload)
	if err != nil {
		return errors.Wrap(err, "hashing PoP payload")
	}

	switch k := pub.(type) {
	case *ecdsa.PublicKey:
		if !ecdsa.VerifyASN1(k, hash, sig) {
			return errors.Wrap(ErrCertInvalid, "PoP signature verification failed")
		}
	default:
		return errors.Errorf("unsupported PoP key type %T", pub)
	}
	return nil
}
