# Run DAOS Autotest

DAOS autotest tests the proper setup of the DAOS configuration, which is used to test the setup.
DAOS autotest performs various activities like connecting to a pool, creating and opening
a container, then reading and writing to the container.

First of all, a pool must be created with dmg to run autotest:

```sh
$ dmg pool create --size=50G autotest_pool

# sample output
Creating DAOS pool with automatic storage allocation: 50 GB NVMe + 6.00% SCM
Pool created with 6.00% SCM/NVMe ratio
---------------------------------------
  UUID          : 6af46954-f704-45f0-8b80-115ef855a065
  Service Ranks : [1-3]
  Storage Ranks : [0-3]
  Total Size    : 53 GB
  SCM           : 3.0 GB (750 MB / rank)
  NVMe          : 50 GB (12 GB / rank)
```

Then run autotest via the daos utility:

```sh
$ daos pool autotest autotest_pool

# Sample output
Step Operation               Status Time(sec) Comment
  0  Initializing DAOS          OK      0.000
  1  Connecting to pool         OK      0.070
  2  Creating container         OK      0.000  uuid =
  3  Opening container          OK      0.050
 10  Generating 1M S1 layouts   OK      4.620
 11  Generating 10K SX layouts  OK      0.140
 20  Inserting 1M 128B values   OK     75.130
 21  Reading 128B values back   OK     71.540
 24  Inserting 1M 4KB values    OK    109.190
 25  Reading 4KB values back    OK    103.620
 28  Inserting 100K 1MB values  OK    413.730
 29  Reading 1MB values back    OK    461.220
 96  Closing container          OK      0.040
 97  Destroying container       OK      0.070
 98  Disconnecting from pool    OK      0.000
 99  Tearing down DAOS          OK      0.000
```
