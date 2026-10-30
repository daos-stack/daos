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
	"crypto/x509"
	"crypto/x509/pkix"
	"math/big"
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"
)

func TestSecurity_ParsePoPPayload(t *testing.T) {
	poolUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")
	handleUUID := uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
	marshal := func(t *testing.T, p *PoPPayload) []byte {
		t.Helper()
		b, err := proto.Marshal(p)
		if err != nil {
			t.Fatal(err)
		}
		return b
	}

	for name, tc := range map[string]struct {
		raw    func(t *testing.T) []byte
		expErr bool
	}{
		"built by buildPoPPayload": {
			raw: func(t *testing.T) []byte {
				b, err := buildPoPPayload(poolUUID, handleUUID)
				if err != nil {
					t.Fatal(err)
				}
				return b
			},
		},
		"not a proto message": {
			raw:    func(t *testing.T) []byte { return []byte{0xff, 0xff, 0xff, 0xff} },
			expErr: true,
		},
		"short pool UUID": {
			raw: func(t *testing.T) []byte {
				return marshal(t, &PoPPayload{PoolUuid: []byte{0x01}, HandleUuid: handleUUID[:], Timestamp: time.Now().Unix()})
			},
			expErr: true,
		},
		"short handle UUID": {
			raw: func(t *testing.T) []byte {
				return marshal(t, &PoPPayload{PoolUuid: poolUUID[:], HandleUuid: []byte{0x01}, Timestamp: time.Now().Unix()})
			},
			expErr: true,
		},
		"missing timestamp": {
			raw: func(t *testing.T) []byte {
				return marshal(t, &PoPPayload{PoolUuid: poolUUID[:], HandleUuid: handleUUID[:]})
			},
			expErr: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			before := time.Now().Unix()
			parsed, err := parsePoPPayload(tc.raw(t))
			if tc.expErr {
				if err == nil {
					t.Fatal("expected parse error, got nil")
				}
				return
			}
			if err != nil {
				t.Fatalf("parsePoPPayload: %v", err)
			}
			if parsed.PoolID() != poolUUID || parsed.HandleID() != handleUUID {
				t.Errorf("UUID mismatch: %+v", parsed)
			}
			if ts := parsed.Timestamp; ts < before || ts > time.Now().Unix() {
				t.Errorf("timestamp %d is not now", ts)
			}
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
		t.Fatalf("signPoP: %v", err)
	}
	if err := verifyPoP(&key.PublicKey, payload, sig); err != nil {
		t.Fatalf("verifyPoP: %v", err)
	}
	tampered := append([]byte{}, payload...)
	tampered[0] ^= 0xff
	if err := verifyPoP(&key.PublicKey, tampered, sig); err == nil {
		t.Fatal("verifyPoP accepted a tampered payload")
	}
}

func TestSecurity_PoP_RejectsOtherKeys(t *testing.T) {
	p256, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	rsaKey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatal(err)
	}
	payload := []byte("payload")
	for name, key := range map[string]crypto.Signer{"ECDSA P-256": p256, "RSA": rsaKey} {
		t.Run(name, func(t *testing.T) {
			if _, err := signPoP(key, payload); err == nil {
				t.Error("signPoP accepted the key")
			}
			if err := verifyPoP(key.Public(), payload, []byte("sig")); err == nil {
				t.Error("verifyPoP accepted the key")
			}
			if err := checkPoPKeyType(selfSignedCert(t, key)); !errors.Is(err, ErrCertInvalid) {
				t.Errorf("checkPoPKeyType: expected %v, got %v", ErrCertInvalid, err)
			}
		})
	}
	p384, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	if err := checkPoPKeyType(selfSignedCert(t, p384)); err != nil {
		t.Errorf("checkPoPKeyType rejected P-384: %v", err)
	}
}

func selfSignedCert(t *testing.T, key crypto.Signer) *x509.Certificate {
	t.Helper()
	tmpl := &x509.Certificate{SerialNumber: big.NewInt(1), Subject: pkix.Name{CommonName: "k"},
		NotBefore: time.Now(), NotAfter: time.Now().Add(time.Hour)}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, key.Public(), key)
	if err != nil {
		t.Fatal(err)
	}
	cert, err := x509.ParseCertificate(der)
	if err != nil {
		t.Fatal(err)
	}
	return cert
}
