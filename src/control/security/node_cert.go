//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"bytes"
	"crypto"
	"crypto/x509"
	"fmt"
	"os"
	"os/user"
	"path/filepath"
	"sync"
	"syscall"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

// DefaultCertMaxClockSkew is tolerated on cert NotBefore and PoP timestamps.
const DefaultCertMaxClockSkew = 5 * time.Minute

var (
	// ErrCertInvalid covers parse, chain and CN policy failures.
	ErrCertInvalid = errors.New("node cert invalid")
	// ErrCertRevoked covers certs at or below their revocation watermark.
	ErrCertRevoked = errors.New("node cert revoked")
	// ErrInvalidInput covers malformed request fields.
	ErrInvalidInput = errors.New("invalid input")
	// ErrPoPInvalid covers payload binding and signature failures.
	ErrPoPInvalid = errors.New("proof-of-possession invalid")
	// ErrPoPStale covers timestamps outside the allowed clock skew.
	ErrPoPStale = errors.New("proof-of-possession stale")
	// ErrWatermarksInvalid covers a cert_watermarks property that cannot be decoded.
	ErrWatermarksInvalid = errors.New("cert watermarks unreadable")
	// ErrCertNotYetValid covers a cert whose NotBefore is beyond the skew tolerance.
	ErrCertNotYetValid = errors.New("node cert not yet valid")
	// ErrNodeCertNotDeployed covers a pool with no certificate file at all.
	ErrNodeCertNotDeployed = errors.New("no node certificate deployed")
)

// NodeCert is a loaded per-pool node certificate and key.
type NodeCert struct {
	PEM  []byte
	Cert *x509.Certificate
	Key  crypto.PrivateKey
}

// NodeCertLoader loads per-pool node certs from disk.
type NodeCertLoader struct {
	certsDir   string
	certsCache sync.Map
}

// NewNodeCertLoader returns a loader that reads <pool_uuid>.{crt,key} from dir.
func NewNodeCertLoader(dir string) *NodeCertLoader {
	return &NodeCertLoader{certsDir: dir}
}

// NodeCertPaths returns the paths of a pool's node certificate and key in dir.
func NodeCertPaths(dir string, poolUUID uuid.UUID) (certPath, keyPath string) {
	return filepath.Join(dir, poolUUID.String()+".crt"), filepath.Join(dir, poolUUID.String()+".key")
}

// fileSig detects a changed file without reading it.
type fileSig struct {
	mtime time.Time
	size  int64
}

type cachedNodeCert struct {
	cert    *NodeCert
	certSig fileSig
	keySig  fileSig
}

func statSig(path string) (fileSig, error) {
	fi, err := os.Stat(path)
	if err != nil {
		return fileSig{}, err
	}
	return fileSig{mtime: fi.ModTime(), size: fi.Size()}, nil
}

// Load returns the pool's cert and key, from cache while the files are unchanged.
func (l *NodeCertLoader) Load(log logging.Logger, poolUUID uuid.UUID) (*NodeCert, error) {
	uuidStr := poolUUID.String()
	certPath, keyPath := NodeCertPaths(l.certsDir, poolUUID)

	certSig, err := statSig(certPath)
	if errors.Is(err, os.ErrNotExist) {
		return nil, errors.Wrapf(ErrNodeCertNotDeployed, "pool %s", uuidStr)
	} else if err != nil {
		return nil, errors.Wrap(err, "stat node certificate")
	}
	keySig, err := statSig(keyPath)
	if err != nil {
		return nil, errors.Wrap(err, "stat node private key")
	}

	if v, ok := l.certsCache.Load(uuidStr); ok {
		c := v.(*cachedNodeCert)
		if c.certSig == certSig && c.keySig == keySig {
			return c.cert, nil
		}
		log.Debugf("node cert file for pool %s changed on disk; reloading", uuidStr)
	}

	insp := inspectNodeCertFiles(l.certsDir, poolUUID)
	if err := insp.filesErr(); err != nil {
		return nil, err
	}

	nc := &NodeCert{PEM: insp.PEM, Cert: insp.Certificate, Key: insp.PrivateKey}
	l.certsCache.Store(uuidStr, &cachedNodeCert{cert: nc, certSig: certSig, keySig: keySig})

	log.Debugf("loaded node cert for pool %s: CN=%s, expires=%s",
		uuidStr, nc.Cert.Subject.CommonName, nc.Cert.NotAfter.Format(time.RFC3339))

	return nc, nil
}

