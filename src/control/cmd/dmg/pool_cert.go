//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"crypto"
	"crypto/x509"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

// poolNodeAuthCmd groups the per-pool node authentication verbs.
type poolNodeAuthCmd struct {
	Enable       poolNodeAuthEnableCmd       `command:"enable" description:"Enable node authentication on a pool"`
	GenerateCA   poolNodeAuthGenerateCACmd   `command:"generate-ca" description:"Generate a pool CA key pair offline (without network access to the MS)"`
	Disable      poolNodeAuthDisableCmd      `command:"disable" description:"Disable node authentication on a pool"`
	Status       poolNodeAuthStatusCmd       `command:"status" description:"Show CAs and revocations for a pool"`
	Issue        poolNodeAuthIssueCmd        `command:"issue" description:"Issue node or tenant certificates for a pool"`
	GenerateCert poolNodeAuthGenerateCertCmd `command:"generate-cert" description:"Mint node or tenant certificates offline (without network access to the MS)"`
	Revoke       poolNodeAuthRevokeCmd       `command:"revoke" description:"Revoke a node or tenant identity"`
	AddCA        poolNodeAuthAddCACmd        `command:"add-ca" description:"Add a CA to the pool's bundle (rotation)"`
	RemoveCA     poolNodeAuthRemoveCACmd     `command:"remove-ca" description:"Remove a CA from the pool's bundle by fingerprint"`
}

func fileExists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

// basePoolCACmd is shared by enable and add-ca.
type basePoolCACmd struct {
	poolCmd
	CAKey    string       `long:"daos-ca-key" description:"Path to DAOS CA private key (generate mode; default: daosCA.key beside the DAOS CA certificate)"`
	Output   string       `long:"output" short:"o" description:"Directory for the generated pool CA key pair (default: pools/ under the dmg certificate directory)"`
	Cert     string       `long:"cert" description:"Path to pre-existing pool CA certificate (import mode)"`
	Validity validityFlag `long:"validity" description:"Pool CA lifetime, e.g. 90d, 26w, 2y (generate mode; default: until the DAOS CA expires)"`
}

// caNotAfter returns the expiry date of a PEM CA certificate.
func caNotAfter(certPEM []byte) (string, error) {
	cert, err := security.ParsePoolCACert(certPEM)
	if err != nil {
		return "", err
	}
	return cert.NotAfter.Format(time.RFC3339), nil
}

// validityFlag is a certificate lifetime in days, weeks, or years (90d, 26w, 2y).
type validityFlag struct {
	time.Duration
}

func (f *validityFlag) UnmarshalFlag(fv string) error {
	units := map[byte]time.Duration{'d': 24 * time.Hour, 'w': 7 * 24 * time.Hour, 'y': 365 * 24 * time.Hour}
	if len(fv) < 2 {
		return errors.Errorf("invalid lifetime %q: use <n>d, <n>w, or <n>y", fv)
	}
	unit, ok := units[fv[len(fv)-1]]
	n, err := strconv.ParseUint(fv[:len(fv)-1], 10, 32)
	if !ok || err != nil || n == 0 {
		return errors.Errorf("invalid lifetime %q: use <n>d, <n>w, or <n>y", fv)
	}
	f.Duration = time.Duration(n) * unit
	return nil
}

// poolCADir returns the default pool CA key-pair directory.
func poolCADir(cfg *control.Config) (string, error) {
	if cfg == nil {
		return "", errors.New("no configuration loaded")
	}
	if cfg.TransportConfig == nil || cfg.TransportConfig.PrivateKeyPath == "" {
		return "", errors.New("transport_config.key is not set")
	}
	return security.DefaultPoolCADir(cfg.TransportConfig.PrivateKeyPath), nil
}

// ensureWritableDir creates the specified directory if needed and verifies that it is writable.
func ensureWritableDir(dir string) error {
	if err := os.MkdirAll(dir, 0700); err != nil {
		return err
	}
	probe, err := os.CreateTemp(dir, ".dmg-probe-*")
	if err != nil {
		return err
	}
	probe.Close()
	return os.Remove(probe.Name())
}

