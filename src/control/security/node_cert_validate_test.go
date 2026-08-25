//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"crypto"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"math/big"
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"

	sectest "github.com/daos-stack/daos/src/control/security/test"
)

type validationFixture struct {
	root     *x509.Certificate
	rootKey  crypto.PrivateKey
	poolCA   *IssuedCert
	poolUUID uuid.UUID
	handle   uuid.UUID
}

func newValidationFixture(t *testing.T) *validationFixture {
	t.Helper()

	rootPEM, rootKey := sectest.NewCA(t, "DAOS Test Root", nil, nil)
	root := sectest.ParseCert(t, rootPEM)
	poolUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")
	poolCA, err := GeneratePoolCA(poolUUID, root, rootKey, 0)
	if err != nil {
		t.Fatal(err)
	}

	return &validationFixture{
		root:     root,
		rootKey:  rootKey,
		poolCA:   poolCA,
		poolUUID: poolUUID,
		handle:   uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"),
	}
}

// issue mints a leaf under the fixture's pool CA with the given window.
func (f *validationFixture) issue(t *testing.T, cn string, notBefore, notAfter time.Time) *IssuedCert {
	t.Helper()
	ic, err := issueClientCert(cn, f.poolCA.Cert, f.poolCA.Key, notBefore, notAfter)
	if err != nil {
		t.Fatal(err)
	}
	return ic
}

