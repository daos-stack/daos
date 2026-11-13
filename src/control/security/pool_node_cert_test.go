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
	"os/user"
	"syscall"
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	sectest "github.com/daos-stack/daos/src/control/security/test"
)

func TestSecurity_NodeCertLoader_Load(t *testing.T) {
	poolUUID := uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
	nodeCN := PoolCertCNPrefixNode + testMachineName
	log, _ := logging.NewTestLogger(t.Name())

	for name, tc := range map[string]struct {
		setup  func(t *testing.T, dir string)
		expErr error
	}{
		"not deployed": {
			setup:  func(t *testing.T, dir string) {},
			expErr: ErrNodeCertNotDeployed,
		},
		"key missing": {
			setup: func(t *testing.T, dir string) {
				deployUsableNodeCert(t, dir, poolUUID)
				removeNodeKey(t, dir, poolUUID)
			},
			expErr: syscall.ENOENT,
		},
		"key mismatch": {
			setup: func(t *testing.T, dir string) {
				deployUsableNodeCert(t, dir, poolUUID)
				replaceNodeKey(t, dir, poolUUID, newCertFixture(t).leaf(t, nodeCN).KeyPEM)
			},
			expErr: ErrCertInvalid,
		},
		"usable": {
			setup: func(t *testing.T, dir string) {
				deployUsableNodeCert(t, dir, poolUUID)
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			dir, cleanup := test.CreateTestDir(t)
			defer cleanup()
			tc.setup(t, dir)

			nc, err := NewNodeCertLoader(dir).Load(log, poolUUID)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, nodeCN, nc.Cert.Subject.CommonName, "CN")
			if err := VerifyCertKey(nc.Key, nc.Cert); err != nil {
				t.Fatal(err)
			}
		})
	}
}

func TestSecurity_NodeCertLoader_Cache(t *testing.T) {
	dir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	log, _ := logging.NewTestLogger(t.Name())
	poolUUID := uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
	loader := NewNodeCertLoader(dir)
	deployUsableNodeCert(t, dir, poolUUID)

	first, err := loader.Load(log, poolUUID)
	if err != nil {
		t.Fatal(err)
	}
	second, err := loader.Load(log, poolUUID)
	if err != nil {
		t.Fatal(err)
	}
	if first != second {
		t.Fatal("unchanged files were reloaded")
	}

	removeNodeKey(t, dir, poolUUID)
	deployUsableNodeCert(t, dir, poolUUID)
	// Coarse-mtime filesystems would otherwise hide the rewrite.
	future := time.Now().Add(2 * time.Second)
	certPath, keyPath := NodeCertPaths(dir, poolUUID)
	for _, path := range []string{certPath, keyPath} {
		if err := os.Chtimes(path, future, future); err != nil {
			t.Fatal(err)
		}
	}

	third, err := loader.Load(log, poolUUID)
	if err != nil {
		t.Fatal(err)
	}
	if third == first {
		t.Fatal("rewritten files were served from the cache")
	}
}

func TestSecurity_NodeCertLoader_CertAndPoP(t *testing.T) {
	poolUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")
	handleUUID := uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
	log, _ := logging.NewTestLogger(t.Name())

	for name, tc := range map[string]struct {
		deploy  bool
		machine string
		expErr  error
	}{
		"not deployed":  {machine: testMachineName, expErr: ErrNodeCertNotDeployed},
		"wrong machine": {deploy: true, machine: "other", expErr: ErrCertInvalid},
		"usable":        {deploy: true, machine: testMachineName},
	} {
		t.Run(name, func(t *testing.T) {
			dir, cleanup := test.CreateTestDir(t)
			defer cleanup()
			if tc.deploy {
				deployUsableNodeCert(t, dir, poolUUID)
			}

			cert, pop, payload, err := NewNodeCertLoader(dir).CertAndPoP(log, poolUUID, handleUUID, tc.machine)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			if err := verifyPoP(cert.Cert.PublicKey, payload, pop); err != nil {
				t.Fatal(err)
			}
			parsed, err := parsePoPPayload(payload)
			if err != nil {
				t.Fatal(err)
			}
			test.AssertEqual(t, poolUUID, parsed.PoolID(), "pool UUID")
			test.AssertEqual(t, handleUUID, parsed.HandleID(), "handle UUID")
		})
	}
}

