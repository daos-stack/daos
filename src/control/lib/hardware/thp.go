//
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

type (
	// THPDetector is an interface for detecting if transparent hugepages is enabled on a
	// system.
	THPDetector interface {
		IsTHPEnabled() (bool, error)
	}
)
