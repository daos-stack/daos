//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

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
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/security"
)

func TestCertWatermarks_RoundTrip(t *testing.T) {
	for name, tc := range map[string]struct {
		input security.CertWatermarks
	}{
		"empty": {
			input: nil,
		},
		"single node": {
			input: map[string]time.Time{
				"node:node1": time.Date(2026, 4, 15, 14, 0, 29, 0, time.UTC),
			},
		},
		"multiple": {
			input: map[string]time.Time{
				"node:node1":   time.Date(2026, 4, 15, 14, 0, 29, 0, time.UTC),
				"node:node2":   time.Date(2026, 4, 15, 14, 0, 30, 0, time.UTC),
				"tenant:teamA": time.Date(2026, 4, 20, 9, 15, 0, 0, time.UTC),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			encoded, err := security.EncodeCertWatermarks(tc.input)
			if err != nil {
				t.Fatalf("encode failed: %v", err)
			}

			// An empty map encodes to nil (no prop bytes), and
			// decoding nil returns (nil, error) because there's
			// nothing to parse — short-circuit that case.
			if len(tc.input) == 0 {
				if encoded != nil {
					t.Fatalf("expected nil encoding for empty map, got %q", encoded)
				}
				return
			}

			got, err := security.DecodeCertWatermarks(encoded)
			if err != nil {
				t.Fatalf("decode failed: %v", err)
			}
			if diff := cmp.Diff(tc.input, got); diff != "" {
				t.Fatalf("round-trip mismatch (-want +got):\n%s", diff)
			}
		})
	}
}

func TestAdvanceCertWatermark(t *testing.T) {
	t0 := time.Date(2026, 4, 15, 14, 0, 0, 0, time.UTC)

	for name, tc := range map[string]struct {
		existing map[string]time.Time
		cn       string
		now      time.Time
		expect   time.Time
	}{
		"no prior entry returns now truncated": {
			existing: nil,
			cn:       "node:node1",
			now:      t0.Add(500 * time.Millisecond),
			expect:   t0,
		},
		"no prior entry for this CN returns now": {
			existing: map[string]time.Time{"node:other": t0.Add(time.Hour)},
			cn:       "node:node1",
			now:      t0,
			expect:   t0,
		},
		"now strictly greater than prior is accepted": {
			existing: map[string]time.Time{"node:node1": t0},
			cn:       "node:node1",
			now:      t0.Add(time.Minute),
			expect:   t0.Add(time.Minute),
		},
		"now equal to prior is promoted by 1s": {
			existing: map[string]time.Time{"node:node1": t0},
			cn:       "node:node1",
			now:      t0,
			expect:   t0.Add(time.Second),
		},
		"now earlier than prior is promoted to prior+1s": {
			existing: map[string]time.Time{"node:node1": t0.Add(time.Hour)},
			cn:       "node:node1",
			now:      t0,
			expect:   t0.Add(time.Hour).Add(time.Second),
		},
		"sub-second precision is truncated": {
			existing: nil,
			cn:       "node:node1",
			now:      t0.Add(999 * time.Millisecond),
			expect:   t0,
		},
	} {
		t.Run(name, func(t *testing.T) {
			got := security.AdvanceCertWatermark(tc.existing, tc.cn, tc.now)
			if !got.Equal(tc.expect) {
				t.Errorf("got %s, want %s", got.Format(time.RFC3339), tc.expect.Format(time.RFC3339))
			}
		})
	}
}

func TestCertWatermarks_DecodeInvalid(t *testing.T) {
	for name, tc := range map[string]struct {
		input   []byte
		wantErr string
	}{
		"malformed json": {
			input:   []byte("not-json"),
			wantErr: "parsing cert watermarks blob",
		},
		"bad timestamp": {
			input:   []byte(`{"node:node1":"not-a-date"}`),
			wantErr: "parsing watermark for node:node1",
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, err := security.DecodeCertWatermarks(tc.input)
			if err == nil {
				t.Fatalf("expected error, got nil")
			}
			if !strings.Contains(err.Error(), tc.wantErr) {
				t.Fatalf("expected error containing %q, got %q",
					tc.wantErr, err.Error())
			}
		})
	}
}

