//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
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

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
)

func writeNodeCertForCN(t *testing.T, dir, poolUUID, cn string, notBefore, notAfter time.Time) {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	serial, _ := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	tmpl := &x509.Certificate{
		SerialNumber: serial,
		Subject:      pkix.Name{CommonName: cn},
		NotBefore:    notBefore,
		NotAfter:     notAfter,
		KeyUsage:     x509.KeyUsageDigitalSignature,
	}
	certDER, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, &key.PublicKey, key)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, poolUUID+".crt"),
		pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: certDER}), 0644); err != nil {
		t.Fatal(err)
	}
	keyDER, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, poolUUID+".key"),
		pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: keyDER}), 0400); err != nil {
		t.Fatal(err)
	}
}

func TestAgent_ValidateNodeCertForUse(t *testing.T) {
	machine, err := auth.GetMachineName()
	if err != nil {
		t.Fatal(err)
	}
	now := time.Now()
	notBefore := now.Add(-time.Minute)
	notAfter := now.Add(time.Hour)

	cases := map[string]struct {
		cn        string
		notBefore time.Time
		notAfter  time.Time
		now       time.Time
		expectErr bool
	}{
		"node cert matches machine": {security.CertCNPrefixNode + machine, notBefore, notAfter, now, false},
		"node cert wrong machine":   {security.CertCNPrefixNode + "wronghost", notBefore, notAfter, now, true},
		"tenant cert skips machine": {security.CertCNPrefixTenant + "team-a", notBefore, notAfter, now, false},
		"unrecognized prefix":       {"weird:thing", notBefore, notAfter, now, true},
		"expired cert":              {security.CertCNPrefixNode + machine, notBefore, now.Add(-time.Minute), now, true},
		"not yet valid":             {security.CertCNPrefixNode + machine, now.Add(time.Hour), notAfter, now, true},
		"not yet valid within skew": {security.CertCNPrefixNode + machine, now.Add(time.Minute), notAfter, now, false},
	}
	for name, tc := range cases {
		t.Run(name, func(t *testing.T) {
			cert := &x509.Certificate{
				Subject:   pkix.Name{CommonName: tc.cn},
				NotBefore: tc.notBefore,
				NotAfter:  tc.notAfter,
			}
			err := validateNodeCertForUse(cert, "12345678-1234-1234-1234-123456789abc", tc.now)
			if tc.expectErr && err == nil {
				t.Fatal("expected error, got nil")
			}
			if !tc.expectErr && err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
		})
	}
}

func TestAgent_GetNodeCertAndPoP(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	machine, err := auth.GetMachineName()
	if err != nil {
		t.Fatal(err)
	}
	poolUUID := "12345678-1234-1234-1234-123456789abc"
	handleUUID := uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
	writeNodeCertForCN(t, tmpDir, poolUUID, security.CertCNPrefixNode+machine,
		time.Now().Add(-time.Minute), time.Now().Add(time.Hour))

	log, _ := logging.NewTestLogger(t.Name())
	loader := security.NewNodeCertLoader(tmpDir)
	_, certPEM, pop, payload, err := getNodeCertAndPoP(log, loader, poolUUID, handleUUID[:])
	if err != nil {
		t.Fatalf("getNodeCertAndPoP failed: %v", err)
	}
	if len(certPEM) == 0 || len(pop) == 0 {
		t.Fatal("expected non-empty certPEM and pop")
	}
	if len(payload) != security.PoPPayloadLen {
		t.Fatalf("expected %d byte payload, got %d", security.PoPPayloadLen, len(payload))
	}
}

func TestAgent_GetNodeCertAndPoP_BadPoolUUID(t *testing.T) {
	log, _ := logging.NewTestLogger(t.Name())
	loader := security.NewNodeCertLoader("/tmp")
	if _, _, _, _, err := getNodeCertAndPoP(log, loader, "not-a-uuid", nil); err == nil {
		t.Fatal("expected error for invalid pool UUID")
	}
}

func TestAgent_GetNodeCertAndPoP_MissingCert(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	log, _ := logging.NewTestLogger(t.Name())
	loader := security.NewNodeCertLoader(tmpDir)
	poolUUID := "99999999-9999-9999-9999-999999999999"
	if _, _, _, _, err := getNodeCertAndPoP(log, loader, poolUUID, nil); err == nil {
		t.Fatal("expected error for missing cert")
	}
}
