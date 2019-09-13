//
// (C) Copyright 2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
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
