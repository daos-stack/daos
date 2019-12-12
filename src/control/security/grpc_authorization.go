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

package security

import "strings"

//Component represents the DAOS component being granted authorization
type Component int

const (
	Dmg = iota
	Agent
	Server
	Undefined
)

func (c Component) String() string {
	return [...]string{"admin", "agent", "server", "undefined"}[c]
}

//methodAuthorizations is the map for checking which components are authorized to make the specific method call.
var methodAuthorizations = map[string]Component{
	"/ctl.MgmtCtl/StoragePrepare":       Dmg,
	"/ctl.MgmtCtl/StorageScan":          Dmg,
	"/ctl.MgmtCtl/SystemMemberQuery":    Dmg,
	"/ctl.MgmtCtl/SystemStop":           Dmg,
	"/ctl.MgmtCtl/NetworkListProviders": Dmg,
	"/ctl.MgmtCtl/StorageFormat":        Dmg,
	"/ctl.MgmtCtl/NetworkScanDevices":   Dmg,
	"/mgmt.MgmtSvc/Join":                Server,
	"/mgmt.MgmtSvc/PoolCreate":          Dmg,
	"/mgmt.MgmtSvc/PoolDestroy":         Dmg,
	"/mgmt.MgmtSvc/PoolGetACL":          Dmg,
	"/mgmt.MgmtSvc/PoolOverwriteACL":    Dmg,
	"/mgmt.MgmtSvc/GetAttachInfo":       Agent,
	"/mgmt.MgmtSvc/BioHealthQuery":      Dmg,
	"/mgmt.MgmtSvc/SmdListDevs":         Dmg,
	"/mgmt.MgmtSvc/SmdListPools":        Dmg,
	"/mgmt.MgmtSvc/KillRank":            Dmg,
	"/mgmt.MgmtSvc/ListPools":           Dmg,
}

//HasAccess check if the given component has access to method given in FullMethod
func (c Component) HasAccess(FullMethod string) bool {
	comp, ok := methodAuthorizations[FullMethod]

	if !ok {
		return false
	}

	if c == comp {
		return true
	}

	return false
}

//CommonNameToComponent returns the correct component based on the CommonName
func CommonNameToComponent(commonname string) Component {

	if strings.HasPrefix(commonname, Component(Dmg).String()) {
		return Dmg
	}
	if commonname == Component(Agent).String() {
		return Agent
	} else if commonname == Component(Server).String() {
		return Server
	} else {
		return Undefined
	}
}