// daosCACertPath returns the configured DAOS CA certificate path.
func daosCACertPath(cfg *control.Config) (string, error) {
	if cfg == nil {
		return "", errors.New("no configuration loaded")
	}
	if cfg.TransportConfig == nil || cfg.TransportConfig.CARootPath == "" {
		return "", errors.New("transport_config.ca_cert is not set")
	}
	return cfg.TransportConfig.CARootPath, nil
}

func (cmd *basePoolCACmd) getDaosCACertPath() (string, error) {
	return daosCACertPath(cmd.config)
}

// writePoolCA writes the pool CA key pair to the given paths.
func writePoolCA(certPath, keyPath string, ca *security.IssuedCert) error {
	dir := filepath.Dir(keyPath)
	if err := ensureWritableDir(dir); err != nil {
		return errors.Wrapf(err, "creating pool CA directory %s (pass --output to use another)", dir)
	}
	return errors.Wrap(ca.Write(certPath, keyPath), "writing pool CA (pass --output to use another directory)")
}

// stagedSuffix marks a generated key pair that is not yet installed, so a
// failed install never clobbers the pair an existing bundle depends on.
const stagedSuffix = ".new"

// commitPoolCA moves a staged key pair into place.
func commitPoolCA(certPath, keyPath string) error {
	for _, p := range []string{keyPath, certPath} {
		if err := os.Rename(p+stagedSuffix, p); err != nil {
			return errors.Wrapf(err, "moving %s into place", p)
		}
	}
	return nil
}

// caInstalled reports whether the pool's bundle contains cert.
func (cmd *basePoolCACmd) caInstalled(ctx context.Context, id string, cert *x509.Certificate) (bool, error) {
	state, err := control.GetPoolNodeAuthState(ctx, cmd.ctlInvoker, id)
	if err != nil {
		return false, err
	}
	return state.Installed(cert), nil
}

// installedCA is what enable and add-ca report.
type installedCA struct {
	PoolUUID       uuid.UUID
	CertPEM        []byte
	HandlesEvicted int32
	CertPath       string // generate mode only
	KeyPath        string
}

// installCA generates (writing the key pair to Output) or imports a pool CA
// and installs it.
func (cmd *basePoolCACmd) installCA(appendCA, noEvict bool) (*installedCA, error) {
	ctx := cmd.MustLogCtx()
	id := cmd.PoolID().String()

	if cmd.Cert != "" {
		// Import mode
		if cmd.CAKey != "" || cmd.Output != "" || cmd.Validity.Duration != 0 {
			return nil, errors.New("--cert is mutually exclusive with --daos-ca-key, --output and --validity")
		}
		certPEM, err := os.ReadFile(cmd.Cert)
		if err != nil {
			return nil, errors.Wrap(err, "reading pool CA certificate")
		}
		resp, err := control.PoolAddCA(ctx, cmd.ctlInvoker,
			&control.PoolAddCAReq{ID: id, CertPEM: certPEM, Append: appendCA, NoEvict: noEvict})
		if err != nil {
			return nil, err
		}
		return &installedCA{PoolUUID: resp.PoolUUID, CertPEM: certPEM, HandlesEvicted: resp.HandlesEvicted}, nil
	}

	// Generate mode
	daosCACert, err := cmd.getDaosCACertPath()
	if err != nil {
		return nil, err
	}
	if cmd.CAKey == "" {
		cmd.CAKey = security.DefaultDAOSCAKeyPath(daosCACert)
		if _, sErr := os.Stat(cmd.CAKey); sErr != nil {
			return nil, errors.Wrapf(sErr,
				"DAOS CA key not found at its default location (beside the DAOS CA certificate); "+
					"pass --daos-ca-key if it is kept elsewhere, or --cert to import a pool CA")
		}
	}
	if cmd.Output == "" {
		dir, err := poolCADir(cmd.config)
		if err != nil {
			return nil, err
		}
		cmd.Output = dir
	}

	caResp, err := control.PoolGetCA(ctx, cmd.ctlInvoker, &control.PoolGetCAReq{ID: id})
	if err != nil {
		return nil, errors.Wrap(err, "resolving pool")
	}

	rootCert, rootKey, err := security.LoadCertAndKey(daosCACert, cmd.CAKey)
	if err != nil {
		return nil, errors.Wrap(err, "loading DAOS CA")
	}
	ca, err := security.GeneratePoolCA(caResp.PoolUUID, rootCert, rootKey, cmd.Validity.Duration)
	if err != nil {
		return nil, err
	}

	certPath, keyPath := security.PoolCAPaths(cmd.Output, caResp.PoolUUID)
	if err := writePoolCA(certPath+stagedSuffix, keyPath+stagedSuffix, ca); err != nil {
		return nil, err
	}

	resp, err := control.PoolAddCA(ctx, cmd.ctlInvoker,
		&control.PoolAddCAReq{ID: id, CertPEM: ca.CertPEM, Append: appendCA, NoEvict: noEvict})
	if err != nil {
		installed, chkErr := cmd.caInstalled(ctx, id, ca.Cert)
		switch {
		case chkErr != nil:
			return nil, errors.Wrapf(err, "install state unknown (%s); key pair kept at %s%s",
				chkErr, keyPath, stagedSuffix)
		case installed:
			if cErr := commitPoolCA(certPath, keyPath); cErr != nil {
				return nil, errors.Wrapf(err, "CA is installed; %s", cErr)
			}
			return nil, errors.Wrapf(err, "CA is installed; key pair kept at %s", keyPath)
		}
		os.Remove(keyPath + stagedSuffix)
		os.Remove(certPath + stagedSuffix)
		return nil, err
	}
	if err := commitPoolCA(certPath, keyPath); err != nil {
		return nil, errors.Wrap(err, "CA is installed")
	}

	return &installedCA{
		PoolUUID:       resp.PoolUUID,
		CertPEM:        ca.CertPEM,
		HandlesEvicted: resp.HandlesEvicted,
		CertPath:       certPath,
		KeyPath:        keyPath,
	}, nil
}

