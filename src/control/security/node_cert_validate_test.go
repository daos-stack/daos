//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"crypto/ecdsa"
	"crypto/x509"
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
