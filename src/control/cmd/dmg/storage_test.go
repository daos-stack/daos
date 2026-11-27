//
// (C) Copyright 2019-2022 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"strings"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestStorageCommands(t *testing.T) {
	storageFormatReq := &control.StorageFormatReq{Reformat: true}
	storageFormatReq.SetHostList([]string{})
	systemQueryReq := &control.SystemQueryReq{FailOnUnavailable: true}
	nvmeRebindReq := &control.NvmeRebindReq{PCIAddr: "0000:80:00.0"}
	nvmeRebindReq.SetHostList([]string{"foo2.com"})
	nvmeAddDeviceReq := func() *control.NvmeAddDeviceReq {
		req := &control.NvmeAddDeviceReq{
			PCIAddr: "0000:80:00.0", EngineIndex: 1, StorageTierIndex: -1,
		}
		req.SetHostList([]string{"foo2.com"})
		return req
	}
	prepareReformatReq := func(rank uint32) *control.StoragePrepareReformatReq {
		req := &control.StoragePrepareReformatReq{Rank: rank}
		req.SetHostList([]string{"10.0.0.1:10001"})
		return req
	}
	rankQueryReq := func(rank ranklist.Rank) *control.SystemQueryReq {
		req := &control.SystemQueryReq{}
		ranks := ranklist.NewRankSet()
		ranks.Add(rank)
		req.SetRanks(ranks)
		return req
	}
	prepareReformatCalls := func(rank uint32) string {
		return strings.Join([]string{
			printRequest(t, rankQueryReq(ranklist.Rank(rank))),
			printRequest(t, prepareReformatReq(rank)),
		}, " ")
	}

	runCmdTests(t, []cmdTest{
		{
			"Format",
			"storage format",
			strings.Join([]string{
				printRequest(t, systemQueryReq),
				printRequest(t, &control.StorageFormatReq{Rank: uint32(ranklist.NilRank)}),
			}, " "),
			nil,
		},
		{
			"Format with reformat",
			"storage format --reformat",
			"",
			errors.New("unknown flag"),
		},
		{
			"Format with force",
			"storage format --force",
			strings.Join([]string{
				printRequest(t, systemQueryReq),
				printRequest(t, &control.StorageFormatReq{Reformat: true, Rank: uint32(ranklist.NilRank)}),
			}, " "),
			nil,
		},
		{
			"Scan summary",
			"storage scan",
			strings.Join([]string{
				printRequest(t, &control.StorageScanReq{NvmeBasic: true}),
			}, " "),
			nil,
		},
		{
			"Scan verbose",
			"storage scan --verbose",
			strings.Join([]string{
				printRequest(t, &control.StorageScanReq{}),
			}, " "),
			nil,
		},
		{
			"Scan NVMe health short",
			"storage scan -n",
			printRequest(t, &control.StorageScanReq{NvmeHealth: true}),
			nil,
		},
		{
			"Scan NVMe health long",
			"storage scan --nvme-health",
			printRequest(t, &control.StorageScanReq{NvmeHealth: true}),
			nil,
		},
		{
			"Scan NVMe health with verbose",
			"storage scan --nvme-health --verbose",
			"",
			errors.New("cannot use --verbose"),
		},
		{
			"Rebind NVMe; no PCI address",
			"storage nvme-rebind",
			"",
			errors.New("required flag"),
		},
		{
			"Rebind NVMe; 0 hosts in hostlist",
			"storage nvme-rebind --pci-address 0000:80:00.0",
			"",
			errors.New("expects a single host"),
		},
		{
			"Rebind NVMe; 2 hosts in hostlist",
			"storage nvme-rebind -l foo[1,2].com --pci-address 0000:80:00.0",
			"",
			errors.New("expects a single host"),
		},
		{
			"Rebind NVMe",
			"storage nvme-rebind -l foo2.com --pci-address 0000:80:00.0",
			printRequest(t, nvmeRebindReq),
			nil,
		},
		{
			"Add NVMe device; no PCI address",
			"storage nvme-add-device",
			"",
			errors.New("required flag"),
		},
		{
			"Add NVMe device; 0 hosts in hostlist",
			"storage nvme-add-device --pci-address 0000:80:00.0 --engine-index 0",
			"",
			errors.New("expects a single host"),
		},
		{
			"Add NVMe device; 2 hosts in hostlist",
			"storage nvme-add-device -l foo[1,2].com --pci-address 0000:80:00.0 --engine-index 0",
			"",
			errors.New("expects a single host"),
		},
		{
			"Add NVMe device; no engine index",
			"storage nvme-add-device -l foo2.com --pci-address 0000:80:00.0",
			"",
			errors.New("engine-index"),
		},
		{
			"Add NVMe device; positive storage tier index",
			"storage nvme-add-device -l foo2.com -a 0000:80:00.0 -e 1 -t 1",
			printRequest(t, nvmeAddDeviceReq().WithStorageTierIndex(1)),
			nil,
		},
		{
			"Add NVMe device; short opts",
			"storage nvme-add-device -l foo2.com -a 0000:80:00.0 -e 1",
			printRequest(t, nvmeAddDeviceReq()),
			nil,
		},
		{
			"Add NVMe device; long opts",
			"storage nvme-add-device --host-list foo2.com --pci-address 0000:80:00.0 --engine-index 1 --tier-index 0",
			printRequest(t, nvmeAddDeviceReq().WithStorageTierIndex(0)),
			nil,
		},
		{
			"Prepare reformat; no rank",
			"storage prepare-reformat",
			"",
			errMissingFlag,
		},
		{
			"Prepare reformat; short opt",
			"storage prepare-reformat -r 5",
			prepareReformatCalls(5),
			nil,
		},
		{
			"Prepare reformat; long opt resolves host from rank",
			"storage prepare-reformat --rank 5",
			prepareReformatCalls(5),
			nil,
		},
		{
			"Prepare reformat; rank zero",
			"storage prepare-reformat --rank 0",
			prepareReformatCalls(0),
			nil,
		},
		{
			"Prepare reformat; rank not in system query",
			"storage prepare-reformat --rank 9",
			printRequest(t, rankQueryReq(9)),
			errors.New("rank 9 not found in system query response"),
		},
		{
			"Prepare reformat; host option not supported",
			"storage prepare-reformat --rank 5 --host foo2.com",
			"",
			errors.New("unknown flag `host'"),
		},
		{
			"Prepare reformat; engine-index option not supported",
			"storage prepare-reformat --rank 5 --engine-index 1",
			"",
			errors.New("unknown flag `engine-index'"),
		},
		{
			"Format with replace; no hosts in hostlist",
			"storage format --replace",
			"",
			errors.New("expects a single host"),
		},
		{
			"Format with replace; multiple hosts in hostlist",
			"storage format --replace -l foo[1,2].com",
			"",
			errors.New("expects a single host"),
		},
		{
			"Format with replace and force",
			"storage format --replace --force",
			"",
			errors.New("may not be mixed with --force"),
		},
		{
			"Format with replace and single host",
			"storage format --replace -l foo1.com",
			strings.Join([]string{
				printRequest(t, func() *control.SystemQueryReq {
					req := &control.SystemQueryReq{}
					req.SetHostList([]string{"foo1.com:10001"})
					hs, err := hostlist.CreateSet("foo1.com:10001")
					if err != nil {
						t.Fatal(err)
					}
					req.SetHosts(hs)
					return req
				}()),
				printRequest(t, func() *control.StorageFormatReq {
					req := &control.StorageFormatReq{
						Replace: true, Rank: uint32(ranklist.NilRank),
					}
					req.SetHostList([]string{"foo1.com"})
					return req
				}()),
			}, " "),
			nil,
		},
		{
			"Format with replace and rank",
			"storage format --replace --rank 5 -l foo1.com",
			strings.Join([]string{
				printRequest(t, func() *control.SystemQueryReq {
					req := &control.SystemQueryReq{}
					req.SetHostList([]string{"foo1.com:10001"})
					hs, err := hostlist.CreateSet("foo1.com:10001")
					if err != nil {
						t.Fatal(err)
					}
					req.SetHosts(hs)
					return req
				}()),
				printRequest(t, func() *control.StorageFormatReq {
					req := &control.StorageFormatReq{
						Replace: true, Rank: 5,
					}
					req.SetHostList([]string{"foo1.com"})
					return req
				}()),
			}, " "),
			nil,
		},
		{
			"Format with rank but no replace",
			"storage format --rank 5 -l foo1.com",
			"",
			errors.New("--rank option is only valid when used with --replace"),
		},
		{
			"Format with rank 0 and replace",
			"storage format --replace --rank 0 -l foo1.com",
			strings.Join([]string{
				printRequest(t, func() *control.SystemQueryReq {
					req := &control.SystemQueryReq{}
					req.SetHostList([]string{"foo1.com:10001"})
					hs, err := hostlist.CreateSet("foo1.com:10001")
					if err != nil {
						t.Fatal(err)
					}
					req.SetHosts(hs)
					return req
				}()),
				printRequest(t, func() *control.StorageFormatReq {
					req := &control.StorageFormatReq{
						Replace: true, Rank: 0,
					}
					req.SetHostList([]string{"foo1.com"})
					return req
				}()),
			}, " "),
			nil,
		},
		{
			"Nonexistent subcommand",
			"storage quack",
			"",
			errors.New("Unknown command"),
		},
	})
}

