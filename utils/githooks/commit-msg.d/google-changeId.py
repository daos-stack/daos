#!/usr/bin/env python3
"""Wrapper for changeId hook"""
import os
import sys
import subprocess

def run_changeid_hook():
    """Execute the changeId.sh hook from user area"""
    skip = True
    msg = []

    hookpath = os.path.realpath(os.path.join('.git', 'hooks', 'commit-msg'))
    if not os.path.exists(hookpath):
        print(f"Please install {hookpath}")
        sys.exit(0)

    # Detect if the message is empty or already has a Change-Id. Skip the real
    # commit-msg hook in either case.
    with open(sys.argv[1], "r") as commit_msg:
        msg = commit_msg.readlines()
        for line in msg:
            if line.startswith("Change-Id:"):
                sys.exit(0)
            elif line.startswith("Signed-off-by"):
                continue
            elif line.strip() == "":
                continue
            elif line.startswith("# ------------------------ >8 ------------------------"):
                break
            elif line.startswith("#"):
                continue
            skip = False
    if skip:
        sys.exit(0)

    args = sys.argv
    args[0] = hookpath
    return subprocess.call(args)


if __name__ == "__main__":
    run_changeid_hook()
