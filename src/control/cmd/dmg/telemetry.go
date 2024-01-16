//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"archive/tar"
	"compress/gzip"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"net/http"
	"os"
	"os/exec"
	"os/user"
	"path"
	"path/filepath"
	"strings"
	"time"

	"github.com/pkg/errors"
	"golang.org/x/sys/unix"
	yaml "gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
)

type telemCmd struct {
	Configure telemConfigCmd `command:"config" description:"Configure telemetry"`
	Metrics   metricsCmd     `command:"metrics" description:"Interact with metrics"`
}

type telemConfigCmd struct {
	baseCmd
	cfgCmd
	cmdutil.JSONOutputCmd
	InstallDir string `long:"install-dir" short:"i" required:"1" description:"Install directory for telemetry binary"`
	System     string `long:"system" short:"s" default:"prometheus" description:"Telemetry system to configure"`
}

func (cmd *telemConfigCmd) fetchAsset(repo, platform string) (*os.File, error) {
	client := &http.Client{
		Timeout: 60 * time.Second,
	}

	apiURL := fmt.Sprintf("https://api.github.com/repos/%s/releases/latest", repo)
	req, err := http.NewRequest(http.MethodGet, apiURL, nil)
	if err != nil {
		return nil, err
	}

	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}

	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}
	resp.Body.Close()

	data := struct {
		TagName string `json:"tag_name"`
		Assets  []struct {
			Name        string `json:"name"`
			DownloadURL string `json:"browser_download_url"`
		} `json:"assets"`
	}{}

	if err := json.Unmarshal(body, &data); err != nil {
		return nil, err
	}

	cmd.Infof("fetching %s %s", repo, data.TagName)

	var dlURL string
	var dlName string
	for _, asset := range data.Assets {
		if !strings.Contains(asset.Name, platform) {
			continue
		}
		dlURL = asset.DownloadURL
		dlName = asset.Name
	}

	if dlURL == "" {
		return nil, errors.Errorf("unable to find URL for %s in the %s release", platform, data.TagName)
	}

	req, err = http.NewRequest(http.MethodGet, dlURL, nil)
	if err != nil {
		return nil, err
	}

	resp, err = client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	outFile, err := ioutil.TempFile("", dlName)
	if err != nil {
		return nil, err
	}

	_, err = io.Copy(outFile, resp.Body)
	if err != nil {
		return nil, err
	}

	_, err = outFile.Seek(0, 0)
	return outFile, err
}

func (cmd *telemConfigCmd) extractFile(rdr *tar.Reader, dest string, mode os.FileMode) error {
	outFile, err := os.OpenFile(dest, os.O_RDWR|os.O_CREATE, mode)
	if err != nil {
		return errors.Wrapf(err, "failed to create %s", dest)
	}
	defer outFile.Close()
	_, err = io.Copy(outFile, rdr)
	if err != nil {
		return errors.Wrapf(err, "failed to write tar contents to %s", dest)
	}

	return nil
}

type installInfo struct {
	binPath string
	cfgPath string
}

func (cmd *telemConfigCmd) installPrometheus(cfgPath string) (*installInfo, error) {
	gzFile, err := cmd.fetchAsset("prometheus/prometheus", "linux-amd64")
	if err != nil {
		return nil, err
	}
	defer gzFile.Close()
	defer os.Remove(gzFile.Name())

	gzRdr, err := gzip.NewReader(gzFile)
	if err != nil {
		return nil, err
	}

	info := &installInfo{}
	tarRdr := tar.NewReader(gzRdr)
	extracted := 0
	for {
		hdr, err := tarRdr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, errors.Wrapf(err, "reading %s failed", gzFile.Name())
		}
		switch filepath.Base(hdr.Name) {
		case "prometheus":
			info.binPath = path.Join(cmd.InstallDir, "prometheus")
			if err := cmd.extractFile(tarRdr, info.binPath, 0755); err != nil {
				return nil, err
			}
			extracted++
		case "prometheus.yml":
			info.cfgPath = cfgPath
			if err := cmd.extractFile(tarRdr, info.cfgPath, 0644); err != nil {
				return nil, err
			}
			extracted++
		}

		if extracted == 2 {
			break
		}
	}

	return info, nil
}

// Define a cut-down version of the actual config:
// https://github.com/prometheus/prometheus/blob/main/config/config.go
//
// We could import the real code but it pulls in all sorts of extra
// dependencies (e.g. AWS SDK, etc.), so instead let's try defining the
// parts that we're concerned with.
type (
	staticConfig struct {
		Targets []string `yaml:"targets,omitempty"`
	}

	scrapeConfig struct {
		JobName        string          `yaml:"job_name"`
		ScrapeInterval time.Duration   `yaml:"scrape_interval,omitempty"`
		ScrapeTimeout  time.Duration   `yaml:"scrape_timeout,omitempty"`
		StaticConfigs  []*staticConfig `yaml:"static_configs,omitempty"`
	}

	promCfg struct {
		Global struct {
			ScrapeInterval time.Duration `yaml:"scrape_interval"`
		} `yaml:"global"`
		ScrapeConfigs []*scrapeConfig `yaml:"scrape_configs"`
	}
)