type caResult struct {
	PoolUUID       uuid.UUID `json:"pool_uuid"`
	CertPath       string    `json:"cert_path,omitempty"`
	KeyPath        string    `json:"key_path,omitempty"`
	NotAfter       string    `json:"not_after"`
	HandlesEvicted int32     `json:"handles_evicted"`
}

// poolNodeAuthEnableCmd enables node authentication by installing the
// pool's first CA.
type poolNodeAuthEnableCmd struct {
	basePoolCACmd
	NoEvict bool `long:"no-evict" description:"Keep existing pool handles open; by default they are evicted so every client proves access"`
}

func (cmd *poolNodeAuthEnableCmd) Execute(args []string) error {
	var result caResult
	err := func() error {
		ca, err := cmd.installCA(false, cmd.NoEvict)
		if errors.Is(err, control.ErrNodeAuthEnabled) {
			return errors.Wrap(err, "use add-ca/remove-ca to rotate, or disable to start over")
		} else if err != nil {
			return err
		}
		notAfter, err := caNotAfter(ca.CertPEM)
		if err != nil {
			return err
		}
		result = caResult{PoolUUID: ca.PoolUUID, CertPath: ca.CertPath, KeyPath: ca.KeyPath, NotAfter: notAfter,
			HandlesEvicted: ca.HandlesEvicted}
		return nil
	}()

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, err)
	}
	if err != nil {
		return err
	}

	if result.CertPath != "" {
		cmd.Infof("Pool CA written to %s and %s", result.CertPath, result.KeyPath)
	}
	cmd.Infof("Node authentication enabled on %s", cmd.PoolID().String())
	if cmd.NoEvict {
		cmd.Infof("Existing handles kept (--no-evict); they need no certificate until they close")
	} else {
		cmd.Infof("Handles evicted: %d", result.HandlesEvicted)
	}
	cmd.Infof("Pool CA valid until %s; rotate before then (add-ca, reissue, remove-ca)", result.NotAfter)
	return nil
}

// poolNodeAuthGenerateCACmd is used to generate a pool CA key pair offline
// (i.e. without network access to the MS). The result is staged in the --output
// directory, and can be installed with `dmg pool node-auth enable <pool> --cert <cert>`.
type poolNodeAuthGenerateCACmd struct {
	baseCmd
	cfgCmd
	cmdutil.JSONOutputCmd

	Args struct {
		Pool PoolID `positional-arg-name:"<pool UUID>" required:"1"`
	} `positional-args:"yes"`
	DaosCACert string       `long:"daos-ca-cert" description:"Path to DAOS CA certificate (default: transport_config.ca_cert)"`
	CAKey      string       `long:"daos-ca-key" description:"Path to DAOS CA private key (default: daosCA.key beside the DAOS CA certificate)"`
	Output     string       `long:"output" short:"o" description:"Directory for the pool CA key pair (default: pools/ under the dmg certificate directory)"`
	Validity   validityFlag `long:"validity" description:"Pool CA lifetime, e.g. 90d, 26w, 2y (default: until the DAOS CA expires)"`
}

