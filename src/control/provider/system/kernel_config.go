//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"bufio"
	"compress/gzip"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/pkg/errors"
	"golang.org/x/sys/unix"
)

// parseKernelConfig parses kernel configuration from a reader into a map of
// config option names to their raw string values.
func parseKernelConfig(r io.Reader) (map[string]string, error) {
	config := make(map[string]string)
	scanner := bufio.NewScanner(r)

	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())

		// Skip empty lines and comments
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		// Parse CONFIG_FOO=value lines
		parts := strings.SplitN(line, "=", 2)
		if len(parts) != 2 {
			continue
		}

		config[parts[0]] = parts[1]
	}

	if err := scanner.Err(); err != nil {
		return nil, errors.Wrap(err, "scanning kernel config")
	}

	return config, nil
}

// parseKernelConfigFile opens and parses a kernel config file at the given path.
// If the path ends in .gz, the file is decompressed before parsing.
func parseKernelConfigFile(path string) (map[string]string, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, errors.Wrapf(err, "opening kernel config %s", path)
	}
	defer f.Close()

	if strings.HasSuffix(path, ".gz") {
		gr, err := gzip.NewReader(f)
		if err != nil {
			return nil, errors.Wrapf(err, "creating gzip reader for %s", path)
		}
		defer gr.Close()

		return parseKernelConfig(gr)
	}

	return parseKernelConfig(f)
}

// ParseKernelConfig loads and parses the running kernel's configuration.
// If overridePath is non-empty, only that path is tried. Otherwise, it
// tries /proc/config.gz (if CONFIG_IKCONFIG_PROC is enabled), then falls
// back to /boot/config-<kernel-release>.
func ParseKernelConfig(overridePath ...string) (map[string]string, error) {
	if len(overridePath) > 0 && overridePath[0] != "" {
		return parseKernelConfigFile(overridePath[0])
	}

	// Try /proc/config.gz first
	if cfg, err := parseKernelConfigFile("/proc/config.gz"); err == nil {
		return cfg, nil
	}

	// Fall back to /boot/config-<release>
	var uts unix.Utsname
	if err := unix.Uname(&uts); err != nil {
		return nil, errors.Wrap(err, "getting kernel release")
	}

	release := unix.ByteSliceToString(uts.Release[:])
	bootConfig := filepath.Join("/boot", "config-"+release)

	return parseKernelConfigFile(bootConfig)
}

// IsKernelConfigEnabled returns true if the given config option is enabled
// (set to "y" or "m") in the provided config map. Returns false if config is nil.
func IsKernelConfigEnabled(config map[string]string, key string) bool {
	val, ok := config[key]
	return ok && (val == "y" || val == "m")
}

// GetKernelConfigValue returns the raw value of a kernel config option and
// a boolean indicating whether the option was present in the config.
func GetKernelConfigValue(config map[string]string, key string) (string, bool) {
	val, ok := config[key]
	return val, ok
}
