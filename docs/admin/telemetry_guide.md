# DAOS Telemetry Example

This document will help to run daos metrics command and collect some key metrics from the 
server to help debug the issues and analyze the system behavior.

## How to run telemetry command:

### Directly on server using daos_metrics command as sudo user

- Example of collecting the pool query metrics on the servers using daos_metrics command

```
$ sudo daos_metrics -C | grep pool_query
ID: 0/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/pool_query,0
ID: 0/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/pool_query_space,0
$ sudo daos_metrics -C -S 1 | grep pool_query
ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/pool_query,12
ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/pool_query_space,10
```

### Dmg command on admin node (dmg telemetry metrics query)

- Example of collecting the pool query metrics from individual servers using dmg command

```
$ sudo dmg telemetry metrics query -m engine_pool_ops_pool_query -l brd-221
connecting to brd-221:9191...
- Metric Set: engine_pool_ops_pool_query (Type: Counter)
  Total number of processed pool query operations
    Metric  Labels                                              Value
    ------  ------                                              -----
    Counter (pool=8259d3ff-523e-4a43-9248-26aba2a62f4c, rank=0) 0
    Counter (pool=8259d3ff-523e-4a43-9248-26aba2a62f4c, rank=1) 0

$ sudo dmg telemetry metrics query -m engine_pool_ops_pool_query -l brd-222
connecting to brd-222:9191...
- Metric Set: engine_pool_ops_pool_query (Type: Counter)
  Total number of processed pool query operations
    Metric  Labels                                              Value
    ------  ------                                              -----
    Counter (pool=8259d3ff-523e-4a43-9248-26aba2a62f4c, rank=2) 0
    Counter (pool=8259d3ff-523e-4a43-9248-26aba2a62f4c, rank=4) 0
```

### Identify the pool UUID and leader rank:
 - Some metrics are only available on pool leader rank so identify the leader rank for that pool from the pool query command.
 - Below is the example of pool query where leader rank is 1
   - Pool 55cc96d8-5c46-41f4-af29-881d293b6f6f, ntarget=48, disabled=0, `leader=1`, version=1, state=Ready      

```
#sudo dmg pool query samir_pool
```
```
Pool 55cc96d8-5c46-41f4-af29-881d293b6f6f, ntarget=48, disabled=0, leader=1, version=1, state=Ready
Pool health info:
- Rebuild idle, 0 objs, 0 recs
Pool space info:
- Target count:48
- Total memory-file size: 151 GB
- Metadata storage:
  Total size: 151 GB
  Free: 118 GB, min:2.5 GB, max:2.5 GB, mean:2.5 GB
- Data storage:
  Total size: 600 GB
  Free: 598 GB, min:12 GB, max:12 GB, mean:12 GB
```

 - Find the leader rank address so that daos_metrics command can be run on that specific server.
   In this example Rank 1 is on `brd-221.daos.hpc.amslabs.hpecorp.net` (`10.214.213.41`)

```
#sudo dmg system query -v
```
```
Rank UUID                                 Control Address      Fault Domain                          State  Reason
---- ----                                 ---------------      ------------                          -----  ------
0    6c481fea-b820-4b50-9845-6a5a04b4cfcf 10.214.213.41:10001  /brd-221.daos.hpc.amslabs.hpecorp.net Joined
1    43865b12-86d3-4107-afe8-3921f19bc9ff 10.214.213.41:10001  /brd-221.daos.hpc.amslabs.hpecorp.net Joined
2    eb413873-c13c-43ea-8bdf-21b691e169c9 10.214.212.229:10001 /brd-222.daos.hpc.amslabs.hpecorp.net Joined
3    607ad987-a55a-4365-ad6b-c4160ac5ff67 10.214.214.190:10001 /brd-223.daos.hpc.amslabs.hpecorp.net Joined
4    6c3d9b9a-2fff-4874-a7f0-309c4126a8e6 10.214.212.229:10001 /brd-222.daos.hpc.amslabs.hpecorp.net Joined
5    6884e5c9-b38b-46aa-b042-7fad9b37cf45 10.214.214.190:10001 /brd-223.daos.hpc.amslabs.hpecorp.net Joined
```
 - dmg command example based on leader Fault Domain (hostname) `-l brd-221`