func generateTestCert(t *testing.T, cn string, isCA bool, parent *x509.Certificate, parentKey *ecdsa.PrivateKey) (*x509.Certificate, *ecdsa.PrivateKey) {
	t.Helper()

	key, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}

	serial, _ := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	template := &x509.Certificate{
		SerialNumber: serial,
		Subject: pkix.Name{
			CommonName:   cn,
			Organization: []string{"DAOS Test"},
		},
		NotBefore:             time.Now().Add(-time.Minute),
		NotAfter:              time.Now().Add(time.Hour),
		BasicConstraintsValid: true,
		IsCA:                  isCA,
	}
	if isCA {
		template.KeyUsage = x509.KeyUsageCertSign
	} else {
		template.KeyUsage = x509.KeyUsageDigitalSignature
	}

	signer := parent
	signerKey := parentKey
	if signer == nil {
		signer = template
		signerKey = key
	}

	certDER, err := x509.CreateCertificate(rand.Reader, template, signer, &key.PublicKey, signerKey)
	if err != nil {
		t.Fatal(err)
	}
	cert, err := x509.ParseCertificate(certDER)
	if err != nil {
		t.Fatal(err)
	}
	return cert, key
}

func certToPEM(t *testing.T, cert *x509.Certificate) []byte {
	t.Helper()
	return pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: cert.Raw})
}

func TestRemoveCertByFingerprint(t *testing.T) {
	certA, _ := generateTestCert(t, "CA-A", true, nil, nil)
	certB, _ := generateTestCert(t, "CA-B", true, nil, nil)
	certC, _ := generateTestCert(t, "CA-C", true, nil, nil)

	fpA := fmt.Sprintf("%x", sha256.Sum256(certA.Raw))
	fpB := fmt.Sprintf("%x", sha256.Sum256(certB.Raw))

	bundle := append(certToPEM(t, certA), certToPEM(t, certB)...)
	bundle = append(bundle, certToPEM(t, certC)...)

	for name, tc := range map[string]struct {
		fingerprint string
		expRemoved  int
		expErr      error
		expCNs      []string // CNs remaining in bundle
	}{
		"remove first": {
			fingerprint: fpA,
			expRemoved:  1,
			expCNs:      []string{"CA-B", "CA-C"},
		},
		"remove middle": {
			fingerprint: fpB,
			expRemoved:  1,
			expCNs:      []string{"CA-A", "CA-C"},
		},
		"not found": {
			fingerprint: "deadbeef",
			expErr:      fmt.Errorf("no CA with fingerprint deadbeef found in bundle"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			remaining, removed, err := security.RemoveCertByFingerprint(bundle, tc.fingerprint)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if removed != tc.expRemoved {
				t.Fatalf("expected %d removed, got %d", tc.expRemoved, removed)
			}

			// Parse remaining bundle and check CNs.
			var gotCNs []string
			rest := remaining
			for len(rest) > 0 {
				var block *pem.Block
				block, rest = pem.Decode(rest)
				if block == nil {
					break
				}
				cert, err := x509.ParseCertificate(block.Bytes)
				if err != nil {
					t.Fatal(err)
				}
				gotCNs = append(gotCNs, cert.Subject.CommonName)
			}

			if diff := cmp.Diff(tc.expCNs, gotCNs); diff != "" {
				t.Fatalf("unexpected remaining certs (-want +got):\n%s", diff)
			}
		})
	}
}

func TestLoadCertAndKey(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	key, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	keyDER, err := x509.MarshalPKCS8PrivateKey(key)
	if err != nil {
		t.Fatal(err)
	}
	keyPEM := pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: keyDER})
	keyPath := filepath.Join(tmpDir, "test.key")
	if err := os.WriteFile(keyPath, keyPEM, 0400); err != nil {
		t.Fatal(err)
	}

	serial, _ := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	template := &x509.Certificate{
		SerialNumber: serial,
		Subject:      pkix.Name{CommonName: "test-cert"},
		NotBefore:    time.Now().Add(-time.Minute),
		NotAfter:     time.Now().Add(time.Hour),
	}
	certDER, err := x509.CreateCertificate(rand.Reader, template, template, &key.PublicKey, key)
	if err != nil {
		t.Fatal(err)
	}
	certPEM := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: certDER})
	if err := os.WriteFile(filepath.Join(tmpDir, "test.crt"), certPEM, 0644); err != nil {
		t.Fatal(err)
	}

	loaded, loadedKey, err := loadCertAndKey(keyPath)
	if err != nil {
		t.Fatalf("loadCertAndKey failed: %v", err)
	}
	if loaded.Subject.CommonName != "test-cert" {
		t.Errorf("expected CN %q, got %q", "test-cert", loaded.Subject.CommonName)
	}
	if _, ok := loadedKey.(*ecdsa.PrivateKey); !ok {
		t.Errorf("expected *ecdsa.PrivateKey, got %T", loadedKey)
	}
}

