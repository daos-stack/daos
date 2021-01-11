//
// (C) Copyright 2019-2021 Intel Corporation.
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

// Component represents the DAOS component being granted authorization.
type Component int

const (
	ComponentUndefined Component = iota
	ComponentAdmin
	ComponentAgent
	ComponentServer
)

func (c Component) String() string {
	return [...]string{"undefined", "admin", "agent", "server"}[c]
}

// methodAuthorizations is the map for checking which components are authorized to make the specific method call.
var methodAuthorizations = map[string][]Component{
	"/ctl.CtlSvc/StoragePrepare":      {ComponentAdmin},
	"/ctl.CtlSvc/StorageScan":         {ComponentAdmin},
	"/ctl.CtlSvc/StorageFormat":       {ComponentAdmin},
	"/ctl.CtlSvc/NetworkScan":         {ComponentAdmin},
	"/ctl.CtlSvc/FirmwareQuery":       {ComponentAdmin},
	"/ctl.CtlSvc/FirmwareUpdate":      {ComponentAdmin},
	"/ctl.CtlSvc/SmdQuery":            {ComponentAdmin},
	"/ctl.CtlSvc/PrepShutdownRanks":   {ComponentServer},
	"/ctl.CtlSvc/StopRanks":           {ComponentServer},
	"/ctl.CtlSvc/PingRanks":           {ComponentServer},
	"/ctl.CtlSvc/ResetFormatRanks":    {ComponentServer},
	"/ctl.CtlSvc/StartRanks":          {ComponentServer},
	"/mgmt.MgmtSvc/Join":              {ComponentServer},
	"/mgmt.MgmtSvc/ClusterEvent":      {ComponentServer},
	"/mgmt.MgmtSvc/LeaderQuery":       {ComponentAdmin},
	"/mgmt.MgmtSvc/SystemQuery":       {ComponentAdmin},
	"/mgmt.MgmtSvc/SystemResetFormat": {ComponentAdmin},
	"/mgmt.MgmtSvc/SystemStart":       {ComponentAdmin},
	"/mgmt.MgmtSvc/SystemStop":        {ComponentAdmin},
	"/mgmt.MgmtSvc/PoolCreate":        {ComponentAdmin},
	"/mgmt.MgmtSvc/PoolDestroy":       {ComponentAdmin},
	"/mgmt.MgmtSvc/PoolResolveID":     {ComponentAdmin},
	"/mgmt.MgmtSvc/PoolQuery":         {ComponentAdmin},
	"/mgmt.MgmtSvc/PoolSetProp":       {ComponentAdmin},
	"/mgmt.MgmtSvc/PoolGetACL":        {ComponentAdmin},
	"/mgmt.MgmtSvc/PoolOverwriteACL":  {ComponentAdmin},
	"/mgmt.MgmtSvc/PoolUpdateACL":     {ComponentAdmin},
	"/mgmt.MgmtSvc/PoolDeleteACL":     {ComponentAdmin},
	"/mgmt.MgmtSvc/PoolExclude":       {ComponentAdmin},
	"/mgmt.MgmtSvc/PoolDrain":         {ComponentAdmin},
	"/mgmt.MgmtSvc/PoolReintegrate":   {ComponentAdmin},
	"/mgmt.MgmtSvc/PoolEvict":         {ComponentAdmin, ComponentAgent},
	"/mgmt.MgmtSvc/PoolExtend":        {ComponentAdmin},
	"/mgmt.MgmtSvc/GetAttachInfo":     {ComponentAgent},
	"/mgmt.MgmtSvc/ListPools":         {ComponentAdmin},
	"/mgmt.MgmtSvc/ListContainers":    {ComponentAdmin},
	"/mgmt.MgmtSvc/ContSetOwner":      {ComponentAdmin},
}

// HasAccess check if the given component has access to method given in FullMethod
func (c Component) HasAccess(FullMethod string) bool {
	compList, ok := methodAuthorizations[FullMethod]

	if !ok {
		return false
	}

	for _, comp := range compList {
		if c == comp {
			return true
		}
	}

	return false
}

// CommonNameToComponent returns the correct component based on the CommonName
func CommonNameToComponent(commonname string) Component {

	switch {
	case commonname == ComponentAdmin.String():
		return ComponentAdmin
	case commonname == ComponentAgent.String():
		return ComponentAgent
	case commonname == ComponentServer.String():
		return ComponentServer
	default:
		return ComponentUndefined
	}
}