func (cmd *telemConfigCmd) loadPromCfg(cfgPath string) (*promCfg, error) {
	data, err := ioutil.ReadFile(cfgPath)
	if err != nil {
		return nil, err
	}

	cfg := &promCfg{}
	return cfg, yaml.Unmarshal(data, cfg)
}

func (cmd *telemConfigCmd) configurePrometheus() (*installInfo, error) {
	if err := unix.Access(cmd.InstallDir, unix.W_OK); err != nil {
		return nil, errors.Wrapf(err,
			"Install folder %s does not have write permission", cmd.InstallDir)
	}

	user, err := user.Current()
	if err != nil {
		return nil, err
	}
	cfgPath := path.Join(user.HomeDir, ".prometheus.yml")
	promInfo := &installInfo{
		cfgPath: cfgPath,
	}

	promInfo.binPath, err = exec.LookPath("prometheus")
	if err != nil {
		cmd.Info("Downloading and installing Prometheus...")
		promInfo, err = cmd.installPrometheus(cfgPath)
		if err != nil {
			return nil, errors.Wrap(err, "failed to install prometheus")
		}
		cmd.Infof("Installed prometheus to %s", promInfo.binPath)
	}

	cmd.Info("Configuring Prometheus for DAOS monitoring...")
	cfg, err := cmd.loadPromCfg(promInfo.cfgPath)
	if err != nil {
		return nil, err
	}

	sc := &staticConfig{}
	for _, h := range cmd.config.HostList {
		host, _, err := common.SplitPort(h, 0)
		if err != nil {
			return nil, err
		}
		sc.Targets = append(sc.Targets, host+":9191")
	}
	cfg.ScrapeConfigs = []*scrapeConfig{
		{
			JobName:        "daos",
			ScrapeInterval: 5 * time.Second,
			StaticConfigs:  []*staticConfig{sc},
		},
	}

	data, err := yaml.Marshal(cfg)
	if err != nil {
		return nil, err
	}

	if err := ioutil.WriteFile(promInfo.cfgPath, data, 0644); err != nil {
		return nil, errors.Wrapf(err, "failed to write %s", promInfo.cfgPath)
	}
	cmd.Infof("Wrote DAOS monitoring config to %s)", promInfo.cfgPath)

	return promInfo, nil
}

func (cmd *telemConfigCmd) Execute(_ []string) error {
	switch strings.ToLower(cmd.System) {
	case "prometheus":
		if _, err := cmd.configurePrometheus(); err != nil {
			return err
		}
		return nil
	default:
		return errors.Errorf("unsupported telemetry system: %q", cmd.System)
	}
}

// metricsCmd includes the commands that act directly on metrics on the DAOS hosts.
type metricsCmd struct {
	List  metricsListCmd  `command:"list" description:"List available metrics on a DAOS storage node"`
	Query metricsQueryCmd `command:"query" description:"Query metrics on a DAOS storage node"`
}

// metricsListCmd provides a list of metrics available from the requested DAOS servers.
type metricsListCmd struct {
	baseCmd
	cmdutil.JSONOutputCmd
	singleHostCmd
	Port uint32 `short:"p" long:"port" default:"9191" description:"Telemetry port on the host"`
}

// Execute runs the command to list metrics from the DAOS storage nodes.
func (cmd *metricsListCmd) Execute(args []string) error {
	host, err := getMetricsHost(cmd.getHostList())
	if err != nil {
		return err
	}

	req := new(control.MetricsListReq)
	req.Port = cmd.Port
	req.Host = host

	if !cmd.JSONOutputEnabled() {
		cmd.Info(getConnectingMsg(req.Host, req.Port))
	}

	resp, err := control.MetricsList(context.Background(), req)
	if err != nil {
		return err
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	err = pretty.PrintMetricsListResp(os.Stdout, resp)
	if err != nil {
		return err
	}
	return nil
}

func getMetricsHost(hostlist []string) (string, error) {
	if len(hostlist) != 1 {
		return "", fmt.Errorf("too many hosts: %v", hostlist)
	}

	// discard port if supplied - we use the metrics port
	parts := strings.Split(hostlist[0], ":")

	return parts[0], nil
}

func getConnectingMsg(host string, port uint32) string {
	return fmt.Sprintf("connecting to %s:%d...", host, port)
}

// metricsQueryCmd collects the requested metrics from the requested DAOS servers.
type metricsQueryCmd struct {
	baseCmd
	cmdutil.JSONOutputCmd
	singleHostCmd
	Port    uint32 `short:"p" long:"port" default:"9191" description:"Telemetry port on the host"`
	Metrics string `short:"m" long:"metrics" default:"" description:"Comma-separated list of metric names"`
}

// Execute runs the command to query metrics from the DAOS storage nodes.
func (cmd *metricsQueryCmd) Execute(args []string) error {
	host, err := getMetricsHost(cmd.getHostList())
	if err != nil {
		return err
	}

	req := new(control.MetricsQueryReq)
	req.Port = cmd.Port
	req.Host = host
	req.MetricNames = common.TokenizeCommaSeparatedString(cmd.Metrics)

	if !cmd.JSONOutputEnabled() {
		cmd.Info(getConnectingMsg(req.Host, req.Port))
	}

	resp, err := control.MetricsQuery(context.Background(), req)
	if err != nil {
		return err
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(resp, err)
	}

	err = pretty.PrintMetricsQueryResp(os.Stdout, resp)
	if err != nil {
		return err
	}
	return nil
}