```
$ sudo dmg telemetry metrics query -m engine_pool_ops_pool_query -l brd-221
connecting to brd-221:9191...
- Metric Set: engine_pool_ops_pool_query (Type: Counter)
  Total number of processed pool query operations
    Metric  Labels                                              Value
    ------  ------                                              -----
    Counter (pool=8259d3ff-523e-4a43-9248-26aba2a62f4c, rank=0) 0
    Counter (pool=8259d3ff-523e-4a43-9248-26aba2a62f4c, rank=1) 2
```

OR

 - dmg command example based on Control Address `-l 10.214.213.41`

```
$ sudo dmg telemetry metrics query -m engine_pool_ops_pool_query -l 10.214.213.41
connecting to 10.214.213.41:9191...
- Metric Set: engine_pool_ops_pool_query (Type: Counter)
  Total number of processed pool query operations
    Metric  Labels                                              Value
    ------  ------                                              -----
    Counter (pool=8259d3ff-523e-4a43-9248-26aba2a62f4c, rank=0) 0
    Counter (pool=8259d3ff-523e-4a43-9248-26aba2a62f4c, rank=1) 2

```

## Engine Metrics:

|Operation| Description | DAOS Command | Metrics Command | Output |
|:---:| :---: | :---: | :---: |:------------: |
|When engine started | Timestamp of last engine startup | None | `sudo daos_metrics -S 1 -C \| grep 'started_at' \| grep -v pool`|ID: 0/started_at,Tue Oct 28 23:21:24 2025|
|When engine become ready | Timestamp when the engine became ready | None | `sudo daos_metrics -S 1 -C \| grep 'servicing_at'`|ID: 0/servicing_at,Tue Oct 28 23:21:33 2025|
|Find Engine Rank ID | Rank ID of this engine | None | `sudo daos_metrics -S 1  -C  \| grep  '/rank' \| grep -v pool`|ID: 1/rank,276|
|check if Engine is dead | engine_events_dead_ranks | None | `sudo daos_metrics -C \| grep '/dead'`| ID: 0/events/dead_ranks,1 |
|last event on rank | Timestamp of last received event | None | `sudo daos_metrics -S 1  -C  \| grep  '/last_event'`| ID: 1/events/last_event_ts,Thu Jan  1 00:00:00 1970 |

## Pool Metrics:

|Operation| Description | DAOS Command | Metrics Command | Output |
|:---:| :---: | :---: | :---: |:------------: |
|With No Pools| Total number of processed pool connect operations | None | `sudo daos_metrics -C \| grep 'ops/pool'`|None|
|After creating single pool| | dmg pool create <POOL_NAME> |  `sudo daos_metrics -C \| grep 'ops/pool'`| ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_evict,0<br>ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_connect,0<br>ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_disconnect,0<br>ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_query,0<br>ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_query_space,0|
|After querying the single pool without storage|   Total number of processed pool query operations | dmg pool query <POOL_NAME> -t |  `sudo daos_metrics -C \| grep 'ops/pool_query'` | ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_query,1|
|After querying the single pool with storage |   Total number of processed pool query (with operation) operations | dmg pool query <POOL_NAME> |  `sudo daos_metrics -C \| grep 'ops/pool_query_space'` | ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_query_space,1|
|After Creating Container| Total number of processed pool connect operations | daos cont create <POOL_NAME> <CONT_NAME>| `sudo daos_metrics -C \| grep 'ops/pool_connect'`| ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_connect,1|
|After Creating Container| Total number of processed pool disconnect operations | daos cont create <POOL_NAME> <CONT_NAME>| `sudo daos_metrics -C \| grep 'ops/pool_disconnect'`|ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_disconnect,1|
|After Mounting FUSE Container| Total number of processed pool connect operations | dfuse -m <MOUNT_POINT> -p <POOL_NAME> -c <CONT_NAME>| `sudo daos_metrics -C \| grep 'ops/pool_connect'`| ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_connect,2|
|After Unmounting FUSE Container| Total number of processed pool disconnect operations | fusermount3 -u -m <MOUNT_POINT> | `sudo daos_metrics -C \| grep 'ops/pool_disconnect'`| ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_disconnect,2|
|After Pool evict |   Total number of pool handle evict operations | dmg pool evict <POOL_NAME> | `sudo daos_metrics -C \| grep 'ops/pool_evict'`| ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_evict,2|

