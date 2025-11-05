# DAOS Telemetry Example

This document will help to run daos metrics command and collect some key metrics from the 
server to help debug the issue or analyze the system behavior.

## How to run telemetry command:

### Directly on server using daos_metrics command

- Example of collecting the pool query metrics on the servers using daos_metrics command

```
$ daos_metrics -C  | grep pool_query
ID: 0/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/pool_query,0
ID: 0/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/pool_query_space,0
$ daos_metrics -C -S 1 | grep pool_query
ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/pool_query,12
ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/pool_query_space,10
```

### Dmg command on admin node (dmg telemetry metrics query)

- Example of collecting the pool query metrics from individual servers using dmg command

```
$ dmg telemetry metrics query -m engine_pool_ops_pool_query  -l brd-221
connecting to brd-221:9191...
- Metric Set: engine_pool_ops_pool_query (Type: Counter)
  Total number of processed pool query operations
    Metric  Labels                                              Value
    ------  ------                                              -----
    Counter (pool=8259d3ff-523e-4a43-9248-26aba2a62f4c, rank=0) 0
    Counter (pool=8259d3ff-523e-4a43-9248-26aba2a62f4c, rank=1) 0

$ dmg telemetry metrics query -m engine_pool_ops_pool_query  -l brd-222
connecting to brd-222:9191...
- Metric Set: engine_pool_ops_pool_query (Type: Counter)
  Total number of processed pool query operations
    Metric  Labels                                              Value
    ------  ------                                              -----
    Counter (pool=8259d3ff-523e-4a43-9248-26aba2a62f4c, rank=2) 0
    Counter (pool=8259d3ff-523e-4a43-9248-26aba2a62f4c, rank=4) 0

$ dmg telemetry metrics query -m engine_pool_ops_pool_query  -l brd-223
connecting to brd-223:9191...
- Metric Set: engine_pool_ops_pool_query (Type: Counter)
  Total number of processed pool query operations
    Metric  Labels                                              Value
    ------  ------                                              -----
    Counter (pool=8259d3ff-523e-4a43-9248-26aba2a62f4c, rank=3) 12
    Counter (pool=8259d3ff-523e-4a43-9248-26aba2a62f4c, rank=5) 0
```

### Identify the pool UUID and leader rank:
 - Some metrics are only be available on pool leader rank so identify the leader rank for that pool from the pool query command.
 - Below is the example of pool query where leader rank is 1

```
#dmg pool query samir_pool
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

 - Find the leader rank address so that daos_metrics command can be run on that server.

```
#dmg system query -v
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

## Engine Metrics:

|Operation| Description | DAOS Command | Metrics Command | Output |
|:---:| :---: | :---: | :---: |:------------: |
|When engine started | Timestamp of last engine startup | None | `daos_metrics -S 1 -C \| grep 'started_at' \| grep -v pool`|ID: 0/started_at,Tue Oct 28 23:21:24 2025|
|When engine become ready | Timestamp when the engine became ready | None | `daos_metrics -S 1 -C \| grep 'servicing_at'`|ID: 0/servicing_at,Tue Oct 28 23:21:33 2025|
|Find Engine Rank ID | Rank ID of this engine | None | `daos_metrics -S 1  -C  \| grep  '/rank' \| grep -v pool`|ID: 1/rank,276|
|check if Engine is dead | engine_events_dead_ranks | None | `daos_metrics -C \| grep '/dead'`| ID: 0/events/dead_ranks,1 |
|last event on rank | Timestamp of last received event | None | `daos_metrics -S 1  -C  \| grep  '/last_event'`| ID: 1/events/last_event_ts,Thu Jan  1 00:00:00 1970 |

## Pool Metrics:

