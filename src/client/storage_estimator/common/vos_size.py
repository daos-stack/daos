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
'''
from __future__ import print_function
from __future__ import division
import yaml
import random
import math


def convert(stat):
    """Convert byte value to pretty string"""
    size = 1024 * 1024 * 1024 * 1024 * 1024
    for mag in ['P', 'T', 'G', 'M', 'K']:
        if stat > size:
            return "%10.2f %s" % (float(stat) / size, mag)
        size = size / 1024
    return "%10d  " % stat


def print_total(name, stat, total):
    "Pretty print"
    print("\t%-20s: %s (%5.2f%%)" % (name, convert(stat),
                                     100 * float(stat) / total))


def check_key_type(spec):
    """check key type field"""
    if spec.get("type", "hashed") not in ["hashed", "integer"]:
        raise RuntimeError("Invalid key type key spec %s" % spec)
    if spec.get("type", "hashed") != "hashed":
        return
    if "size" not in spec:
        raise RuntimeError("Size required for hashed key %s" % spec)


class Stats(object):
    """Class for calculating and storing stats"""

    def __init__(self):
        """Construct a stat object"""
        self.stats = {
            "pool": 0,
            "container": 0,
            "object": 0,
            "dkey": 0,
            "akey": 0,
            "array": 0,
            "single_value": 0,
            "user_value": 0,
            "user_meta": 0,
            "total_meta": 0,
            "nvme_total": 0,
            "total": 0
        }

    def mult(self, multiplier):
        """multiply all stats by a value"""
        for key in self.stats:
            self.stats[key] *= multiplier

    def add_meta(self, stat, count):
        """add a single meta data stat"""
        self.stats[stat] += count
        self.stats["total"] += count
        self.stats["total_meta"] += count

    def add_user_value(self, tree):
        """add a user data"""
        count = tree["value_size"]
        self.stats["user_value"] += count
        self.stats["nvme_total"] += tree["nvme_size"]
        self.stats["total"] += count

    def add_user_meta(self, count):
        """add a user key"""
        self.stats["user_meta"] += count
        self.stats["total"] += count

    def merge(self, child):
        """add child stats to this object"""
        for key in self.stats:
            self.stats[key] += child.get(key)

    def get(self, key):
        """get a stat"""
        return self.stats[key]

    def print_stat(self, name):
        """print the statistic"""
        print_total(name, self.stats[name], self.stats["total"])

    def pretty_print(self):
        """Pretty print statistics"""
        print("Metadata breakdown:")
        self.stats["scm_total"] = self.stats["total"] - \
            self.stats["nvme_total"]
        self.print_stat("pool")
        self.print_stat("container")
        self.print_stat("object")
        self.print_stat("dkey")
        self.print_stat("akey")
        self.print_stat("single_value")
        self.print_stat("array")
        self.print_stat("user_meta")
        self.print_stat("total_meta")
        print("Data breakdown:")
        self.print_stat("total_meta")
        self.print_stat("user_value")
        self.print_stat("total")
        print("Physical storage estimation:")
        self.print_stat("scm_total")
        self.print_stat("nvme_total")
        pretty_total = convert(self.stats["total"])
        print("Total storage required: {0}".format(pretty_total))

# pylint: disable=too-many-instance-attributes


class MetaOverhead(object):
    """Class for calculating overheads"""

    def __init__(self, args, num_pools, meta_yaml):
        """class for keeping track of overheads"""
        self.args = args
        self.meta = meta_yaml
        self.num_pools = num_pools
        self.pools = []
        for _index in range(0, self.num_pools):
            self.pools.append({"trees": [], "dup": 1, "key": "container",
                               "count": 0})
        self.next_cont = 1
        self.next_object = 1
        self._scm_cutoff = meta_yaml.get("scm_cutoff", 4096)
        csummers = meta_yaml.get("csummers", {})

    def set_scm_cutoff(self, scm_cutoff):
        self._scm_cutoff = scm_cutoff

    def init_container(self, cont_spec):
        """Handle a container specification"""
        if "objects" not in cont_spec:
            raise RuntimeError("No objects in container spec %s" % cont_spec)

        for pool in self.pools:
            pool["count"] += int(cont_spec.get("count", 1))
            cont = {"dup": int(cont_spec.get("count", 1)), "key": "object",
                    "count": 0,
                    "csum_size": int(cont_spec.get("csum_size", 0)),
                    "csum_gran": int(cont_spec.get("csum_gran", 1048576)),
                    "trees": []}
            pool["trees"].append(cont)

        for obj_spec in cont_spec.get("objects"):
            self.init_object(obj_spec)

    def init_object(self, obj_spec):
        """Handle an object specification"""
        if "dkeys" not in obj_spec:
            raise RuntimeError("No dkeys in object spec %s" % obj_spec)

        oid = self.next_object
        self.next_object += 1

        # zero means distribute across all available targets
        num_of_targets = obj_spec.get("targets", 0)
        if num_of_targets == 0:
            num_of_targets = self.num_pools

        self.init_dkeys(oid, obj_spec, num_of_targets)

    def init_dkeys(self, oid, obj_spec, num_of_targets):
        """Handle akey specification"""
        start_pool = random.randint(0, self.num_pools - 1)
        pool_idx = start_pool

        for dkey_spec in obj_spec.get("dkeys"):
            if "akeys" not in dkey_spec:
                raise RuntimeError("No akeys in dkey spec %s" % dkey_spec)
            check_key_type(dkey_spec)
            dkey_count = int(dkey_spec.get("count", 1))
            num_pools = num_of_targets
            full_count = dkey_count // num_of_targets
            partial_count = dkey_count % num_of_targets
            if full_count == 0:
                num_pools = partial_count
            for idx in range(0, num_pools):
                pool_idx = ((idx % num_of_targets) +
                            start_pool) % self.num_pools
                pool = self.pools[pool_idx]
                cont = pool["trees"][-1]
                if cont["trees"] == [] or cont["trees"][-1]["oid"] != oid:
                    obj = {"dup": int(obj_spec.get("count", 1)), "key": "dkey",
                           "count": 0, "trees": [], "oid": oid}
                    cont["trees"].append(obj)
                    cont["count"] += int(obj_spec.get("count", 1))
                dup = full_count
                if partial_count > idx:
                    dup += 1
                obj = cont["trees"][-1]
                dkey = {"dup": dup, "key": "akey", "count": 0, "trees": [],
                        "type": dkey_spec.get("type", "hashed"),
                        "size": int(dkey_spec.get("size", 0)),
                        "overhead": dkey_spec.get("overhead", "user")}
                obj["trees"].append(dkey)
                obj["count"] += dup
                for akey_spec in dkey_spec.get("akeys"):
                    self.init_akey(cont, dkey, akey_spec)

    def init_akey(self, cont, dkey, akey_spec):
        """Handle akey specification"""
        check_key_type(akey_spec)
        if "values" not in akey_spec:
            raise RuntimeError("No values in akey spec %s" % akey_spec)
        if "value_type" not in akey_spec:
            raise RuntimeError("No value_type in akey spec %s" % akey_spec)
        akey = {"dup": int(akey_spec.get("count", 1)),
                "key": akey_spec.get("value_type"), "count": 0,
                "type": akey_spec.get("type", "hashed"),
                "size": int(akey_spec.get("size", 0)),
                "overhead": akey_spec.get("overhead", "user"),
                "value_size": 0, "meta_size": 0, "nvme_size": 0}
        dkey["trees"].append(akey)
        dkey["count"] += int(akey_spec.get("count", 1))
        for value_spec in akey_spec.get("values"):
            self.init_value(cont, akey, value_spec)

    def init_value(self, cont, akey, value_spec):
        """Handle value specification"""
        if "size" not in value_spec:
            raise RuntimeError("No size in value spec %s" % value_spec)
        size = value_spec.get("size")
        nvme = True
        if self._scm_cutoff > size:
            nvme = False

        akey["count"] += value_spec.get("count", 1)  # Number of values
        if value_spec.get("overhead", "user") == "user":
            akey["value_size"] += size * \
                value_spec.get("count", 1)  # total size
        else:
            akey["meta_size"] += size * \
                value_spec.get("count", 1)  # total size
        if nvme:
            akey["nvme_size"] += size * \
                value_spec.get("count", 1)  # total size

        # Add checksum overhead

        csum_size = cont["csum_size"]
        if akey["key"] == "array":
            csum_size = int(math.ceil(size / cont["csum_gran"]) * csum_size)

        akey["meta_size"] += csum_size * \
            value_spec.get("count", 1)

    def load_container(self, cont_spec):
        """calculate metadata for update(s)"""
        self.init_container(cont_spec)

    def calc_subtrees(self, stats, parent):
        """Calculate for subtrees"""
        for tree in parent["trees"]:
            if parent["key"] == "container":
                self.csum_size = tree["csum_size"]
            self.calc_tree(stats, tree)

    def get_dynamic(self, key, num_values):
        """Handle dynamic tree ordering.  Retrieve number of nodes and size"""
        order = self.meta["trees"][key]["order"]
        max_dyn = 0

        if self.meta["trees"][key]["num_dynamic"] != 0:
            max_dyn = self.meta["trees"][key]["dynamic"][-1]["order"]
        if num_values > max_dyn:
            leaf_node_size = self.meta["trees"][key]["leaf_node_size"]
            int_node_size = self.meta["trees"][key]["int_node_size"]
            tree_nodes = (num_values * 2 + order - 1) // order
            return leaf_node_size, int_node_size, tree_nodes

        if self.meta["trees"][key]["num_dynamic"] == 0:
            return 0, 0, 0

        for item in self.meta["trees"][key]["dynamic"]:
            if item["order"] >= num_values:
                return item["size"], item["size"], 1
        raise "Bug parsing dynamic tree order information!!!"

    def calc_tree(self, stats, tree):
        """calculate the totals"""
        tree_stats = Stats()
        key = tree["key"]
        num_values = tree["count"]
        record_size = self.meta["trees"][key]["record_msize"]
        leaf_size, int_size, tree_nodes = self.get_dynamic(key, num_values)
        rec_overhead = num_values * record_size
        if leaf_size != int_size and tree_nodes != 1:
            leafs = tree_nodes // 2
            ints = tree_nodes - leafs
            overhead = leafs * leaf_size + ints * int_size + rec_overhead
        else:
            overhead = tree_nodes * leaf_size + rec_overhead
        if key == "akey" or key == "single_value" or key == "array":
            # key refers to child tree
            if tree["overhead"] == "user":
                tree_stats.add_user_meta(num_values * tree["size"])
            else:
                tree_stats.add_meta(key, num_values * tree["size"])
            overhead += self.csum_size * num_values
        tree_stats.add_meta(key, overhead)
        if key == "array" or key == "single_value":
            tree_stats.add_user_value(tree)
            tree_stats.add_meta(key, tree["meta_size"])
            stats.merge(tree_stats)
            return
        self.calc_subtrees(tree_stats, tree)
        tree_stats.mult(tree["dup"])
        stats.merge(tree_stats)

    def print_report(self):
        """Calculate and pretty print a report"""
        stats = Stats()

        for pool in range(0, self.num_pools):
            stats.add_meta("pool", int(self.meta.get("root")))
            self.calc_tree(stats, self.pools[pool])

        stats.pretty_print()