func TestSecurity_validateNodeCertForUse(t *testing.T) {
	poolUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")
	now := time.Now()
	notBefore := now.Add(-time.Minute)
	notAfter := now.Add(time.Hour)

	for name, tc := range map[string]struct {
		cn        string
		notBefore time.Time
		notAfter  time.Time
		expErr    error
	}{
		"node matches":   {cn: PoolCertCNPrefixNode + testMachineName, notBefore: notBefore, notAfter: notAfter},
		"wrong machine":  {cn: PoolCertCNPrefixNode + "wronghost", notBefore: notBefore, notAfter: notAfter, expErr: ErrCertInvalid},
		"tenant":         {cn: PoolCertCNPrefixTenant + "team-a", notBefore: notBefore, notAfter: notAfter},
		"unknown prefix": {cn: "weird:thing", notBefore: notBefore, notAfter: notAfter, expErr: errors.New("must start with")},
		"expired":        {cn: PoolCertCNPrefixNode + testMachineName, notBefore: notBefore, notAfter: now.Add(-time.Minute), expErr: ErrCertInvalid},
		"not yet valid":  {cn: PoolCertCNPrefixNode + testMachineName, notBefore: now.Add(time.Hour), notAfter: notAfter},
	} {
		t.Run(name, func(t *testing.T) {
			cert := &x509.Certificate{
				Subject:   pkix.Name{CommonName: tc.cn},
				NotBefore: tc.notBefore,
				NotAfter:  tc.notAfter,
			}
			test.CmpErr(t, tc.expErr, validateNodeCertForUse(cert, poolUUID, testMachineName, now))
		})
	}
}

