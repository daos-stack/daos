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

// NotBeforeSkewTolerance covers clock drift between admin (cert
// issuer) and agent. Also used as the server-side PoP timestamp skew.
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

// Load returns poolID's cached cert+key, refreshing if either file has changed.
func (l *NodeCertLoader) Load(log logging.Logger, poolID string) (*NodeCert, error) {
	poolUUID, err := uuid.Parse(poolID)
	if err != nil {
		return nil, errors.Wrap(err, "parsing pool UUID")
	}

	certPath := filepath.Join(l.dir, poolUUID.String()+".crt")
	keyPath := filepath.Join(l.dir, poolUUID.String()+".key")

	certSig, err := statSig(certPath)
	if err != nil {
		return nil, errors.Wrap(err, "stat node certificate")
	}
	keySig, err := statSig(keyPath)
	if err != nil {
		return nil, errors.Wrap(err, "stat node private key")
	}

	if v, ok := l.cache.Load(poolID); ok {
		c := v.(*cachedNodeCert)
		if c.certSig == certSig && c.keySig == keySig {
			return c.cert, nil
		}
		log.Debugf("node cert file for pool %s changed on disk; reloading", poolID)
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

	nc := &NodeCert{PEM: certPEM, Cert: cert, Key: key}
	l.cache.Store(poolID, &cachedNodeCert{cert: nc, certSig: certSig, keySig: keySig})

	log.Debugf("loaded node cert for pool %s: CN=%s, expires=%s",
		poolID, cert.Subject.CommonName, cert.NotAfter.Format(time.RFC3339))

	return nc, nil
}