// VerifyCertKey verifies that the key is the private half of cert's public key.
func VerifyCertKey(key crypto.PrivateKey, cert *x509.Certificate) error {
	signer, ok := key.(crypto.Signer)
	if !ok {
		return errors.Wrapf(ErrInvalidInput, "unsupported private key type %T", key)
	}
	pub, ok := signer.Public().(interface{ Equal(crypto.PublicKey) bool })
	if !ok || !pub.Equal(cert.PublicKey) {
		return errors.Wrap(ErrCertInvalid, "private key does not match certificate")
	}
	return nil
}

// NodeCertFile is one file of a node cert deployment as found on disk.
type NodeCertFile struct {
	Path  string
	Mode  os.FileMode // zero when absent
	Owner string      // "" when absent
	Err   error
}

// NodeCertInspection records every problem with a pool's node cert deployment, not just the first.
type NodeCertInspection struct {
	Dir  NodeCertFile
	Cert NodeCertFile
	Key  NodeCertFile

	PEM         []byte
	Certificate *x509.Certificate // nil unless the cert file parsed
	PrivateKey  crypto.PrivateKey // nil unless the key file loaded

	CN       string
	Prefix   string
	Suffix   string
	CNErr    error
	Binding  error // nil for tenant certs
	Validity error
}

// InspectNodeCert examines the deployment as the agent on machineName would use it at now.
func InspectNodeCert(dir string, poolUUID uuid.UUID, machineName string, now time.Time, skew time.Duration) *NodeCertInspection {
	insp := inspectNodeCertFiles(dir, poolUUID)
	if insp.Certificate != nil {
		insp.checkPolicy(machineName, now, skew)
	}
	return insp
}

func (insp *NodeCertInspection) checkPolicy(machineName string, now time.Time, skew time.Duration) {
	insp.CN = insp.Certificate.Subject.CommonName
	insp.Prefix, insp.Suffix, insp.CNErr = ParsePoolCertCN(insp.CN)
	if insp.CNErr == nil {
		insp.Binding = checkCNBinding(insp.Prefix, insp.Suffix, machineName)
	}
	insp.Validity = checkCertValidity(insp.Certificate, now, skew)
}

// Err is the first problem the agent would hit; not-yet-valid is the server's call.
func (insp *NodeCertInspection) Err() error {
	if err := insp.filesErr(); err != nil {
		return err
	}
	switch {
	case insp.CNErr != nil:
		return insp.CNErr
	case insp.Binding != nil:
		return insp.Binding
	case insp.Validity != nil && !errors.Is(insp.Validity, ErrCertNotYetValid):
		return insp.Validity
	}
	return nil
}

func (insp *NodeCertInspection) filesErr() error {
	for _, f := range []NodeCertFile{insp.Dir, insp.Cert, insp.Key} {
		if f.Err != nil {
			return f.Err
		}
	}
	return nil
}

func statNodeCertFile(path string, wantDir bool) NodeCertFile {
	f := NodeCertFile{Path: path}
	fi, err := os.Stat(path)
	if err != nil {
		f.Err = err
		return f
	}
	if wantDir && !fi.IsDir() {
		f.Err = errors.Errorf("%s is not a directory", path)
	}
	f.Mode = fi.Mode().Perm()
	f.Owner = "unknown"
	if st, ok := fi.Sys().(*syscall.Stat_t); ok {
		f.Owner = fmt.Sprintf("uid %d", st.Uid)
		if u, err := user.LookupId(fmt.Sprintf("%d", st.Uid)); err == nil {
			f.Owner = u.Username
		}
	}
	return f
}