func (cmd *poolNodeAuthGenerateCACmd) Execute(args []string) error {
	var result caResult
	err := func() error {
		if !cmd.Args.Pool.HasUUID() {
			return errors.New("generate-ca takes the pool UUID; a label cannot be resolved without the system")
		}
		poolUUID := cmd.Args.Pool.UUID
		if cmd.DaosCACert == "" {
			p, err := daosCACertPath(cmd.config)
			if err != nil {
				return err
			}
			cmd.DaosCACert = p
		}
		if cmd.CAKey == "" {
			cmd.CAKey = security.DefaultDAOSCAKeyPath(cmd.DaosCACert)
		}
		if cmd.Output == "" {
			dir, err := poolCADir(cmd.config)
			if err != nil {
				return err
			}
			cmd.Output = dir
		}
		// Never overwrite a key pair; issue may depend on it.
		if _, keyPath := security.PoolCAPaths(cmd.Output, poolUUID); fileExists(keyPath) {
			return errors.Errorf("pool CA key already exists at %s (pass --output to use another directory)", keyPath)
		}

		rootCert, rootKey, err := security.LoadCertAndKey(cmd.DaosCACert, cmd.CAKey)
		if err != nil {
			return errors.Wrap(err, "loading DAOS CA")
		}
		ca, err := security.GeneratePoolCA(poolUUID, rootCert, rootKey, cmd.Validity.Duration)
		if err != nil {
			return err
		}
		certPath, keyPath := security.PoolCAPaths(cmd.Output, poolUUID)
		if err := writePoolCA(certPath, keyPath, ca); err != nil {
			return err
		}
		result = caResult{PoolUUID: poolUUID, CertPath: certPath, KeyPath: keyPath,
			NotAfter: ca.Cert.NotAfter.Format(time.RFC3339)}
		return nil
	}()

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, err)
	}
	if err != nil {
		return err
	}
	cmd.Infof("Pool CA written to %s and %s", result.CertPath, result.KeyPath)
	cmd.Infof("Pool CA valid until %s", result.NotAfter)
	cmd.Infof("Install it with: dmg pool node-auth enable %s --cert %s", result.PoolUUID, result.CertPath)
	return nil
}

// poolNodeAuthAddCACmd appends a CA to an existing bundle (rotation).
type poolNodeAuthAddCACmd struct {
	basePoolCACmd
}

func (cmd *poolNodeAuthAddCACmd) Execute(args []string) error {
	ca, err := cmd.installCA(true, false)

	var result caResult
	if err == nil {
		var notAfter string
		notAfter, err = caNotAfter(ca.CertPEM)
		result = caResult{PoolUUID: ca.PoolUUID, CertPath: ca.CertPath, KeyPath: ca.KeyPath, NotAfter: notAfter}
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, err)
	}
	if err != nil {
		return err
	}

	if result.CertPath != "" {
		cmd.Infof("Pool CA written to %s and %s", result.CertPath, result.KeyPath)
	}
	cmd.Infof("New pool CA valid until %s", result.NotAfter)
	cmd.Infof("CA added to pool bundle; reissue certificates from the new CA, then remove-ca the old fingerprint")
	return nil
}

// poolNodeAuthDisableCmd removes all CAs, disabling node authentication.
type poolNodeAuthDisableCmd struct {
	poolCmd
}

func (cmd *poolNodeAuthDisableCmd) Execute(args []string) error {
	resp, err := control.PoolRemoveCA(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolRemoveCAReq{
			ID:  cmd.PoolID().String(),
			All: true,
		})

	type disableResult struct {
		CertsRemoved int    `json:"certs_removed"`
		Status       string `json:"status"`
	}
	var result disableResult
	if err == nil {
		result = disableResult{
			CertsRemoved: resp.CertsRemoved,
			Status:       "node authentication disabled",
		}
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, err)
	}
	if err != nil {
		return err
	}

	cmd.Infof("Node authentication disabled on %s (%d CA(s) removed)",
		cmd.PoolID().String(), resp.CertsRemoved)
	return nil
}

