//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package build

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"regexp"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/dlopen"
)

/*
#include <stdint.h>

int
get_fi_version(void *fn, int *major, int *minor, int *patch)
{
	uint32_t version;

	if (fn == NULL || major == NULL || minor == NULL || patch == NULL) {
		return -1;
	}
	// uint32_t fi_version(void);
	version = ((uint32_t (*)(void))fn)();

	if (version == 0) {
		return -1;
	}

	// FI_MAJOR(version);
	*major = (version >> 16);
	// FI_MINOR(version);
	*minor = (version & 0xFFFF);
	// No way to get at revision number via ABI. :(

	return 0;
}

int
get_hg_version(void *fn, int *major, int *minor, int *patch)
{
	if (fn == NULL || major == NULL || minor == NULL || patch == NULL) {
		return -1;
	}
	// int HG_Version_get(int *, int *, int *);
	return ((int (*)(int *, int *, int *))fn)(major, minor, patch);
}

int
get_daos_version(void *fn, int *major, int *minor, int *fix)
{
	if (fn == NULL || major == NULL || minor == NULL || fix == NULL) {
		return -1;
	}
	// int daos_version_get(int *, int *, int *);
	return ((int (*)(int *, int *, int *))fn)(major, minor, fix);
}
*/
import "C"

// readMappedLibPath attempts to resolve the given library name to an on-disk path.
// NB: The library must be loaded in order for it to be found!
func readMappedLibPath(input io.Reader, libName string) (string, error) {
	if libName == "" {
		return "", nil
	}

	libs := make(map[string]struct{})
	libRe := regexp.MustCompile(fmt.Sprintf(`\s([^\s]+/%s[\-\.\d]*\.so.*$)`, libName))
	scanner := bufio.NewScanner(input)
	for scanner.Scan() {
		if matches := libRe.FindStringSubmatch(scanner.Text()); len(matches) > 1 {
			libs[matches[1]] = struct{}{}
		}
	}

	if len(libs) == 0 {
		return "", errors.Errorf("unable to find path for %q", libName)
	} else if len(libs) > 1 {
		return "", errors.Errorf("multiple paths found for %q: %v", libName, libs)
	}

	var libPath string
	for lib := range libs {
		libPath = lib
		break
	}
	return libPath, nil
}

func getLibPath(libName string) (string, error) {
	f, err := os.Open("/proc/self/maps")
	if err != nil {
		return "", errors.Wrapf(err, "failed to open %s", "/proc/self/maps")
	}
	defer f.Close()

	return readMappedLibPath(f, libName)
}

func getLibHandle(libName string) (string, *dlopen.LibHandle, error) {
	searchLib := libName + ".so"

	// Check to see if it's already loaded, and if so, use that path
	// instead of searching in order to make sure we're getting a
	// handle to the correct library.
	libPath, _ := getLibPath(libName)
	if libPath != "" {
		searchLib = libPath
	}

	hdl, err := dlopen.GetHandle(searchLib)
	if err != nil {
		return "", nil, err
	}

	if libPath == "" {
		libPath, err = getLibPath(libName)
		if err != nil {
			fmt.Fprintf(os.Stderr, "failed to get path for %q: %+v\n", libName, err)
			return "", nil, err
		}
	}

	return libPath, hdl, err
}

func getLibFabricVersion() (*Version, string, error) {
	libPath, hdl, err := getLibHandle("libfabric")
	if err != nil {
		return nil, "", err
	}
	defer hdl.Close()

	ptr, err := hdl.GetSymbolPointer("fi_version")
	if err != nil {
		return nil, "", err
	}

	var major, minor, patch C.int
	rc := C.get_fi_version(ptr, &major, &minor, &patch)
	if rc != 0 {
		return nil, "", errors.Errorf("get_fi_version() failed: %d", rc)
	}

	return &Version{
		Major: int(major),
		Minor: int(minor),
		Patch: int(patch),
	}, libPath, nil
}

func getMercuryVersion() (*Version, string, error) {
	libPath, hdl, err := getLibHandle("libmercury")
	if err != nil {
		return nil, "", err
	}
	defer hdl.Close()

	ptr, err := hdl.GetSymbolPointer("HG_Version_get")
	if err != nil {
		return nil, "", err
	}

	var major, minor, patch C.int
	rc := C.get_hg_version(ptr, &major, &minor, &patch)
	if rc != 0 {
		return nil, "", errors.Errorf("get_hg_version() failed: %d", rc)
	}

	return &Version{
		Major: int(major),
		Minor: int(minor),
		Patch: int(patch),
	}, libPath, nil
}

func getDAOSVersion() (*Version, string, error) {
	libPath, hdl, err := getLibHandle("libdaos")
	if err != nil {
		return nil, "", err
	}
	defer hdl.Close()

	ptr, err := hdl.GetSymbolPointer("daos_version_get")
	if err != nil {
		return nil, "", err
	}

	var major, minor, fix C.int
	rc := C.get_daos_version(ptr, &major, &minor, &fix)
	if rc != 0 {
		return nil, "", errors.Errorf("get_daos_version() failed: %d", rc)
	}

	return &Version{
		Major: int(major),
		Minor: int(minor),
		Patch: int(fix),
	}, libPath, nil
}

// GetLibraryInfo attempts to resolve the given library name into a version and path.
// NB: The library must provide an ABI method to obtain its version, and that
// method needs to be added to this package in order to support it.
func GetLibraryInfo(libName string) (*Version, string, error) {
	switch strings.TrimPrefix(strings.ToLower(libName), "lib") {
	case "fabric":
		return getLibFabricVersion()
	case "mercury":
		return getMercuryVersion()
	case "daos":
		return getDAOSVersion()
	default:
		return nil, "", errors.Errorf("unsupported library: %q", libName)
	}
}
