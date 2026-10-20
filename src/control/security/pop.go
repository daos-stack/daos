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

// popSigDomain separates PoP signatures from any other use of the node cert key.
const popSigDomain = "DAOS-NODE-POP-V1\x00"

// PoolID returns the pool UUID; valid only after parsePoPPayload.
func (p *PoPPayload) PoolID() uuid.UUID {
	var id uuid.UUID
	copy(id[:], p.PoolUuid)
	return id
}

// HandleID returns the handle UUID; valid only after parsePoPPayload.
func (p *PoPPayload) HandleID() uuid.UUID {
	var id uuid.UUID
	copy(id[:], p.HandleUuid)
	return id
}

// Time returns the payload creation timestamp.
func (p *PoPPayload) Time() time.Time {
	return time.Unix(p.Timestamp, 0)
}

// parsePoPPayload must only see bytes whose signature has been verified.
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

func buildPoPPayload(poolUUID, handleUUID uuid.UUID) ([]byte, error) {
	return proto.Marshal(&PoPPayload{
		PoolUuid:   poolUUID[:],
		HandleUuid: handleUUID[:],
		Timestamp:  time.Now().Unix(),
	})
}

func popHashFunc(key crypto.PublicKey) (crypto.Hash, error) {
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

// checkPoPKeyType rejects a cert whose key PoP cannot sign or verify with.
func checkPoPKeyType(cert *x509.Certificate) error {
	if _, err := popHashFunc(cert.PublicKey); err != nil {
		return errors.Wrap(ErrCertInvalid, err.Error())
	}
	return nil
}

func hashPoP(key crypto.PublicKey, payload []byte) ([]byte, error) {
	hashFunc, err := popHashFunc(key)
	if err != nil {
		return nil, err
	}

	h := hashFunc.New()
	h.Write([]byte(popSigDomain))
	h.Write(payload)
	return h.Sum(nil), nil
}

func signPoP(key crypto.PrivateKey, payload []byte) ([]byte, error) {
	k, ok := key.(*ecdsa.PrivateKey)
	if !ok {
		return nil, errors.Errorf("unsupported PoP key type %T", key)
	}
	hash, err := hashPoP(&k.PublicKey, payload)
	if err != nil {
		return nil, errors.Wrap(err, "hashing PoP payload")
	}
	return ecdsa.SignASN1(rand.Reader, k, hash)
}

func verifyPoP(pub crypto.PublicKey, payload, sig []byte) error {
	k, ok := pub.(*ecdsa.PublicKey)
	if !ok {
		return errors.Errorf("unsupported PoP key type %T", pub)
	}
	hash, err := hashPoP(k, payload)
	if err != nil {
		return errors.Wrap(err, "hashing PoP payload")
	}
	if !ecdsa.VerifyASN1(k, hash, sig) {
		return errors.Wrap(ErrCertInvalid, "PoP signature verification failed")
	}
	return nil
}
