
# Storage Estimation Tool

The daos_storage_estimator.py tool estimates the utilization of the Storage Class Memory (SCM) required for DAOS deployments. DAOS uses the <a href="https://github.com/daos-stack/daos/blob/master/src/vos/README.md">Versioning Object Store (VOS)</a> to keep track of the DAOS objects metadata.
There are three options to feed the tool with the description of the items that will be stored in the <a href="https://github.com/daos-stack/daos/blob/master/src/client/dfs/README.md">DAOS File system</a>.

```
$ daos_storage_estimator.py -h
usage: daos_storage_estimator.py [-h]

                                 {create_example,explore_fs,read_yaml,read_csv}
                                 ...

DAOS estimation tool This CLI is able to estimate the SCM/NVMe ratios

optional arguments:
  -h, --help            show this help message and exit

subcommands:
  valid subcommands

  {create_example,explore_fs,read_yaml,read_csv}
    create_example      Create a YAML example of the DFS layout
    explore_fs          Estimate the VOS overhead from a given tree directory
    read_yaml           Estimate the VOS overhead from a given YAML file
    read_csv            Estimate the VOS overhead from a given CSV file
```

## CSV input file

The daos_storage_estimator.py can ingest a csv file with the description of directories, symbolic links and files. The files are divided on buckets of different sizes. <a href="common/tests/test_files/test_data.csv">test_data.csv</a> is an example of the csv format.

```
$ daos_storage_estimator.py read_csv test_data.csv
Using DAOS version: 1.1.2.1
  Reading VOS structures from current installation
Metadata breakdown:
        pool                :     273.44 K ( 0.00%)
        container           :     664.06 K ( 0.00%)
        object              :      28.16 M ( 0.00%)
        dkey                :       3.03 G ( 0.01%)
        akey                :       3.30 G ( 0.01%)
        single_value        :       5.22 M ( 0.00%)
        array               :      25.32 G ( 0.11%)
        user_meta           :       1.05 M ( 0.00%)
        total_meta          :      31.69 G ( 0.14%)
Data breakdown:
        total_meta          :      31.69 G ( 0.14%)
        user_value          :      22.17 T (99.86%)
        total               :      22.20 T (100.00%)
Physical storage estimation:
        scm_total           :      31.71 G ( 0.14%)
        nvme_total          :      22.17 T (99.86%)
Total storage required:      22.20 T
```

## YAML input file

The daos_storage_estimator.py can process a yaml file with the full description of every supported item that will be stored on the DAOS File System using the Versioning Object Store data structures. In other words, the yaml file has a description of all the akeys and dkeys used to store and represent each item. <a href="common/tests/test_files/test_data_sx.yaml">test_data.yaml</a> is an example of the yaml format. It represents the following file structure:

```
.
+-- data
|   +-- deploy
|   |   +-- driver.bin                                     5767168 bytes
|   |   +-- my_file -> ../../specs/very_importan_file.txt
|   +-- secret_plan.txt                                    3670016 bytes
+-- specs
    +-- readme.txt                                         1572864 bytes
    +-- very_importan_file.txt                             2621440 bytes
```

And its estimation is:

```
$ daos_storage_estimator.py read_yaml test_data_sx.yaml
Using DAOS version: 1.1.2.1
  Reading VOS structures from current installation
Metadata breakdown:
        pool                :     273.44 K ( 1.91%)
        container           :     664.06 K ( 4.64%)
        object              :      12.38 K ( 0.09%)
        dkey                :       3.91 K ( 0.03%)
        akey                :       4.86 K ( 0.03%)
        single_value        :       1.75 K ( 0.01%)
        array               :      23.81 K ( 0.17%)
        user_meta           :         80   ( 0.00%)
        total_meta          :     984.20 K ( 6.88%)
Data breakdown:
        total_meta          :     984.20 K ( 6.88%)
        user_value          :      13.00 M (93.12%)
        total               :      13.96 M (100.00%)
Physical storage estimation:
        scm_total           :     985.65 K ( 6.89%)
        nvme_total          :      13.00 M (93.11%)
Total storage required:      13.96 M
```

## Reading files and directories

It is possible to measure a given set of files and directories by passing the path to the daos_storage_estimator.py. The tool then, will account and measure all the items under that path.
It is possible to save the a yaml file with the statistics and its representation by using the --output flag and providing a file name.

```
$ daos_storage_estimator.py explore_fs /mnt/storage
Using DAOS version: 1.1.2.1
  Reading VOS structures from current installation

Summary:

  directories 123867 count 3 MiB
  files       59080 count 204 GiB
  symlinks    106 count 2 KiB
  errors      0 count

  total count   183053
  total size    204 GiB (219098587396 bytes)

Metadata breakdown:
        pool                :     273.44 K ( 0.00%)
        container           :     664.06 K ( 0.00%)
        object              :      56.17 M ( 0.03%)
        dkey                :      84.73 M ( 0.04%)
        akey                :      90.32 M ( 0.04%)
        single_value        :      33.08 M ( 0.02%)
        array               :     464.37 M ( 0.22%)
        user_meta           :       3.63 M ( 0.00%)
        total_meta          :     729.57 M ( 0.35%)
Data breakdown:
        total_meta          :     729.57 M ( 0.35%)
        user_value          :     204.08 G (99.65%)
        total               :     204.80 G (100.00%)
Physical storage estimation:
        scm_total           :     815.50 M ( 0.39%)
        nvme_total          :     204.00 G (99.61%)
Total storage required:     204.80 G
```

