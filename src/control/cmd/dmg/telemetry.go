//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"archive/tar"
	"compress/gzip"
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

	"github.com/daos-stack/daos/src/control/common"
)

type telemCmd struct {
	Configure telemConfigCmd `command:"config" description:"Configure telemetry"`
	Run       telemRunCmd    `command:"run" description:"Launch telemetry system"`
}

type telemConfigCmd struct {
	logCmd
	cfgCmd
	jsonOutputCmd
	For string `long:"for" short:"f" default:"prometheus" description:"Telemetry system to configure"`
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

	cmd.log.Infof("fetching %s %s", repo, data.TagName)

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

func (cmd *telemConfigCmd) findInstallPath() (string, error) {
	path := os.Getenv("PATH")
	for _, dir := range filepath.SplitList(path) {
		if err := unix.Access(dir, unix.W_OK); err == nil {
			return dir, nil
		}
	}

	return "", errors.New("unable to find writable $PATH entry")
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
	iPath, err := cmd.findInstallPath()
	if err != nil {
		return nil, err
	}

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
			info.binPath = path.Join(iPath, "prometheus")
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
		cmd.log.Info("Downloading and installing Prometheus...")
		promInfo, err = cmd.installPrometheus(cfgPath)
		if err != nil {
			return nil, errors.Wrap(err, "failed to install prometheus")
		}
		cmd.log.Infof("Installed prometheus to %s", promInfo.binPath)
	}

	cmd.log.Info("Configuring Prometheus for DAOS monitoring...")
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
	cmd.log.Infof("Wrote DAOS monitoring config to %s)", promInfo.cfgPath)

	return promInfo, nil
}

func (cmd *telemConfigCmd) Execute(_ []string) error {
	switch strings.ToLower(cmd.For) {
	case "prometheus":
		if _, err := cmd.configurePrometheus(); err != nil {
			return err
		}
		return nil
	default:
		return errors.Errorf("unsupported telemetry system: %q", cmd.For)
	}
}

type telemRunCmd struct {
	telemConfigCmd
}

func (cmd *telemRunCmd) runTelemetry(bin string, args ...string) error {
	return unix.Exec(bin, append([]string{bin}, args...), nil)
}

func (cmd *telemRunCmd) Execute(_ []string) error {
	switch strings.ToLower(cmd.For) {
	case "prometheus":
		promInfo, err := cmd.configurePrometheus()
		if err != nil {
			return err
		}

		cmd.log.Info("Starting Prometheus monitoring...")
		dataPath := path.Join(filepath.Dir(promInfo.cfgPath), ".prometheus_data")
		return cmd.runTelemetry(promInfo.binPath,
			"--config.file", promInfo.cfgPath,
			"--storage.tsdb.path", dataPath,
		)
	default:
		return errors.Errorf("unsupported telemetry system: %q", cmd.For)
	}
}
