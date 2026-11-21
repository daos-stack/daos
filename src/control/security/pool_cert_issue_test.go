//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	sectest "github.com/daos-stack/daos/src/control/security/test"
)

func TestSecurity_GeneratePoolCA(t *testing.T) {
	f := newCertFixture(t)

	for name, tc := range map[string]struct {
		validity    time.Duration
		expNotAfter time.Time
		expErr      error
	}{
		"root's term":   {expNotAfter: f.root.NotAfter},
		"outlasts root": {validity: 48 * time.Hour, expErr: errors.New("pool CA: lifetime ends")},
		"shorter":       {validity: time.Hour, expNotAfter: time.Now().Add(time.Hour)},
	} {
		t.Run(name, func(t *testing.T) {
			ca, err := GeneratePoolCA(f.poolUUID, f.root, f.rootKey, tc.validity)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			parsed, err := ParsePoolCACert(ca.CertPEM)
			if err != nil {
				t.Fatal(err)
			}
			if err := CheckPoolCAIdentity(parsed, f.poolUUID); err != nil {
				t.Fatal(err)
			}
			if err := VerifyCertKey(ca.Key, parsed); err != nil {
				t.Fatal(err)
			}
			test.AssertTrue(t, parsed.MaxPathLenZero, "pool CA may sign further CAs")
			test.CmpAny(t, "NotBefore", time.Now().Add(-DefaultCertMaxClockSkew), parsed.NotBefore, cmpopts.EquateApproxTime(2*time.Second))
			test.CmpAny(t, "NotAfter", tc.expNotAfter, parsed.NotAfter, cmpopts.EquateApproxTime(2*time.Second))
		})
	}
}

func TestSecurity_IssueClientCertsReq_Validate(t *testing.T) {
	for name, tc := range map[string]struct {
		req    *IssueClientCertsReq
		expErr error
	}{
		"nothing requested": {req: &IssueClientCertsReq{}, expErr: errors.New("no node or tenant names")},
		"nodes and tenants": {req: &IssueClientCertsReq{Nodes: []string{"a"}, Tenants: []string{"t"}}, expErr: errors.New("mutually exclusive")},
		"node path":         {req: &IssueClientCertsReq{Nodes: []string{"etc/passwd"}}, expErr: errors.New("disallowed character")},
		"dotted node":       {req: &IssueClientCertsReq{Nodes: []string{"host1.example.com"}}, expErr: errors.New("short host name")},
		"tenant space":      {req: &IssueClientCertsReq{Tenants: []string{"team a"}}, expErr: errors.New("disallowed character")},
		"nodes":             {req: &IssueClientCertsReq{Nodes: []string{"host1", "host2"}}},
		"tenants":           {req: &IssueClientCertsReq{Tenants: []string{"teamA"}}},
	} {
		t.Run(name, func(t *testing.T) {
			test.CmpErr(t, tc.expErr, tc.req.Validate())
		})
	}
}

