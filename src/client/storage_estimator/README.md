
# Storage Estimation Tool

The daos_storage_estimator.py tool estimates the utilization of the Storage Class Memory (SCM) required for DAOS deployments. DAOS uses the <a href="https://github.com/daos-stack/daos/blob/master/src/vos/README.md">Versioning Object Store (VOS)</a> to keep track of the DAOS objects metadata.
There are three options to feed the tool with the description of the items that will be stored in the <a href="https://github.com/daos-stack/daos/blob/master/src/client/dfs/README.md">DAOS File system</a>.

```
$ daos_storage_estimator.py -h
usage: daos_storage_estimator.py [-h]
                            {create_example,explore_fs,read_yaml,read_csv} ...

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

The daos_storage_estimator.py can ingest a csv file with the description of directories, symbolic links and files. The files are divided on buckets of different sizes. <a href="common/tests/test_data.csv">test_data.csv</a> is an example of the csv format.

```
$ daos_storage_estimator.py read_csv test_data.csv
Using DAOS version: 1.1.0
Total files 55226 files
Processing 4931 directories
Unsupported 0 items
Processing 895 symlinks
  Calculating average
  assuming 1 symlinks per directory
  assuming average symlink size of 0 bytes
  assuming 12 files and directories per directory
  Reading VOS structures from current installation
Metadata totals:
        pool                :     273.44 K ( 0.00%)
        container           :     664.06 K ( 0.00%)
        object              :       7.96 G ( 0.00%)
        dkey                :      21.50 G ( 0.01%)
        akey                :      23.23 G ( 0.01%)
        single_value        :      14.75 M ( 0.00%)
        array               :     127.14 G ( 0.08%)
        total_meta          :     179.85 G ( 0.11%)
        user_meta           :       1.96 M ( 0.00%)
        user_value          :     156.39 T (99.89%)
        scm_total           :     179.86 G ( 0.11%)
Total bytes with user data:     156.57 T
```

## YAML input file

The daos_storage_estimator.py can process a yaml file with the full description of every supported item that will be stored on the DAOS File System using the Versioning Object Store data structures. In other words, the yaml file has a description of all the akeys and dkeys used to store and represent each item. <a href="common/tests/test_data.yaml">test_data.yaml</a> is an example of the yaml format. It represents the following file structure:

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
$ daos_storage_estimator.py read_yaml src/client/storage_estimator/common/tests/test_data.yaml
Using DAOS version: 1.1.0
  Reading VOS structures from current installation
Metadata totals:
        pool                :     273.44 K ( 1.91%)
        container           :     664.06 K ( 4.64%)
        object              :      22.43 K ( 0.16%)
        dkey                :       4.30 K ( 0.03%)
        akey                :       4.86 K ( 0.03%)
        single_value        :       1.75 K ( 0.01%)
        array               :      19.63 K ( 0.14%)
        total_meta          :     990.48 K ( 6.92%)
        user_meta           :         80   ( 0.00%)
        user_value          :      13.00 M (93.07%)
        scm_total           :     991.92 K ( 6.93%)
Total bytes with user data:      13.97 M
```

## Reading files and directories

It is possible to measure a given set of files and directories by passing the path to the daos_storage_estimator.py. The tool then, will account and measure all the items under that path.
It is possible to save the a yaml file with the statistics and its representation by using the --output flag and providing a file name.

```
$ daos_storage_estimator.py explore_fs /mnt/storage
Using DAOS version: 1.1.0
processing path: /mnt/storage

Summary:

  directories 113487 count  1 MiB
  files        12633 count  4 GiB
  symlinks      1585 count 45 KiB
  errors           4 count

  total count   127709
  total size    4 GiB (4626320138 bytes)

  Reading VOS structures from current installation
Metadata totals:
        pool                :     273.44 K ( 0.01%)
        container           :     664.06 K ( 0.01%)
        object              :      61.42 M ( 1.27%)
        dkey                :      51.61 M ( 1.07%)
        akey                :      51.61 M ( 1.07%)
        single_value        :      30.30 M ( 0.63%)
        array               :     193.65 M ( 4.01%)
        total_meta          :     389.51 M ( 8.06%)
        user_meta           :       1.75 M ( 0.04%)
        user_value          :       4.33 G (91.90%)
        scm_total           :     502.78 M (10.41%)
Total bytes with user data:       4.72 G
```

To speed up the estimation analysis it is recommended to use the average analysis by adding the -x flag to the command line. The estimation results are similar as shown below

```
$ daos_storage_estimator.py explore_fs -x /mnt/storage
Using DAOS version: 1.1.0
processing path: /mnt/storage

Summary:

  directories 113487 count  1 MiB
  files        12633 count  4 GiB
  symlinks      1585 count 45 KiB
  errors           4 count

  total count   127709
  total size    4 GiB (4626320138 bytes)

  Reading VOS structures from current installation
Metadata totals:
        pool                :     273.44 K ( 0.01%)
        container           :     664.06 K ( 0.01%)
        object              :      60.66 M ( 1.26%)
        dkey                :      51.21 M ( 1.06%)
        akey                :      51.22 M ( 1.06%)
        single_value        :      30.30 M ( 0.63%)
        array               :     192.06 M ( 3.98%)
        total_meta          :     386.36 M ( 8.00%)
        user_meta           :       1.69 M ( 0.03%)
        user_value          :       4.33 G (91.96%)
        scm_total           :     416.89 M ( 8.64%)
Total bytes with user data:       4.71 G
```

## Advanced Usage

It is possible to play around with the assumptions that daos_storage_estimator.py uses. The number of VOS pools and even its internal structures can be changed. First, you need to dump the vos_size.yaml file.
Then, customizes it. And finally passing it to the daos_storage_estimator.py tool by using -m flag. You can also generate and example of a DFS for your reference.

```
$ daos_storage_estimator.py create_example -v
Using DAOS version: 1.1.0
Vos metadata overhead:
  Reading VOS structures from current installation
  Output file: vos_size.yaml
  Output file: vos_dfs_sample.yaml
```

Finally, you can use these files as templates to feed the estimation tool.

```
$ daos_storage_estimator.py read_yaml vos_dfs_sample.yaml --meta vos_size.yaml
Using DAOS version: 1.1.0
Metadata totals:
        pool                :     273.44 K ( 0.00%)
        container           :     664.06 K ( 0.01%)
        object              :     344.08 M ( 2.85%)
        dkey                :     422.96 M ( 3.51%)
        akey                :     564.58 M ( 4.68%)
        single_value        :     267.03 M ( 2.21%)
        array               :       2.33 G (19.82%)
        total_meta          :       3.90 G (33.08%)
        user_meta           :      15.26 M ( 0.13%)
        user_value          :       7.87 G (66.79%)
        scm_total           :       4.15 G (35.23%)
Total bytes with user data:      11.78 G
```
