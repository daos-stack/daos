//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package security

import (
	"crypto"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

// NotBeforeSkewTolerance is the maximum allowed clock skew for a
// node cert's NotBefore time. Also the default server-side PoP
// timestamp skew (pool_cert_max_clock_skew overrides).
const NotBeforeSkewTolerance = 5 * time.Minute

// NodeCert is a loaded per-pool node certificate and key.
type NodeCert struct {
	PEM  []byte
	Cert *x509.Certificate
	Key  crypto.PrivateKey
}

// NodeCertLoader loads per-pool node certs from disk; refreshes on
// mtime/size change so revoked-and-reissued certs land without a restart.
type NodeCertLoader struct {
	dir   string
	cache sync.Map
}

// NewNodeCertLoader returns a loader that reads <pool_uuid>.{crt,key} from dir.
func NewNodeCertLoader(dir string) *NodeCertLoader {
	return &NodeCertLoader{dir: dir}
}

// NodeCertPaths returns the paths of a pool's node certificate and key in dir.
func NodeCertPaths(dir string, poolUUID uuid.UUID) (certPath, keyPath string) {
	return filepath.Join(dir, poolUUID.String()+".crt"), filepath.Join(dir, poolUUID.String()+".key")
}

// PoolCAPaths returns the paths of a pool's CA certificate and key in dir.
func PoolCAPaths(dir string, poolUUID uuid.UUID) (certPath, keyPath string) {
	return filepath.Join(dir, poolUUID.String()+"_ca.crt"), filepath.Join(dir, poolUUID.String()+"_ca.key")
}

// DefaultPoolCADir returns the default pool CA directory: pools/ beside the
// admin's private key.
func DefaultPoolCADir(adminKeyPath string) string {
	return filepath.Join(filepath.Dir(adminKeyPath), "pools")
}

// DefaultDAOSCAKeyPath returns the DAOS CA private key path implied by the
// DAOS CA certificate path.
func DefaultDAOSCAKeyPath(caCertPath string) string {
	return filepath.Join(filepath.Dir(caCertPath), "daosCA.key")
}

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

// Load returns the cert+key struct for poolUUID. If a struct was previously
// loaded and the files have not changed, it is returned from cache.
func (l *NodeCertLoader) Load(log logging.Logger, poolUUID uuid.UUID) (*NodeCert, error) {
	uuidStr := poolUUID.String()
	certPath, keyPath := NodeCertPaths(l.dir, poolUUID)

	certSig, err := statSig(certPath)
	if err != nil {
		return nil, errors.Wrap(err, "stat node certificate")
	}
	keySig, err := statSig(keyPath)
	if err != nil {
		return nil, errors.Wrap(err, "stat node private key")
	}

	if v, ok := l.cache.Load(uuidStr); ok {
		c := v.(*cachedNodeCert)
		if c.certSig == certSig && c.keySig == keySig {
			return c.cert, nil
		}
		log.Debugf("node cert file for pool %s changed on disk; reloading", uuidStr)
	}

	certPEM, err := LoadPEMData(certPath, MaxCertPerm)
	if err != nil {
		return nil, errors.Wrap(err, "loading node certificate")
	}
	block, _ := pem.Decode(certPEM)
	if block == nil {
		return nil, fmt.Errorf("invalid PEM data in %s", certPath)
	}
	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		return nil, errors.Wrap(err, "parsing node certificate")
	}
	key, err := LoadPrivateKey(keyPath)
	if err != nil {
		return nil, errors.Wrap(err, "loading node private key")
	}
	if err := KeyMatchesCert(key, cert); err != nil {
		return nil, errors.Wrapf(err, "%s vs %s", keyPath, certPath)
	}

	nc := &NodeCert{PEM: certPEM, Cert: cert, Key: key}
	l.cache.Store(uuidStr, &cachedNodeCert{cert: nc, certSig: certSig, keySig: keySig})

	log.Debugf("loaded node cert for pool %s: CN=%s, expires=%s",
		uuidStr, cert.Subject.CommonName, cert.NotAfter.Format(time.RFC3339))

	return nc, nil
}

