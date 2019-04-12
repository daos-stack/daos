#!/usr/bin/env python
'''
  (C) Copyright 2019 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.

  Takes customer csv data and parses it.  Mostly checking in for backup purpose
'''
import sys

FILE_SIZES = ['4k', '64k', '128k', '256k', '512k', '768k', '1m', '8m', '64m',
              '128m', '1g', '10g', '100g', '250g', '500g', '1t', '10t', '100t']

def split(array, indent):
    """Split an array into a pretty string"""
    last = array[-1]
    full_str = ""
    mystr = ""
    while array:
        if array[0] == last:
            thisstr = array[0]
        else:
            thisstr = "%s, " % array[0]
        if (len(mystr) + len(thisstr)) > (80 - indent):
            full_str = full_str + "%*s" % (indent, " ") + mystr.strip() + "\n"
            mystr = ""
            continue
        mystr = mystr + thisstr
        array = array[1:]
    return full_str + "%*s" % (indent, " ") + mystr.strip()

def produce_results(args, file_histo, dir_histo):
    """Produce yaml file"""
    with open(args.output, "w") as yaml_file:
        yaml_file.write("---\n# Generated file data\n")
        yaml_file.write("""# Assumptions:
# Average file name size: {0}
# Average link size:      {1}
# Chunk size:             {2}
# I/O size:               {3}
num_pools: {4}
# File timestamps
time_key: &time
  count: 3
  size: 5
  overhead: meta
  value_type: single_value
  values: [{{"overhead": "meta", "count": 3, "size": 8}}]

# Object ID key
oid_key: &oid
  size: 3
  overhead: meta
  value_type: single_value
  values: [{{"overhead": "meta", "size": 16}}]

# File mode
mode_key: &mode
  size: 4
  overhead: meta
  value_type: single_value
  values: [{{"overhead": "meta", "size": 4}}]

# symlink.  Hard coded to average length of {1} bytes
link_key: &link
  size: 4
  overhead: meta
  value_type: single_value
  values: [{{"size": {1}}}]

# File directory entry.  Hard coded filename length {0} bytes
file_dirent_key: &file_dirent
  count: {5}
  size: {0}
  akeys: [*time, *oid, *mode]

# File directory entry.  Hard coded filename length {0} bytes
link_dirent_key: &link_dirent
  count: {6}
  size: {0}
  akeys: [*time, *mode, *link]

# Directory
dir_obj: &dir
  count: {7}
  dkeys: [*file_dirent, *link_dirent]

# Array metadata for file data
array_meta: &file_meta
  size: 19
  overhead: meta
  value_type: single_value
  values: [{{"overhead": "meta", "size": 24}}]

""".format(args.name, args.link, args.chunk_size, args.io_size, args.num_pools,
           dir_histo["avgfiles"], dir_histo["avglink"], dir_histo["count"]))
        obj = ["*dir"]
        full_chunk_writes = args.chunk_size / args.io_size
        for size in file_histo.keys():
            num_chunks = file_histo[size]["avgsize"] / args.chunk_size
            remainder = file_histo[size]["avgsize"] % args.chunk_size
            yaml_file.write("# Files in %s bucket\n" % size)
            num_full = 0
            num_partial = 0
            if num_chunks:
                num_full = num_chunks - 1
                yaml_file.write("""{0}_array_akey: &{0}_file_data
  size: 1
  overhead: meta
  value_type: array
  values: [{{"count": {1}, "size": {2}}}]

""".format(size, full_chunk_writes, args.io_size))
                dkey0_akey = "*%s_file_data" % size
            if remainder:
                if num_chunks:
                    num_partial = 1
                else:
                    dkey0_akey = "*%s_file_tail" % size
                last_count = remainder / args.io_size
                last_partial = remainder % args.io_size
                lst_val = []
                if last_count:
                    lst_val.append('{"count": %d, "size": %d}' % (last_count,
                                                                  args.io_size))
                if last_partial:
                    lst_val.append('{"count": 1, "size": %d}' % (last_partial))
                yaml_file.write("""{0}_array_akey_tail: &{0}_file_tail
  size: 1
  overhead: meta
  value_type: array
  values: [
{1}
  ]

""".format(size, split(lst_val, 4)))
            yaml_file.write("""{0}_dkey_0: &{0}_dkey0
  count: 1
  type: integer
  akeys: [{1}, *file_meta]

""".format(size, dkey0_akey))
            dkeys = ["*%s_dkey0" % size]
            if num_full:
                dkeys.append("*%s_dkeyfull" % size)
                yaml_file.write("""{0}_dkey_full: &{0}_dkeyfull
  count: {1}
  type: integer
  akeys: [*{0}_file_data]

""".format(size, num_full))

            if num_partial:
                dkeys.append("*%s_dkeytail" % size)
                yaml_file.write("""{0}_dkey_tail: &{0}_dkeytail
  count: 1
  type: integer
  akeys: [*{0}_file_tail]

""".format(size))
            yaml_file.write("""{0}_file_key: &{0}_file
  count: {1}
  dkeys: [
{2}
  ]

""".format(size, file_histo[size]["count"], split(dkeys, 4)))
            obj.append("*%s_file" % size)
        yaml_file.write("""posix_key: &posix
  objects: [
{0}
  ]

containers: [*posix]
""".format(split(obj, 4)))