func TestSecurity_IssueClientCerts(t *testing.T) {
	f := newCertFixture(t)
	other := newCertFixture(t)
	nodeCN := PoolCertCNPrefixNode + "m"
	now := time.Now().UTC().Truncate(time.Second)
	future := now.Add(time.Hour)

	for name, tc := range map[string]struct {
		req          *IssueClientCertsReq
		expCNs       map[string]string
		expNotBefore time.Time
		expNotAfter  time.Time
		expErr       error
	}{
		"no CA": {
			req:    &IssueClientCertsReq{Nodes: []string{"m"}},
			expErr: errors.New("no pool CA"),
		},
		"key of other CA": {
			req:    &IssueClientCertsReq{CACert: f.poolCA.Cert, CAKey: other.poolCA.Key, Nodes: []string{"m"}},
			expErr: ErrCertInvalid,
		},
		"cert of other CA": {
			req:    &IssueClientCertsReq{CACert: other.poolCA.Cert, CAKey: f.poolCA.Key, Nodes: []string{"m"}},
			expErr: ErrCertInvalid,
		},
		"invalid request": {
			req:    &IssueClientCertsReq{CACert: f.poolCA.Cert, CAKey: f.poolCA.Key, Nodes: []string{"a.b"}},
			expErr: errors.New("short host name"),
		},
		"outlasts CA": {
			req:    &IssueClientCertsReq{CACert: f.poolCA.Cert, CAKey: f.poolCA.Key, Nodes: []string{"m"}, Validity: 48 * time.Hour},
			expErr: errors.New(`client cert "node:m": lifetime ends`),
		},
		"nodes": {
			req:          &IssueClientCertsReq{CACert: f.poolCA.Cert, CAKey: f.poolCA.Key, Nodes: []string{"host1", "host2"}},
			expCNs:       map[string]string{"host1": "node:host1", "host2": "node:host2"},
			expNotBefore: now,
			expNotAfter:  f.poolCA.Cert.NotAfter,
		},
		"tenant": {
			req:          &IssueClientCertsReq{CACert: f.poolCA.Cert, CAKey: f.poolCA.Key, Tenants: []string{"teamA"}},
			expCNs:       map[string]string{"teamA": "tenant:teamA"},
			expNotBefore: now,
			expNotAfter:  f.poolCA.Cert.NotAfter,
		},
		"shorter validity": {
			req:          &IssueClientCertsReq{CACert: f.poolCA.Cert, CAKey: f.poolCA.Key, Nodes: []string{"m"}, Validity: time.Hour},
			expCNs:       map[string]string{"m": nodeCN},
			expNotBefore: now,
			expNotAfter:  now.Add(time.Hour),
		},
		"past watermark": {
			req: &IssueClientCertsReq{
				CACert: f.poolCA.Cert, CAKey: f.poolCA.Key, Nodes: []string{"m"},
				Watermarks: CertWatermarks{nodeCN: now.Add(-time.Hour)},
			},
			expCNs:       map[string]string{"m": nodeCN},
			expNotBefore: now,
			expNotAfter:  f.poolCA.Cert.NotAfter,
		},
		"watermark now": {
			req: &IssueClientCertsReq{
				CACert: f.poolCA.Cert, CAKey: f.poolCA.Key, Nodes: []string{"m"},
				Watermarks: CertWatermarks{nodeCN: now},
			},
			expCNs:       map[string]string{"m": nodeCN},
			expNotBefore: now.Add(time.Second),
			expNotAfter:  f.poolCA.Cert.NotAfter,
		},
		"future watermark": {
			req: &IssueClientCertsReq{
				CACert: f.poolCA.Cert, CAKey: f.poolCA.Key, Nodes: []string{"m"},
				Watermarks: CertWatermarks{nodeCN: future},
			},
			expCNs:       map[string]string{"m": nodeCN},
			expNotBefore: future.Add(time.Second),
			expNotAfter:  f.poolCA.Cert.NotAfter,
		},
	} {
		t.Run(name, func(t *testing.T) {
			certs, err := IssueClientCerts(tc.req)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			gotCNs := make(map[string]string)
			for _, c := range certs {
				gotCNs[c.Name] = c.CN
				test.AssertEqual(t, c.CN, c.Cert.Subject.CommonName, "CN")
				if err := VerifyCertKey(c.Key, c.Cert); err != nil {
					t.Fatal(err)
				}
				if err := VerifyNodeCertChain(c.Cert, f.root, f.poolCA.CertPEM, c.Cert.NotBefore); err != nil {
					t.Fatal(err)
				}
				test.CmpAny(t, "NotBefore", tc.expNotBefore, c.Cert.NotBefore, cmpopts.EquateApproxTime(2*time.Second))
				test.CmpAny(t, "NotAfter", tc.expNotAfter, c.Cert.NotAfter, cmpopts.EquateApproxTime(2*time.Second))
			}
			if diff := cmp.Diff(tc.expCNs, gotCNs); diff != "" {
				t.Fatalf("unexpected CNs (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestSecurity_IssuedCert_Write(t *testing.T) {
	f := newCertFixture(t)
	nodeCN := PoolCertCNPrefixNode + "m"

	for name, tc := range map[string]struct {
		existing *IssuedCert
		subdir   string
		expErr   error
	}{
		"fresh":       {},
		"replace":     {existing: f.leaf(t, nodeCN)},
		"missing dir": {subdir: "missing", expErr: errors.New("no such file or directory")},
	} {
		t.Run(name, func(t *testing.T) {
			dir, cleanup := test.CreateTestDir(t)
			defer cleanup()

			certPath, keyPath := NodeCertPaths(filepath.Join(dir, tc.subdir), f.poolUUID)
			if tc.existing != nil {
				sectest.WriteCertFiles(t, certPath, keyPath, tc.existing.CertPEM, tc.existing.KeyPEM)
			}

			ic := f.leaf(t, nodeCN)
			err := ic.Write(certPath, keyPath)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			cert, key, err := LoadCertAndKey(certPath, keyPath)
			if err != nil {
				t.Fatal(err)
			}
			if diff := cmp.Diff(ic.Cert.Raw, cert.Raw); diff != "" {
				t.Fatalf("unexpected certificate (-want, +got):\n%s\n", diff)
			}
			if err := VerifyCertKey(key, cert); err != nil {
				t.Fatal(err)
			}
			fi, err := os.Stat(keyPath)
			if err != nil {
				t.Fatal(err)
			}
			test.AssertEqual(t, os.FileMode(0400), fi.Mode().Perm(), "key mode")
		})
	}
}