// signedPresentation issues a leaf cert for cn, builds a matching payload, and
// signs the PoP — a fully valid presentation to mutate per test case.
func (f *validationFixture) signedPresentation(t *testing.T, cn string) *NodeCertPresentation {
	t.Helper()

	leaf := f.issue(t, cn, time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
	payload, err := buildPoPPayload(f.poolUUID, f.handle)
	if err != nil {
		t.Fatal(err)
	}
	pop, err := signPoP(leaf.Key, payload)
	if err != nil {
		t.Fatal(err)
	}

	return &NodeCertPresentation{
		Root:        f.root,
		PoolCA:      f.poolCA.CertPEM,
		Cert:        leaf.CertPEM,
		PoPSig:      pop,
		PoPPayload:  payload,
		PoolUUID:    f.poolUUID,
		MachineName: "machine1",
		MaxSkew:     DefaultCertMaxClockSkew,
		Now:         time.Now(),
	}
}

// reissue swaps in a leaf with the given validity window, re-signing the PoP.
func (f *validationFixture) reissue(t *testing.T, v *NodeCertPresentation, notBefore, notAfter time.Time) {
	t.Helper()

	leaf := f.issue(t, PoolCertCNPrefixNode+v.MachineName, notBefore, notAfter)
	pop, err := signPoP(leaf.Key, v.PoPPayload)
	if err != nil {
		t.Fatal(err)
	}
	v.Cert = leaf.CertPEM
	v.PoPSig = pop
}

// leafWithKey signs a client cert for cn under ca with the given key,
// which production issuance never does.
func leafWithKey(t *testing.T, cn string, ca *IssuedCert, key crypto.Signer) []byte {
	t.Helper()
	tmpl := &x509.Certificate{
		SerialNumber: big.NewInt(1),
		Subject:      pkix.Name{CommonName: cn},
		NotBefore:    time.Now().Add(-time.Minute),
		NotAfter:     time.Now().Add(time.Hour),
		KeyUsage:     x509.KeyUsageDigitalSignature,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
	}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, ca.Cert, key.Public(), ca.Key)
	if err != nil {
		t.Fatal(err)
	}
	return pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der})
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
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		payload, err := v.Validate()
		if err != nil {
			t.Fatalf("Validate failed: %v", err)
		}
		if payload.PoolID() != f.poolUUID || payload.HandleID() != f.handle {
			t.Fatalf("parsed payload mismatch: %+v", payload)
		}
	})

	t.Run("valid tenant cert ignores machine name", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixTenant+"team-a")
		v.MachineName = "someone-else"
		if _, err := v.Validate(); err != nil {
			t.Fatalf("Validate failed: %v", err)
		}
	})

	t.Run("nil root", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		v.Root = nil
		expectValidationErr(t, v, ErrInvalidInput)
	})

	t.Run("garbage cert", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		v.Cert = []byte("not a PEM")
		expectValidationErr(t, v, ErrCertInvalid)
	})

	t.Run("empty pool CA bundle", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		v.PoolCA = nil
		expectValidationErr(t, v, ErrCertInvalid)
	})

	t.Run("cert not chained to root", func(t *testing.T) {
		otherRootPEM, otherRootKey := sectest.NewCA(t, "Other Root", nil, nil)
		otherCA, err := GeneratePoolCA(f.poolUUID, sectest.ParseCert(t, otherRootPEM), otherRootKey, 0)
		if err != nil {
			t.Fatal(err)
		}
		leaf, err := issueClientCert(PoolCertCNPrefixNode+"machine1", otherCA.Cert, otherCA.Key,
			time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
		if err != nil {
			t.Fatal(err)
		}

		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		v.Cert = leaf.CertPEM
		v.PoolCA = otherCA.CertPEM
		pop, err := signPoP(leaf.Key, v.PoPPayload)
		if err != nil {
			t.Fatal(err)
		}
		v.PoPSig = pop
		expectValidationErr(t, v, ErrCertInvalid)
	})

	t.Run("wrong key type", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		p256, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
		if err != nil {
			t.Fatal(err)
		}
		v.Cert = leafWithKey(t, PoolCertCNPrefixNode+"machine1", f.poolCA, p256)
		expectValidationErr(t, v, ErrCertInvalid)
	})

	t.Run("CN policy violation", func(t *testing.T) {
		v := f.signedPresentation(t, "no-prefix-here")
		expectValidationErr(t, v, ErrCertInvalid)
	})

	t.Run("machine name mismatch", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		v.MachineName = "machine2"
		expectValidationErr(t, v, ErrCertInvalid)
	})

	t.Run("empty machine name for node cert", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		v.MachineName = ""
		expectValidationErr(t, v, ErrInvalidInput)
	})

	t.Run("not yet valid within skew", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		f.reissue(t, v, time.Now().Add(time.Minute), time.Now().Add(time.Hour))
		if _, err := v.Validate(); err != nil {
			t.Fatalf("Validate failed: %v", err)
		}
	})

	t.Run("not yet valid beyond skew", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		f.reissue(t, v, time.Now().Add(DefaultCertMaxClockSkew+time.Minute), time.Now().Add(time.Hour))
		expectValidationErr(t, v, ErrCertNotYetValid)
	})

	t.Run("expired despite skew", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		f.reissue(t, v, time.Now().Add(-time.Hour), time.Now().Add(-time.Minute))
		expectValidationErr(t, v, ErrCertInvalid)
	})

	t.Run("unreadable watermarks", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		v.Watermarks = []byte("{not json")
		expectValidationErr(t, v, ErrBadWatermarks)
	})

	t.Run("revoked by watermark", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		wm, err := EncodeCertWatermarks(CertWatermarks{
			PoolCertCNPrefixNode + "machine1": time.Now().Add(time.Minute),
		})
		if err != nil {
			t.Fatal(err)
		}
		v.Watermarks = wm
		expectValidationErr(t, v, ErrCertRevoked)
	})

	t.Run("reissued past watermark", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		wm, err := EncodeCertWatermarks(CertWatermarks{
			PoolCertCNPrefixNode + "machine1": time.Now().Add(-time.Hour),
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
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		leaf := f.issue(t, PoolCertCNPrefixNode+"machine1", time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
		garbage := []byte{0xff, 0xff, 0xff, 0xff}
		pop, err := signPoP(leaf.Key, garbage)
		if err != nil {
			t.Fatal(err)
		}
		v.Cert = leaf.CertPEM
		v.PoPPayload = garbage
		v.PoPSig = pop
		expectValidationErr(t, v, ErrInvalidInput)
	})

	t.Run("signed short handle UUID", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		leaf := f.issue(t, PoolCertCNPrefixNode+"machine1", time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
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
		pop, err := signPoP(leaf.Key, payload)
		if err != nil {
			t.Fatal(err)
		}
		v.Cert = leaf.CertPEM
		v.PoPPayload = payload
		v.PoPSig = pop
		expectValidationErr(t, v, ErrInvalidInput)
	})

	t.Run("payload bound to other pool", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		v.PoolUUID = uuid.MustParse("99999999-9999-9999-9999-999999999999")
		expectValidationErr(t, v, ErrPoPInvalid)
	})

	t.Run("stale timestamp", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		v.Now = time.Now().Add(DefaultCertMaxClockSkew + time.Minute)
		expectValidationErr(t, v, ErrPoPStale)
	})

	t.Run("tampered payload", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		v.PoPPayload[len(v.PoPPayload)-1] ^= 0xff
		expectValidationErr(t, v, ErrPoPInvalid)
	})

	t.Run("signed by wrong key", func(t *testing.T) {
		v := f.signedPresentation(t, PoolCertCNPrefixNode+"machine1")
		other := f.issue(t, PoolCertCNPrefixNode+"machine1", time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
		pop, err := signPoP(other.Key, v.PoPPayload)
		if err != nil {
			t.Fatal(err)
		}
		v.PoPSig = pop
		expectValidationErr(t, v, ErrPoPInvalid)
	})
}