func inspectNodeCertFiles(dir string, poolUUID uuid.UUID) *NodeCertInspection {
	insp := &NodeCertInspection{}
	certPath, keyPath := NodeCertPaths(dir, poolUUID)

	insp.Dir = statNodeCertFile(dir, true)
	if insp.Dir.Err != nil {
		insp.Dir.Err = errors.Wrap(insp.Dir.Err, "node cert directory")
		insp.Cert, insp.Key = NodeCertFile{Path: certPath}, NodeCertFile{Path: keyPath}
		return insp
	}

	insp.Cert = statNodeCertFile(certPath, false)
	if insp.Cert.Err == nil {
		insp.PEM, insp.Cert.Err = LoadPEMData(certPath, MaxCertPerm)
	}
	if insp.Cert.Err == nil {
		insp.Certificate, insp.Cert.Err = parseCertificate(insp.PEM)
	}
	if insp.Cert.Err == nil {
		insp.Cert.Err = checkPoPKeyType(insp.Certificate)
	}
	if insp.Cert.Err != nil {
		insp.Cert.Err = errors.Wrap(insp.Cert.Err, "node certificate")
		insp.Key = NodeCertFile{Path: keyPath}
		return insp
	}

	insp.Key = statNodeCertFile(keyPath, false)
	if insp.Key.Err == nil {
		insp.PrivateKey, insp.Key.Err = LoadPrivateKey(keyPath)
	}
	if insp.Key.Err == nil {
		insp.Key.Err = VerifyCertKey(insp.PrivateKey, insp.Certificate)
	}
	if insp.Key.Err != nil {
		insp.Key.Err = errors.Wrap(insp.Key.Err, "node private key")
	}
	return insp
}

func validateNodeCertForUse(cert *x509.Certificate, poolUUID uuid.UUID, machineName string, now time.Time) error {
	insp := &NodeCertInspection{Certificate: cert}
	insp.checkPolicy(machineName, now, 0)
	return errors.Wrapf(insp.Err(), "node cert for pool %s", poolUUID)
}

// checkCertValidity tolerates skew on NotBefore only.
func checkCertValidity(cert *x509.Certificate, now time.Time, skew time.Duration) error {
	if now.After(cert.NotAfter) {
		return errors.Wrapf(ErrCertInvalid, "expired at %s", cert.NotAfter.Format(time.RFC3339))
	}
	if now.Add(skew).Before(cert.NotBefore) {
		return errors.Wrapf(ErrCertNotYetValid, "notBefore=%s, now=%s",
			cert.NotBefore.Format(time.RFC3339), now.Format(time.RFC3339))
	}
	return nil
}

// VerifyNodeCertChain verifies cert to root through one of the pool's CAs.
func VerifyNodeCertChain(cert, root *x509.Certificate, bundle []byte, now time.Time) error {
	if root == nil {
		return errors.Wrap(ErrInvalidInput, "no trust anchor")
	}
	poolCAs, err := parsePoolCABundle(bundle)
	if err != nil {
		return err
	}
	roots := x509.NewCertPool()
	roots.AddCert(root)
	intermediates := x509.NewCertPool()
	for _, ca := range poolCAs {
		intermediates.AddCert(ca)
	}
	chains, err := cert.Verify(x509.VerifyOptions{
		Roots:         roots,
		Intermediates: intermediates,
		CurrentTime:   now,
		KeyUsages:     []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
	})
	if err != nil {
		return errors.Wrapf(ErrCertInvalid, "chain validation: %s", err)
	}
	// A leaf the DAOS CA signed directly would otherwise verify on every pool.
	for _, chain := range chains {
		for _, ca := range poolCAs {
			if len(chain) > 1 && bytes.Equal(chain[1].Raw, ca.Raw) {
				return nil
			}
		}
	}
	return errors.Wrap(ErrCertInvalid, "chain validation: issuer is not one of the pool's CAs")
}

// CheckCertRevocation treats a cert issued at or before its CN's watermark as revoked.
func CheckCertRevocation(cert *x509.Certificate, cn string, watermarks CertWatermarks) error {
	if wm, ok := watermarks[cn]; ok && !cert.NotBefore.After(wm) {
		return errors.Wrapf(ErrCertRevoked,
			"cert for %q revoked (NotBefore=%s, watermark=%s)", cn,
			cert.NotBefore.Format(time.RFC3339), wm.Format(time.RFC3339))
	}
	return nil
}

