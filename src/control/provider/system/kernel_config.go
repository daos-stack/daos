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

// ParseKernelConfig loads and parses the running kernel's configuration.
// It first tries /proc/config.gz (if CONFIG_IKCONFIG_PROC is enabled),
// then falls back to /boot/config-<kernel-release>.
func ParseKernelConfig() (map[string]string, error) {
	// Try /proc/config.gz first
	if f, err := os.Open("/proc/config.gz"); err == nil {
		defer f.Close()

		gr, err := gzip.NewReader(f)
		if err != nil {
			return nil, errors.Wrap(err, "creating gzip reader for /proc/config.gz")
		}
		defer gr.Close()

		return parseKernelConfig(gr)
	}

	// Fall back to /boot/config-<release>
	var uts unix.Utsname
	if err := unix.Uname(&uts); err != nil {
		return nil, errors.Wrap(err, "getting kernel release")
	}

	release := unix.ByteSliceToString(uts.Release[:])
	bootConfig := filepath.Join("/boot", "config-"+release)

	f, err := os.Open(bootConfig)
	if err != nil {
		return nil, errors.Errorf("kernel config not available: tried /proc/config.gz and %s", bootConfig)
	}
	defer f.Close()

	return parseKernelConfig(f)
}

// IsKernelConfigEnabled returns true if the given config option is enabled
// (set to "y" or "m") in the provided config map.
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
