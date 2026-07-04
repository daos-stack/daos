//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	sectest "github.com/daos-stack/daos/src/control/security/test"
)

const testMachineName = "testhost"

func writeTestNodeCert(t *testing.T, dir, poolUUID string) {
	t.Helper()
	sectest.WriteNodeCert(t, dir, poolUUID, CertCNPrefixNode+testMachineName,
		time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
}

func TestNodeCertLoader_Load(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	log, _ := logging.NewTestLogger(t.Name())
	loader := NewNodeCertLoader(tmpDir)

	poolUUID := uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
	writeTestNodeCert(t, tmpDir, poolUUID.String())

	c1, err := loader.Load(log, poolUUID)
	if err != nil {
		t.Fatalf("first load: %v", err)
	}
	c2, err := loader.Load(log, poolUUID)
	if err != nil {
		t.Fatalf("second load: %v", err)
	}
	if c1 != c2 {
		t.Error("expected cache hit to return same pointer")
	}

	// Missing cert -> error
	if _, err := loader.Load(log, uuid.MustParse("bbbbbbbb-cccc-dddd-eeee-ffffffffffff")); err == nil {
		t.Error("expected error for missing cert")
	}
}

func TestNodeCertLoader_ReloadOnFileChange(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	log, _ := logging.NewTestLogger(t.Name())
	loader := NewNodeCertLoader(tmpDir)

	poolUUID := "cccccccc-dddd-eeee-ffff-000000000000"
	writeTestNodeCert(t, tmpDir, poolUUID)

	c1, err := loader.Load(log, uuid.MustParse(poolUUID))
	if err != nil {
		t.Fatalf("first load: %v", err)
	}

	for _, name := range []string{poolUUID + ".crt", poolUUID + ".key"} {
		if err := os.Remove(filepath.Join(tmpDir, name)); err != nil {
			t.Fatalf("remove %s: %v", name, err)
		}
	}
	writeTestNodeCert(t, tmpDir, poolUUID)
	// Bump mtime past 1s resolution so a stat-based change detector
	// reliably sees it regardless of filesystem granularity.
	futureTime := time.Now().Add(2 * time.Second)
	for _, name := range []string{poolUUID + ".crt", poolUUID + ".key"} {
		p := filepath.Join(tmpDir, name)
		if err := os.Chtimes(p, futureTime, futureTime); err != nil {
			t.Fatalf("chtimes %s: %v", p, err)
		}
	}

	c2, err := loader.Load(log, uuid.MustParse(poolUUID))
	if err != nil {
		t.Fatalf("reload: %v", err)
	}
	if c1 == c2 {
		t.Error("expected cache to invalidate after cert file changed")
	}
}

func TestNodeCertLoader_KeyCertMismatch(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	log, _ := logging.NewTestLogger(t.Name())
	loader := NewNodeCertLoader(tmpDir)

	poolUUID := "dddddddd-eeee-ffff-0000-111111111111"
	writeTestNodeCert(t, tmpDir, poolUUID)

	// Replace the key with one that doesn't match the cert.
	otherKey, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	keyDER, err := x509.MarshalPKCS8PrivateKey(otherKey)
	if err != nil {
		t.Fatal(err)
	}
	keyPEM := pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: keyDER})
	keyPath := filepath.Join(tmpDir, poolUUID+".key")
	if err := os.Remove(keyPath); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(keyPath, keyPEM, 0400); err != nil {
		t.Fatal(err)
	}

	if _, err := loader.Load(log, uuid.MustParse(poolUUID)); err == nil ||
		!strings.Contains(err.Error(), "does not match") {
		t.Fatalf("expected key/cert mismatch error, got %v", err)
	}
}

func TestSecurity_ValidateNodeCertForUse(t *testing.T) {
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
		"node cert matches machine": {CertCNPrefixNode + testMachineName, notBefore, notAfter, now, false},
		"node cert wrong machine":   {CertCNPrefixNode + "wronghost", notBefore, notAfter, now, true},
		"tenant cert skips machine": {CertCNPrefixTenant + "team-a", notBefore, notAfter, now, false},
		"unrecognized prefix":       {"weird:thing", notBefore, notAfter, now, true},
		"expired cert":              {CertCNPrefixNode + testMachineName, notBefore, now.Add(-time.Minute), now, true},
		"not yet valid":             {CertCNPrefixNode + testMachineName, now.Add(time.Hour), notAfter, now, true},
		"not yet valid within skew": {CertCNPrefixNode + testMachineName, now.Add(time.Minute), notAfter, now, false},
	}
	for name, tc := range cases {
		t.Run(name, func(t *testing.T) {
			cert := &x509.Certificate{
				Subject:   pkix.Name{CommonName: tc.cn},
				NotBefore: tc.notBefore,
				NotAfter:  tc.notAfter,
			}
			err := validateNodeCertForUse(cert, "12345678-1234-1234-1234-123456789abc",
				testMachineName, tc.now)
			if tc.expectErr && err == nil {
				t.Fatal("expected error, got nil")
			}
			if !tc.expectErr && err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
		})
	}
}

func TestNodeCertLoader_CertAndPoP(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	poolUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")
	handleUUID := uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
	writeTestNodeCert(t, tmpDir, poolUUID.String())

	log, _ := logging.NewTestLogger(t.Name())
	loader := NewNodeCertLoader(tmpDir)
	cert, pop, payload, err := loader.CertAndPoP(log, poolUUID, handleUUID, testMachineName)
	if err != nil {
		t.Fatalf("CertAndPoP failed: %v", err)
	}
	if len(cert.PEM) == 0 || len(pop) == 0 {
		t.Fatal("expected non-empty cert PEM and pop")
	}
	if _, err := parsePoPPayload(payload); err != nil {
		t.Fatalf("payload does not parse: %v", err)
	}
}
