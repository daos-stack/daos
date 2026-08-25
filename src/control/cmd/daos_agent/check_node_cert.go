//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"os"
	"os/user"
	"path/filepath"
	"syscall"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/security/auth"
)

// checkNodeCertCmd is an admin command used to query the MS for the given pool's
// certificate requirements, and then check the local node's certificate deployment
// against those requirements.
type checkNodeCertCmd struct {
	configCmd
	ctlInvokerCmd
	cmdutil.LogCmd
	cmdutil.JSONOutputCmd
	Args struct {
		Pool string `positional-arg-name:"<pool label or UUID>" required:"1"`
	} `positional-args:"yes"`
}

type nodeCertCheck struct {
	Name  string `json:"name"`
	Value string `json:"value"`
	OK    bool   `json:"ok"`
	Warn  bool   `json:"warn,omitempty"`
}

type nodeCertChecker struct {
	checks      []nodeCertCheck
	failed      bool
	notDeployed bool
}

func (c *nodeCertChecker) add(name, value string, ok bool) {
	c.checks = append(c.checks, nodeCertCheck{Name: name, Value: value, OK: ok})
	if !ok {
		c.failed = true
	}
}

func (c *nodeCertChecker) pass(name, value string) { c.add(name, value, true) }
func (c *nodeCertChecker) fail(name, value string) { c.add(name, value, false) }
func (c *nodeCertChecker) warn(name, value string) {
	c.checks = append(c.checks, nodeCertCheck{Name: name, Value: value, OK: true, Warn: true})
}

// msInfo is what the management service tells us about the pool.
type msInfo struct {
	err        error
	poolUUID   uuid.UUID
	requires   bool
	caBundle   []byte
	watermarks security.CertWatermarks
	wmErr      error
}

func (cmd *checkNodeCertCmd) queryMS() *msInfo {
	mi := &msInfo{}
	resp, err := control.PoolGetCA(cmd.MustLogCtx(), cmd.ctlInvoker,
		&control.PoolGetCAReq{ID: cmd.Args.Pool})
	if err != nil {
		mi.err = err
		return mi
	}
	mi.poolUUID = resp.PoolUUID
	mi.requires = len(resp.Certs) > 0
	mi.caBundle = resp.PEM

	if mi.requires {
		mi.watermarks, mi.wmErr = control.PoolGetCertWatermarks(cmd.MustLogCtx(),
			cmd.ctlInvoker, &control.PoolGetCertWatermarksReq{ID: cmd.Args.Pool})
	}
	return mi
}

func fileModeOwner(path string) (string, error) {
	fi, err := os.Stat(path)
	if err != nil {
		return "", err
	}
	owner := "unknown"
	if st, ok := fi.Sys().(*syscall.Stat_t); ok {
		if u, err := user.LookupId(fmt.Sprintf("%d", st.Uid)); err == nil {
			owner = u.Username
		} else {
			owner = fmt.Sprintf("uid %d", st.Uid)
		}
	}
	return fmt.Sprintf("mode %#o, owner %s", fi.Mode().Perm(), owner), nil
}

// absent records a missing cert dir or cert file: a hard failure when
// the pool requires certificates, a clean nothing-to-do otherwise.
func (c *nodeCertChecker) absent(mi *msInfo, name, value string) {
	if mi.requires {
		c.fail(name, value+" — pool requires node certificates")
		return
	}
	c.pass(name, value)
	c.notDeployed = true
}

func (cmd *checkNodeCertCmd) runChecks(mi *msInfo) *nodeCertChecker {
	c := &nodeCertChecker{}

	if mi.err != nil {
		c.fail("management service", fmt.Sprintf(
			"unreachable (%s) — the agent cannot serve this pool at all; "+
				"fix connectivity before anything cert-related", mi.err))
		return c
	}
	poolUUID := mi.poolUUID
	req := "does not require node certificates"
	if mi.requires {
		req = "requires node certificates"
	}
	c.pass("pool", fmt.Sprintf("%s (%s)", mi.poolUUID, req))

	certDir := cmd.cfg.CredentialConfig.NodeCertDir
	if certDir == "" {
		certDir = security.DefaultNodeCertDir
	}
	if fi, err := os.Stat(certDir); err != nil {
		if os.IsNotExist(err) {
			c.absent(mi, "cert dir", fmt.Sprintf("%s (does not exist)", certDir))
			return c
		}
		c.fail("cert dir", fmt.Sprintf("%s (%s)", certDir, err))
		return c
	} else if !fi.IsDir() {
		c.fail("cert dir", fmt.Sprintf("%s (not a directory)", certDir))
		return c
	}
	c.pass("cert dir", fmt.Sprintf("%s (exists)", certDir))

	certPath, keyPath := security.NodeCertPaths(certDir, poolUUID)
	if _, err := os.Stat(certPath); os.IsNotExist(err) {
		c.absent(mi, "cert file", fmt.Sprintf("%s (not deployed)", filepath.Base(certPath)))
		return c
	}
	cert, err := security.LoadCertificate(certPath)
	if err != nil {
		c.fail("cert file", fmt.Sprintf("%s (%s)", certPath, err))
		return c
	}
	c.pass("cert file", fmt.Sprintf("%s  found, parses OK", filepath.Base(certPath)))

	keyDesc, err := fileModeOwner(keyPath)
	if err != nil {
		c.fail("key file", fmt.Sprintf("%s (%s)", keyPath, err))
		return c
	}
	key, err := security.LoadPrivateKey(keyPath)
	if err != nil {
		c.fail("key file", fmt.Sprintf("%s (%s, %s)", filepath.Base(keyPath), keyDesc, err))
		return c
	}
	if err := security.KeyMatchesCert(key, cert); err != nil {
		c.fail("key file", fmt.Sprintf("%s (%s, %s)", filepath.Base(keyPath), keyDesc, err))
		return c
	}
	c.pass("key file", fmt.Sprintf("%s  found, %s, matches cert", filepath.Base(keyPath), keyDesc))

	cn := cert.Subject.CommonName
	prefix, suffix, err := security.ValidatePoolCertCN(cn)
	if err != nil {
		c.fail("CN", fmt.Sprintf("%q (%s)", cn, err))
		return c
	}
	c.pass("CN", cn)

	if prefix == security.CertCNPrefixNode {
		machine, err := auth.GetMachineName()
		if err != nil {
			c.fail("machine name", fmt.Sprintf("cannot determine local machine name: %s", err))
		} else if err := security.CheckCNBinding(prefix, suffix, machine); err != nil {
			c.fail("machine name", fmt.Sprintf("%s  (MISMATCH: cert is for %q)", machine, suffix))
		} else {
			c.pass("machine name", fmt.Sprintf("%s  (match)", machine))
		}
	} else {
		c.pass("machine name", "n/a (tenant certificate)")
	}

	now := time.Now()
	window := fmt.Sprintf("%s .. %s",
		cert.NotBefore.Format("2006-01-02"), cert.NotAfter.Format("2006-01-02"))
	if err := security.CheckValidity(cert, now, security.NotBeforeSkewTolerance); err != nil {
		c.fail("validity", fmt.Sprintf("%s (%s)", window, err))
	} else if w := security.ExpiryWarning("certificate", cert, now, security.CertExpiryWarnWindow); w != "" {
		c.warn("validity", fmt.Sprintf("%s (%s — reissue)", window, w))
	} else {
		c.pass("validity", fmt.Sprintf("%s (OK)", window))
	}

	cmd.checkChain(c, mi, cert, now)
	cmd.checkRevocation(c, mi, cert, cn)

	return c
}

