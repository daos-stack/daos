#!/usr/bin/env python3

"""Git hook to copy forward commit message metadata to new commits."""

import sys
import subprocess  # nosec
from collections import OrderedDict


class NotTag(Exception):
    """Nothing"""

# TODO: Call get_tag_kv function from main and check https: links.


def main():
    """Run the check"""

    print(f'copy-tags args are {sys.argv}')
    # Will be unset for a regular commit, 'commit' and amend.
    if len(sys.argv) > 2 and sys.argv[2] not in ('message', 'template'):
        return

    def get_tag_kv(line):
        """Convert a line of test to a key/value"""

        if ':' not in line:
            raise NotTag
        (raw_key, value) = line.split(':', maxsplit=1)
        key = raw_key.strip()
        if key in ('Date', 'Author', 'Signed-off-by', 'Merge', 'Co-authored-by'):
            raise NotTag
        if ' ' in key:
            raise NotTag
        return (key, value.strip())

    def add_text():
        output = '# ------------------------ >8 ------------------------\n'
        output += 'Skip-func-hw-test: true\n'
        output += 'Skip-func-test: true\n'
        output += 'Quick-Functional: true\n'
        output += 'Test-tag: dfuse\n'
        return output

    tags = OrderedDict()
    rc = subprocess.run(['git', 'log', '-1'], stdout=subprocess.PIPE, check=True)
    for line in rc.stdout.decode('utf-8').splitlines():
        if ':' not in line:
            continue
        (raw_key, value) = line.split(':', maxsplit=1)
        key = raw_key.strip()
        if key in ('Date', 'Author', 'Signed-off-by', 'Merge', 'Co-authored-by', 'https'):
            continue
        if ' ' in key:
            continue

        tags[key] = value.strip()

    with open(sys.argv[1], 'r+', encoding='utf-8') as fd:

        file_lines = []
        # Check for existing tags.
        for line in fd.readlines():
            file_lines.append(line)
            try:
                (tag, value) = get_tag_kv(line)
                try:
                    del tags[tag]
                except KeyError:
                    file_lines.pop()
            except NotTag:
                pass

        output = []
        matched = False
        for line in file_lines:
            if not matched and line.startswith('#'):
                matched = True

                idx = -1
                try:
                    if output and output[idx] == '':
                        idx -= 1
                except IndexError:
                    print(idx)
                    print(output)
                    raise

                for (key, value) in tags.items():
                    output.insert(idx, f'{key}: {value}')

                output.append(add_text())
            output.append(line.strip('\n'))

        if not matched:
            output.append(add_text())

        fd.seek(0)
        fd.truncate(0)
        fd.write('\n'.join(output))


if __name__ == '__main__':
    main()