func TestSecurity_InspectNodeCert(t *testing.T) {
	poolUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")
	nodeCN := PoolCertCNPrefixNode + testMachineName
	now := time.Now()
	me, err := user.Current()
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		setup       func(t *testing.T, dir string)
		expDirErr   error
		expCertErr  error
		expKeyErr   error
		expKeyMode  os.FileMode
		expKeyOwner string
		expSuffix   string
		expBinding  error
		expValidity error
		expErr      error
	}{
		"missing dir": {
			setup: func(t *testing.T, dir string) {
				if err := os.RemoveAll(dir); err != nil {
					t.Fatal(err)
				}
			},
			expDirErr: syscall.ENOENT,
			expErr:    syscall.ENOENT,
		},
		"not deployed": {
			setup:      func(t *testing.T, dir string) {},
			expCertErr: syscall.ENOENT,
			expErr:     syscall.ENOENT,
		},
		"usable": {
			setup: func(t *testing.T, dir string) {
				deployNodeCert(t, dir, poolUUID, nodeCN, now.Add(-time.Minute), now.Add(time.Hour))
			},
			expKeyMode:  0400,
			expKeyOwner: me.Username,
			expSuffix:   testMachineName,
		},
		"key readable by others": {
			setup: func(t *testing.T, dir string) {
				deployNodeCert(t, dir, poolUUID, nodeCN, now.Add(-time.Minute), now.Add(time.Hour))
				_, keyPath := NodeCertPaths(dir, poolUUID)
				if err := os.Chmod(keyPath, 0644); err != nil {
					t.Fatal(err)
				}
			},
			expKeyErr:   errors.New("insecure permissions"),
			expKeyMode:  0644,
			expKeyOwner: me.Username,
			expSuffix:   testMachineName,
			expErr:      errors.New("insecure permissions"),
		},
		"wrong key type": {
			setup: func(t *testing.T, dir string) {
				p256, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
				if err != nil {
					t.Fatal(err)
				}
				certPath, keyPath := NodeCertPaths(dir, poolUUID)
				sectest.WriteCertFiles(t, certPath, keyPath, sectest.NewLeafForKey(t, nodeCN, p256, nil, nil), sectest.KeyPEM(t, p256))
			},
			expCertErr: ErrCertInvalid,
			expSuffix:  testMachineName,
			expErr:     ErrCertInvalid,
		},
		"key missing": {
			setup: func(t *testing.T, dir string) {
				deployNodeCert(t, dir, poolUUID, nodeCN, now.Add(-time.Minute), now.Add(time.Hour))
				removeNodeKey(t, dir, poolUUID)
			},
			expKeyErr: syscall.ENOENT,
			expSuffix: testMachineName,
			expErr:    syscall.ENOENT,
		},
		"key mismatch": {
			setup: func(t *testing.T, dir string) {
				deployNodeCert(t, dir, poolUUID, nodeCN, now.Add(-time.Minute), now.Add(time.Hour))
				replaceNodeKey(t, dir, poolUUID, newCertFixture(t).leaf(t, nodeCN).KeyPEM)
			},
			expKeyErr:   ErrCertInvalid,
			expKeyMode:  0400,
			expKeyOwner: me.Username,
			expSuffix:   testMachineName,
			expErr:      ErrCertInvalid,
		},
		"wrong machine": {
			setup: func(t *testing.T, dir string) {
				deployNodeCert(t, dir, poolUUID, PoolCertCNPrefixNode+"otherhost", now.Add(-time.Minute), now.Add(time.Hour))
			},
			expKeyMode:  0400,
			expKeyOwner: me.Username,
			expSuffix:   "otherhost",
			expBinding:  ErrCertInvalid,
			expErr:      ErrCertInvalid,
		},
		"tenant": {
			setup: func(t *testing.T, dir string) {
				deployNodeCert(t, dir, poolUUID, PoolCertCNPrefixTenant+"team-a", now.Add(-time.Minute), now.Add(time.Hour))
			},
			expKeyMode:  0400,
			expKeyOwner: me.Username,
			expSuffix:   "team-a",
		},
		"expired": {
			setup: func(t *testing.T, dir string) {
				deployNodeCert(t, dir, poolUUID, nodeCN, now.Add(-2*time.Hour), now.Add(-time.Hour))
			},
			expKeyMode:  0400,
			expKeyOwner: me.Username,
			expSuffix:   testMachineName,
			expValidity: ErrCertInvalid,
			expErr:      ErrCertInvalid,
		},
		"not yet valid": {
			setup: func(t *testing.T, dir string) {
				deployNodeCert(t, dir, poolUUID, nodeCN, now.Add(time.Hour), now.Add(2*time.Hour))
			},
			expKeyMode:  0400,
			expKeyOwner: me.Username,
			expSuffix:   testMachineName,
			expValidity: ErrCertNotYetValid,
		},
	} {
		t.Run(name, func(t *testing.T) {
			dir, cleanup := test.CreateTestDir(t)
			defer cleanup()
			tc.setup(t, dir)

			insp := InspectNodeCert(dir, poolUUID, testMachineName, now, DefaultCertMaxClockSkew)
			test.CmpErr(t, tc.expDirErr, insp.Dir.Err)
			test.CmpErr(t, tc.expCertErr, insp.Cert.Err)
			test.CmpErr(t, tc.expKeyErr, insp.Key.Err)
			test.AssertEqual(t, tc.expKeyMode, insp.Key.Mode, "key mode")
			test.AssertEqual(t, tc.expKeyOwner, insp.Key.Owner, "key owner")
			test.AssertEqual(t, tc.expSuffix, insp.Suffix, "CN suffix")
			test.CmpErr(t, tc.expBinding, insp.Binding)
			test.CmpErr(t, tc.expValidity, insp.Validity)
			test.CmpErr(t, tc.expErr, insp.Err())
		})
	}
}
