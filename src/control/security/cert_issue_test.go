//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"testing"
	"time"

	"github.com/google/uuid"

	sectest "github.com/daos-stack/daos/src/control/security/test"
)

func TestSecurity_GeneratePoolCA(t *testing.T) {
	rootPEM, rootKey := sectest.NewCA(t, "DAOS Test Root", nil, nil)
	root := sectest.ParseCert(t, rootPEM)
	poolUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")

	for name, tc := range map[string]struct {
		validity    time.Duration
		expNotAfter time.Time
	}{
		"full term of the root": {validity: 0, expNotAfter: root.NotAfter},
		"clipped to the root":   {validity: 48 * time.Hour, expNotAfter: root.NotAfter},
		"shorter than the root": {validity: time.Hour, expNotAfter: time.Now().Add(time.Hour)},
	} {
		t.Run(name, func(t *testing.T) {
			ca, err := GeneratePoolCA(poolUUID, root, rootKey, tc.validity)
			if err != nil {
				t.Fatal(err)
			}
			parsed, err := ParsePoolCACert(ca.CertPEM)
			if err != nil {
				t.Fatalf("issued pool CA rejected by ParsePoolCACert: %v", err)
			}
			if err := CheckPoolCAIdentity(parsed, poolUUID); err != nil {
				t.Fatal(err)
			}
			if err := VerifyCertKey(ca.Key, parsed); err != nil {
				t.Fatal(err)
			}
			if !parsed.MaxPathLenZero {
				t.Error("pool CA must not be able to sign further CAs")
			}
			if d := time.Since(parsed.NotBefore) - DefaultCertMaxClockSkew; d < 0 || d > 2*time.Second {
				t.Errorf("NotBefore %s is not backdated by %s", parsed.NotBefore, DefaultCertMaxClockSkew)
			}
			if d := parsed.NotAfter.Sub(tc.expNotAfter); d < -2*time.Second || d > 2*time.Second {
				t.Errorf("NotAfter %s, want %s", parsed.NotAfter, tc.expNotAfter)
			}
		})
	}
}

func TestSecurity_IssueClientCerts_Validity(t *testing.T) {
	rootPEM, rootKey := sectest.NewCA(t, "DAOS Test Root", nil, nil)
	root := sectest.ParseCert(t, rootPEM)
	poolCA, err := GeneratePoolCA(uuid.Nil, root, rootKey, 0)
	if err != nil {
		t.Fatal(err)
	}
	now := time.Now()

	for name, tc := range map[string]struct {
		validity    time.Duration
		expNotAfter time.Time
	}{
		"default, clipped to the CA": {validity: 0, expNotAfter: poolCA.Cert.NotAfter},
		"clipped to the CA":          {validity: 48 * time.Hour, expNotAfter: poolCA.Cert.NotAfter},
		"shorter than the CA":        {validity: time.Hour, expNotAfter: now.Add(time.Hour)},
	} {
		t.Run(name, func(t *testing.T) {
			certs, err := IssueClientCerts(&IssueClientCertsReq{
				CACert: poolCA.Cert, CAKey: poolCA.Key, Nodes: []string{"m"}, Validity: tc.validity,
			})
			if err != nil {
				t.Fatal(err)
			}
			ic := certs[0]
			if err := VerifyNodeCertChain(ic.Cert, root, poolCA.CertPEM, now.Add(time.Second)); err != nil {
				t.Fatalf("issued cert rejected by VerifyNodeCertChain: %v", err)
			}
			if err := VerifyCertKey(ic.Key, ic.Cert); err != nil {
				t.Fatal(err)
			}
			if d := ic.Cert.NotAfter.Sub(tc.expNotAfter); d < -2*time.Second || d > 2*time.Second {
				t.Errorf("NotAfter %s, want %s", ic.Cert.NotAfter, tc.expNotAfter)
			}
			if d := ic.Cert.NotBefore.Sub(now); d < -2*time.Second || d > 2*time.Second {
				t.Errorf("NotBefore %s, want ~%s", ic.Cert.NotBefore, now)
			}
		})
	}
}

