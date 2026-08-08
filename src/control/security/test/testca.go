//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Package test provides crypto fixtures for DAOS test code.
package test

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"fmt"
	"math/big"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// NewCA returns a fresh CA cert (PEM) and its private key. If parent is
// non-nil the new cert is signed by parent/parentKey; otherwise it is
// self-signed.
func NewCA(t *testing.T, cn string, parent *x509.Certificate, parentKey *ecdsa.PrivateKey) ([]byte, *ecdsa.PrivateKey) {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	serial, _ := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	tmpl := &x509.Certificate{
		SerialNumber:          serial,
		Subject:               pkix.Name{CommonName: cn},
		NotBefore:             time.Now().Add(-time.Minute),
		NotAfter:              time.Now().Add(time.Hour),
		KeyUsage:              x509.KeyUsageCertSign,
		BasicConstraintsValid: true,
		IsCA:                  true,
	}
	issuer := tmpl
	signerKey := key
	if parent != nil {
		issuer = parent
		signerKey = parentKey
	}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, issuer, &key.PublicKey, signerKey)
	if err != nil {
		t.Fatal(err)
	}
	return pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}), key
}

// CertFingerprint returns the SHA-256 fingerprint (hex) of a PEM cert.
func CertFingerprint(t *testing.T, certPEM []byte) string {
	t.Helper()
	block, _ := pem.Decode(certPEM)
	if block == nil {
		t.Fatal("pem.Decode returned nil block")
	}
	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		t.Fatal(err)
	}
	return fmt.Sprintf("%x", sha256.Sum256(cert.Raw))
}

// WriteCAFiles writes a fresh self-signed CA cert + key into dir as
// "test_ca.crt" / "test_ca.key" and returns their paths.
func WriteCAFiles(t *testing.T, dir string) (keyPath, certPath string) {
	t.Helper()
	certPEM, key := NewCA(t, "Test CA", nil, nil)
	certPath = filepath.Join(dir, "test_ca.crt")
	if err := os.WriteFile(certPath, certPEM, 0644); err != nil {
		t.Fatal(err)
	}
	keyDER, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		t.Fatal(err)
	}
	keyPath = filepath.Join(dir, "test_ca.key")
	if err := os.WriteFile(keyPath,
		pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: keyDER}),
		0400); err != nil {
		t.Fatal(err)
	}
	return keyPath, certPath
}