|Operation| Description | DAOS Command | Metrics Command | Output |
|:---:| :---: | :---: | :---: |:------------: |
|With No Pools| Total number of processed pool connect operations | None | `daos_metrics -C \| grep 'ops/pool'`|None|
|After creating single pool| | dmg pool create <POOL_NAME> |  `daos_metrics -C \| grep 'ops/pool'`| ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_evict,0<br>ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_connect,0<br>ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_disconnect,0<br>ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_query,0<br>ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_query_space,0|
|After querying the single pool without storage|   Total number of processed pool query operations | dmg pool query <POOL_NAME> -t |  `daos_metrics -C \| grep 'ops/pool_query'` | ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_query,1|
|After querying the single pool with storage |   Total number of processed pool query (with operation) operations | dmg pool query <POOL_NAME> |  `daos_metrics -C \| grep 'ops/pool_query_space'` | ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_query_space,1|
|After Creating Container| Total number of processed pool connect operations | daos cont create <POOL_NAME> <CONT_NAME>| `daos_metrics -C \| grep 'ops/pool_connect'`| ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_connect,1|
|After Creating Container| Total number of processed pool disconnect operations | daos cont create <POOL_NAME> <CONT_NAME>| `daos_metrics -C \| grep 'ops/pool_disconnect'`|ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_disconnect,1|
|After Mounting FUSE Container| Total number of processed pool connect operations | dfuse -m <MOUNT_POINT> -p <POOL_NAME> -c <CONT_NAME>| `daos_metrics -C \| grep 'ops/pool_connect'`| ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_connect,2|
|After Unmounting FUSE Container| Total number of processed pool disconnect operations | fusermount3 -u -m <MOUNT_POINT> | `daos_metrics -C \| grep 'ops/pool_disconnect'`| ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_disconnect,2|
|After Pool evict |   Total number of pool handle evict operations | dmg pool evict <POOL_NAME> | `daos_metrics -C \| grep 'ops/pool_evict'`| ID: 1/pool/55cc96d8-5c46-41f4-af29-881d293b6f6f/ops/pool_evict,2|

## Container Metrics:

|Operation| Description | DAOS Command | Metrics Command | Output |
|:---:| :---: | :---: | :---: |:------------: |
| Container creation | Total number of successful container create operations | daos cont create <POOL_NAME> <CONT_NAME> --type='POSIX' | `daos_metrics -C \| grep cont_create \| grep <POOL_UUID>`|ID: 0/pool/c22c6a6c-7e31-4788-90a4-a55d1083d57b/ops/cont_create,1|
| Container query | Total number of successful container query operations | daos container query <POOL_NAME> <CONT_NAME> | `daos_metrics -C \| grep cont_query \| grep <POOL_UUID>`|ID: 0/pool/c22c6a6c-7e31-4788-90a4-a55d1083d57b/ops/cont_query,4|
| Container open | Total number of successful container open operations | dfuse -m <MOUNT_POINT> -p <POOL_NAME> -c <CONT_NAME> | `daos_metrics -C \| grep cont_open \| grep <POOL_UUID>`|ID: 0/pool/c22c6a6c-7e31-4788-90a4-a55d1083d57b/ops/cont_open,3|
| Container destroy | Total number of successful container destroy operations | daos cont destroy <POOL_NAME> <CONT_NAME> | `daos_metrics -C \| grep cont_destroy \| grep <POOL_UUID>`|ID: 0/pool/c22c6a6c-7e31-4788-90a4-a55d1083d57b/ops/cont_destroy,1|

## I/O Metrics:

|Operation| Description | DAOS Command | Metrics Command | Output |
|:---:| :---: | :---: | :---: |:------------: |
| data written | Total number of bytes updated/written |Write the Data using any IO | `daos_metrics  -C -S 1 \| grep <POOl_UUID>  \| grep 'xferred/update'`|ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/update/tgt_0,1335885824<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/update/tgt_7,1337983064<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/update/tgt_2,1342177280<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/update/tgt_4,1325400064<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/update/tgt_6,1337982976<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/update/tgt_5,1384120320<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/update/tgt_1,1332740096<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/update/tgt_3,1341128828|
| data read | Total number of bytes fetched/read | Read the Data using any IO | `daos_metrics  -C -S 1 \| grep <POOl_UUID>  \| grep 'xferred/fetch'`|ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/fetch/tgt_0,1335885824<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/fetch/tgt_7,1337983240<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/fetch/tgt_2,1342177280<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/fetch/tgt_4,1325400064<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/fetch/tgt_6,1337982976<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/fetch/tgt_5,1384120320<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/fetch/tgt_1,1332740096<br>ID: 1/pool/e63e81dd-7d5d-4622-8196-83256b12326c/xferred/fetch/tgt_3,1341129076|
| Write IOPS operation | Total number of processed object RPCs | Write the Data using any IO | `daos_metrics  -S 1 -C \| grep <POOL_UUID> \| grep 'ops/update'`|ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/update/tgt_6,222<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/update/tgt_2,204<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/update/tgt_5,210<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/update/tgt_7,223<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/update/tgt_4,224<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/update/tgt_0,196<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/update/tgt_1,198<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/update/tgt_3,206|
| Read IOPS operation | Total number of processed object RPCs | Read the Data using any IO | `daos_metrics  -S 1 -C \| grep <POOL_UUID> \| grep 'ops/fetch'`|ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/fetch/tgt_6,234<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/fetch/tgt_2,206<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/fetch/tgt_5,215<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/fetch/tgt_7,214<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/fetch/tgt_4,225<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/fetch/tgt_0,192<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/fetch/tgt_1,202<br>ID: 1/pool/8259d3ff-523e-4a43-9248-26aba2a62f4c/ops/fetch/tgt_3,221|
| IO latency Update | update RPC processing time | Write the Data using 1MiB xfersize | `daos_metrics  -S 1 -C \|  grep 'io/latency/update'`|ID: 1/io/latency/update/1MB/tgt_0,34423,9173,239956,24216.092267,1875,45405173,35287.151469<br>ID: 1/io/latency/update/1MB/tgt_1,34824,9195,224337,24619.489373,1882,46333879,35692.908836<br>ID: 1/io/latency/update/1MB/tgt_2,17586,9187,246820,25627.308223,1885,48307476,37184.782868<br>ID: 1/io/latency/update/1MB/tgt_3,60684,9182,264286,25998.202265,1943,50514507,38227.372221<br>ID: 1/io/latency/update/1MB/tgt_4,83487,9193,235707,26626.855799,1914,50963802,37382.179815<br>ID: 1/io/latency/update/1MB/tgt_5,26402,9200,235859,24656.685802,1951,48105194,34931.529382<br>ID: 1/io/latency/update/1MB/tgt_6,107294,9190,244975,26761.485861,1945,52051090,38022.684882<br>ID: 1/io/latency/update/1MB/tgt_7,79041,9213,219362,25710.023921,1923,49440376,36611.272385|
| IO latency Fetch | fetch RPC processing time | Read the Data using 1MiB xfersize | `daos_metrics  -S 1 -C \|  grep 'io/latency/fetch'`|ID: 1/io/latency/fetch/1MB/tgt_0,29630,9419,225908,19060.848723,1527,29105916,26764.971072<br>ID: 1/io/latency/fetch/1MB/tgt_1,18329,9406,343931,17769.093144,1546,27471018,23882.809783<br>ID: 1/io/latency/fetch/1MB/tgt_2,9887,9385,131315,18075.996768,1547,27963567,22973.594024<br>ID: 1/io/latency/fetch/1MB/tgt_3,39508,9411,155136,19332.508228,1580,30545363,25593.694908<br>ID: 1/io/latency/fetch/1MB/tgt_4,22616,9413,412206,19062.688062,1558,29699668,27359.057624<br>ID: 1/io/latency/fetch/1MB/tgt_5,22280,9418,126520,17382.379032,1612,28020395,20937.262665<br>ID: 1/io/latency/fetch/1MB/tgt_6,40743,9409,207370,18697.681472,1576,29467546,23236.574768<br>ID: 1/io/latency/fetch/1MB/tgt_7,24048,9417,112182,17725.164955,1558,27615807,21375.496411|