// KeyMatchesCert reports whether key is the private half of cert's public key.
func KeyMatchesCert(key crypto.PrivateKey, cert *x509.Certificate) error {
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

// validateNodeCertForUse checks that a loaded cert is currently valid and,
// for node-scoped certs, that its CN matches machineName.
func validateNodeCertForUse(cert *x509.Certificate, poolID, machineName string, now time.Time) error {
	prefix, suffix, err := ValidatePoolCertCN(cert.Subject.CommonName)
	if err != nil {
		return errors.Wrapf(err, "node cert for pool %s", poolID)
	}
	if err := CheckCNBinding(prefix, suffix, machineName); err != nil {
		return errors.Wrapf(err, "node cert for pool %s", poolID)
	}
	if err := CheckValidity(cert, now, NotBeforeSkewTolerance); err != nil {
		return errors.Wrapf(err, "node cert for pool %s", poolID)
	}
	return nil
}

// CheckValidity reports whether cert is within its validity window at now,
// tolerating skew on NotBefore.
func CheckValidity(cert *x509.Certificate, now time.Time, skew time.Duration) error {
	if now.After(cert.NotAfter) {
		return errors.Wrapf(ErrCertInvalid, "expired at %s", cert.NotAfter.Format(time.RFC3339))
	}
	if now.Add(skew).Before(cert.NotBefore) {
		return errors.Wrapf(ErrCertInvalid, "not yet valid (notBefore=%s, now=%s)",
			cert.NotBefore.Format(time.RFC3339), now.Format(time.RFC3339))
	}
	return nil
}

// CertExpiryWarnWindow is how far ahead of NotAfter expiry is reported.
const CertExpiryWarnWindow = 30 * 24 * time.Hour

// ExpiryWarning describes a certificate that is expired or expires within
// window of now; it returns "" otherwise.
func ExpiryWarning(what string, cert *x509.Certificate, now time.Time, window time.Duration) string {
	left := cert.NotAfter.Sub(now)
	switch {
	case left < 0:
		return fmt.Sprintf("%s expired on %s", what, cert.NotAfter.Format("2006-01-02"))
	case left < window:
		return fmt.Sprintf("%s expires in %d day(s), on %s", what,
			int(left.Hours()/24), cert.NotAfter.Format("2006-01-02"))
	}
	return ""
}

// VerifyNodeCertChain verifies cert against the DAOS root through the pool's
// CA bundle.
func VerifyNodeCertChain(cert, root *x509.Certificate, bundle []byte, now time.Time) error {
	if root == nil {
		return errors.Wrap(ErrInvalidInput, "no trust anchor")
	}
	intermediates, err := parsePoolCABundle(bundle)
	if err != nil {
		return err
	}
	roots := x509.NewCertPool()
	roots.AddCert(root)
	if _, err := cert.Verify(x509.VerifyOptions{
		Roots:         roots,
		Intermediates: intermediates,
		CurrentTime:   now,
		KeyUsages:     []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
	}); err != nil {
		return errors.Wrapf(ErrCertInvalid, "chain validation: %s", err)
	}
	return nil
}

// CheckRevocation reports whether the pool's watermark for cn covers cert.
func CheckRevocation(cert *x509.Certificate, cn string, watermarks CertWatermarks) error {
	if wm, ok := watermarks[cn]; ok && !cert.NotBefore.After(wm) {
		return errors.Wrapf(ErrCertRevoked,
			"cert for %q revoked (NotBefore=%s, watermark=%s)", cn,
			cert.NotBefore.Format(time.RFC3339), wm.Format(time.RFC3339))
	}
	return nil
}

// CertAndPoP loads the pool's node cert and signs a fresh proof-of-possession
// binding it to the given handle. machineName must come from the same source
// as the AUTH_SYS machine name; revoke-by-CN depends on it.
func (l *NodeCertLoader) CertAndPoP(log logging.Logger, poolUUID, handleUUID uuid.UUID, machineName string) (cert *NodeCert, pop, payload []byte, err error) {
	cert, err = l.Load(log, poolUUID)
	if err != nil {
		return nil, nil, nil, err
	}
	if err := validateNodeCertForUse(cert.Cert, poolUUID.String(), machineName, time.Now()); err != nil {
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

// Sentinel errors classifying NodeCertPresentation failures. Callers map
// these to wire statuses; the details ride along via error wrapping.
var (
	// ErrCertInvalid covers certs that can't be parsed, don't chain to
	// the root, or violate CN policy.
	ErrCertInvalid = errors.New("node cert invalid")
	// ErrCertRevoked covers certs at or below their revocation watermark.
	ErrCertRevoked = errors.New("node cert revoked")
	// ErrInvalidInput covers malformed request fields.
	ErrInvalidInput = errors.New("invalid input")
	// ErrPoPInvalid covers payload binding and signature failures.
	ErrPoPInvalid = errors.New("proof-of-possession invalid")
	// ErrPoPStale covers timestamps outside the allowed clock skew.
	ErrPoPStale = errors.New("proof-of-possession stale")
)

// NodeCertPresentation bundles everything a server knows when a node cert is
// presented at pool connect.
type NodeCertPresentation struct {
	Root        *x509.Certificate // trust anchor (the DAOS CA)
	PoolCA      []byte            // PEM bundle from the pool_ca property
	Cert        []byte            // PEM, as presented
	PoPSig      []byte            // signature over PoPPayload
	PoPPayload  []byte            // raw marshaled PoPPayload bytes
	PoolUUID    uuid.UUID         // pool being connected to
	MachineName string            // client's AUTH_SYS machine name
	Watermarks  []byte            // cert_watermarks property, may be empty
	MaxSkew     time.Duration     // tolerate clock skew on PoP timestamps; zero defaults to NotBeforeSkewTolerance
	Now         time.Time         // current time for validation; zero defaults to time.Now()
}

// CheckCNBinding enforces what a CN prefix binds the cert to; unknown
// prefixes are rejected.
func CheckCNBinding(prefix, suffix, machineName string) error {
	cn := prefix + suffix
	switch prefix {
	case CertCNPrefixNode:
		if machineName == "" {
			return errors.Wrapf(ErrInvalidInput,
				"cert CN %q is node-scoped but machine name is empty", cn)
		}
		if suffix != machineName {
			return errors.Wrapf(ErrCertInvalid,
				"cert CN %q does not match machine name %q", cn, machineName)
		}
	case CertCNPrefixTenant:
		// Deliberately unbound: one cert is shared by every machine in the tenant.
	default:
		return errors.Wrapf(ErrInvalidInput, "cert CN %q has unsupported prefix %q", cn, prefix)
	}
	return nil
}

// Validate checks the presented cert and proof-of-possession, returning
// the parsed payload.
func (p *NodeCertPresentation) Validate() (*PoPPayload, error) {
	now := p.Now
	if now.IsZero() {
		now = time.Now()
	}
	maxSkew := p.MaxSkew
	if maxSkew <= 0 {
		maxSkew = NotBeforeSkewTolerance
	}

	cert, err := p.verifyCert(now)
	if err != nil {
		return nil, err
	}
	return p.verifyProof(cert, now, maxSkew)
}

// parsePoolCABundle parses a PEM bundle into a pool of intermediate CAs.
func parsePoolCABundle(bundle []byte) (*x509.CertPool, error) {
	intermediates := x509.NewCertPool()
	nCAs := 0
	for rest := bundle; len(rest) > 0; {
		var caBlock *pem.Block
		caBlock, rest = pem.Decode(rest)
		if caBlock == nil {
			break
		}
		if caBlock.Type != "CERTIFICATE" {
			return nil, errors.Wrapf(ErrCertInvalid,
				"unexpected PEM block type %q in pool CA bundle", caBlock.Type)
		}
		caCert, err := x509.ParseCertificate(caBlock.Bytes)
		if err != nil {
			return nil, errors.Wrapf(ErrCertInvalid, "malformed cert in pool CA bundle: %s", err)
		}
		intermediates.AddCert(caCert)
		nCAs++
	}
	if nCAs == 0 {
		return nil, errors.Wrap(ErrCertInvalid, "pool CA bundle contains no certificates")
	}
	return intermediates, nil
}

// verifyCert checks that the presented cert is acceptable
func (p *NodeCertPresentation) verifyCert(now time.Time) (*x509.Certificate, error) {
	block, _ := pem.Decode(p.Cert)
	if block == nil {
		return nil, errors.Wrap(ErrCertInvalid, "no PEM data in node cert")
	}
	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		return nil, errors.Wrapf(ErrCertInvalid, "parsing node cert: %s", err)
	}

	if err := VerifyNodeCertChain(cert, p.Root, p.PoolCA, now); err != nil {
		return nil, err
	}

	cn := cert.Subject.CommonName
	prefix, suffix, err := ValidatePoolCertCN(cn)
	if err != nil {
		return nil, errors.Wrapf(ErrCertInvalid, "CN policy: %s", err)
	}
	if err := CheckCNBinding(prefix, suffix, p.MachineName); err != nil {
		return nil, err
	}

	if len(p.Watermarks) > 0 {
		watermarks, err := DecodeCertWatermarks(p.Watermarks)
		if err != nil {
			return nil, errors.Wrapf(ErrInvalidInput, "cert watermarks: %s", err)
		}
		if err := CheckRevocation(cert, cn, watermarks); err != nil {
			return nil, err
		}
	}

	return cert, nil
}

// verifyProof checks that the presenter holds the cert key, that the
// proof is fresh, and that it's bound to the requested pool. The signature
// is verified over the raw payload bytes before they are parsed.
func (p *NodeCertPresentation) verifyProof(cert *x509.Certificate, now time.Time, maxSkew time.Duration) (*PoPPayload, error) {
	if err := verifyPoP(cert.PublicKey, p.PoPPayload, p.PoPSig); err != nil {
		return nil, errors.Wrapf(ErrPoPInvalid, "signature: %s", err)
	}

	payload, err := parsePoPPayload(p.PoPPayload)
	if err != nil {
		return nil, errors.Wrapf(ErrInvalidInput, "PoP payload: %s", err)
	}

	// Cross-pool replay defense: a node cert shared across pools must not
	// let a PoP captured for pool A authenticate a connect to pool B.
	if payload.PoolID() != p.PoolUUID {
		return nil, errors.Wrap(ErrPoPInvalid,
			"payload pool UUID does not match request pool ID")
	}

	skew := now.Sub(payload.Time())
	if skew < 0 {
		skew = -skew
	}
	if skew > maxSkew {
		return nil, errors.Wrapf(ErrPoPStale,
			"timestamp skew too large: %s (max %s)", skew, maxSkew)
	}

	return payload, nil
}
