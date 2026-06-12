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

	"github.com/google/go-cmp/cmp"
)

func TestDecodeCertWatermarks(t *testing.T) {
	for name, tc := range map[string]struct {
		input   string
		wantLen int
		wantErr bool
	}{
		"empty object":     {`{}`, 0, false},
		"one entry":        {`{"node:host1":"2026-01-02T03:04:05Z"}`, 1, false},
		"two entries":      {`{"node:host1":"2026-01-02T03:04:05Z","tenant:t1":"2026-02-01T00:00:00Z"}`, 2, false},
		"malformed JSON":   {`not json`, 0, true},
		"bad timestamp":    {`{"node:host1":"not-a-time"}`, 0, true},
		"missing timezone": {`{"node:host1":"2026-01-02T03:04:05"}`, 0, true},
	} {
		t.Run(name, func(t *testing.T) {
			got, err := DecodeCertWatermarks([]byte(tc.input))
			if tc.wantErr {
				if err == nil {
					t.Fatal("expected error, got nil")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if len(got) != tc.wantLen {
				t.Errorf("got %d entries, want %d", len(got), tc.wantLen)
			}
			for cn, ts := range got {
				if ts.Location() != time.UTC {
					t.Errorf("watermark for %q is not UTC: %v", cn, ts)
				}
			}
		})
	}
}

func TestCertWatermarks_RoundTrip(t *testing.T) {
	for name, tc := range map[string]struct {
		input CertWatermarks
	}{
		"empty": {input: nil},
		"single node": {input: CertWatermarks{
			"node:node1": time.Date(2026, 4, 15, 14, 0, 29, 0, time.UTC),
		}},
		"multiple": {input: CertWatermarks{
			"node:node1":   time.Date(2026, 4, 15, 14, 0, 29, 0, time.UTC),
			"node:node2":   time.Date(2026, 4, 15, 14, 0, 30, 0, time.UTC),
			"tenant:teamA": time.Date(2026, 4, 20, 9, 15, 0, 0, time.UTC),
		}},
	} {
		t.Run(name, func(t *testing.T) {
			encoded, err := EncodeCertWatermarks(tc.input)
			if err != nil {
				t.Fatalf("encode: %v", err)
			}
			if len(tc.input) == 0 {
				if encoded != nil {
					t.Fatalf("empty map must encode to nil, got %q", encoded)
				}
				return
			}
			got, err := DecodeCertWatermarks(encoded)
			if err != nil {
				t.Fatalf("decode: %v", err)
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
		existing CertWatermarks
		cn       string
		now      time.Time
		want     time.Time
	}{
		"no prior":             {nil, "node:n1", t0.Add(500 * time.Millisecond), t0},
		"no prior for cn":      {CertWatermarks{"node:other": t0.Add(time.Hour)}, "node:n1", t0, t0},
		"now > prior":          {CertWatermarks{"node:n1": t0}, "node:n1", t0.Add(time.Minute), t0.Add(time.Minute)},
		"now == prior":         {CertWatermarks{"node:n1": t0}, "node:n1", t0, t0.Add(time.Second)},
		"now < prior":          {CertWatermarks{"node:n1": t0.Add(time.Hour)}, "node:n1", t0, t0.Add(time.Hour).Add(time.Second)},
		"sub-second truncated": {nil, "node:n1", t0.Add(999 * time.Millisecond), t0},
	} {
		t.Run(name, func(t *testing.T) {
			got := AdvanceCertWatermark(tc.existing, tc.cn, tc.now)
			if !got.Equal(tc.want) {
				t.Errorf("got %s, want %s", got.Format(time.RFC3339), tc.want.Format(time.RFC3339))
			}
		})
	}
}

func TestPruneCertWatermarks(t *testing.T) {
	t0 := time.Date(2026, 4, 15, 14, 0, 0, 0, time.UTC)
	wm := CertWatermarks{
		"node:fresh":    t0,
		"node:stale":    t0.Add(-CertWatermarkRetention - time.Hour),
		"node:boundary": t0.Add(-CertWatermarkRetention), // exactly at cutoff stays
	}
	out, pruned := PruneCertWatermarks(wm, t0.Add(-CertWatermarkRetention))
	if pruned != 1 {
		t.Fatalf("pruned=%d, want 1", pruned)
	}
	if _, ok := out["node:stale"]; ok {
		t.Errorf("stale entry survived prune")
	}
	if _, ok := out["node:fresh"]; !ok {
		t.Errorf("fresh entry was pruned")
	}
	if _, ok := out["node:boundary"]; !ok {
		t.Errorf("boundary entry (==cutoff) was pruned")
	}
}

// generateTestCA produces a self-signed CA cert + key.
func generateTestCA(t *testing.T, cn string) (*x509.Certificate, *ecdsa.PrivateKey, []byte) {
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
		IsCA:                  true,
		KeyUsage:              x509.KeyUsageCertSign | x509.KeyUsageDigitalSignature,
		BasicConstraintsValid: true,
	}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, &key.PublicKey, key)
	if err != nil {
		t.Fatal(err)
	}
	cert, _ := x509.ParseCertificate(der)
	pemBytes := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der})
	return cert, key, pemBytes
}

// signCertWith creates a leaf cert (or intermediate CA) signed by parent.
func signCertWith(t *testing.T, parent *x509.Certificate, parentKey *ecdsa.PrivateKey, cn string, isCA bool) ([]byte, *x509.Certificate) {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	serial, _ := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	usage := x509.KeyUsageDigitalSignature
	if isCA {
		usage |= x509.KeyUsageCertSign
	}
	tmpl := &x509.Certificate{
		SerialNumber:          serial,
		Subject:               pkix.Name{CommonName: cn},
		NotBefore:             time.Now().Add(-time.Minute),
		NotAfter:              time.Now().Add(time.Hour),
		IsCA:                  isCA,
		KeyUsage:              usage,
		BasicConstraintsValid: true,
	}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, parent, &key.PublicKey, parentKey)
	if err != nil {
		t.Fatal(err)
	}
	cert, _ := x509.ParseCertificate(der)
	return pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}), cert
}