## Container Metrics:

|Operation| Description | DAOS Command | Metrics Command | Output |
|:---:| :---: | :---: | :---: |:------------: |
| Container creation | Total number of successful container create operations | daos cont create <POOL_NAME> <CONT_NAME> --type='POSIX' | `sudo daos_metrics -C \| grep cont_create \| grep <POOL_UUID>`|ID: 0/pool/c22c6a6c-7e31-4788-90a4-a55d1083d57b/ops/cont_create,1|
| Container query | Total number of successful container query operations | daos container query <POOL_NAME> <CONT_NAME> | `sudo daos_metrics -C \| grep cont_query \| grep <POOL_UUID>`|ID: 0/pool/c22c6a6c-7e31-4788-90a4-a55d1083d57b/ops/cont_query,4|
| Container open | Total number of successful container open operations | dfuse -m <MOUNT_POINT> -p <POOL_NAME> -c <CONT_NAME> | `sudo daos_metrics -C \| grep cont_open \| grep <POOL_UUID>`|ID: 0/pool/c22c6a6c-7e31-4788-90a4-a55d1083d57b/ops/cont_open,3|
| Container destroy | Total number of successful container destroy operations | daos cont destroy <POOL_NAME> <CONT_NAME> | `sudo daos_metrics -C \| grep cont_destroy \| grep <POOL_UUID>`|ID: 0/pool/c22c6a6c-7e31-4788-90a4-a55d1083d57b/ops/cont_destroy,1|

## I/O Metrics:

|Operation| Description | DAOS Command | Metrics Command | Output |
|:---:| :---: | :---: | :---: |:------------: |
| data written | Total number of bytes updated/written |Write the Data using any IO | `sudo daos_metrics  -C -S 1 \| grep <POOl_UUID>  \| grep 'xferred/update'`|ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/update/tgt_0,1335885824<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/update/tgt_7,1337983064<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/update/tgt_2,1342177280<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/update/tgt_4,1325400064<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/update/tgt_6,1337982976<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/update/tgt_5,1384120320<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/update/tgt_1,1332740096<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/update/tgt_3,1341128828|
| data read | Total number of bytes fetched/read | Read the Data using any IO | `sudo daos_metrics  -C -S 1 \| grep <POOl_UUID>  \| grep 'xferred/fetch'`|ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/fetch/tgt_0,1335885824<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/fetch/tgt_7,1337983240<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/fetch/tgt_2,1342177280<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/fetch/tgt_4,1325400064<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/fetch/tgt_6,1337982976<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/fetch/tgt_5,1384120320<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/fetch/tgt_1,1332740096<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/fetch/tgt_3,1341129076|
| Write IOPS operation | Total number of processed object RPCs | Write the Data using any IO | `sudo daos_metrics  -S 1 -C \| grep <POOL_UUID> \| grep 'ops/update'`|ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/update/tgt_6,222<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/update/tgt_2,204<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/update/tgt_5,210<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/update/tgt_7,223<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/update/tgt_4,224<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/update/tgt_0,196<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/update/tgt_1,198<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/update/tgt_3,206|
| Read IOPS operation | Total number of processed object RPCs | Read the Data using any IO | `sudo daos_metrics  -S 1 -C \| grep <POOL_UUID> \| grep 'ops/fetch'`|ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/fetch/tgt_6,234<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/fetch/tgt_2,206<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/fetch/tgt_5,215<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/fetch/tgt_7,214<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/fetch/tgt_4,225<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/fetch/tgt_0,192<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/fetch/tgt_1,202<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/fetch/tgt_3,221|
| IO latency Update | update RPC processing time | Write the Data using 1MiB xfersize | `sudo daos_metrics  -S 1 -C \|  grep 'io/latency/update'`|ID: 1/io/latency/update/1MB/tgt_0,34423,9173,239956,24216.092267,1875,45405173,35287.151469<br>ID: 1/io/latency/update/1MB/tgt_1,34824,9195,224337,24619.489373,1882,46333879,35692.908836<br>ID: 1/io/latency/update/1MB/tgt_2,17586,9187,246820,25627.308223,1885,48307476,37184.782868<br>ID: 1/io/latency/update/1MB/tgt_3,60684,9182,264286,25998.202265,1943,50514507,38227.372221<br>ID: 1/io/latency/update/1MB/tgt_4,83487,9193,235707,26626.855799,1914,50963802,37382.179815<br>ID: 1/io/latency/update/1MB/tgt_5,26402,9200,235859,24656.685802,1951,48105194,34931.529382<br>ID: 1/io/latency/update/1MB/tgt_6,107294,9190,244975,26761.485861,1945,52051090,38022.684882<br>ID: 1/io/latency/update/1MB/tgt_7,79041,9213,219362,25710.023921,1923,49440376,36611.272385|
| IO latency Fetch | fetch RPC processing time | Read the Data using 1MiB xfersize | `sudo daos_metrics  -S 1 -C \|  grep 'io/latency/fetch'`|ID: 1/io/latency/fetch/1MB/tgt_0,29630,9419,225908,19060.848723,1527,29105916,26764.971072<br>ID: 1/io/latency/fetch/1MB/tgt_1,18329,9406,343931,17769.093144,1546,27471018,23882.809783<br>ID: 1/io/latency/fetch/1MB/tgt_2,9887,9385,131315,18075.996768,1547,27963567,22973.594024<br>ID: 1/io/latency/fetch/1MB/tgt_3,39508,9411,155136,19332.508228,1580,30545363,25593.694908<br>ID: 1/io/latency/fetch/1MB/tgt_4,22616,9413,412206,19062.688062,1558,29699668,27359.057624<br>ID: 1/io/latency/fetch/1MB/tgt_5,22280,9418,126520,17382.379032,1612,28020395,20937.262665<br>ID: 1/io/latency/fetch/1MB/tgt_6,40743,9409,207370,18697.681472,1576,29467546,23236.574768<br>ID: 1/io/latency/fetch/1MB/tgt_7,24048,9417,112182,17725.164955,1558,27615807,21375.496411|

## Troubleshooting:

### No response to pool query or any I/O operation

In case of no response to any dmg pool command or any I/O operation means any one of the single xstream might have stuck. Either ULT is stuck or NVMe cannot respond to I/O operation.
To check if ULT is stuck, run metrics command on each server and check for sched/cycle_duration and sched/cycle_size.
In this case sched/cycle_duration and sched/cycle_size for stuck xstream counter value is higher (outlier) compared to other xstream and ULT count. 

- sched/cycle_duration: Schedule cycle duration, units: ms
- sched/cycle_size: Schedule cycle size, units: ULT

