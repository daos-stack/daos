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
	"testing"
	"time"

	"github.com/google/uuid"
)

func TestBuildPoPPayload(t *testing.T) {
	poolUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")
	handleUUID := uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")

	before := time.Now().Unix()
	payload := BuildPoPPayload(poolUUID, handleUUID[:])
	after := time.Now().Unix()

	if len(payload) != PoPPayloadLen {
		t.Fatalf("expected %d bytes, got %d", PoPPayloadLen, len(payload))
	}

	var gotPoolUUID uuid.UUID
	copy(gotPoolUUID[:], payload[0:16])
	if gotPoolUUID != poolUUID {
		t.Errorf("pool UUID mismatch: got %s, want %s", gotPoolUUID, poolUUID)
	}

	var gotHandleUUID uuid.UUID
	copy(gotHandleUUID[:], payload[16:32])
	if gotHandleUUID != handleUUID {
		t.Errorf("handle UUID mismatch: got %s, want %s", gotHandleUUID, handleUUID)
	}

	ts := int64(binary.BigEndian.Uint64(payload[32:40]))
	if ts < before || ts > after {
		t.Errorf("timestamp %d not in range [%d, %d]", ts, before, after)
	}
}

func TestSignPoP_ECDSA_P384(t *testing.T) {
	key, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	cert := &x509.Certificate{PublicKey: &key.PublicKey}
	payload := make([]byte, PoPPayloadLen)
	if _, err := rand.Read(payload); err != nil {
		t.Fatal(err)
	}

	sig, err := SignPoP(key, cert, payload)
	if err != nil {
		t.Fatalf("SignPoP failed: %v", err)
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
	cert := &x509.Certificate{PublicKey: &key.PublicKey}
	payload := make([]byte, PoPPayloadLen)
	if _, err := rand.Read(payload); err != nil {
		t.Fatal(err)
	}

	sig, err := SignPoP(key, cert, payload)
	if err != nil {
		t.Fatalf("SignPoP failed: %v", err)
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
	cert := &x509.Certificate{PublicKey: &key.PublicKey}
	payload := make([]byte, PoPPayloadLen)
	if _, err := rand.Read(payload); err != nil {
		t.Fatal(err)
	}

	sig, err := SignPoP(key, cert, payload)
	if err != nil {
		t.Fatalf("SignPoP failed: %v", err)
	}
	h := sha512.Sum512(append([]byte(popSigDomain), payload...))
	if err := rsa.VerifyPSS(&key.PublicKey, crypto.SHA512, h[:], sig, &rsa.PSSOptions{
		SaltLength: rsa.PSSSaltLengthEqualsHash,
		Hash:       crypto.SHA512,
	}); err != nil {
		t.Fatalf("RSA-PSS signature verification failed: %v", err)
	}
}
