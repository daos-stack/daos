# Run dbench

Install dbench on all client nodes:

```sh
sudo yum install dbench
```

From one of the client node:

```sh
$ dbench --clients-per-process 10 --directory /tmp/daos_dfuse/ --loadfile /usr/share/dbench/client.txt --timelimit 10 10

dbench version 4.00 - Copyright Andrew Tridgell 1999-2004

Running for 10 seconds with load '/usr/share/dbench/client.txt' and minimum warmup 2 secs
failed to create barrier semaphore
9 of 10 processes prepared for launch   0 sec
10 of 10 processes prepared for launch   0 sec
releasing clients
   0         3     0.00 MB/sec  warmup   1 sec  latency 826.199 ms
   0         7     0.00 MB/sec  warmup   2 sec  latency 269.284 ms
 100       114   288.13 MB/sec  execute   1 sec  latency 230.428 ms
 100       141   184.47 MB/sec  execute   2 sec  latency 246.159 ms
 100       166   147.88 MB/sec  execute   3 sec  latency 266.298 ms
 100       194   133.59 MB/sec  execute   4 sec  latency 255.767 ms
 100       219   121.64 MB/sec  execute   5 sec  latency 257.980 ms
 100       248   117.41 MB/sec  execute   6 sec  latency 278.191 ms
 100       274   112.64 MB/sec  execute   7 sec  latency 283.694 ms
 100       299   107.89 MB/sec  execute   8 sec  latency 274.483 ms
 100       325   104.57 MB/sec  execute   9 sec  latency 285.639 ms
 100  cleanup  10 sec
 100  cleanup  11 sec
  90  cleanup  12 sec
  70  cleanup  13 sec
  50  cleanup  14 sec
  35  cleanup  15 sec
  20  cleanup  16 sec
   0  cleanup  17 sec

 Operation      Count    AvgLat    MaxLat
 ----------------------------------------
 NTCreateX       3877    24.215   170.761
 Close           3800     0.004     0.022
 Qfileinfo       3110     1.488     4.579
 WriteX         18750     0.274     6.484

Throughput 104.568 MB/sec  100 clients  10 procs  max_latency=285.639 ms
```

List the dfuse mount point:

```sh
# 'testfile' comes from ior run
# 'test-dir.0-0' comes from mdtest run
# 'clients' comes from dbench run
$ ls /tmp/daos_dfuse
clients  test-dir.0-0  testfile
```
