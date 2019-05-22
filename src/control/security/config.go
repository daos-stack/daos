package security

import (
	"crypto/tls"
	"crypto/x509"

	"github.com/pkg/errors"
)

// FIXME: These should probably not be used directly;
// instead, add a function which determines the correct
// root path and prepends it -- need to make it easy
// to test, though.
const (
	defaultCACert     = ".daos/daosCA.crt"
	defaultServerCert = ".daos/daos_server.crt"
	defaultServerKey  = ".daos/daos_server.key"
	defaultClientCert = ".daos/client.crt"
	defaultClientKey  = ".daos/client.key"
	defaultServer     = "server"
	defaultInsecure   = false
)

type TransportConfig struct {
	AllowInsecure     bool `yaml:"allow_insecure"`
	CertificateConfig `yaml:",inline"`
}

type CertificateConfig struct {
	ServerName      string           `yaml:"server_name,omitempty"`
	CARootPath      string           `yaml:"ca_cert"`
	CertificatePath string           `yaml:"cert"`
	PrivateKeyPath  string           `yaml:"key"`
	TLSKeypair      *tls.Certificate `yaml:"-"`
	CAPool          *x509.CertPool   `yaml:"-"`
}

// TODO: Add docs/test coverage for these functions

func DefaultClientTransportConfig() *TransportConfig {
	return &TransportConfig{
		AllowInsecure: defaultInsecure,
		CertificateConfig: CertificateConfig{
			ServerName:      defaultServer,
			CARootPath:      defaultCACert,
			CertificatePath: defaultClientCert,
			PrivateKeyPath:  defaultClientKey,
			TLSKeypair:      nil,
			CAPool:          nil,
		},
	}
}

func DefaultServerTransportConfig() *TransportConfig {
	return &TransportConfig{
		AllowInsecure: defaultInsecure,
		CertificateConfig: CertificateConfig{
			CARootPath:      defaultCACert,
			CertificatePath: defaultServerCert,
			PrivateKeyPath:  defaultServerKey,
			TLSKeypair:      nil,
			CAPool:          nil,
		},
	}
}

func (cfg *TransportConfig) PreLoadCertData() error {
	if cfg == nil {
		return errors.New("nil TransportConfig")
	}
	if cfg.TLSKeypair != nil && cfg.CAPool != nil || cfg.AllowInsecure == true {
		// In this case the data is already preloaded.
		// In order to reload data use ReloadCertDatA
		return nil
	}
	certificate, certPool, err := loadCertWithCustomCA(cfg.CARootPath, cfg.CertificatePath, cfg.PrivateKeyPath)
	if err != nil {
		return err
	}

	cfg.TLSKeypair = certificate
	cfg.CAPool = certPool

	return nil
}

func (cfg *TransportConfig) ReloadCertData() error {
	cfg.TLSKeypair = nil
	cfg.CAPool = nil
	return cfg.PreLoadCertData()
}
