//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"crypto/ecdsa"
	"crypto/x509"
	"encoding/pem"
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	sectest "github.com/daos-stack/daos/src/control/security/test"
)

type validationFixture struct {
	root     *x509.Certificate
	poolCA   []byte
	caKey    *ecdsa.PrivateKey
	caCert   *x509.Certificate
	poolUUID uuid.UUID
	handle   uuid.UUID
}

func newValidationFixture(t *testing.T) *validationFixture {
	t.Helper()

	rootPEM, rootKey := sectest.NewCA(t, "DAOS Test Root", nil, nil)
	root := sectest.ParseCert(t, rootPEM)
	poolCAPEM, poolCAKey := sectest.NewCA(t, "Pool CA", root, rootKey)

	return &validationFixture{
		root:     root,
		poolCA:   poolCAPEM,
		caKey:    poolCAKey,
		caCert:   sectest.ParseCert(t, poolCAPEM),
		poolUUID: uuid.MustParse("12345678-1234-1234-1234-123456789abc"),
		handle:   uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"),
	}
}

// signedPresentation issues a leaf cert for cn, builds a matching payload, and
// signs the PoP — a fully valid presentation to mutate per test case.
func (f *validationFixture) signedPresentation(t *testing.T, cn string) *NodeCertPresentation {
	t.Helper()

	leafPEM, leafKey := sectest.NewLeafCert(t, cn, f.caCert, f.caKey,
		time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
	payload, err := buildPoPPayload(f.poolUUID, f.handle)
	if err != nil {
		t.Fatal(err)
	}
	pop, err := signPoP(leafKey, payload)
	if err != nil {
		t.Fatal(err)
	}

	return &NodeCertPresentation{
		Root:        f.root,
		PoolCA:      f.poolCA,
		Cert:        leafPEM,
		PoPSig:      pop,
		PoPPayload:  payload,
		PoolUUID:    f.poolUUID,
		MachineName: "machine1",
	}
}

func expectValidationErr(t *testing.T, v *NodeCertPresentation, sentinel error) {
	t.Helper()
	if _, err := v.Validate(); !errors.Is(err, sentinel) {
		t.Fatalf("expected %v, got %v", sentinel, err)
	}
}

func TestSecurity_NodeCertPresentation(t *testing.T) {
	f := newValidationFixture(t)

	t.Run("valid node cert", func(t *testing.T) {
		v := f.signedPresentation(t, CertCNPrefixNode+"machine1")
		payload, err := v.Validate()
		if err != nil {
			t.Fatalf("Validate failed: %v", err)
		}
		if payload.PoolID() != f.poolUUID || payload.HandleID() != f.handle {
			t.Fatalf("parsed payload mismatch: %+v", payload)
		}
		expKey := f.poolUUID.String() + ":" + f.handle.String()
		if payload.ReplayKey() != expKey {
			t.Fatalf("replay key %q, want %q", payload.ReplayKey(), expKey)
		}
	})

	t.Run("valid tenant cert ignores machine name", func(t *testing.T) {
		v := f.signedPresentation(t, CertCNPrefixTenant+"team-a")
		v.MachineName = "someone-else"
		if _, err := v.Validate(); err != nil {
			t.Fatalf("Validate failed: %v", err)
		}
	})

	t.Run("nil root", func(t *testing.T) {
		v := f.signedPresentation(t, CertCNPrefixNode+"machine1")
		v.Root = nil
		expectValidationErr(t, v, ErrInvalidInput)
	})

	t.Run("garbage cert", func(t *testing.T) {
		v := f.signedPresentation(t, CertCNPrefixNode+"machine1")
		v.Cert = []byte("not a PEM")
		expectValidationErr(t, v, ErrCertInvalid)
	})

	t.Run("empty pool CA bundle", func(t *testing.T) {
		v := f.signedPresentation(t, CertCNPrefixNode+"machine1")
		v.PoolCA = nil
		expectValidationErr(t, v, ErrCertInvalid)
	})

	t.Run("cert not chained to root", func(t *testing.T) {
		otherRootPEM, otherRootKey := sectest.NewCA(t, "Other Root", nil, nil)
		otherCAPEM, otherCAKey := sectest.NewCA(t, "Other Pool CA",
			sectest.ParseCert(t, otherRootPEM), otherRootKey)
		leafPEM, leafKey := sectest.NewLeafCert(t, CertCNPrefixNode+"machine1",
			sectest.ParseCert(t, otherCAPEM), otherCAKey,
			time.Now().Add(-time.Minute), time.Now().Add(time.Hour))

		v := f.signedPresentation(t, CertCNPrefixNode+"machine1")
		v.Cert = leafPEM
		v.PoolCA = otherCAPEM
		payload, err := buildPoPPayload(f.poolUUID, f.handle)
		if err != nil {
			t.Fatal(err)
		}
		pop, err := signPoP(leafKey, payload)
		if err != nil {
			t.Fatal(err)
		}
		v.PoPPayload = payload
		v.PoPSig = pop
		expectValidationErr(t, v, ErrCertInvalid)
	})

	t.Run("CN policy violation", func(t *testing.T) {
		v := f.signedPresentation(t, "no-prefix-here")
		expectValidationErr(t, v, ErrCertInvalid)
	})

	t.Run("machine name mismatch", func(t *testing.T) {
		v := f.signedPresentation(t, CertCNPrefixNode+"machine1")
		v.MachineName = "machine2"
		expectValidationErr(t, v, ErrCertInvalid)
	})

	t.Run("empty machine name for node cert", func(t *testing.T) {
		v := f.signedPresentation(t, CertCNPrefixNode+"machine1")
		v.MachineName = ""
		expectValidationErr(t, v, ErrInvalidInput)
	})

	t.Run("revoked by watermark", func(t *testing.T) {
		v := f.signedPresentation(t, CertCNPrefixNode+"machine1")
		wm, err := EncodeCertWatermarks(CertWatermarks{
			CertCNPrefixNode + "machine1": time.Now().Add(time.Minute),
		})
		if err != nil {
			t.Fatal(err)
		}
		v.Watermarks = wm
		expectValidationErr(t, v, ErrCertRevoked)
	})

	t.Run("reissued past watermark", func(t *testing.T) {
		v := f.signedPresentation(t, CertCNPrefixNode+"machine1")
		wm, err := EncodeCertWatermarks(CertWatermarks{
			CertCNPrefixNode + "machine1": time.Now().Add(-time.Hour),
		})
		if err != nil {
			t.Fatal(err)
		}
		v.Watermarks = wm
		if _, err := v.Validate(); err != nil {
			t.Fatalf("Validate failed: %v", err)
		}
	})

	t.Run("signed garbage payload", func(t *testing.T) {
		// A correctly signed but unparsable payload is a producer bug,
		// not an auth failure: signature passes, parse must reject.
		v := f.signedPresentation(t, CertCNPrefixNode+"machine1")
		leafPEM, leafKey := sectest.NewLeafCert(t, CertCNPrefixNode+"machine1",
			f.caCert, f.caKey, time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
		garbage := []byte{0xff, 0xff, 0xff, 0xff}
		pop, err := signPoP(leafKey, garbage)
		if err != nil {
			t.Fatal(err)
		}
		v.Cert = leafPEM
		v.PoPPayload = garbage
		v.PoPSig = pop
		expectValidationErr(t, v, ErrInvalidInput)
	})

	t.Run("signed short handle UUID", func(t *testing.T) {
		v := f.signedPresentation(t, CertCNPrefixNode+"machine1")
		leafPEM, leafKey := sectest.NewLeafCert(t, CertCNPrefixNode+"machine1",
			f.caCert, f.caKey, time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
		// Craft a signed payload with a short handle directly; the typed
		// helper can no longer produce one.
		payload, err := proto.Marshal(&PoPPayload{
			PoolUuid:   f.poolUUID[:],
			HandleUuid: []byte{0x01, 0x02},
			Timestamp:  time.Now().Unix(),
		})
		if err != nil {
			t.Fatal(err)
		}
		pop, err := signPoP(leafKey, payload)
		if err != nil {
			t.Fatal(err)
		}
		v.Cert = leafPEM
		v.PoPPayload = payload
		v.PoPSig = pop
		expectValidationErr(t, v, ErrInvalidInput)
	})

	t.Run("payload bound to other pool", func(t *testing.T) {
		v := f.signedPresentation(t, CertCNPrefixNode+"machine1")
		v.PoolUUID = uuid.MustParse("99999999-9999-9999-9999-999999999999")
		expectValidationErr(t, v, ErrPoPInvalid)
	})

	t.Run("stale timestamp", func(t *testing.T) {
		v := f.signedPresentation(t, CertCNPrefixNode+"machine1")
		v.Now = time.Now().Add(NotBeforeSkewTolerance + time.Minute)
		expectValidationErr(t, v, ErrPoPStale)
	})

	t.Run("tampered payload", func(t *testing.T) {
		v := f.signedPresentation(t, CertCNPrefixNode+"machine1")
		v.PoPPayload[len(v.PoPPayload)-1] ^= 0xff
		expectValidationErr(t, v, ErrPoPInvalid)
	})

	t.Run("signed by wrong key", func(t *testing.T) {
		v := f.signedPresentation(t, CertCNPrefixNode+"machine1")
		_, otherKey := sectest.NewLeafCert(t, CertCNPrefixNode+"machine1",
			f.caCert, f.caKey, time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
		pop, err := signPoP(otherKey, v.PoPPayload)
		if err != nil {
			t.Fatal(err)
		}
		v.PoPSig = pop
		expectValidationErr(t, v, ErrPoPInvalid)
	})
}

func TestSecurity_CheckCNBinding(t *testing.T) {
	for name, tc := range map[string]struct {
		prefix, suffix, machine string
		expErr                  error
	}{
		"node cert bound to its machine": {
			prefix: CertCNPrefixNode, suffix: "machine1", machine: "machine1",
		},
		"node cert on the wrong machine": {
			prefix: CertCNPrefixNode, suffix: "machine1", machine: "machine2",
			expErr: ErrCertInvalid,
		},
		"node cert with no machine name to bind to": {
			prefix: CertCNPrefixNode, suffix: "machine1", machine: "",
			expErr: ErrInvalidInput,
		},
		"tenant cert is not machine-bound": {
			prefix: CertCNPrefixTenant, suffix: "teamA", machine: "machine2",
		},
		"tenant cert needs no machine name": {
			prefix: CertCNPrefixTenant, suffix: "teamA", machine: "",
		},
		// Only reachable if ValidatePoolCertCN ever grows a prefix this
		// function was not taught about; it must fail closed.
		"unknown prefix is rejected": {
			prefix: "service:", suffix: "x", machine: "x",
			expErr: ErrInvalidInput,
		},
	} {
		t.Run(name, func(t *testing.T) {
			err := CheckCNBinding(tc.prefix, tc.suffix, tc.machine)
			if tc.expErr == nil {
				if err != nil {
					t.Fatalf("unexpected error: %v", err)
				}
				return
			}
			if !errors.Is(err, tc.expErr) {
				t.Fatalf("expected %v, got %v", tc.expErr, err)
			}
		})
	}
}

func TestSecurity_ExpiryWarning(t *testing.T) {
	now := time.Date(2026, 8, 26, 12, 0, 0, 0, time.UTC)
	mk := func(notAfter time.Time) *x509.Certificate {
		return &x509.Certificate{NotAfter: notAfter}
	}
	for name, tc := range map[string]struct {
		cert *x509.Certificate
		exp  string
	}{
		"far off":         {cert: mk(now.Add(90 * 24 * time.Hour)), exp: ""},
		"at the window":   {cert: mk(now.Add(CertExpiryWarnWindow)), exp: ""},
		"inside window":   {cert: mk(now.Add(3*24*time.Hour + time.Hour)), exp: "node cert expires in 3 day(s), on 2026-08-29"},
		"already expired": {cert: mk(now.Add(-time.Hour)), exp: "node cert expired on 2026-08-26"},
	} {
		t.Run(name, func(t *testing.T) {
			if got := ExpiryWarning("node cert", tc.cert, now, CertExpiryWarnWindow); got != tc.exp {
				t.Fatalf("got %q, want %q", got, tc.exp)
			}
		})
	}
}

func TestSecurity_CheckValidity(t *testing.T) {
	f := newValidationFixture(t)
	now := time.Now()
	leaf := func(nb, na time.Time) *x509.Certificate {
		pem, _ := sectest.NewLeafCert(t, CertCNPrefixNode+"m", f.caCert, f.caKey, nb, na)
		return sectest.ParseCert(t, pem)
	}
	for name, tc := range map[string]struct {
		cert   *x509.Certificate
		expErr error
	}{
		"in window":                     {cert: leaf(now.Add(-time.Hour), now.Add(time.Hour))},
		"expired":                       {cert: leaf(now.Add(-2*time.Hour), now.Add(-time.Hour)), expErr: ErrCertInvalid},
		"not yet valid":                 {cert: leaf(now.Add(time.Hour), now.Add(2*time.Hour)), expErr: ErrCertInvalid},
		"not yet valid but within skew": {cert: leaf(now.Add(time.Minute), now.Add(time.Hour))},
	} {
		t.Run(name, func(t *testing.T) {
			err := CheckValidity(tc.cert, now, NotBeforeSkewTolerance)
			if tc.expErr == nil && err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if tc.expErr != nil && !errors.Is(err, tc.expErr) {
				t.Fatalf("expected %v, got %v", tc.expErr, err)
			}
		})
	}
}

func TestSecurity_VerifyNodeCertChain(t *testing.T) {
	f := newValidationFixture(t)
	now := time.Now()
	leafPEM, _ := sectest.NewLeafCert(t, CertCNPrefixNode+"m", f.caCert, f.caKey,
		now.Add(-time.Minute), now.Add(time.Hour))
	leaf := sectest.ParseCert(t, leafPEM)
	otherRootPEM, _ := sectest.NewCA(t, "Other Root", nil, nil)
	otherRoot := sectest.ParseCert(t, otherRootPEM)

	for name, tc := range map[string]struct {
		root   *x509.Certificate
		bundle []byte
		expErr error
	}{
		"chains through the pool CA": {root: f.root, bundle: f.poolCA},
		"wrong root":                 {root: otherRoot, bundle: f.poolCA, expErr: ErrCertInvalid},
		"no trust anchor":            {root: nil, bundle: f.poolCA, expErr: ErrInvalidInput},
		"empty bundle":               {root: f.root, bundle: nil, expErr: ErrCertInvalid},
		"bundle with a block that is not a certificate": {
			root: f.root,
			bundle: append(pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: []byte("not a certificate")}),
				f.poolCA...),
			expErr: ErrCertInvalid,
		},
	} {
		t.Run(name, func(t *testing.T) {
			err := VerifyNodeCertChain(leaf, tc.root, tc.bundle, now)
			if tc.expErr == nil && err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if tc.expErr != nil && !errors.Is(err, tc.expErr) {
				t.Fatalf("expected %v, got %v", tc.expErr, err)
			}
		})
	}
}

func TestSecurity_CheckRevocation(t *testing.T) {
	f := newValidationFixture(t)
	nb := time.Now().Add(-time.Hour)
	leafPEM, _ := sectest.NewLeafCert(t, CertCNPrefixNode+"m", f.caCert, f.caKey, nb, nb.Add(2*time.Hour))
	leaf := sectest.ParseCert(t, leafPEM)
	cn := CertCNPrefixNode + "m"

	for name, tc := range map[string]struct {
		wm     CertWatermarks
		expErr error
	}{
		"no watermarks":                  {wm: nil},
		"watermark for another identity": {wm: CertWatermarks{CertCNPrefixNode + "other": nb.Add(time.Hour)}},
		"watermark older than the cert":  {wm: CertWatermarks{cn: nb.Add(-time.Minute)}},
		"watermark equal to NotBefore":   {wm: CertWatermarks{cn: nb}, expErr: ErrCertRevoked},
		"watermark newer than the cert":  {wm: CertWatermarks{cn: nb.Add(time.Minute)}, expErr: ErrCertRevoked},
	} {
		t.Run(name, func(t *testing.T) {
			err := CheckRevocation(leaf, cn, tc.wm)
			if tc.expErr == nil && err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if tc.expErr != nil && !errors.Is(err, tc.expErr) {
				t.Fatalf("expected %v, got %v", tc.expErr, err)
			}
		})
	}
}

func TestSecurity_KeyMatchesCert(t *testing.T) {
	f := newValidationFixture(t)
	now := time.Now()
	certPEM, key := sectest.NewLeafCert(t, CertCNPrefixNode+"m", f.caCert, f.caKey, now.Add(-time.Minute), now.Add(time.Hour))
	otherPEM, otherKey := sectest.NewLeafCert(t, CertCNPrefixNode+"m", f.caCert, f.caKey, now.Add(-time.Minute), now.Add(time.Hour))
	cert := sectest.ParseCert(t, certPEM)
	_ = otherPEM

	if err := KeyMatchesCert(key, cert); err != nil {
		t.Fatalf("matching key rejected: %v", err)
	}
	if err := KeyMatchesCert(otherKey, cert); !errors.Is(err, ErrCertInvalid) {
		t.Fatalf("expected %v for a foreign key, got %v", ErrCertInvalid, err)
	}
	if err := KeyMatchesCert("not a key", cert); !errors.Is(err, ErrInvalidInput) {
		t.Fatalf("expected %v for a non-signer, got %v", ErrInvalidInput, err)
	}
}

func TestSecurity_CertPathHelpers(t *testing.T) {
	for name, tc := range map[string]struct{ got, exp string }{
		"node cert":    {func() string { c, _ := NodeCertPaths("/d", uuid.Nil); return c }(), "/d/" + uuid.Nil.String() + ".crt"},
		"node key":     {func() string { _, k := NodeCertPaths("/d", uuid.Nil); return k }(), "/d/" + uuid.Nil.String() + ".key"},
		"pool CA cert": {func() string { c, _ := PoolCAPaths("/d", uuid.Nil); return c }(), "/d/" + uuid.Nil.String() + "_ca.crt"},
		"pool CA key":  {func() string { _, k := PoolCAPaths("/d", uuid.Nil); return k }(), "/d/" + uuid.Nil.String() + "_ca.key"},
		"pool CA dir":  {DefaultPoolCADir("/etc/daos/certs/admin.key"), "/etc/daos/certs/pools"},
		"DAOS CA key":  {DefaultDAOSCAKeyPath("/etc/daos/certs/daosCA.crt"), "/etc/daos/certs/daosCA.key"},
	} {
		if tc.got != tc.exp {
			t.Errorf("%s: got %q, want %q", name, tc.got, tc.exp)
		}
	}
}