Below is the example on real system where ULT was stuck and not responding. You can see the outlier for xs_3\
**xs_3: 87624970 ms**\
**xs_3: 72508 ULT**
```
# sudo daos_metrics -C | grep -e cycle
OR
# daos_metrics
 
        cycle_duration
            xs_0: 4 ms [min: 0, max: 736, avg: 1, sum: 4374707, stddev: 2, samples: 4337768]
            xs_1: 0 ms [min: 0, max: 4, avg: 0, sum: 12, stddev: 1, samples: 57]
            xs_2: 1000 ms [min: 0, max: 1008, avg: 1001, sum: 170805474, stddev: 5, samples: 170636]
            xs_3: 87624970 ms [min: 0, max: 87624970, avg: 0, sum: 155215898, stddev: 677, samples: 16729436166]
            xs_4: 0 ms [min: 0, max: 480, avg: 0, sum: 170775422, stddev: 0, samples: 52866364909]
            xs_5: 0 ms [min: 0, max: 532, avg: 0, sum: 170774266, stddev: 0, samples: 53277804155]
            xs_6: 0 ms [min: 0, max: 457, avg: 0, sum: 170775310, stddev: 0, samples: 52654423202]
            xs_7: 0 ms [min: 0, max: 449, avg: 0, sum: 170774942, stddev: 0, samples: 53289078146]
            xs_8: 0 ms [min: 0, max: 696, avg: 0, sum: 170779578, stddev: 0, samples: 53348599756]
            xs_9: 0 ms [min: 0, max: 444, avg: 0, sum: 170775582, stddev: 0, samples: 53085628214]
            xs_10: 0 ms [min: 0, max: 456, avg: 0, sum: 170775386, stddev: 0, samples: 53361992047]
            xs_11: 0 ms [min: 0, max: 668, avg: 0, sum: 170779354, stddev: 0, samples: 52868332788]
            xs_12: 0 ms [min: 0, max: 664, avg: 0, sum: 170779222, stddev: 0, samples: 53207853905]
            xs_13: 0 ms [min: 0, max: 484, avg: 0, sum: 170778230, stddev: 0, samples: 53161107629]
            xs_14: 0 ms [min: 0, max: 452, avg: 0, sum: 170778690, stddev: 0, samples: 54026864334]
            xs_15: 0 ms [min: 0, max: 664, avg: 0, sum: 170779106, stddev: 0, samples: 53240085110]
            xs_16: 0 ms [min: 0, max: 588, avg: 0, sum: 170778746, stddev: 0, samples: 53324006952]
            xs_17: 0 ms [min: 0, max: 664, avg: 0, sum: 170778646, stddev: 0, samples: 53244261876]
            xs_18: 0 ms [min: 0, max: 452, avg: 0, sum: 170779198, stddev: 0, samples: 53498338576]
            xs_19: 4 ms [min: 0, max: 108, avg: 0, sum: 30913, stddev: 1, samples: 461742]
            xs_20: 0 ms [min: 0, max: 112, avg: 0, sum: 30832, stddev: 1, samples: 460370]
            xs_21: 0 ms [min: 0, max: 112, avg: 0, sum: 31340, stddev: 1, samples: 461099]
            xs_22: 0 ms [min: 0, max: 116, avg: 0, sum: 105495174, stddev: 0, samples: 92074321933]
        cycle_size
            xs_0: 1 ULT [min: 1, max: 672, avg: 1, sum: 4486893, stddev: 1, samples: 4337768]
            xs_1: 1 ULT [min: 1, max: 15, avg: 2, sum: 116, stddev: 2, samples: 57]
            xs_2: 1 ULT [min: 1, max: 1, avg: 1, sum: 170636, stddev: 0, samples: 170636]
            xs_3: 72508 ULT [min: 1, max: 72508, avg: 1, sum: 16944980993, stddev: 1, samples: 16729436166]
            xs_4: 1 ULT [min: 1, max: 253, avg: 1, sum: 53106562082, stddev: 0, samples: 52866364919]
            xs_5: 1 ULT [min: 1, max: 293, avg: 1, sum: 53517385848, stddev: 0, samples: 53277804166]
            xs_6: 1 ULT [min: 1, max: 262, avg: 1, sum: 52893882375, stddev: 0, samples: 52654423213]
            xs_7: 1 ULT [min: 1, max: 263, avg: 1, sum: 53529014337, stddev: 0, samples: 53289078157]
            xs_8: 1 ULT [min: 1, max: 269, avg: 1, sum: 53588382832, stddev: 0, samples: 53348599768]
            xs_9: 1 ULT [min: 1, max: 538, avg: 1, sum: 53325349666, stddev: 0, samples: 53085628225]
            xs_10: 1 ULT [min: 1, max: 440, avg: 1, sum: 53601721471, stddev: 0, samples: 53361992058]
            xs_11: 1 ULT [min: 1, max: 365, avg: 1, sum: 53108191221, stddev: 0, samples: 52868332799]
            xs_12: 1 ULT [min: 1, max: 268, avg: 1, sum: 53447917652, stddev: 0, samples: 53207853917]
            xs_13: 1 ULT [min: 1, max: 258, avg: 1, sum: 53400854712, stddev: 0, samples: 53161107641]
            xs_14: 1 ULT [min: 1, max: 265, avg: 1, sum: 54266784187, stddev: 0, samples: 54026864345]
            xs_15: 1 ULT [min: 1, max: 440, avg: 1, sum: 53480318341, stddev: 0, samples: 53240085122]
            xs_16: 1 ULT [min: 1, max: 270, avg: 1, sum: 53564352374, stddev: 0, samples: 53324006963]
            xs_17: 1 ULT [min: 1, max: 273, avg: 1, sum: 53484431253, stddev: 0, samples: 53244261888]
            xs_18: 1 ULT [min: 1, max: 275, avg: 1, sum: 53738248689, stddev: 0, samples: 53498338588]
            xs_19: 1 ULT [min: 1, max: 2, avg: 1, sum: 461743, stddev: 0, samples: 461742]
            xs_20: 1 ULT [min: 1, max: 1, avg: 1, sum: 460370, stddev: 0, samples: 460370]
            xs_21: 1 ULT [min: 1, max: 1, avg: 1, sum: 461099, stddev: 0, samples: 461099]
            xs_22: 1 ULT [min: 1, max: 3, avg: 1, sum: 92074426829, stddev: 0, samples: 92074321962]
```

