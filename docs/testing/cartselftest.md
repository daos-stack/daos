# Run CaRT self\_test

On the client, run the CaRT `self_test` to verify basic network
connectivity.
It is advisable to use the `-u` (or `--use-daos-agent-env`) option
in order to obtain the fabric environment from the running
`daos_agent` process. See `self_test --help` for details.

```bash
 # set env
 export FI_UNIVERSE_SIZE=2048

 # for 4 servers --endpoint 0-3:0-1 ranks:tags.
 self_test --group-name daos_server --use-daos-agent-env --endpoint 0-1:0-1

 Adding endpoints:
   ranks: 0-1 (# ranks = 2)
   tags: 0-1 (# tags = 2)
 Warning: No --master-endpoint specified; using this command line application as the master endpoint
 Self Test Parameters:
   Group name to test against: daos_server
   # endpoints:                4
   Message sizes:              [(200000-BULK_GET 200000-BULK_PUT), (200000-BULK_GET 0-EMPTY), (0-EMPTY 200000-BULK_PUT), (200000-BULK_GET 1000-IOV), (1000-IOV 200000-BULK_PUT), (1000-IOV 1000-IOV), (1000-IOV 0-EMPTY), (0-EMPTY 1000-IOV), (0-EMPTY 0-EMPTY)]
   Buffer addresses end with:  <Default>
   Repetitions per size:       40000
   Max inflight RPCs:          1000

 CLI [rank=0 pid=40050]  Attached daos_server
 ##################################################
 Results for message size (200000-BULK_GET 200000-BULK_PUT) (max_inflight_rpcs = 1000):

 Master Endpoint 2:0
 -------------------
  RPC Bandwidth (MB/sec): 197.56
  RPC Throughput (RPCs/sec): 518
  RPC Latencies (us):
   Min    : 38791
   25th  %: 1695365
   Median : 1916632
   75th  %: 2144087
   Max    : 2969361
   Average: 1907415
   Std Dev: 373832.81
  RPC Failures: 0

  Endpoint results (rank:tag - Median Latency (us)):
   0:0 - 1889518
   0:1 - 1712934
   1:0 - 1924995
   1:1 - 2110649

 ##################################################
 Results for message size (200000-BULK_GET 0-EMPTY) (max_inflight_rpcs = 1000):

 Master Endpoint 2:0
 -------------------
  RPC Bandwidth (MB/sec): 112.03
  RPC Throughput (RPCs/sec): 587
  RPC Latencies (us):
   Min    : 4783
   25th  %: 1480053
   Median : 1688064
   75th  %: 1897392
   Max    : 2276555
   Average: 1681303
   Std Dev: 314999.11
  RPC Failures: 0

  Endpoint results (rank:tag - Median Latency (us)):
   0:0 - 2001222
   0:1 - 1793990
   1:0 - 1385306
   1:1 - 1593675

 ##################################################
 Results for message size (0-EMPTY 200000-BULK_PUT) (max_inflight_rpcs = 1000):

 Master Endpoint 2:0
 -------------------
  RPC Bandwidth (MB/sec): 112.12
  RPC Throughput (RPCs/sec): 588
  RPC Latencies (us):
   Min    : 6302
   25th  %: 1063532
   Median : 1654468
   75th  %: 2287784
   Max    : 3488227
   Average: 1680617
   Std Dev: 880402.68
  RPC Failures: 0

  Endpoint results (rank:tag - Median Latency (us)):
   0:0 - 1251585
   0:1 - 1323953
   1:0 - 2099173
   1:1 - 2043352

 ##################################################
 Results for message size (200000-BULK_GET 1000-IOV) (max_inflight_rpcs = 1000):

 Master Endpoint 2:0
 -------------------
  RPC Bandwidth (MB/sec): 112.54
  RPC Throughput (RPCs/sec): 587
  RPC Latencies (us):
   Min    : 5426
   25th  %: 1395359
   Median : 1687404
   75th  %: 1983402
   Max    : 2426175
   Average: 1681970
   Std Dev: 393256.99
  RPC Failures: 0

  Endpoint results (rank:tag - Median Latency (us)):
   0:0 - 2077476
   0:1 - 1870102
   1:0 - 1318136
   1:1 - 1529193

 ##################################################
 Results for message size (1000-IOV 200000-BULK_PUT) (max_inflight_rpcs = 1000):

 Master Endpoint 2:0
 -------------------
  RPC Bandwidth (MB/sec): 112.66
  RPC Throughput (RPCs/sec): 588
  RPC Latencies (us):
   Min    : 5340
   25th  %: 442729
   Median : 1221371
   75th  %: 2936906
   Max    : 3502405
   Average: 1681142
   Std Dev: 1308472.80
  RPC Failures: 0

  Endpoint results (rank:tag - Median Latency (us)):
   0:0 - 3006315
   0:1 - 2913808
   1:0 - 434763
   1:1 - 465469

 ##################################################
 Results for message size (1000-IOV 1000-IOV) (max_inflight_rpcs = 1000):

 Master Endpoint 2:0
 -------------------
  RPC Bandwidth (MB/sec): 80.71
  RPC Throughput (RPCs/sec): 42315
  RPC Latencies (us):
   Min    : 1187
   25th  %: 20187
   Median : 23322
   75th  %: 26833
   Max    : 30246
   Average: 23319
   Std Dev: 4339.87
  RPC Failures: 0

  Endpoint results (rank:tag - Median Latency (us)):
   0:0 - 26828
   0:1 - 26839
   1:0 - 20275
   1:1 - 20306

 ##################################################
 Results for message size (1000-IOV 0-EMPTY) (max_inflight_rpcs = 1000):

 Master Endpoint 2:0
 -------------------
  RPC Bandwidth (MB/sec): 42.68
  RPC Throughput (RPCs/sec): 44758
  RPC Latencies (us):
   Min    : 935
   25th  %: 15880
   Median : 21444
   75th  %: 28434
   Max    : 34551
   Average: 22035
   Std Dev: 7234.26
  RPC Failures: 0

  Endpoint results (rank:tag - Median Latency (us)):
   0:0 - 28418
   0:1 - 28449
   1:0 - 16301
   1:1 - 16318

 ##################################################
 Results for message size (0-EMPTY 1000-IOV) (max_inflight_rpcs = 1000):

 Master Endpoint 2:0
 -------------------
  RPC Bandwidth (MB/sec): 42.91
  RPC Throughput (RPCs/sec): 44991
  RPC Latencies (us):
   Min    : 789
   25th  %: 20224
   Median : 22195
   75th  %: 24001
   Max    : 26270
   Average: 21943
   Std Dev: 3039.50
  RPC Failures: 0

  Endpoint results (rank:tag - Median Latency (us)):
   0:0 - 24017
   0:1 - 23987
   1:0 - 20279
   1:1 - 20309

 ##################################################
 Results for message size (0-EMPTY 0-EMPTY) (max_inflight_rpcs = 1000):

 Master Endpoint 2:0
 -------------------
  RPC Bandwidth (MB/sec): 0.00
  RPC Throughput (RPCs/sec): 47807
  RPC Latencies (us):
   Min    : 774
   25th  %: 16161
   Median : 20419
   75th  %: 25102
   Max    : 29799
   Average: 20633
   Std Dev: 5401.96
  RPC Failures: 0

  Endpoint results (rank:tag - Median Latency (us)):
   0:0 - 25103
   0:1 - 25099
   1:0 - 16401
   1:1 - 16421
```