// poolNodeAuthRemoveCACmd removes a single CA from the bundle by fingerprint.
type poolNodeAuthRemoveCACmd struct {
	poolCmd
	Fingerprint string `long:"fingerprint" required:"1" description:"SHA-256 fingerprint of CA to remove (hex)"`
}

func (cmd *poolNodeAuthRemoveCACmd) Execute(args []string) error {
	resp, err := control.PoolRemoveCA(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolRemoveCAReq{
			ID:          cmd.PoolID().String(),
			Fingerprint: cmd.Fingerprint,
		})

	type removeCAResult struct {
		CertsRemoved int `json:"certs_removed"`
	}
	var result removeCAResult
	if err == nil {
		result = removeCAResult{CertsRemoved: resp.CertsRemoved}
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, err)
	}
	if err != nil {
		return err
	}

	cmd.Infof("Removed %d CA(s) from bundle; certificates issued by them are no longer accepted",
		resp.CertsRemoved)
	return nil
}

// poolNodeAuthStatusCmd shows the pool's CA bundle and revocations in one view.
type poolNodeAuthStatusCmd struct {
	poolCmd
}

func (cmd *poolNodeAuthStatusCmd) Execute(args []string) error {
	state, err := control.GetPoolNodeAuthState(cmd.MustLogCtx(), cmd.ctlInvoker, cmd.PoolID().String())
	if err != nil {
		return err
	}
	watermarks := state.Watermarks

	type poolCertInfo struct {
		Subject     string `json:"subject"`
		Issuer      string `json:"issuer"`
		NotBefore   string `json:"not_before"`
		NotAfter    string `json:"not_after"`
		Fingerprint string `json:"fingerprint"`
	}
	type statusResult struct {
		Enabled      bool              `json:"enabled"`
		Certificates []poolCertInfo    `json:"certificates"`
		Revocations  map[string]string `json:"revocations"`
	}
	result := statusResult{
		Enabled:     state.Enabled(),
		Revocations: make(map[string]string, len(watermarks)),
	}
	for _, e := range state.CAs {
		result.Certificates = append(result.Certificates, poolCertInfo{
			Subject:     e.Cert.Subject.String(),
			Issuer:      e.Cert.Issuer.String(),
			NotBefore:   e.Cert.NotBefore.Format(time.RFC3339),
			NotAfter:    e.Cert.NotAfter.Format(time.RFC3339),
			Fingerprint: e.Fingerprint,
		})
	}
	for cn, wm := range watermarks {
		result.Revocations[cn] = wm.Format(time.RFC3339)
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, nil)
	}

	if !result.Enabled {
		cmd.Infof("Node authentication: disabled")
		return nil
	}

	cmd.Infof("Node authentication: enabled")
	now := time.Now()
	for i, ci := range result.Certificates {
		cmd.Infof("CA Certificate [%d]:", i)
		cmd.Infof("  Subject:     %s", ci.Subject)
		cmd.Infof("  Issuer:      %s", ci.Issuer)
		cmd.Infof("  Not Before:  %s", ci.NotBefore)
		cmd.Infof("  Not After:   %s", ci.NotAfter)
		cmd.Infof("  Fingerprint: %s", ci.Fingerprint)
		if w := cmdutil.CertExpiryWarning("CA", state.CAs[i].Cert.NotAfter, now); w != "" {
			cmd.Infof("  WARNING:     %s — rotate (add-ca, reissue, remove-ca)", w)
		}
	}

	if len(watermarks) == 0 {
		cmd.Infof("Revocations: none")
		return nil
	}
	cns := make([]string, 0, len(watermarks))
	for cn := range watermarks {
		cns = append(cns, cn)
	}
	sort.Strings(cns)
	cmd.Infof("Revocations:")
	for _, cn := range cns {
		cmd.Infof("  %s  %s", cn, watermarks[cn].Format(time.RFC3339))
	}
	return nil
}

// poolCAFlags locate the pool CA key pair that issue and generate-cert sign with.
type poolCAFlags struct {
	CACert string `long:"pool-ca-cert" description:"Path to pool CA certificate (default: <pool_uuid>_ca.crt beside the key)"`
	CAKey  string `long:"pool-ca-key" description:"Path to pool CA private key (default: <pool_uuid>_ca.key under pools/ in the dmg certificate directory)"`
}

