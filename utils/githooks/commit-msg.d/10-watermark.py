#!/usr/bin/env python3
# Disable check for module name. disable-next doesn't work here.
# pylint: disable=invalid-name
# pylint: enable=invalid-name
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
    """Find the required git hooks"""
    hooks = []
    for fname in os.listdir('utils/githooks'):
        if fname == "commit-msg.d":
            continue
        if not fname.endswith('.d'):
            continue
        if not os.path.isdir(os.path.join("utils/githooks", fname)):
            continue
        hooks.append(fname[:-2])
    return hooks


def emit_watermark(msg):
    """Print the watermark to the commit message"""
    msg.write("Required-githooks: true\n")
    skipped = os.environ.get("DAOS_GITHOOK_SKIP", None)
    if skipped:
        msg.write(f"Skipped-githooks: {skipped}\n")
    msg.write("\n")


def run_check():
    """Run the checks for the required commit hooks"""
    empty_commit = True
    msg = []
    with open(sys.argv[1], "r") as commit_msg:
        msg = commit_msg.readlines()
        for line in msg:
            if line.startswith("Required-githooks: true"):
                sys.exit(0)
            elif line.startswith("Signed-off-by"):
                continue
            elif line.startswith("Change-Id"):
                continue
            elif line.strip() == "":
                continue
            elif line.startswith("# ------------------------ >8 ------------------------"):
                break
            elif line.startswith("#"):
                continue
            empty_commit = False
    if empty_commit:
        sys.exit(0)

    hooks = find_hooks()
    if not all(list(map(check_if_run, hooks))):
        sys.exit(0)

    with open(sys.argv[1], "w") as commit_msg:
        hook_emitted = False
        for line in msg:
            if not hook_emitted:
                if line.startswith("Change-Id"):
                    emit_watermark(commit_msg)
                    hook_emitted = True
                if line.startswith("Signed-off-by"):
                    emit_watermark(commit_msg)
                    hook_emitted = True
                elif line.startswith("#"):
                    emit_watermark(commit_msg)
                    hook_emitted = True
            commit_msg.write(line)


if __name__ == "__main__":
    print("Checking that required hooks ran")
    run_check()
