# DAOS Control API

The control package exposes an RPC-based API for control plane client
applications to communicate with DAOS Server processes.
The underlying transport mechanism is [gRPC](https://grpc.io/docs/languages/go/) and the externally exposed API
request and response structures are converted into protobuf messages internally
(protobuf definitions are not directly exposed to the consumer of the API).

Public API functions that return a structured response adhere to the following signature pattern:
  
  `func ObjectAction(context.Context, UnaryInvoker, *ObjectActionReq) (*ObjectActionResp, error)`

Public API functions that are pass/fail adhere to the following signature pattern:

  `func ObjectAction(context.Context, UnaryInvoker, *ObjectActionReq) error`

The provided [Context](https://golang.org/pkg/context/) is primarily used for cancellation and deadlines. [UnaryInvoker](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#UnaryInvoker) is an interface normally implemented by the [Client](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#Client), but can be mocked out for testing.

For usage examples, please refer to the [dmg utility](https://pkg.go.dev/github.com/daos-stack/daos/src/control/cmd/dmg)'s source code, as it is the primary consumer of this API.

The Control API is organized into two primary areas of focus: Control, and Management. The Control APIs are intended to be used to interact with the Control Plane servers, e.g. to query or format storage as part of bringing a DAOS system online. The Management APIs are intended to be used with a running system, e.g. to create or manage Pools.

## Control Methods
---
  - [ConfigGenerate(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#ConfigGenerate)context.Context, UnaryInvoker, [*ConfigGenerateReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#ConfigGenerateReq)) ([*ConfigGenerateResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#ConfigGenerateResp), error)
  - [NetworkScan(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#NetworkScan)context.Context, UnaryInvoker, [*NetworkScanReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#NetworkScanReq)) ([*NetworkScanResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#NetworkScanResp), error)
  - [SmdQuery(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#SmdQuery)context.Context, UnaryInvoker, [*SmdQueryReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#SmdQueryReq)) ([*SmdQueryResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#SmdQueryResp), error)
  - [StorageFormat(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#StorageFormat)context.Context, UnaryInvoker, [*StorageFormatReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#StorageFormatReq)) ([*StorageFormatResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#StorageFormatResp), error)
  - [StoragePrepare(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#StoragePrepare)context.Context, UnaryInvoker, [*StoragePrepareReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#StoragePrepareReq)) ([*StoragePrepareResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#StoragePrepareResp), error)
  - [StorageScan(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#StorageScan)context.Context, UnaryInvoker, [*StorageScanReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#StorageScanReq)) ([*StorageScanResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#StorageScanResp), error)

## Management Methods
---
### Container Methods
  - [ContSetOwner(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#ContSetOwner)context.Context, UnaryInvoker, [*ContSetOwnerReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#ContSetOwnerReq)) error

### Pool Methods
  - [PoolCreate(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolCreate)context.Context, UnaryInvoker, [*PoolCreateReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolCreateReq)) ([*PoolCreateResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolCreateResp), error)
  - [PoolQuery(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolQuery)context.Context, UnaryInvoker, [*PoolQueryReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolQueryReq)) ([*PoolQueryResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolQueryResp), error)
  - [PoolDestroy(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolDestroy)context.Context, UnaryInvoker, [*PoolDestroyReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolDestroyReq)) error
  - [PoolDrain(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolDrain)context.Context, UnaryInvoker, [*PoolDrainReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolDrainReq)) error
  - [PoolEvict(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolEvict)context.Context, UnaryInvoker, [*PoolEvictReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolEvictReq)) error
  - [PoolExclude(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolExclude)context.Context, UnaryInvoker, [*PoolExcludeReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolExcludeReq)) error
  - [PoolExtend(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolExtend)context.Context, UnaryInvoker, [*PoolExtendReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolExtendReq)) error
  - [PoolReintegrate(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolReintegrate)context.Context, UnaryInvoker, [*PoolReintegrateReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolReintegrateReq)) error
  - [PoolDeleteACL(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolDeleteACL)context.Context, UnaryInvoker, [*PoolDeleteACLReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolDeleteACLReq)) ([*PoolDeleteACLResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolDeleteACLResp), error)
  - [PoolGetACL(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolGetACL)context.Context, UnaryInvoker, [*PoolGetACLReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolGetACLReq)) ([*PoolGetACLResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolGetACLResp), error)
  - [PoolOverwriteACL(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolOverwriteACL)context.Context, UnaryInvoker, [*PoolOverwriteACLReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolOverwriteACLReq)) ([*PoolOverwriteACLResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#PoolOverwriteACLResp), error)

### System Methods
  - [ListPools(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#ListPools)context.Context, UnaryInvoker, [*ListPoolsReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#ListPoolsReq)) ([*ListPoolsResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#ListPoolsResp), error)
  - [LeaderQuery(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#LeaderQuery)context.Context, UnaryInvoker, [*LeaderQueryReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#LeaderQueryReq)) ([*LeaderQueryResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#LeaderQueryResp), error)
  - [SystemErase(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#SystemErase)context.Context, UnaryInvoker, [*SystemEraseReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#SystemEraseReq)) ([*SystemEraseResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#SystemEraseResp), error)
  - [SystemQuery(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#SystemQuery)context.Context, UnaryInvoker, [*SystemQueryReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#SystemQueryReq)) ([*SystemQueryResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#SystemQueryResp), error)
  - [SystemStart(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#SystemStart)context.Context, UnaryInvoker, [*SystemStartReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#SystemStartReq)) ([*SystemStartResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#SystemStartResp), error)
  - [SystemStop(](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#SystemStop)context.Context, UnaryInvoker, [*SystemStopReq](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#SystemStopReq)) ([*SystemStopResp](https://pkg.go.dev/github.com/daos-stack/daos/src/control/lib/control#SystemStopResp), error)

## Invoking RPCs
---
In the following simple usage example, we'll see the invocation of a storage scan across a set of hosts. The output will either be pretty-printed or JSON-formatted, depending on what the user specifies.

```go
package main

import (
        "context"
        "encoding/json"
        "flag"
        "fmt"
        "os"
        "strings"

        "github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
        "github.com/daos-stack/daos/src/control/lib/control"
        "github.com/daos-stack/daos/src/control/lib/hostlist"
)

var (
        scanNvmeHealth   bool
        scanNvmeMetadata bool
        jsonOutput       bool
        hostList         string
        cfgPath          string
)

func init() {
        flag.BoolVar(&scanNvmeHealth, "d", false, "include NVMe device health")
        flag.BoolVar(&scanNvmeMetadata, "m", false, "include NVMe device metadata")
        flag.BoolVar(&jsonOutput, "j", false, "emit JSON-formatted output")
        flag.StringVar(&hostList, "l", "", "optional hostlist")
        flag.StringVar(&cfgPath, "c", "", "optional path to control config")
}

func main() {
        flag.Parse()

        ctlClient := control.NewClient()

        cfg, err := control.LoadConfig(cfgPath)
        if err != nil {
                panic(err)
        }
        ctlClient.SetConfig(cfg)

        request := &control.StorageScanReq{
                NvmeHealth: scanNvmeHealth,
                NvmeMeta:   scanNvmeMetadata,
        }

        // If no hostlist is supplied, the daos_control.yml file will be
        // checked to supply a default.
        if hostList != "" {
                hs, err := hostlist.CreateSet(hostList)
                if err != nil {
                        panic(err)
                }
                request.SetHostList(hs.Slice())
        }

        response, err := control.StorageScan(context.Background(), ctlClient, request)
        if err != nil {
                panic(err)
        }

        if jsonOutput {
                data, err := json.Marshal(response)
                if err != nil {
                        panic(err)
                }
                fmt.Println(string(data))
                os.Exit(0)
        }

        var bld strings.Builder
        if err := pretty.PrintHostStorageMap(response.HostStorage, &bld); err != nil {
                panic(err)
        }
        fmt.Println(bld.String())
}
```