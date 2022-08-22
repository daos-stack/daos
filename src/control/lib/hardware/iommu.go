//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hardware

type (
	// IOMMUDetector is an interface for detecting if IOMMU is enabled on a system.
	IOMMUDetector interface {
		IsIOMMUEnabled() (bool, error)
	}
)