def ingest(fname, args):
    """Parse csv and produce yaml input to vos_size.py"""
    value_dict = {}
    with open(fname, "r") as csv_file:
        idx = 0
        fields = csv_file.readline().strip().split(',')
        values = csv_file.readline().strip().split(',')
        if len(fields) != len(values):
            print "CSV must provide one row of values that matches fields"
            print "Number of fields is %d" % len(fields)
            print "Number of values is %d" % len(values)
            sys.exit(-1)
        for name in fields:
            value_dict[name] = values[idx]
            idx += 1
        file_histo = {}
        for size in FILE_SIZES:
            num_files = int(value_dict["%s_count" % size])
            total_size = int(value_dict["%s_size" % size])
            if num_files != 0:
                file_histo[size] = {"count": num_files,
                                    "avgsize": total_size / num_files}
        dir_count = int(value_dict["dir_count"])
        link_count = int(value_dict["link_count"])
        if link_count < dir_count:
            link_count = dir_count # assume at least one link per dir
        obj_count = int(value_dict["total_objects"])
        dir_histo = {"count": dir_count,
                     "avgfiles": obj_count / dir_count,
                     "avglink": link_count / dir_count}
        produce_results(args, file_histo, dir_histo)

def run_main():
    """Run the program"""
    import argparse

    parser = argparse.ArgumentParser(description="Estimate VOS Overhead")
    parser.add_argument('csv', metavar='CSV', type=str, nargs=1,
                        help='Input CSV (assumes Argonne format)')
    parser.add_argument('--name-len', type=int, dest="name",
                        help='Average file name length', default=32)
    parser.add_argument('--link-len', type=int, dest="link",
                        help='Average link length', default=32)
    parser.add_argument('--chunk-size', dest="chunk_size", type=int,
                        help='Array chunk size.  Must be multiple of I/O size',
                        default=1048576)
    parser.add_argument('--num-pools', dest="num_pools", type=int,
                        help='Number of vos pools', default=1000)
    parser.add_argument('--io-size', dest="io_size", type=int,
                        help='I/O size', default=1048576)
    parser.add_argument('--output', dest="output", type=str,
                        help='Output file name', default="output.yaml")
    args = parser.parse_args()

    if args.io_size > args.chunk_size:
        print "Assuming --io-size set to --chunk-size %d\n" % args.chunk_size
        args.io_size = args.chunk_size

    if args.chunk_size % args.io_size != 0:
        print "--chunk-size must be evenly divisble by --io-size"
        parser.print_help()
        sys.exit(-1)

    ingest(args.csv[0], args)

if __name__ == "__main__":
    run_main()
