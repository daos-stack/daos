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
	"math/big"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func writeTestNodeCert(t *testing.T, dir, poolUUID string) {
	t.Helper()

	key, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}

	serial, _ := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	template := &x509.Certificate{
		SerialNumber: serial,
		Subject:      pkix.Name{CommonName: CertCNPrefixNode + "testhost"},
		NotBefore:    time.Now().Add(-time.Minute),
		NotAfter:     time.Now().Add(time.Hour),
		KeyUsage:     x509.KeyUsageDigitalSignature,
	}
	certDER, err := x509.CreateCertificate(rand.Reader, template, template, &key.PublicKey, key)
	if err != nil {
		t.Fatal(err)
	}

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
}

func TestNodeCertLoader_Load(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	log, _ := logging.NewTestLogger(t.Name())
	loader := NewNodeCertLoader(tmpDir)

	poolUUID := "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
	writeTestNodeCert(t, tmpDir, poolUUID)

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
	if _, err := loader.Load(log, "bbbbbbbb-cccc-dddd-eeee-ffffffffffff"); err == nil {
		t.Error("expected error for missing cert")
	}
	// Invalid pool UUID -> error
	if _, err := loader.Load(log, "not-a-uuid"); err == nil {
		t.Error("expected error for invalid pool UUID")
	}
}

func TestNodeCertLoader_ReloadOnFileChange(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	log, _ := logging.NewTestLogger(t.Name())
	loader := NewNodeCertLoader(tmpDir)

	poolUUID := "cccccccc-dddd-eeee-ffff-000000000000"
	writeTestNodeCert(t, tmpDir, poolUUID)

	c1, err := loader.Load(log, poolUUID)
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

	c2, err := loader.Load(log, poolUUID)
	if err != nil {
		t.Fatalf("reload: %v", err)
	}
	if c1 == c2 {
		t.Error("expected cache to invalidate after cert file changed")
	}
}