// CertAndPoP binds the node cert to handleUUID; machineName must be the AUTH_SYS name revoke-by-CN keys on.
func (l *NodeCertLoader) CertAndPoP(log logging.Logger, poolUUID, handleUUID uuid.UUID, machineName string) (cert *NodeCert, pop, payload []byte, err error) {
	cert, err = l.Load(log, poolUUID)
	if err != nil {
		return nil, nil, nil, err
	}
	if err := validateNodeCertForUse(cert.Cert, poolUUID, machineName, time.Now()); err != nil {
		return nil, nil, nil, err
	}

	payload, err = buildPoPPayload(poolUUID, handleUUID)
	if err != nil {
		return nil, nil, nil, errors.Wrap(err, "building PoP payload")
	}
	pop, err = signPoP(cert.Key, payload)
	if err != nil {
		return nil, nil, nil, errors.Wrap(err, "signing PoP")
	}
	return cert, pop, payload, nil
}

// NodeCertPresentation is what the server holds when a node cert is presented at pool connect.
type NodeCertPresentation struct {
	Root        *x509.Certificate // the DAOS CA
	PoolCA      []byte            // ca_cert property
	Cert        []byte            // PEM
	PoPSig      []byte
	PoPPayload  []byte // marshaled PoPPayload
	PoolUUID    uuid.UUID
	MachineName string // AUTH_SYS machine name
	Watermarks  []byte // cert_watermarks property, may be empty
	MaxSkew     time.Duration
	Now         time.Time
}

// Validate checks the cert and proof-of-possession and returns the parsed payload.
func (p *NodeCertPresentation) Validate() (*PoPPayload, error) {
	cert, err := p.verifyCert()
	if err != nil {
		return nil, err
	}
	return p.verifyProof(cert)
}

func parsePoolCABundle(bundle []byte) ([]*x509.Certificate, error) {
	entries, err := ParseCABundle(bundle)
	if err != nil {
		return nil, errors.Wrapf(ErrCertInvalid, "pool CA bundle: %s", err)
	}
	if len(entries) == 0 {
		return nil, errors.Wrap(ErrCertInvalid, "pool CA bundle contains no certificates")
	}
	cas := make([]*x509.Certificate, len(entries))
	for i, e := range entries {
		cas[i] = e.Cert
	}
	return cas, nil
}

func (p *NodeCertPresentation) verifyCert() (*x509.Certificate, error) {
	cert, err := parseCertificate(p.Cert)
	if err != nil {
		return nil, errors.Wrapf(ErrCertInvalid, "node cert: %s", err)
	}
	if err := checkPoPKeyType(cert); err != nil {
		return nil, err
	}

	if err := checkCertValidity(cert, p.Now, p.MaxSkew); err != nil {
		return nil, err
	}
	if err := VerifyNodeCertChain(cert, p.Root, p.PoolCA, p.Now.Add(p.MaxSkew)); err != nil {
		return nil, err
	}

	cn := cert.Subject.CommonName
	prefix, suffix, err := ParsePoolCertCN(cn)
	if err != nil {
		return nil, errors.Wrapf(ErrCertInvalid, "CN policy: %s", err)
	}
	if err := checkCNBinding(prefix, suffix, p.MachineName); err != nil {
		return nil, err
	}

	if len(p.Watermarks) > 0 {
		watermarks, err := DecodeCertWatermarks(p.Watermarks)
		if err != nil {
			return nil, errors.Wrapf(ErrWatermarksInvalid, "%s", err)
		}
		if err := CheckCertRevocation(cert, cn, watermarks); err != nil {
			return nil, err
		}
	}

	return cert, nil
}

func (p *NodeCertPresentation) verifyProof(cert *x509.Certificate) (*PoPPayload, error) {
	if err := verifyPoP(cert.PublicKey, p.PoPPayload, p.PoPSig); err != nil {
		return nil, errors.Wrapf(ErrPoPInvalid, "signature: %s", err)
	}

	payload, err := parsePoPPayload(p.PoPPayload)
	if err != nil {
		return nil, errors.Wrapf(ErrInvalidInput, "PoP payload: %s", err)
	}

	if payload.PoolID() != p.PoolUUID {
		return nil, errors.Wrap(ErrPoPInvalid,
			"payload pool UUID does not match request pool ID")
	}

	skew := p.Now.Sub(payload.Time())
	if skew < 0 {
		skew = -skew
	}
	if skew > p.MaxSkew {
		return nil, errors.Wrapf(ErrPoPStale,
			"timestamp skew too large: %s (max %s)", skew, p.MaxSkew)
	}

	return payload, nil
}
