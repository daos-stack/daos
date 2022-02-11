import re
import subprocess
import sys

modified_re = re.compile(r'^(?:M|A)(\s+)(?P<name>.*)')

def rebasing():
    process = subprocess.Popen(["git", "branch"],
                               stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)

    if process.wait():
        print("prepare-commit-hook error running git: %s" % process.communicate()[1])
        sys.exit(-1)

    return process.communicate()[0].decode().split('\n')[0].startswith("* (no branch, rebasing")

def submodule_check(modname, msg_file):
    modified = False

    p = subprocess.Popen(['git', 'status', '--porcelain'], stdout=subprocess.PIPE)
    out, _ = p.communicate()
    for line in out.decode().splitlines():
        match = modified_re.match(line)
        if match:
            modified = modified | (match.group('name') == modname)

    if not rebasing() and modified:
        with open(msg_file, 'r') as f:
            lines = f.readlines()

        message = '# WARNING *** This patch modifies the {0} reference.  ' \
                  'Are you sure this is intended? *** WARNING'.format(modname)

        if lines[0] != message:
            lines = [message, "\n", "\n"] + lines

        with open(msg_file, 'w') as f:
            f.writelines(lines)


def main(msg_file):
    for l in subprocess.check_output(['git', 'submodule',
                                      'status']).decode().rstrip().split('\n'):
        if l:
            submodule_check(l[1:].split(' ')[1], msg_file)

if __name__ == '__main__':
    main(sys.argv[1])