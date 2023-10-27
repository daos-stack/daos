#!/usr/bin/env python3
"""Run evt_ctl with a specific pattern that causes a segfault with the default sort algorithm"""
import argparse
import json
import os
from os.path import join


class EVTStress():
    """Helper class for running the test"""
    def __init__(self):
        parser = argparse.ArgumentParser(description='Run evt_ctl with pattern from DAOS-11894')
        parser.add_argument('--algo', default='dist', choices=['dist', 'dist_even', 'soff'])
        self.args = parser.parse_args()

        file_self = os.path.dirname(os.path.abspath(__file__))
        json_file = None
        while True:
            new_file = join(file_self, '.build_vars.json')
            if os.path.exists(new_file):
                json_file = new_file
                break
            file_self = os.path.dirname(file_self)
            if file_self == '/':
                raise FileNotFoundError('build file not found')
        with open(json_file, 'r', encoding='utf-8') as ofh:
            self._bc = json.load(ofh)

    def __getitem__(self, key):
        return self._bc[key]

    def run_cmd(self):
        """Run the configured command"""
        algo = ""
        if self.args.algo != "dist":
            algo = f"-s {self.args.algo}"
        test_name = f"\"evtree stress {self.args.algo}\""

        cmd = f"{self['PREFIX']}/bin/evt_ctl --start-test {test_name} {algo} -C o:23"
        for start in range(1, 707):
            end = 1024
            cmd += f" -a {start}-{end}@{start}"
        cmd += " -b -2 -D"

        os.system(cmd)


def run():
    """Run the stress test"""

    conf = EVTStress()

    conf.run_cmd()


if __name__ == "__main__":
    run()
