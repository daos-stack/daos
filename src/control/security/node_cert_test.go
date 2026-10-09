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
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	sectest "github.com/daos-stack/daos/src/control/security/test"
)

const testMachineName = "testhost"

// issueTestNodeCert mints a node cert for testMachineName under a throwaway CA.
func issueTestNodeCert(t *testing.T) *IssuedCert {
	t.Helper()
	caPEM, caKey := sectest.NewCA(t, "Test CA", nil, nil)
	ic, err := issueClientCert(PoolCertCNPrefixNode+testMachineName, sectest.ParseCert(t, caPEM), caKey,
		time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
	if err != nil {
		t.Fatal(err)
	}
	return ic
}

func writeTestNodeCert(t *testing.T, dir string, poolUUID uuid.UUID) {
	t.Helper()
	ic := issueTestNodeCert(t)
	certPath, keyPath := NodeCertPaths(dir, poolUUID)
	sectest.WriteCertFiles(t, certPath, keyPath, ic.CertPEM, ic.KeyPEM)
}

func TestNodeCertLoader_Load(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	log, _ := logging.NewTestLogger(t.Name())
	loader := NewNodeCertLoader(tmpDir)

	poolUUID := uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
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

	if _, err := loader.Load(log, uuid.MustParse("bbbbbbbb-cccc-dddd-eeee-ffffffffffff")); !errors.Is(err, ErrNodeCertNotDeployed) {
		t.Errorf("missing cert: expected %v, got %v", ErrNodeCertNotDeployed, err)
	}

	// A certificate without its key is a broken deployment, not an absent one.
	_, keyPath := NodeCertPaths(tmpDir, poolUUID)
	if err := os.Remove(keyPath); err != nil {
		t.Fatal(err)
	}
	if _, err := loader.Load(log, poolUUID); err == nil || errors.Is(err, ErrNodeCertNotDeployed) {
		t.Errorf("missing key: expected an error other than %v, got %v", ErrNodeCertNotDeployed, err)
	}
}

func TestNodeCertLoader_ReloadOnFileChange(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	log, _ := logging.NewTestLogger(t.Name())
	loader := NewNodeCertLoader(tmpDir)

	poolUUID := uuid.MustParse("cccccccc-dddd-eeee-ffff-000000000000")
	writeTestNodeCert(t, tmpDir, poolUUID)

	c1, err := loader.Load(log, poolUUID)
	if err != nil {
		t.Fatalf("first load: %v", err)
	}

	for _, name := range []string{poolUUID.String() + ".crt", poolUUID.String() + ".key"} {
		if err := os.Remove(filepath.Join(tmpDir, name)); err != nil {
			t.Fatalf("remove %s: %v", name, err)
		}
	}
	writeTestNodeCert(t, tmpDir, poolUUID)
	// Bump mtime past 1s resolution so a stat-based change detector
	// reliably sees it regardless of filesystem granularity.
	futureTime := time.Now().Add(2 * time.Second)
	for _, name := range []string{poolUUID.String() + ".crt", poolUUID.String() + ".key"} {
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

func TestNodeCertLoader_KeyCertMismatch(t *testing.T) {
	tmpDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	log, _ := logging.NewTestLogger(t.Name())
	loader := NewNodeCertLoader(tmpDir)

	poolUUID := uuid.MustParse("dddddddd-eeee-ffff-0000-111111111111")
	writeTestNodeCert(t, tmpDir, poolUUID)

	// Replace the key with one that doesn't match the cert.
	_, keyPath := NodeCertPaths(tmpDir, poolUUID)
	if err := os.Remove(keyPath); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(keyPath, issueTestNodeCert(t).KeyPEM, 0400); err != nil {
		t.Fatal(err)
	}

	if _, err := loader.Load(log, poolUUID); err == nil ||
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
		"node cert matches machine":          {PoolCertCNPrefixNode + testMachineName, notBefore, notAfter, now, false},
		"node cert wrong machine":            {PoolCertCNPrefixNode + "wronghost", notBefore, notAfter, now, true},
		"tenant cert skips machine":          {PoolCertCNPrefixTenant + "team-a", notBefore, notAfter, now, false},
		"unrecognized prefix":                {"weird:thing", notBefore, notAfter, now, true},
		"expired cert":                       {PoolCertCNPrefixNode + testMachineName, notBefore, now.Add(-time.Minute), now, true},
		"not yet valid is the server's call": {PoolCertCNPrefixNode + testMachineName, now.Add(time.Hour), notAfter, now, false},
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
	writeTestNodeCert(t, tmpDir, poolUUID)

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

func TestSecurity_InspectNodeCert(t *testing.T) {
	poolUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")
	now := time.Now()
	deploy := func(t *testing.T, dir, cn string, notBefore, notAfter time.Time) *IssuedCert {
		t.Helper()
		caPEM, caKey := sectest.NewCA(t, "Test CA", nil, nil)
		ic, err := issueClientCert(cn, sectest.ParseCert(t, caPEM), caKey, notBefore, notAfter)
		if err != nil {
			t.Fatal(err)
		}
		certPath, keyPath := NodeCertPaths(dir, poolUUID)
		sectest.WriteCertFiles(t, certPath, keyPath, ic.CertPEM, ic.KeyPEM)
		return ic
	}

	for name, tc := range map[string]struct {
		setup       func(t *testing.T, dir string)
		expDeployed bool
		expErr      error // sentinel the agent would hit; nil with expFail for a typed error
		expFail     bool
		check       func(t *testing.T, insp *NodeCertInspection)
	}{
		"missing dir": {
			setup:  func(t *testing.T, dir string) { os.RemoveAll(dir) },
			expErr: os.ErrNotExist,
		},
		"not deployed": {
			setup:  func(t *testing.T, dir string) {},
			expErr: os.ErrNotExist,
		},
		"usable node cert": {
			setup: func(t *testing.T, dir string) {
				deploy(t, dir, PoolCertCNPrefixNode+testMachineName, now.Add(-time.Minute), now.Add(time.Hour))
			},
			expDeployed: true,
			check: func(t *testing.T, insp *NodeCertInspection) {
				if insp.Key.Mode != 0400 || insp.Key.Owner == "" || insp.Suffix != testMachineName {
					t.Errorf("unexpected inspection: %+v", insp)
				}
			},
		},
		"key readable by others": {
			setup: func(t *testing.T, dir string) {
				deploy(t, dir, PoolCertCNPrefixNode+testMachineName, now.Add(-time.Minute), now.Add(time.Hour))
				_, keyPath := NodeCertPaths(dir, poolUUID)
				if err := os.Chmod(keyPath, 0644); err != nil {
					t.Fatal(err)
				}
			},
			expDeployed: true,
			expFail:     true,
			check: func(t *testing.T, insp *NodeCertInspection) {
				if insp.Key.Err == nil || insp.Certificate == nil {
					t.Errorf("key problem not isolated to the key: %+v", insp)
				}
			},
		},
		"wrong key type": {
			setup: func(t *testing.T, dir string) {
				rootPEM, rootKey := sectest.NewCA(t, "Test CA", nil, nil)
				ca, err := GeneratePoolCA(poolUUID, sectest.ParseCert(t, rootPEM), rootKey, 0)
				if err != nil {
					t.Fatal(err)
				}
				p256, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
				if err != nil {
					t.Fatal(err)
				}
				certPath, keyPath := NodeCertPaths(dir, poolUUID)
				sectest.WriteCertFiles(t, certPath, keyPath,
					leafWithKey(t, PoolCertCNPrefixNode+testMachineName, ca, p256), sectest.KeyPEM(t, p256))
			},
			expDeployed: true,
			expErr:      ErrCertInvalid,
			check: func(t *testing.T, insp *NodeCertInspection) {
				if insp.Cert.Err == nil || insp.Key.Err != nil {
					t.Errorf("key type not reported on the certificate: %+v", insp)
				}
			},
		},
		"key missing": {
			setup: func(t *testing.T, dir string) {
				deploy(t, dir, PoolCertCNPrefixNode+testMachineName, now.Add(-time.Minute), now.Add(time.Hour))
				_, keyPath := NodeCertPaths(dir, poolUUID)
				if err := os.Remove(keyPath); err != nil {
					t.Fatal(err)
				}
			},
			expDeployed: true,
			expFail:     true,
			check: func(t *testing.T, insp *NodeCertInspection) {
				if insp.Key.Err == nil || insp.Certificate == nil {
					t.Errorf("missing key not isolated to the key: %+v", insp)
				}
			},
		},
		"key does not match": {
			setup: func(t *testing.T, dir string) {
				deploy(t, dir, PoolCertCNPrefixNode+testMachineName, now.Add(-time.Minute), now.Add(time.Hour))
				other := deploy(t, t.TempDir(), PoolCertCNPrefixNode+testMachineName, now.Add(-time.Minute), now.Add(time.Hour))
				_, keyPath := NodeCertPaths(dir, poolUUID)
				os.Remove(keyPath)
				if err := os.WriteFile(keyPath, other.KeyPEM, 0400); err != nil {
					t.Fatal(err)
				}
			},
			expDeployed: true,
			expErr:      ErrCertInvalid,
		},
		"wrong machine": {
			setup: func(t *testing.T, dir string) {
				deploy(t, dir, PoolCertCNPrefixNode+"otherhost", now.Add(-time.Minute), now.Add(time.Hour))
			},
			expDeployed: true,
			expErr:      ErrCertInvalid,
			check: func(t *testing.T, insp *NodeCertInspection) {
				if insp.Binding == nil || insp.CNErr != nil || insp.Key.Err != nil {
					t.Errorf("mismatch not isolated to the binding: %+v", insp)
				}
			},
		},
		"tenant cert is unbound": {
			setup: func(t *testing.T, dir string) {
				deploy(t, dir, PoolCertCNPrefixTenant+"team-a", now.Add(-time.Minute), now.Add(time.Hour))
			},
			expDeployed: true,
		},
		"expired": {
			setup: func(t *testing.T, dir string) {
				deploy(t, dir, PoolCertCNPrefixNode+testMachineName, now.Add(-2*time.Hour), now.Add(-time.Hour))
			},
			expDeployed: true,
			expErr:      ErrCertInvalid,
		},
		"not yet valid is usable": {
			setup: func(t *testing.T, dir string) {
				deploy(t, dir, PoolCertCNPrefixNode+testMachineName, now.Add(time.Hour), now.Add(2*time.Hour))
			},
			expDeployed: true,
			check: func(t *testing.T, insp *NodeCertInspection) {
				if !errors.Is(insp.Validity, ErrCertNotYetValid) {
					t.Errorf("expected ErrCertNotYetValid, got %v", insp.Validity)
				}
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			dir := t.TempDir()
			tc.setup(t, dir)
			insp := InspectNodeCert(dir, poolUUID, testMachineName, now, DefaultCertMaxClockSkew)
			if insp.Deployed() != tc.expDeployed {
				t.Fatalf("Deployed() = %v, want %v: %+v", insp.Deployed(), tc.expDeployed, insp)
			}
			err := insp.Err()
			switch {
			case tc.expErr != nil:
				if !errors.Is(err, tc.expErr) {
					t.Fatalf("expected %v, got %v", tc.expErr, err)
				}
			case tc.expFail:
				if err == nil {
					t.Fatal("expected an error")
				}
			case err != nil:
				t.Fatalf("unexpected error: %v", err)
			}
			if tc.check != nil {
				tc.check(t, insp)
			}
		})
	}
}
