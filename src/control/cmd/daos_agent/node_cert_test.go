//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"crypto"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/sha512"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/binary"
	"encoding/pem"
	"math/big"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

func TestBuildPoPPayload(t *testing.T) {
	poolUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")
	handleUUID := uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")

	before := time.Now().Unix()
	payload := buildPoPPayload(poolUUID, handleUUID[:])
	after := time.Now().Unix()

	if len(payload) != popPayloadLen {
		t.Fatalf("expected %d bytes, got %d", popPayloadLen, len(payload))
	}

	// Check pool UUID bytes
	var gotPoolUUID uuid.UUID
	copy(gotPoolUUID[:], payload[0:16])
	if gotPoolUUID != poolUUID {
		t.Errorf("pool UUID mismatch: got %s, want %s", gotPoolUUID, poolUUID)
	}

	// Check handle UUID bytes
	var gotHandleUUID uuid.UUID
	copy(gotHandleUUID[:], payload[16:32])
	if gotHandleUUID != handleUUID {
		t.Errorf("handle UUID mismatch: got %s, want %s", gotHandleUUID, handleUUID)
	}

	// Check timestamp is reasonable
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
	payload := make([]byte, popPayloadLen)
	if _, err := rand.Read(payload); err != nil {
		t.Fatal(err)
	}

	sig, err := signPoP(key, cert, payload)
	if err != nil {
		t.Fatalf("signPoP failed: %v", err)
	}

	// Verify the signature
	h := sha512.Sum384(payload)
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
	payload := make([]byte, popPayloadLen)
	if _, err := rand.Read(payload); err != nil {
		t.Fatal(err)
	}

	sig, err := signPoP(key, cert, payload)
	if err != nil {
		t.Fatalf("signPoP failed: %v", err)
	}

	h := sha256.Sum256(payload)
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
	payload := make([]byte, popPayloadLen)
	if _, err := rand.Read(payload); err != nil {
		t.Fatal(err)
	}

	sig, err := signPoP(key, cert, payload)
	if err != nil {
		t.Fatalf("signPoP failed: %v", err)
	}

	h := sha512.Sum512(payload)
	err = rsa.VerifyPSS(&key.PublicKey, crypto.SHA512, h[:], sig, &rsa.PSSOptions{
		SaltLength: rsa.PSSSaltLengthEqualsHash,
		Hash:       crypto.SHA512,
	})
	if err != nil {
		t.Fatalf("RSA-PSS signature verification failed: %v", err)
	}
}

func writeTestNodeCert(t *testing.T, dir, poolUUID string) (*ecdsa.PrivateKey, *x509.Certificate) {
	t.Helper()

	key, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}

	hostname, err := os.Hostname()
	if err != nil {
		t.Fatal(err)
	}

	serial, _ := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	template := &x509.Certificate{
		SerialNumber: serial,
		Subject:      pkix.Name{CommonName: security.CertCNPrefixNode + hostname},
		NotBefore:    time.Now().Add(-time.Minute),
		NotAfter:     time.Now().Add(time.Hour),
		KeyUsage:     x509.KeyUsageDigitalSignature,
	}

	certDER, err := x509.CreateCertificate(rand.Reader, template, template, &key.PublicKey, key)
	if err != nil {
		t.Fatal(err)
	}
	cert, _ := x509.ParseCertificate(certDER)

	certPEM := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: certDER})
	if err := os.WriteFile(filepath.Join(dir, poolUUID+".crt"), certPEM, 0644); err != nil {
		t.Fatal(err)
	}

	keyDER, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		t.Fatal(err)
	}
	keyPEM := pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: keyDER})
	if err := os.WriteFile(filepath.Join(dir, poolUUID+".key"), keyPEM, 0400); err != nil {
		t.Fatal(err)
	}

	return key, cert
}

func TestGetNodeCertAndPoP(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	poolUUID := "12345678-1234-1234-1234-123456789abc"
	handleUUID := uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")

	key, _ := writeTestNodeCert(t, tmpDir, poolUUID)

	log, _ := logging.NewTestLogger(t.Name())
	certPEM, pop, payload, err := getNodeCertAndPoP(log, tmpDir, poolUUID, handleUUID[:])
	if err != nil {
		t.Fatalf("getNodeCertAndPoP failed: %v", err)
	}

	if len(certPEM) == 0 {
		t.Fatal("expected non-empty certPEM")
	}
	if len(pop) == 0 {
		t.Fatal("expected non-empty PoP signature")
	}
	if len(payload) != popPayloadLen {
		t.Fatalf("expected %d byte payload, got %d", popPayloadLen, len(payload))
	}

	// Verify the PoP signature
	h := sha512.Sum384(payload)
	if !ecdsa.VerifyASN1(&key.PublicKey, h[:], pop) {
		t.Fatal("PoP signature verification failed")
	}
}

func TestGetNodeCertAndPoP_NoCert(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	poolUUID := "99999999-9999-9999-9999-999999999999"
	handleUUID := uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")

	log, _ := logging.NewTestLogger(t.Name())
	_, _, _, err := getNodeCertAndPoP(log, tmpDir, poolUUID, handleUUID[:])
	if err == nil {
		t.Fatal("expected error when cert file is missing")
	}
}

func TestGetNodeCertAndPoP_BadPoolUUID(t *testing.T) {
	log, _ := logging.NewTestLogger(t.Name())
	_, _, _, err := getNodeCertAndPoP(log, "/tmp", "not-a-uuid", nil)
	if err == nil {
		t.Fatal("expected error for invalid pool UUID")
	}
}

func TestLoadNodeCertCached(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	log, _ := logging.NewTestLogger(t.Name())
	poolUUID := "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
	writeTestNodeCert(t, tmpDir, poolUUID)

	// First load — cache miss
	c1, err := loadNodeCertCached(log, tmpDir, poolUUID)
	if err != nil {
		t.Fatalf("first load: %v", err)
	}

	// Second load — cache hit, same pointer
	c2, err := loadNodeCertCached(log, tmpDir, poolUUID)
	if err != nil {
		t.Fatalf("second load: %v", err)
	}
	if c1 != c2 {
		t.Error("expected cache hit to return same pointer")
	}

	// Different pool UUID — cache miss
	poolUUID2 := "bbbbbbbb-cccc-dddd-eeee-ffffffffffff"
	_, err = loadNodeCertCached(log, tmpDir, poolUUID2)
	if err == nil {
		t.Error("expected error for uncached missing cert")
	}
}