// load resolves the defaults and loads the pool CA.
func (f *poolCAFlags) load(cfg *control.Config, poolUUID uuid.UUID) (*x509.Certificate, crypto.PrivateKey, error) {
	if f.CAKey == "" {
		dir, err := poolCADir(cfg)
		if err != nil {
			return nil, nil, err
		}
		_, f.CAKey = security.PoolCAPaths(dir, poolUUID)
		if _, err := os.Stat(f.CAKey); err != nil {
			return nil, nil, errors.Wrapf(err,
				"pool CA key not found at its default location (written there by node-auth enable on this host); "+
					"pass --pool-ca-key if it is kept elsewhere")
		}
	}
	if f.CACert == "" {
		f.CACert, _ = security.PoolCAPaths(filepath.Dir(f.CAKey), poolUUID)
	}
	cert, key, err := security.LoadCertAndKey(f.CACert, f.CAKey)
	if err != nil {
		return nil, nil, errors.Wrap(err, "loading pool CA")
	}
	return cert, key, nil
}

// poolNodeAuthIssueCmd issues certificates signed by the pool's CA for
// --node or --tenant identities.
type poolNodeAuthIssueCmd struct {
	poolCmd
	poolCAFlags
	Nodes    []string     `long:"node" description:"Node name(s) to generate certs for"`
	Tenants  []string     `long:"tenant" description:"Tenant name(s) to generate certs for"`
	Output   string       `long:"output" short:"o" description:"Directory for the issued certificates, one subdirectory per name (default: <pool_uuid>/ under pools/ in the dmg certificate directory)"`
	Replace  bool         `long:"replace" description:"Rotate: revoke the node's existing certificate before issuing (node certs only)"`
	Validity validityFlag `long:"validity" description:"Certificate lifetime, e.g. 90d, 26w, 2y (default: 1y; never past the pool CA's expiry)"`
}

// stagedNodeCerts returns the requested nodes whose staged certificate is
// still live (not covered by the pool's watermark). Tenants are never reported.
func (cmd *poolNodeAuthIssueCmd) stagedNodeCerts(poolUUID uuid.UUID, watermarks security.CertWatermarks) []string {
	var found []string
	for _, name := range cmd.Nodes {
		path, _ := security.NodeCertPaths(filepath.Join(cmd.Output, name), poolUUID)
		if _, err := os.Stat(path); err != nil {
			continue
		}
		cert, err := security.LoadCertificate(path)
		if err != nil {
			// Present but unreadable: treat as live rather than overwrite it.
			found = append(found, name)
			continue
		}
		if security.CheckCertRevocation(cert, security.PoolCertCNPrefixNode+name, watermarks) != nil {
			continue
		}
		found = append(found, name)
	}
	return found
}

func (cmd *poolNodeAuthIssueCmd) Execute(args []string) error {
	if err := (&security.IssueClientCertsReq{Nodes: cmd.Nodes, Tenants: cmd.Tenants}).Validate(); err != nil {
		return err
	}
	if cmd.Replace && len(cmd.Tenants) > 0 {
		return errors.New("--replace applies to node certificates only; " +
			"deploy the new tenant certificate first, then revoke the old one")
	}

	state, err := control.GetPoolNodeAuthState(cmd.MustLogCtx(), cmd.ctlInvoker, cmd.PoolID().String())
	if err != nil {
		return err
	}
	poolUUID := state.PoolUUID
	caCert, caKey, err := cmd.poolCAFlags.load(cmd.config, poolUUID)
	if err != nil {
		return err
	}

	if cmd.Output == "" {
		dir, err := poolCADir(cmd.config)
		if err != nil {
			return err
		}
		cmd.Output = filepath.Join(dir, poolUUID.String())
	}
	if err := ensureWritableDir(cmd.Output); err != nil {
		return errors.Wrapf(err,
			"certificate directory %s is not writable; fix its permissions or pass --output",
			cmd.Output)
	}

	// Detection is limited to certificates this host staged into cmd.Output.
	if staged := cmd.stagedNodeCerts(poolUUID, state.Watermarks); len(staged) > 0 && !cmd.Replace {
		return errors.Errorf("certificate already issued for node(s) %s in %s; "+
			"reissuing leaves the existing certificate valid until it expires. "+
			"Pass --replace to revoke it first, or --output to issue alongside it",
			strings.Join(staged, ", "), cmd.Output)
	}

	certs, err := control.PoolIssueClientCerts(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolIssueClientCertsReq{
			ID:       cmd.PoolID().String(),
			CACert:   caCert,
			CAKey:    caKey,
			Validity: cmd.Validity.Duration,
			Nodes:    cmd.Nodes,
			Tenants:  cmd.Tenants,
			Replace:  cmd.Replace,
		})
	if err != nil {
		return err
	}
	if cmd.Replace {
		cmd.Infof("Revoked the existing certificate(s) for node(s) %s", strings.Join(cmd.Nodes, ", "))
	}

	results, err := writeClientCerts(cmd.Output, poolUUID, certs)
	if err != nil {
		return err
	}
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(results, nil)
	}
	reportClientCerts(cmd, results, len(cmd.Nodes), len(cmd.Tenants))
	return nil
}

