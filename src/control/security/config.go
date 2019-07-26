package security

import (
	"crypto/tls"
	"crypto/x509"

	"github.com/pkg/errors"
)

const (
	defaultCACert     = ".daos/daosCA.crt"
	defaultServerCert = ".daos/daos_server.crt"
	defaultServerKey  = ".daos/daos_server.key"
	defaultClientCert = ".daos/client.crt"
	defaultClientKey  = ".daos/client.key"
	defaultServer     = "server"
	defaultInsecure   = false
)

//TransportConfig contains all the information on whether or not to use
//certificates and their location if their use is specified.
type TransportConfig struct {
	AllowInsecure     bool `yaml:"allow_insecure"`
	CertificateConfig `yaml:",inline"`
}

//CertificateConfig contains the specific certificate information for the daos
//component. ServerName is only needed if the config is being used as a
//transport credential for a gRPC tls client.
type CertificateConfig struct {
	ServerName      string           `yaml:"server_name,omitempty"`
	CARootPath      string           `yaml:"ca_cert"`
	CertificatePath string           `yaml:"cert"`
	PrivateKeyPath  string           `yaml:"key"`
	TLSKeypair      *tls.Certificate `yaml:"-"`
	CAPool          *x509.CertPool   `yaml:"-"`
}

//DefaultClientTransportConfig provides a default transport config disabling
//certificate usage and specifying certificates located under .daos. As this
//credential is meant to be used as a client credential it specifies a default
//ServerName as well.
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

//DefaultServerTransportConfig provides a default transport config disabling
//certificate usage and specifying certificates located under .daos.
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

//PreLoadCertData reads the certificate files in and parses them into TLS key
//pair and Certificate pool to provide a mechanism for detecting certificate/
//error before first use.
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

//ReloadCertData reloads and stores the certificate data in the case when
//certificate data has changed since initial loading.
func (cfg *TransportConfig) ReloadCertData() error {
	cfg.TLSKeypair = nil
	cfg.CAPool = nil
	return cfg.PreLoadCertData()
}