func TestRemoveCertByFingerprint(t *testing.T) {
	_, _, pemA := generateTestCA(t, "CA-A")
	certA, _ := x509.ParseCertificate(decodePEM(t, pemA))
	_, _, pemB := generateTestCA(t, "CA-B")
	certB, _ := x509.ParseCertificate(decodePEM(t, pemB))
	_, _, pemC := generateTestCA(t, "CA-C")

	bundle := append(append(pemA, pemB...), pemC...)
	fpA := fmt.Sprintf("%x", sha256.Sum256(certA.Raw))
	fpB := fmt.Sprintf("%x", sha256.Sum256(certB.Raw))

	for name, tc := range map[string]struct {
		fp     string
		want   int
		errSub string
	}{
		"remove first":  {fpA, 1, ""},
		"remove middle": {fpB, 1, ""},
		"not found":     {"deadbeef", 0, "no CA with fingerprint"},
	} {
		t.Run(name, func(t *testing.T) {
			_, removed, err := RemoveCertByFingerprint(bundle, tc.fp)
			if tc.errSub != "" {
				if err == nil {
					t.Fatal("expected error")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if removed != tc.want {
				t.Errorf("removed=%d, want %d", removed, tc.want)
			}
		})
	}
}

func TestRemoveCertByFingerprint_MalformedBundle(t *testing.T) {
	_, _, validCA := generateTestCA(t, "valid")

	// Non-CERTIFICATE PEM block in bundle is rejected.
	notACert := pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: []byte("x")})
	if _, _, err := RemoveCertByFingerprint(append(validCA, notACert...), "anything"); err == nil {
		t.Fatal("expected error for non-CERTIFICATE PEM block")
	}

	// CERTIFICATE block with garbage bytes is rejected.
	garbage := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: []byte("not der")})
	if _, _, err := RemoveCertByFingerprint(append(validCA, garbage...), "anything"); err == nil {
		t.Fatal("expected error for malformed CERTIFICATE block")
	}
}

