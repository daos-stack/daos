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

Prerequisite
Run vos_size to generate vos_size.yaml
Usage: vos_size.py input.yaml [alternate vos_size.yaml]

Sample input yaml
---
# yaml sample
num_pools: 10
updates:
  - array:
      rec_per_akey: 100
      avgsize: 1000000
      container: a
      object: a
      dkey: 20
      dkey_type: "integer_dkey"
      akey_per_dkey: 1
  - single_value:
      avgsize: 512000
      container: a
      object: a
      dkey: 20
      akey_per_dkey: 10
'''
import sys
import yaml

def print_total(name, pool_stats):
    "Pretty print"
    stat = pool_stats[name]
    total = pool_stats["total"]

    print "\t%-20s: %10d K (%5.2f%%)" % (name, stat / 1024,
                                         100 * float(stat) / total)

# pylint: disable=too-many-instance-attributes
class MetaOverhead(object):
    """Class for calculating overheads"""
    def __init__(self, num_pools, meta_yaml):
        """class for keeping track of overheads"""
        self.meta = meta_yaml
        self.num_pools = num_pools
        self.pools = []
        for _index in range(0, self.num_pools):
            self.pools.append({"key" : "container", "count" : 0})
        self.next_cont = 1341344
        self.next_object = 1341344
        self.value_size = 0
        self.unique_entries = 0
        self.container_key = None

    def init_container(self, array_spec):
        """ Ensure the existence of container records"""
        if "container" in array_spec:
            key = array_spec["container"]
        else:
            key = self.next_cont
            self.next_cont += 1

        for pool in self.pools:
            if key not in pool:
                pool[key] = {"key" : "object", "count": 0} #object count
                pool["count"] += 1

        self.container_key = key

    def update_object(self, array_spec, dkey_type, pool_num):
        """Ensure existence of the object"""
        cont = self.pools[pool_num][self.container_key]
        if "object" in array_spec:
            key = array_spec["object"]
        else:
            key = self.next_object
            self.next_object += 1

        if key not in cont:
            cont[key] = {"key" : dkey_type, "count": 0} #dkey count
            cont["count"] += 1

        return cont[key]

    def update_value(self, array_spec, value_type):
        """Update the value according to the spec"""
        dkey_type = array_spec.get("dkey_type", "dkey")
        akey_type = array_spec.get("akey_type", "akey")

        dkeys = int(array_spec.get("dkey", 1))
        akeys = int(array_spec.get("akey_per_dkey", 1))
        records = int(array_spec.get("rec_per_akey", 1))

        for dkey in range(0, dkeys):
            pool_num = (dkey % self.num_pools)
            obj = self.update_object(array_spec, dkey_type, pool_num)
            if dkey not in obj:
                obj[dkey] = {"key": akey_type, "count": 0} #akey count
                obj["count"] += 1
            dkey_tree = obj[dkey]
            for akey in range(0, akeys):
                if akey not in dkey_tree:
                    dkey_tree[akey] = {"key" : value_type, "count": 0}
                    dkey_tree["count"] += 1
                akey_tree = dkey_tree[akey]
                akey_tree["count"] += records
        unique = dkeys * akeys * records
        self.unique_entries += unique
        self.value_size += unique * int(array_spec.get("avgsize"))

    def update(self, value_type, array_spec):
        """calculate metadata for update(s)"""
        self.init_container(array_spec)
        self.update_value(array_spec, value_type)

    def calc_tree(self, tree, pool_stats):
        """calculate the totals"""
        key = tree["key"]
        num_values = tree["count"]
        record_size = self.meta["trees"][key]["record_msize"]
        node_size = self.meta["trees"][key]["node_size"]
        order = self.meta["trees"][key]["order"]
        rec_overhead = num_values * record_size
        tree_nodes = (num_values * 2 + order -1) / order
        overhead = tree_nodes * node_size + rec_overhead
        key = key.replace("integer_", "")
        pool_stats[key] += overhead
        pool_stats["total"] += overhead
        pool_stats["total_meta"] += overhead
        if key == "array" or key == "single_value":
            return
        for key in tree.keys():
            if key == "count":
                continue
            if key == "key":
                continue
            self.calc_tree(tree[key], pool_stats)

    def print_report(self):
        """Calculate and pretty print a report"""
        pool_stats = {"pool" : 0,
                      "container" : 0,
                      "object" : 0,
                      "dkey" : 0,
                      "akey" : 0,
                      "array" : 0,
                      "single_value" : 0,
                      "total_meta" : 0,
                      "total" : self.value_size
                     }

        for pool in range(0, self.num_pools):
            pool_stats["pool"] += int(self.meta.get("root"))
            pool_stats["total_meta"] += int(self.meta.get("root"))
            pool_stats["total"] += int(self.meta.get("root"))
            self.calc_tree(self.pools[pool], pool_stats)

        print "Metadata totals:"
        print_total("pool", pool_stats)
        print_total("container", pool_stats)
        print_total("object", pool_stats)
        print_total("dkey", pool_stats)
        print_total("akey", pool_stats)
        print_total("single_value", pool_stats)
        print_total("array", pool_stats)
        print_total("total_meta", pool_stats)
        print "Total bytes with user data: %dK" % (pool_stats["total"] / 1024)

# pylint: enable=too-many-instance-attributes

def run_vos_size():
    """Run the tool"""

    if len(sys.argv) < 2:
        print "Usage: %s <configuration> [<meta config>]" % sys.argv[0]
        sys.exit(-1)

    config_name = open(sys.argv[1], "r")
    config_yaml = yaml.load(config_name)

    meta_name = "vos_size.yaml"
    if len(sys.argv) == 3:
        meta_name = sys.argv[2]
    meta_yaml = yaml.load(open(meta_name, "r"))

    num_pools = config_yaml.get("num_pools", 1)

    overheads = MetaOverhead(num_pools, meta_yaml)

    for update in config_yaml.get("updates"):
        for update_type in update.keys():
            if update_type == "array" or update_type == "single_value":
                overheads.update(update_type, update[update_type])

    overheads.print_report()

if __name__ == "__main__":
    run_vos_size()