### Slow performance

If DAOS system is performing slower, check write(update) & read(fetch) metrics to indicate the source of the problem across all engines.

For example, mention below, one of the NVMe was impacting the overall IO performance because write BW on that specific drive was slower. As you can see two targets (tgt_0 & tgt_8) latency for 4MB write were too high compared to other targets. That indicate that specific drive is having lower write BW which increase the update latency too high.
This metrics are available in different IO size ranges from 256B to 4GB so looks for matching IO size used for testing the performance. Below example we used IOR write size 4MB.

```
#sudo daos_metrics -C | grep 'io/latency/update'

ID: 0/io/latency/update/4MB/tgt_0,16349826,733843,16349826,7329515.976190,42,307839671,4196687.177444 
ID: 0/io/latency/update/4MB/tgt_1,1260,1147,2191,1463.423077,52,76098,273.640909 
ID: 0/io/latency/update/4MB/tgt_2,1252,1122,2275,1452.000000,62,90024,272.896966 
ID: 0/io/latency/update/4MB/tgt_3,1637,1179,2639,1558.844444,45,70148,302.601219 
ID: 0/io/latency/update/4MB/tgt_4,1155,1119,2280,1496.857143,49,73346,281.746857 
ID: 0/io/latency/update/4MB/tgt_5,1804,1139,1920,1493.767442,43,64232,234.072520 
ID: 0/io/latency/update/4MB/tgt_6,1160,1136,2550,1560.862745,51,79604,293.899440 
ID: 0/io/latency/update/4MB/tgt_7,1399,1126,1969,1411.929825,57,80480,195.942125 
ID: 0/io/latency/update/4MB/tgt_8,15264368,857936,19645847,9109087.453125,64,582981597,5094157.829112 
ID: 0/io/latency/update/4MB/tgt_9,1601,1146,2455,1437.038462,52,74726,262.549712 
ID: 0/io/latency/update/4MB/tgt_10,1366,1138,2094,1459.828125,64,93429,228.692526 
ID: 0/io/latency/update/4MB/tgt_11,1118,1113,2742,1475.378788,66,97375,309.820731 
ID: 0/io/latency/update/4MB/tgt_12,1169,1158,2531,1492.392857,56,83574,270.312323 
ID: 0/io/latency/update/4MB/tgt_13,1477,1148,2204,1485.853659,41,60920,244.983118 
ID: 0/io/latency/update/4MB/tgt_14,1159,1159,2390,1523.333333,48,73120,318.466026 
ID: 0/io/latency/update/4MB/tgt_15,1511,1165,2318,1447.608696,46,66590,253.351094

#sudo daos_metrics -C | grep 'io/latency/fetch'

ID: 0/io/latency/fetch/4MB/tgt_0,1390,1099,2169,1380.785714,42,57993,202.810200 
ID: 0/io/latency/fetch/4MB/tgt_1,1902,1413,2956,1845.769231,52,95980,313.043041 
ID: 0/io/latency/fetch/4MB/tgt_2,1741,1395,2493,1783.983871,62,110607,226.501945 
ID: 0/io/latency/fetch/4MB/tgt_3,1543,1241,2568,1824.800000,45,82116,281.414092 
ID: 0/io/latency/fetch/4MB/tgt_4,1705,1506,2426,1850.020408,49,90651,232.079413 
ID: 0/io/latency/fetch/4MB/tgt_5,1579,1251,2396,1754.139535,43,75428,213.314275 
ID: 0/io/latency/fetch/4MB/tgt_6,1566,1262,2403,1747.823529,51,89139,260.631134 
ID: 0/io/latency/fetch/4MB/tgt_7,1663,1354,2912,1853.631579,57,105657,287.610267 
ID: 0/io/latency/fetch/4MB/tgt_8,1508,1051,2276,1417.562500,64,90724,271.118956 
ID: 0/io/latency/fetch/4MB/tgt_9,1508,1404,2468,1791.788462,52,93173,251.042324 
ID: 0/io/latency/fetch/4MB/tgt_10,1746,1453,2645,1796.203125,64,114957,230.458630 
ID: 0/io/latency/fetch/4MB/tgt_11,1695,1394,2416,1761.151515,66,116236,220.046376 
ID: 0/io/latency/fetch/4MB/tgt_12,1966,1396,2654,1740.464286,56,97466,238.501684 
ID: 0/io/latency/fetch/4MB/tgt_13,1915,1341,2613,1774.536585,41,72756,237.038298 
ID: 0/io/latency/fetch/4MB/tgt_14,1861,1337,2543,1807.625000,48,86766,279.890680 
ID: 0/io/latency/fetch/4MB/tgt_15,1740,1326,2420,1733.521739,46,79742,238.393674 

```


