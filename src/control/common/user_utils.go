//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/logging"
)

// GetConsent scans stdin for yes/no
func GetConsent(log logging.Logger) bool {
	var response string

	log.Info("Are you sure you want to continue? (yes/no)\n")

	_, err := fmt.Scanln(&response)
	if err != nil {
		log.Errorf("Error reading input: %s\n", err)
		return false
	}

	if response == "no" {
		return false
	} else if response != "yes" {
		log.Info("Please type yes or no and then press enter:")
		return GetConsent(log)
	}

	return true
}
