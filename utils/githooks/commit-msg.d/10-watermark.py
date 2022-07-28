#!/usr/bin/env python3
"""Add watermark to commit message"""
import os
import sys

def check_if_run(name):
    """Check existence of a file, removes file"""
    if not os.path.exists(f".{name}"):
        print(f"Required githook {name} is not installed. See utils/githooks/README.md")
        return False
    os.remove(f".{name}")
    return True

def find_hooks():
    """Find the required githooks"""
    hooks = []
    for fname in os.listdir('utils/githooks'):
        if fname == "commit-msg.d":
            continue
        if not fname.endswith('.d'):
            continue
        if not os.path.isdir(os.path.join("utils/githooks")):
            continue
        hooks.append(fname[:-2])
    return hooks

def run_check():
    """Run the checks for the required commit hooks"""
    empty_commit = True
    msg = []
    with open(sys.argv[1], "r") as commit_msg:
        for line in commit_msg.readlines():
            if line.startswith("Required-githooks: true"):
                sys.exit(0)
            elif line.startswith("Signed-off-by"):
                continue
            elif line.strip() == "":
                continue
            elif line.startswith("#"):
                break
            empty_commit = False
    if empty_commit:
        sys.exit(0)

    hooks = find_hooks()
    missing = False
    for hook in hooks:
        if not check_if_run(hook):
            missing = True
    if missing:
        sys.exit(0)

    msg = []
    with open(sys.argv[1], "r") as commit_msg:
        hook_emitted = False
        for line in commit_msg.readlines():
            if not hook_emitted:
                if line.startswith("Signed-off-by"):
                    msg.append("Required-githooks: true\n\n")
                    hook_emitted = True
                elif line.startswith("#"):
                    msg.append("Required-githooks: true\n\n")
                    hook_emitted = True
            msg.append(line)
    with open(sys.argv[1], "w") as commit_msg:
        for line in msg:
            commit_msg.write(line)

if __name__ == "__main__":
    run_check()
