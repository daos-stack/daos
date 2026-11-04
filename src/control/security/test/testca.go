//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Package test mints the certificates production code cannot: roots, and CAs with arbitrary names.
package test

import (
	"crypto"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"math/big"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// NewCA returns a CA cert (PEM) and key, self-signed when parent is nil.
func NewCA(t *testing.T, cn string, parent *x509.Certificate, parentKey *ecdsa.PrivateKey) ([]byte, *ecdsa.PrivateKey) {
	t.Helper()
	tmpl := &x509.Certificate{
		Subject:               pkix.Name{CommonName: cn},
		NotBefore:             time.Now().Add(-time.Minute),
		NotAfter:              time.Now().Add(24 * time.Hour),
		KeyUsage:              x509.KeyUsageCertSign,
		BasicConstraintsValid: true,
		IsCA:                  true,
	}
	if parent == nil {
		return mint(t, tmpl, nil, nil)
	}
	return mint(t, tmpl, parent, parentKey)
}

// NewLeaf issues a client cert with a caller-chosen validity window.
func NewLeaf(t *testing.T, cn string, parent *x509.Certificate, parentKey crypto.Signer, notBefore, notAfter time.Time) (certPEM, keyPEM []byte) {
	t.Helper()
	tmpl := &x509.Certificate{
		Subject:     pkix.Name{CommonName: cn},
		NotBefore:   notBefore,
		NotAfter:    notAfter,
		KeyUsage:    x509.KeyUsageDigitalSignature,
		ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
	}
	certPEM, key := mint(t, tmpl, parent, parentKey)
	return certPEM, KeyPEM(t, key)
}

func mint(t *testing.T, tmpl, parent *x509.Certificate, parentKey crypto.Signer) ([]byte, *ecdsa.PrivateKey) {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	tmpl.SerialNumber, err = rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	if err != nil {
		t.Fatal(err)
	}
	if parent == nil {
		parent, parentKey = tmpl, key
	}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, parent, &key.PublicKey, parentKey)
	if err != nil {
		t.Fatal(err)
	}
	return pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}), key
}

// KeyPEM encodes a private key as PKCS#8 PEM.
func KeyPEM(t *testing.T, key crypto.PrivateKey) []byte {
	t.Helper()
	der, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		t.Fatal(err)
	}
	return pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: der})
}

// ParseCert parses the first CERTIFICATE block in a PEM bundle.
func ParseCert(t *testing.T, certPEM []byte) *x509.Certificate {
	t.Helper()
	block, _ := pem.Decode(certPEM)
	if block == nil {
		t.Fatal("pem.Decode returned nil block")
	}
	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		t.Fatal(err)
	}
	return cert
}

// WriteCertFiles writes a cert and key with the permissions the loaders expect.
func WriteCertFiles(t *testing.T, certPath, keyPath string, certPEM, keyPEM []byte) {
	t.Helper()
	if err := os.WriteFile(certPath, certPEM, 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(keyPath, keyPEM, 0400); err != nil {
		t.Fatal(err)
	}
}

// WriteCAFiles writes a self-signed CA as test_ca.{crt,key} in dir.
func WriteCAFiles(t *testing.T, dir string) (certPath, keyPath string) {
	t.Helper()
	certPEM, key := NewCA(t, "Test CA", nil, nil)
	keyPEM := KeyPEM(t, key)
	certPath = filepath.Join(dir, "test_ca.crt")
	keyPath = filepath.Join(dir, "test_ca.key")
	WriteCertFiles(t, certPath, keyPath, certPEM, keyPEM)
	return certPath, keyPath
}