// prepareReformatInvoker fails the prepare-reformat request on its host when hostErr is set,
// otherwise it behaves like the bridge invoker.
type prepareReformatInvoker struct {
	bridgeConnInvoker
	hostErr error
}

func (pri *prepareReformatInvoker) InvokeUnaryRPC(ctx context.Context, req control.UnaryRequest) (*control.UnaryResponse, error) {
	if _, ok := req.(*control.StoragePrepareReformatReq); ok && pri.hostErr != nil {
		pri.conn.appendInvocation(printRequest(pri.t, req))
		return &control.UnaryResponse{
			Responses: []*control.HostResponse{
				{Addr: "10.0.0.1:10001", Error: pri.hostErr},
			},
		}, nil
	}

	return pri.bridgeConnInvoker.InvokeUnaryRPC(ctx, req)
}

func TestStorageCommands_PrepareReformat_Output(t *testing.T) {
	for name, tc := range map[string]struct {
		hostErr error
		expErr  error
		expOut  []string
		notOut  string
	}{
		"success reports how to start the engine": {
			expOut: []string{
				"Rank 5 on host 10.0.0.1:10001 prepared for reformat",
				"restarting daos_server on that host",
				"\"dmg system start\" cannot start it",
				"\"dmg storage format\"",
			},
		},
		"failure on host does not report success": {
			hostErr: errors.New("engine 1 is still running"),
			expErr:  errors.New("1 host had errors"),
			expOut:  []string{"engine 1 is still running"},
			notOut:  "prepared for reformat",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			invoker := &prepareReformatInvoker{
				bridgeConnInvoker: bridgeConnInvoker{
					MockInvoker: *control.DefaultMockInvoker(log),
					t:           t,
					conn:        newTestConn(t),
				},
				hostErr: tc.hostErr,
			}

			err := runCmd(t, "storage prepare-reformat --rank 5", log, invoker)
			test.CmpErr(t, tc.expErr, err)

			test.AssertStringContains(t, buf.String(), tc.expOut...)
			if tc.notOut != "" && strings.Contains(buf.String(), tc.notOut) {
				t.Fatalf("unexpected %q in output:\n%s", tc.notOut, buf.String())
			}
		})
	}
}
