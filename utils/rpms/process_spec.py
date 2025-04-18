#!/bin/env python3
import os
import re


def process_input(fname, script_dir, out):
    """Read one spec file and write to out"""
    with open(f"{script_dir}/{fname}", "r") as inspec:
        for line in inspec.readlines():
            match = re.match(r"^__include__\s+(\S+\.spec)", line)
            if not match:
                out.write(line)
                continue
            process_input(match.group(1), script_dir, out)


def process_spec():
    """Convert daos.spec.in to daos.spec"""
    script_dir = os.path.dirname(os.path.realpath(__file__))
    with open(f"{script_dir}/daos.spec", "w") as out:
        process_input("daos.spec.in", script_dir, out)


if __name__ == '__main__':
    process_spec()
