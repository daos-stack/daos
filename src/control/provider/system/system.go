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

package system

type (
	// IsMountedProvider is the interface that wraps the IsMounted method,
	// which can be provided by a system-specific implementation or a mock.
	IsMountedProvider interface {
		IsMounted(target string) (bool, error)
	}
	// MountProvider is the interface that wraps the Mount method, which
	// can be provided by a system-specific implementation or a mock.
	MountProvider interface {
		Mount(source, target, fstype string, flags uintptr, data string) error
	}
	// UnmountProvider is the interface that wraps the Unmount method, which
	// can be provided by a system-specific implementation or a mock.
	UnmountProvider interface {
		Unmount(target string, flags int) error
	}
)