func TestSecurity_CheckCertValidity(t *testing.T) {
	f := newValidationFixture(t)
	now := time.Now()
	leaf := func(nb, na time.Time) *x509.Certificate {
		return f.issue(t, PoolCertCNPrefixNode+"m", nb, na).Cert
	}
	for name, tc := range map[string]struct {
		cert   *x509.Certificate
		expErr error
	}{
		"in window":                     {cert: leaf(now.Add(-time.Hour), now.Add(time.Hour))},
		"expired":                       {cert: leaf(now.Add(-2*time.Hour), now.Add(-time.Hour)), expErr: ErrCertInvalid},
		"not yet valid":                 {cert: leaf(now.Add(time.Hour), now.Add(2*time.Hour)), expErr: ErrCertNotYetValid},
		"not yet valid but within skew": {cert: leaf(now.Add(time.Minute), now.Add(time.Hour))},
	} {
		t.Run(name, func(t *testing.T) {
			err := CheckCertValidity(tc.cert, now, DefaultCertMaxClockSkew)
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
	leaf := f.issue(t, PoolCertCNPrefixNode+"m", now.Add(-time.Minute), now.Add(time.Hour)).Cert
	otherRootPEM, _ := sectest.NewCA(t, "Other Root", nil, nil)
	otherRoot := sectest.ParseCert(t, otherRootPEM)
	direct, err := issueClientCert(PoolCertCNPrefixNode+"m", f.root, f.rootKey, now.Add(-time.Minute), now.Add(time.Hour))
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		leaf   *x509.Certificate
		root   *x509.Certificate
		bundle []byte
		expErr error
	}{
		"chains through the pool CA": {root: f.root, bundle: f.poolCA.CertPEM},
		"signed directly by the DAOS CA": {
			leaf: direct.Cert, root: f.root, bundle: f.poolCA.CertPEM, expErr: ErrCertInvalid,
		},
		"wrong root":      {root: otherRoot, bundle: f.poolCA.CertPEM, expErr: ErrCertInvalid},
		"no trust anchor": {root: nil, bundle: f.poolCA.CertPEM, expErr: ErrInvalidInput},
		"empty bundle":    {root: f.root, bundle: nil, expErr: ErrCertInvalid},
		"bundle with a block that is not a certificate": {
			root: f.root,
			bundle: append(pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: []byte("not a certificate")}),
				f.poolCA.CertPEM...),
			expErr: ErrCertInvalid,
		},
	} {
		t.Run(name, func(t *testing.T) {
			if tc.leaf == nil {
				tc.leaf = leaf
			}
			err := VerifyNodeCertChain(tc.leaf, tc.root, tc.bundle, now)
			if tc.expErr == nil && err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if tc.expErr != nil && !errors.Is(err, tc.expErr) {
				t.Fatalf("expected %v, got %v", tc.expErr, err)
			}
		})
	}
}

func TestSecurity_CheckCertRevocation(t *testing.T) {
	f := newValidationFixture(t)
	nb := time.Now().Add(-time.Hour)
	leaf := f.issue(t, PoolCertCNPrefixNode+"m", nb, nb.Add(2*time.Hour)).Cert
	cn := PoolCertCNPrefixNode + "m"

	for name, tc := range map[string]struct {
		wm     CertWatermarks
		expErr error
	}{
		"no watermarks":                  {wm: nil},
		"watermark for another identity": {wm: CertWatermarks{PoolCertCNPrefixNode + "other": nb.Add(time.Hour)}},
		"watermark older than the cert":  {wm: CertWatermarks{cn: nb.Add(-time.Minute)}},
		"watermark equal to NotBefore":   {wm: CertWatermarks{cn: nb}, expErr: ErrCertRevoked},
		"watermark newer than the cert":  {wm: CertWatermarks{cn: nb.Add(time.Minute)}, expErr: ErrCertRevoked},
	} {
		t.Run(name, func(t *testing.T) {
			err := CheckCertRevocation(leaf, cn, tc.wm)
			if tc.expErr == nil && err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if tc.expErr != nil && !errors.Is(err, tc.expErr) {
				t.Fatalf("expected %v, got %v", tc.expErr, err)
			}
		})
	}
}

func TestSecurity_VerifyCertKey(t *testing.T) {
	f := newValidationFixture(t)
	now := time.Now()
	ic := f.issue(t, PoolCertCNPrefixNode+"m", now.Add(-time.Minute), now.Add(time.Hour))
	other := f.issue(t, PoolCertCNPrefixNode+"m", now.Add(-time.Minute), now.Add(time.Hour))

	if err := VerifyCertKey(ic.Key, ic.Cert); err != nil {
		t.Fatalf("matching key rejected: %v", err)
	}
	if err := VerifyCertKey(other.Key, ic.Cert); !errors.Is(err, ErrCertInvalid) {
		t.Fatalf("expected %v for a foreign key, got %v", ErrCertInvalid, err)
	}
	if err := VerifyCertKey("not a key", ic.Cert); !errors.Is(err, ErrInvalidInput) {
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