// checkChain verifies the deployed cert against the pool's current CA bundle.
func (cmd *checkNodeCertCmd) checkChain(c *nodeCertChecker, mi *msInfo, cert *x509.Certificate, now time.Time) {
	if len(mi.caBundle) == 0 {
		c.pass("chain", "not checked (pool has no CA)")
		return
	}

	var caPath string
	if cmd.cfg.TransportConfig != nil {
		caPath = cmd.cfg.TransportConfig.CARootPath
	}
	if caPath == "" {
		c.fail("chain", "cannot verify: no DAOS CA configured (transport_config.ca_cert)")
		return
	}
	root, err := security.LoadCertificate(caPath)
	if err != nil {
		c.fail("chain", fmt.Sprintf("cannot load DAOS CA %s: %s", caPath, err))
		return
	}

	if err := security.VerifyNodeCertChain(cert, root, mi.caBundle, now); err != nil {
		c.fail("chain", fmt.Sprintf("does NOT chain to the pool's current CA (%s) — "+
			"reissue from the current CA", err))
		return
	}
	c.pass("chain", "verifies against the pool's current CA")
	for _, w := range poolCAExpiryWarnings(mi.caBundle, now) {
		c.warn("pool CA", w+" — rotate the pool CA (dmg pool node-auth add-ca)")
	}
}

// poolCAExpiryWarnings reports every CA in the bundle nearing expiry.
func poolCAExpiryWarnings(bundle []byte, now time.Time) []string {
	var warnings []string
	for rest := bundle; ; {
		var block *pem.Block
		block, rest = pem.Decode(rest)
		if block == nil {
			return warnings
		}
		ca, err := x509.ParseCertificate(block.Bytes)
		if err != nil {
			continue
		}
		what := fmt.Sprintf("pool CA %q", ca.Subject.CommonName)
		if w := security.ExpiryWarning(what, ca, now, security.CertExpiryWarnWindow); w != "" {
			warnings = append(warnings, w)
		}
	}
}

func (cmd *checkNodeCertCmd) checkRevocation(c *nodeCertChecker, mi *msInfo, cert *x509.Certificate, cn string) {
	if !mi.requires {
		return
	}
	if mi.wmErr != nil {
		c.fail("revocation", fmt.Sprintf("cannot verify: watermarks unavailable (%s)", mi.wmErr))
		return
	}
	if err := security.CheckRevocation(cert, cn, mi.watermarks); err != nil {
		c.fail("revocation", fmt.Sprintf("REVOKED (%s) — reissue", err))
		return
	}
	c.pass("revocation", "not revoked")
}

func (cmd *checkNodeCertCmd) Execute(_ []string) error {
	mi := cmd.queryMS()
	c := cmd.runChecks(mi)

	if cmd.JSONOutputEnabled() {
		result := struct {
			Deployed bool            `json:"deployed"`
			Requires bool            `json:"requires_certs"`
			Checks   []nodeCertCheck `json:"checks"`
			Passed   bool            `json:"passed"`
		}{
			Deployed: !c.notDeployed,
			Requires: mi.requires,
			Checks:   c.checks,
			Passed:   !c.failed,
		}
		return cmd.OutputJSON(result, nil)
	}

	// The report goes to stdout regardless of the agent's log configuration.
	for _, chk := range c.checks {
		marker := ""
		if !chk.OK {
			marker = "FAIL "
		} else if chk.Warn {
			marker = "WARN "
		}
		fmt.Printf("  %-20s %s%s\n", chk.Name+":", marker, chk.Value)
	}
	if c.notDeployed {
		fmt.Println("Pool does not require node certificates; nothing to do.")
		return nil
	}
	if c.failed {
		return errors.New("node certificate check failed")
	}
	return nil
}