## Advanced Usage

It is possible to play around with the assumptions that daos_storage_estimator.py uses. The number of VOS pools and even its internal structures can be changed. First, you need to dump the vos_size.yaml file.
Then, customizes it. And finally passing it to the daos_storage_estimator.py tool by using -m flag. You can also generate and example of a DFS for your reference.

```
$ daos_storage_estimator.py create_example -v
Using DAOS version: 1.1.2.1
  Reading VOS structures from current installation
Vos metadata overhead:
  Output file: vos_size.yaml
  Output file: vos_dfs_sample.yaml
```

Finally, you can use these files as templates to feed the estimation tool.

```
$ daos_storage_estimator.py read_yaml vos_dfs_sample.yaml --meta vos_size.yaml
Using DAOS version: 1.1.2.1
  Reading VOS structures from current installation
Metadata breakdown:
        pool                :     273.44 K ( 0.00%)
        container           :     664.06 K ( 0.01%)
        object              :     172.42 M ( 1.45%)
        dkey                :     407.70 M ( 3.43%)
        akey                :     564.58 M ( 4.75%)
        single_value        :     267.03 M ( 2.25%)
        array               :       2.33 G (20.13%)
        user_meta           :      15.26 M ( 0.13%)
        total_meta          :       3.71 G (32.03%)
Data breakdown:
        total_meta          :       3.71 G (32.03%)
        user_value          :       7.87 G (67.84%)
        total               :      11.60 G (100.00%)
Physical storage estimation:
        scm_total           :       3.97 G (34.21%)
        nvme_total          :       7.63 G (65.79%)
Total storage required:      11.60 G
```

## Case Study

We would like to estimate the amount of SCM and NVMe memory required to store the POSIX items described by <a href="common/tests/test_files/test_data.csv">test_data.csv</a> into a single container.
Let's imagine that the target system has 20 store node and 16 targets per node. That give us a total us 320 pools.

Below are three examples using different error correction and detection techniques.

### Cyclic Redundancy Check (CRC-32)

```
$ daos_storage_estimator.py read_csv test_data.csv --file_oclass SX --dir_oclass S1 --num_shards 320 --chunk_size 1M --checksum crc32
Using DAOS version: 1.1.2.1
  Reading VOS structures from current installation
Metadata breakdown:
        pool                :      87.50 K ( 0.00%)
        container           :     212.50 K ( 0.00%)
        object              :      15.75 M ( 0.00%)
        dkey                :       3.04 G ( 0.01%)
        akey                :       3.39 G ( 0.01%)
        single_value        :       5.66 M ( 0.00%)
        array               :      26.71 G ( 0.12%)
        user_meta           :       1.05 M ( 0.00%)
        total_meta          :      33.16 G ( 0.15%)
Data breakdown:
        total_meta          :      33.16 G ( 0.15%)
        user_value          :      22.17 T (99.85%)
        total               :      22.20 T (100.00%)
Physical storage estimation:
        scm_total           :      33.19 G ( 0.15%)
        nvme_total          :      22.17 T (99.85%)
Total storage required:      22.20 T
```

### 3-Way Replication + Cyclic Redundancy Check (CRC-32)

```
$ daos_storage_estimator.py read_csv test_data.csv --file_oclass RP_3GX --dir_oclass S1 --num_shards 320 --chunk_size 1M --checksum crc32
Using DAOS version: 1.1.2.1
  Reading VOS structures from current installation
Metadata breakdown:
        pool                :      87.50 K ( 0.00%)
        container           :     212.50 K ( 0.00%)
        object              :      33.89 M ( 0.00%)
        dkey                :       9.09 G ( 0.01%)
        akey                :      10.15 G ( 0.01%)
        single_value        :      16.99 M ( 0.00%)
        array               :      80.08 G ( 0.12%)
        user_meta           :       1.05 M ( 0.00%)
        total_meta          :      99.37 G ( 0.15%)
Data breakdown:
        total_meta          :      99.37 G ( 0.15%)
        user_value          :      66.52 T (99.85%)
        total               :      66.61 T (100.00%)
Physical storage estimation:
        scm_total           :      99.44 G ( 0.15%)
        nvme_total          :      66.52 T (99.85%)
Total storage required:      66.61 T
```

### Erasure Code (16+2) + Cyclic Redundancy Check (CRC-32)

```
$ daos_storage_estimator.py read_csv test_data.csv --file_oclass EC_16P2GX --dir_oclass S1 --num_shards 320 --chunk_size 1M --checksum crc32
Using DAOS version: 1.1.2.1
  Reading VOS structures from current installation
Metadata breakdown:
        pool                :      87.50 K ( 0.00%)
        container           :     212.50 K ( 0.00%)
        object              :      20.13 M ( 0.00%)
        dkey                :       3.43 G ( 0.01%)
        akey                :       3.82 G ( 0.01%)
        single_value        :      16.99 M ( 0.00%)
        array               :      30.09 G ( 0.12%)
        user_meta           :       1.05 M ( 0.00%)
        total_meta          :      37.38 G ( 0.15%)
Data breakdown:
        total_meta          :      37.38 G ( 0.15%)
        user_value          :      24.96 T (99.85%)
        total               :      25.00 T (100.00%)
Physical storage estimation:
        scm_total           :      37.44 G ( 0.15%)
        nvme_total          :      24.96 T (99.85%)
Total storage required:      25.00 T
```
