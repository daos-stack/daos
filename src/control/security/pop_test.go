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
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/test"
	sectest "github.com/daos-stack/daos/src/control/security/test"
)

func marshalPoP(t *testing.T, p *PoPPayload) []byte {
	t.Helper()

	b, err := proto.Marshal(p)
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func TestSecurity_parsePoPPayload(t *testing.T) {
	poolUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")
	handleUUID := uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
	timestamp := time.Date(2026, 4, 15, 14, 0, 29, 0, time.UTC)

	for name, tc := range map[string]struct {
		raw    []byte
		expErr error
	}{
		"valid": {
			raw: marshalPoP(t, &PoPPayload{PoolUuid: poolUUID[:], HandleUuid: handleUUID[:], Timestamp: timestamp.Unix()}),
		},
		"not a proto message": {
			raw:    []byte{0xff, 0xff, 0xff, 0xff},
			expErr: errors.New("unmarshaling PoP payload"),
		},
		"short pool UUID": {
			raw:    marshalPoP(t, &PoPPayload{PoolUuid: []byte{0x01}, HandleUuid: handleUUID[:], Timestamp: timestamp.Unix()}),
			expErr: errors.New("pool UUID is 1 bytes, want 16"),
		},
		"short handle UUID": {
			raw:    marshalPoP(t, &PoPPayload{PoolUuid: poolUUID[:], HandleUuid: []byte{0x01}, Timestamp: timestamp.Unix()}),
			expErr: errors.New("handle UUID is 1 bytes, want 16"),
		},
		"missing timestamp": {
			raw:    marshalPoP(t, &PoPPayload{PoolUuid: poolUUID[:], HandleUuid: handleUUID[:]}),
			expErr: errors.New("timestamp 0 is not positive"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			parsed, err := parsePoPPayload(tc.raw)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, poolUUID, parsed.PoolID(), "pool UUID")
			test.AssertEqual(t, handleUUID, parsed.HandleID(), "handle UUID")
			test.AssertTrue(t, timestamp.Equal(parsed.Time()), "timestamp")
		})
	}
}

func TestSecurity_PoP_RoundTrip(t *testing.T) {
	key, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	payload := make([]byte, 64)
	if _, err := rand.Read(payload); err != nil {
		t.Fatal(err)
	}
	sig, err := signPoP(key, payload)
	if err != nil {
		t.Fatal(err)
	}
	tampered := append([]byte{}, payload...)
	tampered[0] ^= 0xff

	for name, tc := range map[string]struct {
		payload []byte
		sig     []byte
		expErr  error
	}{
		"valid":            {payload: payload, sig: sig},
		"tampered payload": {payload: tampered, sig: sig, expErr: ErrCertInvalid},
		"garbage sig":      {payload: payload, sig: []byte("sig"), expErr: ErrCertInvalid},
	} {
		t.Run(name, func(t *testing.T) {
			test.CmpErr(t, tc.expErr, verifyPoP(&key.PublicKey, tc.payload, tc.sig))
		})
	}
}

func TestSecurity_PoP_KeyTypes(t *testing.T) {
	p384, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	p256, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	rsaKey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatal(err)
	}
	payload := []byte("payload")

	for name, tc := range map[string]struct {
		key    crypto.Signer
		expErr error
	}{
		"P-384": {key: p384},
		"P-256": {key: p256, expErr: errors.New("unsupported PoP key curve P-256")},
		"RSA":   {key: rsaKey, expErr: errors.New("unsupported PoP key type")},
	} {
		t.Run(name, func(t *testing.T) {
			sig, err := signPoP(tc.key, payload)
			test.CmpErr(t, tc.expErr, err)
			test.CmpErr(t, tc.expErr, verifyPoP(tc.key.Public(), payload, sig))
			cert := sectest.ParseCert(t, sectest.NewLeafForKey(t, "k", tc.key, nil, nil))
			test.CmpErr(t, tc.expErr, checkPoPKeyType(cert))
		})
	}
}
