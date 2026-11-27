//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"crypto"
	"crypto/x509"
	"os"
	"testing"
	"time"

	"github.com/google/uuid"

	sectest "github.com/daos-stack/daos/src/control/security/test"
)

const testMachineName = "machine1"

// certFixture is a DAOS CA with one pool CA signed by it.
type certFixture struct {
	root     *x509.Certificate
	rootKey  crypto.PrivateKey
	poolCA   *IssuedCert
	poolUUID uuid.UUID
	handle   uuid.UUID
}

func newCertFixture(t *testing.T) *certFixture {
	t.Helper()

	rootPEM, rootKey := sectest.NewCA(t, "DAOS Test Root", nil, nil)
	root := sectest.ParseCert(t, rootPEM)
	poolUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")
	poolCA, err := GeneratePoolCA(poolUUID, root, rootKey, 0)
	if err != nil {
		t.Fatal(err)
	}

	return &certFixture{
		root:     root,
		rootKey:  rootKey,
		poolCA:   poolCA,
		poolUUID: poolUUID,
		handle:   uuid.MustParse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"),
	}
}

func (f *certFixture) issue(t *testing.T, cn string, notBefore, notAfter time.Time) *IssuedCert {
	t.Helper()

	ic, err := issueClientCert(cn, f.poolCA.Cert, f.poolCA.Key, notBefore, notAfter)
	if err != nil {
		t.Fatal(err)
	}
	return ic
}

// leaf issues a cert from the pool CA that is valid now.
func (f *certFixture) leaf(t *testing.T, cn string) *IssuedCert {
	t.Helper()

	return f.issue(t, cn, time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
}

// presentation is what the server sees when leaf connects from testMachineName.
func (f *certFixture) presentation(t *testing.T, leaf *IssuedCert) *NodeCertPresentation {
	t.Helper()

	payload, err := buildPoPPayload(f.poolUUID, f.handle)
	if err != nil {
		t.Fatal(err)
	}
	v := &NodeCertPresentation{
		Root:        f.root,
		PoolCA:      f.poolCA.CertPEM,
		PoolUUID:    f.poolUUID,
		MachineName: testMachineName,
		MaxSkew:     DefaultCertMaxClockSkew,
		Now:         time.Now(),
	}
	f.present(t, v, leaf, payload)
	return v
}

// present swaps in leaf and payload, signed with leaf's key.
func (f *certFixture) present(t *testing.T, v *NodeCertPresentation, leaf *IssuedCert, payload []byte) {
	t.Helper()

	pop, err := signPoP(leaf.Key, payload)
	if err != nil {
		t.Fatal(err)
	}
	v.Cert = leaf.CertPEM
	v.PoPPayload = payload
	v.PoPSig = pop
}

func watermarksJSON(t *testing.T, wm CertWatermarks) []byte {
	t.Helper()

	data, err := encodeCertWatermarks(wm)
	if err != nil {
		t.Fatal(err)
	}
	return data
}

// deployNodeCert writes a cert for cn, signed by a throwaway CA, where a loader for dir will find it.
func deployNodeCert(t *testing.T, dir string, poolUUID uuid.UUID, cn string, notBefore, notAfter time.Time) *IssuedCert {
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

func deployUsableNodeCert(t *testing.T, dir string, poolUUID uuid.UUID) *IssuedCert {
	t.Helper()

	return deployNodeCert(t, dir, poolUUID, PoolCertCNPrefixNode+testMachineName,
		time.Now().Add(-time.Minute), time.Now().Add(time.Hour))
}

func removeNodeKey(t *testing.T, dir string, poolUUID uuid.UUID) {
	t.Helper()

	_, keyPath := NodeCertPaths(dir, poolUUID)
	if err := os.Remove(keyPath); err != nil {
		t.Fatal(err)
	}
}

func replaceNodeKey(t *testing.T, dir string, poolUUID uuid.UUID, keyPEM []byte) {
	t.Helper()

	removeNodeKey(t, dir, poolUUID)
	_, keyPath := NodeCertPaths(dir, poolUUID)
	if err := os.WriteFile(keyPath, keyPEM, 0400); err != nil {
		t.Fatal(err)
	}
}