type clientCertResult struct {
	CN       string `json:"cn"`
	CertPath string `json:"cert_path"`
	KeyPath  string `json:"key_path"`
	NotAfter string `json:"not_after"`
}

// writeClientCerts stages each cert/key pair under <dir>/<name>/.
func writeClientCerts(dir string, poolUUID uuid.UUID, certs []*security.ClientCert) ([]clientCertResult, error) {
	var results []clientCertResult
	for _, c := range certs {
		outDir := filepath.Join(dir, c.Name)
		if err := os.MkdirAll(outDir, 0700); err != nil {
			return nil, errors.Wrapf(err, "creating directory for %s", c.CN)
		}
		r := clientCertResult{CN: c.CN, NotAfter: c.Cert.NotAfter.Format(time.RFC3339)}
		r.CertPath, r.KeyPath = security.NodeCertPaths(outDir, poolUUID)
		if err := c.Write(r.CertPath, r.KeyPath); err != nil {
			return nil, errors.Wrapf(err, "writing %s", c.CN)
		}
		results = append(results, r)
	}
	return results, nil
}

func reportClientCerts(log logging.Logger, results []clientCertResult, nodes, tenants int) {
	for _, r := range results {
		log.Infof("  %s: %s, %s (valid until %s)", r.CN, r.CertPath, r.KeyPath, r.NotAfter)
	}
	kind, count := "node", nodes
	if tenants > 0 {
		kind, count = "tenant", tenants
	}
	log.Infof("Certificates issued for %d %s(s)", count, kind)
	log.Infof("Deploy each pair to its node in %s/ (readable by the daos_agent user; key mode 0400)",
		security.DefaultNodeCertDir)
}

// poolNodeAuthGenerateCertCmd mints client certificates where the pool CA
// key is kept, without contacting the system.
type poolNodeAuthGenerateCertCmd struct {
	baseCmd
	cfgCmd
	cmdutil.JSONOutputCmd

	Args struct {
		Pool PoolID `positional-arg-name:"<pool UUID>" required:"1"`
	} `positional-args:"yes"`
	poolCAFlags
	Nodes    []string     `long:"node" description:"Node name(s) to generate certs for"`
	Tenants  []string     `long:"tenant" description:"Tenant name(s) to generate certs for"`
	Output   string       `long:"output" short:"o" description:"Directory for the certificates, one subdirectory per name (default: <pool_uuid>/ under pools/ in the dmg certificate directory)"`
	Validity validityFlag `long:"validity" description:"Certificate lifetime, e.g. 90d, 26w, 2y (default: 1y; never past the pool CA's expiry)"`
}