### NVMe Device Error

Many times, NVMe device has error which can also be an indication for slow performance or system stuck issue.

```
#sudo daos_metrics -M | grep errs
  media_errs: 0 errs, desc: Number of unrecovered data integrity error, units: errs
  read_errs: 0 errs, desc: Number of errors reported to the engine on read commands, units: errs
  write_errs: 0 errs, desc: Number of errors reported to the engine on write commands, units: errs
  unmap_errs: 0 errs, desc: Number of errors reported to the engine on unmap/trim commands, units: errs
  checksum_mismatch: 0 errs, desc: Number of checksum mismatch detected by the engine, units: errs

#sudo daos_metrics -C | grep nvm | grep err
ID: 0/nvme/0000:83:00.0/commands/media_errs,0
ID: 0/nvme/0000:83:00.0/commands/read_errs,0
ID: 0/nvme/0000:83:00.0/commands/write_errs,0
ID: 0/nvme/0000:83:00.0/commands/unmap_errs,0
ID: 0/nvme/0000:83:00.0/vendor/endtoend_err_cnt_raw,0
ID: 0/nvme/0000:83:00.0/vendor/crc_err_cnt_raw,0
```

## Metrics Unit Type

daos_metrics output is available in multiple units. for example, Counters, Guage. It can display the data based on different unit type.

