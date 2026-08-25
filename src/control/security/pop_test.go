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
	"testing"
	"time"

	"github.com/google/uuid"
	"google.golang.org/protobuf/proto"
)

func TestBuildPoPPayload(t *testing.T) {
	poolUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")
	handleUUID := uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")

	before := time.Now().Unix()
	payload, err := buildPoPPayload(poolUUID, handleUUID)
	if err != nil {
		t.Fatal(err)
	}
	after := time.Now().Unix()

	parsed, err := parsePoPPayload(payload)
	if err != nil {
		t.Fatalf("parsePoPPayload: %v", err)
	}
	if parsed.PoolID() != poolUUID {
		t.Errorf("pool UUID mismatch: got %s, want %s", parsed.PoolID(), poolUUID)
	}
	if parsed.HandleID() != handleUUID {
		t.Errorf("handle UUID mismatch: got %s, want %s", parsed.HandleID(), handleUUID)
	}
	if ts := parsed.Timestamp; ts < before || ts > after {
		t.Errorf("timestamp %d not in range [%d, %d]", ts, before, after)
	}
}

func TestParsePoPPayload_Invalid(t *testing.T) {
	poolUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")
	handleUUID := uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")

	for name, raw := range map[string]func(t *testing.T) []byte{
		"not a proto message": func(t *testing.T) []byte {
			return []byte{0xff, 0xff, 0xff, 0xff}
		},
		"short pool UUID": func(t *testing.T) []byte {
			b, err := proto.Marshal(&PoPPayload{
				PoolUuid: []byte{0x01}, HandleUuid: handleUUID[:],
				Timestamp: time.Now().Unix(),
			})
			if err != nil {
				t.Fatal(err)
			}
			return b
		},
		"short handle UUID": func(t *testing.T) []byte {
			b, err := proto.Marshal(&PoPPayload{
				PoolUuid: poolUUID[:], HandleUuid: []byte{0x01},
				Timestamp: time.Now().Unix(),
			})
			if err != nil {
				t.Fatal(err)
			}
			return b
		},
		"missing timestamp": func(t *testing.T) []byte {
			b, err := proto.Marshal(&PoPPayload{
				PoolUuid: poolUUID[:], HandleUuid: handleUUID[:],
			})
			if err != nil {
				t.Fatal(err)
			}
			return b
		},
	} {
		t.Run(name, func(t *testing.T) {
			if _, err := parsePoPPayload(raw(t)); err == nil {
				t.Fatal("expected parse error, got nil")
			}
		})
	}
}

func TestSignPoP_ECDSA_P384(t *testing.T) {
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
		t.Fatalf("signPoP failed: %v", err)
	}
	h := sha512.Sum384(append([]byte(popSigDomain), payload...))
	if !ecdsa.VerifyASN1(&key.PublicKey, h[:], sig) {
		t.Fatal("ECDSA P-384 signature verification failed")
	}
}

func TestSignPoP_ECDSA_P256(t *testing.T) {
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	payload := make([]byte, 64)
	if _, err := rand.Read(payload); err != nil {
		t.Fatal(err)
	}

	sig, err := signPoP(key, payload)
	if err != nil {
		t.Fatalf("signPoP failed: %v", err)
	}
	h := sha256.Sum256(append([]byte(popSigDomain), payload...))
	if !ecdsa.VerifyASN1(&key.PublicKey, h[:], sig) {
		t.Fatal("ECDSA P-256 signature verification failed")
	}
}

func TestSignPoP_RSA(t *testing.T) {
	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatal(err)
	}
	payload := make([]byte, 64)
	if _, err := rand.Read(payload); err != nil {
		t.Fatal(err)
	}

	sig, err := signPoP(key, payload)
	if err != nil {
		t.Fatalf("signPoP failed: %v", err)
	}
	h := sha512.Sum512(append([]byte(popSigDomain), payload...))
	if err := rsa.VerifyPSS(&key.PublicKey, crypto.SHA512, h[:], sig, &rsa.PSSOptions{
		SaltLength: rsa.PSSSaltLengthEqualsHash,
		Hash:       crypto.SHA512,
	}); err != nil {
		t.Fatalf("RSA-PSS signature verification failed: %v", err)
	}
}

func TestVerifyPoP_RoundTrip(t *testing.T) {
	payload := make([]byte, 64)
	if _, err := rand.Read(payload); err != nil {
		t.Fatal(err)
	}

	ec384, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	ec256, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	rsaKey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatal(err)
	}

	for name, k := range map[string]struct {
		priv crypto.PrivateKey
		pub  crypto.PublicKey
	}{
		"ECDSA P-384": {ec384, &ec384.PublicKey},
		"ECDSA P-256": {ec256, &ec256.PublicKey},
		"RSA-PSS":     {rsaKey, &rsaKey.PublicKey},
	} {
		t.Run(name, func(t *testing.T) {
			sig, err := signPoP(k.priv, payload)
			if err != nil {
				t.Fatalf("signPoP: %v", err)
			}
			if err := verifyPoP(k.pub, payload, sig); err != nil {
				t.Fatalf("verifyPoP: %v", err)
			}
			// Tampering the payload must fail verification.
			tampered := make([]byte, len(payload))
			copy(tampered, payload)
			tampered[0] ^= 0xff
			if err := verifyPoP(k.pub, tampered, sig); err == nil {
				t.Fatal("verifyPoP accepted a tampered payload")
			}
		})
	}
}
