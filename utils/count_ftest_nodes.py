#!/usr/bin/env python3
"""Report the number of nodes (test_servers + test_clients) used by each ftest yaml file."""

import io
import json
import os
import sys
from argparse import ArgumentParser
from contextlib import redirect_stdout

import yaml
from ClusterShell.NodeSet import NodeSet

FTEST_DIR = os.path.realpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'tests', 'ftest'))
sys.path.insert(0, FTEST_DIR)

# pylint: disable=import-error,wrong-import-position
from tags import run_dump                                    # noqa: E402

KEYS = ("test_servers", "test_clients")
SIZES = ("medium", "large")


class _Loader(yaml.SafeLoader):
    """Loader that tolerates the custom tags (e.g. !mux) used by avocado yaml files."""


def _construct_unknown(loader, suffix, node):
    """Construct a node with an unknown tag as its plain yaml equivalent."""
    if isinstance(node, yaml.MappingNode):
        return loader.construct_mapping(node, deep=True)
    if isinstance(node, yaml.SequenceNode):
        return loader.construct_sequence(node, deep=True)
    return loader.construct_scalar(node)


_Loader.add_multi_constructor('', _construct_unknown)


def count_nodes(value):
    """Convert a test_servers/test_clients value into a node count.

    Args:
        value (object): the yaml value, e.g. an int, a NodeSet string, or a list of either.

    Returns:
        int: the number of nodes represented by the value.
    """
    if isinstance(value, bool) or value is None:
        return 0
    if isinstance(value, int):
        return value
    if isinstance(value, (list, tuple, set)):
        return sum(count_nodes(item) for item in value)
    try:
        return int(value)
    except (TypeError, ValueError):
        return len(NodeSet(str(value)))


def find_counts(data, counts):
    """Recursively find the largest node count for each key in a yaml structure.

    Args:
        data (object): the yaml data to search.
        counts (dict): mapping of key to the largest node count found so far.
    """
    if isinstance(data, dict):
        for key, value in data.items():
            if key in KEYS and not isinstance(value, dict):
                counts[key] = max(counts[key] or 0, count_nodes(value))
            else:
                find_counts(value, counts)
    elif isinstance(data, (list, tuple)):
        for item in data:
            find_counts(item, counts)


def get_node_total(file_name):
    """Get the total number of nodes required by a yaml file.

    Args:
        file_name (str): the yaml file to parse.

    Returns:
        int: the sum of the test_servers and test_clients counts.
    """
    with open(file_name, 'r', encoding='utf-8') as yaml_file:
        data = yaml.load(yaml_file, Loader=_Loader)
    counts = {key: None for key in KEYS}
    find_counts(data, counts)
    if counts['test_clients'] is None:
        counts['test_clients'] = 1
    return sum(count or 0 for count in counts.values())


def has_tags(file_name, tags):
    """Determine if the yaml file has an associated test matching the specified tags.

    Args:
        file_name (str): the yaml file to check.
        tags (set): the tags that the test must have.

    Returns:
        bool: True if dumping the tagged tests for this file succeeds.
    """
    try:
        with redirect_stdout(io.StringIO()):
            return run_dump([file_name], [set(tags)]) == 0
    except Exception:       # pylint: disable=broad-except
        return False


def main():
    """Generate the yaml file to node count mapping."""
    parser = ArgumentParser(description=__doc__)
    parser.add_argument(
        '--ftest-dir', default=FTEST_DIR, help='path to the src/tests/ftest directory')
    parser.add_argument('--json', action='store_true', help='output the mapping as json')
    args = parser.parse_args()

    ftest_dir = os.path.normpath(args.ftest_dir)
    mapping = {size: {} for size in SIZES}
    for entry in sorted(os.listdir(ftest_dir)):
        sub_dir = os.path.join(ftest_dir, entry)
        if not os.path.isdir(sub_dir):
            continue
        for name in sorted(os.listdir(sub_dir)):
            if not name.endswith('.yaml'):
                continue
            file_name = os.path.join(sub_dir, name)
            for size in SIZES:
                if not has_tags(file_name, {'hw', size}):
                    continue
                try:
                    mapping[size][os.path.relpath(file_name, ftest_dir)] = \
                        get_node_total(file_name)
                except Exception as error:      # pylint: disable=broad-except
                    print(f'Error parsing {file_name}: {error}', file=sys.stderr)

    for size in SIZES:
        mapping[size] = dict(
            sorted(mapping[size].items(), key=lambda item: (-item[1], item[0])))

    if args.json:
        print(json.dumps(mapping, indent=4))
    else:
        for size in SIZES:
            print(f'{size}:')
            for file_name, total in mapping[size].items():
                print(f'  {total}: {file_name}')


if __name__ == '__main__':
    main()