### Display Counter type metrics

```
sudo daos_metrics -c -M -C
name,value,min,max,mean,sample_size,sum,std_dev,description,units
ID: 0/events/dead_ranks,0,,,,,,Number of dead rank events received,events
ID: 0/net/uri/lookup_self,0,,,,,,total number of URI requests for self
ID: 0/net/uri/lookup_other,0,,,,,,total number of URI requests for other ranks
ID: 0/net/ofi+tcp;ofi_rxm/hg/bulks/ctx_0,0,,,,,,Mercury-layer count of bulk transfers,bulks
ID: 0/net/ofi+tcp;ofi_rxm/hg/bulks/ctx_1,0,,,,,,Mercury-layer count of bulk transfers,bulks
```

### Display Guage type metrics

```
sudo daos_metrics -g -M -C | more
name,value,min,max,mean,sample_size,sum,std_dev,description,units
ID: 0/rank,0,,,,,,Rank ID of this engine
ID: 0/net/ofi+tcp;ofi_rxm/hg/active_rpcs/ctx_0,0,,,,,,Mercury-layer count of active RPCs,rpcs
ID: 0/net/ofi+tcp;ofi_rxm/hg/active_rpcs/ctx_1,0,,,,,,Mercury-layer count of active RPCs,rpcs
ID: 0/net/ofi+tcp;ofi_rxm/hg/active_rpcs/ctx_2,0,,,,,,Mercury-layer count of active RPCs,rpcs
ID: 0/net/ofi+tcp;ofi_rxm/hg/active_rpcs/ctx_3,0,,,,,,Mercury-layer count of active RPCs,rpcs
```

## Metrics Unit output format

Some Metrics units are in format where multiple values are display for number of samples. For example, update/fetch latency output.

```
        latency 
            update  
                256B    
                    tgt_0: 118 us [min: 15, max: 3703, avg: 100, sum: 200968, stddev: 124, samples: 2010]
```

|metrics type| definition||
|:---:|:-------------------:|:---:|
|value|Current value|118 us|
|min|The minimum value from all data samples|15 us|
|max|The maximum value from all data samples|3703 us|
|avg|The average value based on all data samples|100 us|
|sum|The total value of all data samples|200968 us|
|stddev| Standard deviation |124 us|
|samples|Total number of data samples used for metrics at given point|2010|

## Reset the metrics counter

Metrics counter will be reset when system restarts or it can be reset using below command on individual servers.

For Engine 0
```
sudo daos_metrics -e
Item: xs_0 has unknown type: 0x800
Item: xs_1 has unknown type: 0x800
Item: xs_2 has unknown type: 0x800
Item: xs_3 has unknown type: 0x800
Item: xs_4 has unknown type: 0x800
Item: xs_5 has unknown type: 0x800
Item: xs_6 has unknown type: 0x800
Item: xs_7 has unknown type: 0x800
Item: xs_8 has unknown type: 0x800
Item: xs_9 has unknown type: 0x800
Item: xs_10 has unknown type: 0x800
```

Foe Engine 1 (Incase multiple engines are running on same node)
```
daos_metrics -S 1 -e
Item: xs_0 has unknown type: 0x800
Item: xs_1 has unknown type: 0x800
Item: xs_2 has unknown type: 0x800
Item: xs_3 has unknown type: 0x800
Item: xs_4 has unknown type: 0x800
Item: xs_5 has unknown type: 0x800
Item: xs_6 has unknown type: 0x800
Item: xs_7 has unknown type: 0x800
Item: xs_8 has unknown type: 0x800
Item: xs_9 has unknown type: 0x800
Item: xs_10 has unknown type: 0x800
```