func TestSecurity_IssueClientCerts(t *testing.T) {
	rootPEM, rootKey := sectest.NewCA(t, "DAOS Test Root", nil, nil)
	poolCA, err := GeneratePoolCA(uuid.Nil, sectest.ParseCert(t, rootPEM), rootKey, 0)
	if err != nil {
		t.Fatal(err)
	}
	now := time.Now().UTC().Truncate(time.Second)
	issue := func(t *testing.T, req *IssueClientCertsReq) map[string]*ClientCert {
		t.Helper()
		req.CACert, req.CAKey = poolCA.Cert, poolCA.Key
		certs, err := IssueClientCerts(req)
		if err != nil {
			t.Fatal(err)
		}
		out := make(map[string]*ClientCert, len(certs))
		for _, c := range certs {
			out[c.Name] = c
		}
		return out
	}

	// Validate needs no CA, so callers can refuse before loading a key.
	for name, req := range map[string]*IssueClientCertsReq{
		"nothing requested":           {},
		"nodes and tenants together":  {Nodes: []string{"a"}, Tenants: []string{"t"}},
		"node name with a path in it": {Nodes: []string{"../etc"}},
		"dotted node name":            {Nodes: []string{"host1.example.com"}},
		"tenant name with a space":    {Tenants: []string{"team a"}},
	} {
		t.Run(name, func(t *testing.T) {
			if err := req.Validate(); err == nil {
				t.Fatal("expected error")
			}
		})
	}

	t.Run("CA key must match the CA", func(t *testing.T) {
		otherPEM, otherKey := sectest.NewCA(t, "Other", nil, nil)
		for name, req := range map[string]*IssueClientCertsReq{
			"no CA":            {Nodes: []string{"m"}},
			"key of other CA":  {CACert: poolCA.Cert, CAKey: otherKey, Nodes: []string{"m"}},
			"cert of other CA": {CACert: sectest.ParseCert(t, otherPEM), CAKey: poolCA.Key, Nodes: []string{"m"}},
		} {
			if _, err := IssueClientCerts(req); err == nil {
				t.Errorf("%s: expected error", name)
			}
		}
	})

	t.Run("CN prefixes and default validity", func(t *testing.T) {
		certs := issue(t, &IssueClientCertsReq{Nodes: []string{"host1"}})
		c := certs["host1"]
		if c.CN != PoolCertCNPrefixNode+"host1" {
			t.Fatalf("CN %q", c.CN)
		}
		// The default lifetime is a year, clipped here to the test CA's day.
		if !c.Cert.NotAfter.Equal(poolCA.Cert.NotAfter) {
			t.Fatalf("NotAfter %s, want the CA's %s", c.Cert.NotAfter, poolCA.Cert.NotAfter)
		}
		if certs := issue(t, &IssueClientCertsReq{Tenants: []string{"teamA"}}); certs["teamA"].CN != PoolCertCNPrefixTenant+"teamA" {
			t.Fatalf("CN %q", certs["teamA"].CN)
		}
	})

	// A revoked CN's replacement is postdated past the watermark, or it is
	// revoked at birth; a watermark in the current second must still be bumped.
	t.Run("postdated past the watermark", func(t *testing.T) {
		wm := CertWatermarks{
			PoolCertCNPrefixNode + "future": now.Add(time.Hour),
			PoolCertCNPrefixNode + "same":   now,
			PoolCertCNPrefixNode + "past":   now.Add(-time.Hour),
		}
		certs := issue(t, &IssueClientCertsReq{Nodes: []string{"future", "same", "past", "clean"}, Watermarks: wm})
		for _, name := range []string{"future", "same"} {
			if nb := certs[name].Cert.NotBefore; !nb.After(wm[PoolCertCNPrefixNode+name]) {
				t.Errorf("%s: NotBefore %s not past watermark %s", name, nb, wm[PoolCertCNPrefixNode+name])
			}
		}
		for _, name := range []string{"past", "clean"} {
			if nb := certs[name].Cert.NotBefore; nb.Before(now) || nb.After(now.Add(time.Minute)) {
				t.Errorf("%s: NotBefore %s, want ~now", name, nb)
			}
		}
	})
}