func (cmd *poolNodeAuthGenerateCertCmd) Execute(args []string) error {
	issueReq := &security.IssueClientCertsReq{
		Nodes:    cmd.Nodes,
		Tenants:  cmd.Tenants,
		Validity: cmd.Validity.Duration,
	}
	if err := issueReq.Validate(); err != nil {
		return err
	}
	if !cmd.Args.Pool.HasUUID() {
		return errors.New("generate-cert takes the pool UUID; a label cannot be resolved without the system")
	}
	poolUUID := cmd.Args.Pool.UUID
	var err error
	issueReq.CACert, issueReq.CAKey, err = cmd.poolCAFlags.load(cmd.config, poolUUID)
	if err != nil {
		return err
	}
	if cmd.Output == "" {
		dir, err := poolCADir(cmd.config)
		if err != nil {
			return err
		}
		cmd.Output = filepath.Join(dir, poolUUID.String())
	}
	// Revocation state lives on the system; never overwrite a staged cert here.
	for _, name := range append(append([]string{}, cmd.Nodes...), cmd.Tenants...) {
		if certPath, _ := security.NodeCertPaths(filepath.Join(cmd.Output, name), poolUUID); fileExists(certPath) {
			return errors.Errorf("certificate already staged for %s at %s (pass --output to use another directory)", name, certPath)
		}
	}
	if err := ensureWritableDir(cmd.Output); err != nil {
		return errors.Wrapf(err, "certificate directory %s is not writable; fix its permissions or pass --output", cmd.Output)
	}

	certs, err := security.IssueClientCerts(issueReq)
	if err != nil {
		return err
	}
	results, err := writeClientCerts(cmd.Output, poolUUID, certs)
	if err != nil {
		return err
	}
	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(results, nil)
	}
	reportClientCerts(cmd, results, len(cmd.Nodes), len(cmd.Tenants))
	cmd.Infof("Revocation state was not consulted: revoke before minting a replacement for a revoked identity")
	return nil
}

// poolNodeAuthRevokeCmd advances the pool's revocation watermark for a CN
// and evicts its active handles.
type poolNodeAuthRevokeCmd struct {
	poolCmd
	Node            string `long:"node" description:"Node name to revoke"`
	Tenant          string `long:"tenant" description:"Tenant name to revoke"`
	EvictAllHandles bool   `long:"evict-all-handles" description:"Evict every active pool handle (default for tenant: revocations)"`
	NoEvict         bool   `long:"no-evict" description:"Advance the watermark but leave active handles alive"`
}

func (cmd *poolNodeAuthRevokeCmd) Execute(args []string) error {
	if cmd.Node == "" && cmd.Tenant == "" {
		return errors.New("specify --node or --tenant")
	}
	if cmd.Node != "" && cmd.Tenant != "" {
		return errors.New("--node and --tenant are mutually exclusive")
	}
	if cmd.EvictAllHandles && cmd.NoEvict {
		return errors.New("--evict-all-handles and --no-evict are mutually exclusive")
	}

	evictMode := daos.PoolRevokeEvictDefault
	switch {
	case cmd.EvictAllHandles:
		evictMode = daos.PoolRevokeEvictPoolWide
	case cmd.NoEvict:
		evictMode = daos.PoolRevokeEvictNone
	}

	resp, err := control.PoolRevokeClient(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolRevokeClientReq{
			ID:        cmd.PoolID().String(),
			Node:      cmd.Node,
			Tenant:    cmd.Tenant,
			EvictMode: evictMode,
		})
	if err != nil {
		return errors.Wrap(err, "revoking client")
	}

	type revokeResult struct {
		CN             string `json:"cn"`
		Watermark      string `json:"watermark"`
		HandlesEvicted int32  `json:"handles_evicted"`
		EvictScope     string `json:"evict_scope"`
	}
	result := revokeResult{
		CN:             resp.CN,
		Watermark:      resp.Watermark.Format(time.RFC3339),
		HandlesEvicted: resp.HandlesEvicted,
		EvictScope:     string(resp.EvictScope),
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(result, nil)
	}

	cmd.Infof("Revoked %s", resp.CN)
	cmd.Infof("  Watermark:       %s", result.Watermark)
	cmd.Infof("  Handles evicted: %d (%s)", result.HandlesEvicted, result.EvictScope)
	if cmd.Node != "" {
		cmd.Infof("  Revocation is by certificate identity: a host connecting with a tenant certificate reconnects; revoke the tenant to cut it off")
	}
	cmd.Infof("To restore access, issue a new certificate: dmg pool node-auth issue")
	if resp.EvictScope == daos.PoolRevokeEvictScopeMachine && result.HandlesEvicted == 0 {
		cmd.Noticef("No active handles matched %s — verify the CN is correct (the client may legitimately not have an active connection).",
			resp.CN)
	}
	return nil
}