func TestLoadCertAndKey_MissingCert(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	keyPath := filepath.Join(tmpDir, "pool_ca.key")
	if err := os.WriteFile(keyPath, []byte("fake"), 0400); err != nil {
		t.Fatal(err)
	}

	_, _, err := loadCertAndKey(keyPath)
	if err == nil {
		t.Fatal("expected error when cert file is missing")
	}
}

func TestGenerateClientCert(t *testing.T) {
	// Create a self-signed CA to act as pool CA.
	caCert, caKey := generateTestCert(t, "Test Pool CA", true, nil, nil)

	certPEM, keyPEM, err := generateClientCert(
		security.CertCNPrefixNode+"testnode",
		caCert, caKey,
	)
	if err != nil {
		t.Fatal(err)
	}

	// Parse the generated cert and verify.
	block, _ := pem.Decode(certPEM)
	if block == nil {
		t.Fatal("no PEM block in generated cert")
	}
	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		t.Fatal(err)
	}

	if cert.Subject.CommonName != security.CertCNPrefixNode+"testnode" {
		t.Errorf("expected CN %q, got %q",
			security.CertCNPrefixNode+"testnode", cert.Subject.CommonName)
	}
	if !cert.IsCA {
		// Should NOT be a CA.
	}
	if cert.KeyUsage&x509.KeyUsageDigitalSignature == 0 {
		t.Error("expected DigitalSignature key usage")
	}

	// Verify the key PEM is valid.
	keyBlock, _ := pem.Decode(keyPEM)
	if keyBlock == nil {
		t.Fatal("no PEM block in generated key")
	}
	parsedKey, err := x509.ParsePKCS8PrivateKey(keyBlock.Bytes)
	if err != nil {
		t.Fatal(err)
	}
	if _, ok := parsedKey.(*ecdsa.PrivateKey); !ok {
		t.Errorf("expected *ecdsa.PrivateKey, got %T", parsedKey)
	}

	// Verify chain: client cert -> pool CA.
	roots := x509.NewCertPool()
	roots.AddCert(caCert)
	if _, err := cert.Verify(x509.VerifyOptions{
		Roots:     roots,
		KeyUsages: []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
	}); err != nil {
		t.Errorf("cert should chain to pool CA: %v", err)
	}
}

func TestPoolGenerateClientCerts(t *testing.T) {
	// Create a self-signed CA and write it to disk.
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	caKey, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	serial, _ := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	caTemplate := &x509.Certificate{
		SerialNumber:          serial,
		Subject:               pkix.Name{CommonName: "Test CA"},
		NotBefore:             time.Now().Add(-time.Minute),
		NotAfter:              time.Now().Add(time.Hour),
		KeyUsage:              x509.KeyUsageCertSign,
		BasicConstraintsValid: true,
		IsCA:                  true,
	}
	caDER, err := x509.CreateCertificate(rand.Reader, caTemplate, caTemplate, &caKey.PublicKey, caKey)
	if err != nil {
		t.Fatal(err)
	}
	certPEM := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: caDER})
	if err := os.WriteFile(filepath.Join(tmpDir, "ca.crt"), certPEM, 0644); err != nil {
		t.Fatal(err)
	}
	keyDER, err := x509.MarshalPKCS8PrivateKey(caKey)
	if err != nil {
		t.Fatal(err)
	}
	keyPEM := pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: keyDER})
	if err := os.WriteFile(filepath.Join(tmpDir, "ca.key"), keyPEM, 0400); err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		nodes   []string
		tenants []string
		expCNs  []string
		expErr  bool
	}{
		"nodes": {
			nodes:  []string{"n1", "n2"},
			expCNs: []string{"node:n1", "node:n2"},
		},
		"tenants": {
			tenants: []string{"teamA"},
			expCNs:  []string{"tenant:teamA"},
		},
		"both fails": {
			nodes:   []string{"n1"},
			tenants: []string{"t1"},
			expErr:  true,
		},
		"neither fails": {
			expErr: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			certs, err := PoolGenerateClientCerts(
				&PoolGenerateClientCertsReq{
					CAKeyPath: filepath.Join(tmpDir, "ca.key"),
					Nodes:     tc.nodes,
					Tenants:   tc.tenants,
				})
			if tc.expErr {
				if err == nil {
					t.Fatal("expected error")
				}
				return
			}
			if err != nil {
				t.Fatal(err)
			}

			var gotCNs []string
			for _, c := range certs {
				gotCNs = append(gotCNs, c.CN)
			}
			if diff := cmp.Diff(tc.expCNs, gotCNs); diff != "" {
				t.Fatalf("unexpected CNs (-want +got):\n%s", diff)
			}
		})
	}
}