func TestCountPEMCerts(t *testing.T) {
	_, _, pemA := generateTestCA(t, "A")
	_, _, pemB := generateTestCA(t, "B")
	if got := CountPEMCerts(pemA); got != 1 {
		t.Errorf("one cert: got %d, want 1", got)
	}
	if got := CountPEMCerts(append(pemA, pemB...)); got != 2 {
		t.Errorf("two certs: got %d, want 2", got)
	}
	if got := CountPEMCerts(nil); got != 0 {
		t.Errorf("nil bundle: got %d, want 0", got)
	}
	// A non-CERTIFICATE block does not count.
	notACert := pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: []byte("x")})
	if got := CountPEMCerts(append(pemA, notACert...)); got != 1 {
		t.Errorf("cert+other: got %d, want 1", got)
	}
}

func TestParsePoolCACert(t *testing.T) {
	_, _, caPEM := generateTestCA(t, "valid CA")

	// Build a non-CA cert (leaf) for the IsCA-false case.
	rootCert, rootKey, _ := generateTestCA(t, "root")
	leafPEM, _ := signCertWith(t, rootCert, rootKey, "leaf", false)

	for name, tc := range map[string]struct {
		input   []byte
		wantErr string
	}{
		"valid CA":    {caPEM, ""},
		"not a CA":    {leafPEM, "IsCA"},
		"empty input": {nil, "PEM-encoded CERTIFICATE"},
		"two blocks":  {append(caPEM, caPEM...), "exactly one CERTIFICATE"},
	} {
		t.Run(name, func(t *testing.T) {
			_, err := ParsePoolCACert(tc.input)
			if tc.wantErr != "" {
				if err == nil {
					t.Fatal("expected error")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
		})
	}
}

func TestVerifyPoolCAChain(t *testing.T) {
	rootCert, rootKey, rootPEM := generateTestCA(t, "root")
	_, interPEM := signCertWith(t, rootCert, rootKey, "pool CA", true)

	// Write root to a temp file (LoadCertificate requires MaxCertPerm).
	dir := t.TempDir()
	rootPath := filepath.Join(dir, "root.crt")
	if err := os.WriteFile(rootPath, rootPEM, MaxCertPerm); err != nil {
		t.Fatal(err)
	}

	if err := VerifyPoolCAChain(interPEM, rootPath); err != nil {
		t.Fatalf("valid chain rejected: %v", err)
	}

	// Empty rootCAPath skips verification (AllowInsecure mode).
	if err := VerifyPoolCAChain(interPEM, ""); err != nil {
		t.Fatalf("empty rootCAPath should be a no-op: %v", err)
	}

	// Cert signed by a different root must not validate against rootPath.
	freshRoot, freshKey, _ := generateTestCA(t, "fresh root")
	_, badPEM := signCertWith(t, freshRoot, freshKey, "bad chain", true)
	if err := VerifyPoolCAChain(badPEM, rootPath); err == nil {
		t.Fatal("cert not chaining to root should be rejected")
	}
}

func TestAppendCACert(t *testing.T) {
	_, _, caA := generateTestCA(t, "A")
	_, _, caB := generateTestCA(t, "B")
	bundle, err := AppendCACert(caA, caB)
	if err != nil {
		t.Fatalf("valid append failed: %v", err)
	}
	if CountPEMCerts(bundle) != 2 {
		t.Errorf("bundle should have 2 certs, got %d", CountPEMCerts(bundle))
	}
	// Non-CA cert rejected.
	root, key, _ := generateTestCA(t, "root")
	leaf, _ := signCertWith(t, root, key, "leaf", false)
	if _, err := AppendCACert(caA, leaf); err == nil {
		t.Fatal("AppendCACert should reject non-CA cert")
	}
}

// decodePEM returns the DER bytes of the first PEM block, or fails.
func decodePEM(t *testing.T, data []byte) []byte {
	t.Helper()
	block, _ := pem.Decode(data)
	if block == nil {
		t.Fatal("no PEM block")
	}
	return block.Bytes
}
