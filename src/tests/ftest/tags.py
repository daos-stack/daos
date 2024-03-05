#!/usr/bin/env python3
"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import ast
import os
import re
import sys
from argparse import ArgumentParser, RawDescriptionHelpFormatter
from collections import defaultdict
from copy import deepcopy
from pathlib import Path

THIS_FILE = os.path.realpath(__file__)
FTEST_DIR = os.path.dirname(THIS_FILE)


class LintFailure(Exception):
    """Exception for lint failures."""


def all_python_files(path):
    """Get a list of all .py files recursively in a directory.

    Args:
        path (str): directory to look in

    Returns:
        list: sorted path names of .py files
    """
    return sorted(map(str, Path(path).rglob("*.py")))


class FtestTagMap():
    """Represent tags for ftest/avocado."""

    def __init__(self, paths):
        """Initialize the tag mapping.

        Args:
            paths (list): the file or dir path(s) to update from
        """
        self.__mapping = {}  # str(file_name) : str(class_name) : str(test_name) : set(tags)
        for path in paths:
            self.__update_from_path(path)

    def __iter__(self):
        """Iterate over the mapping.

        Yields:
            tuple: file_name, class_name mapping
        """
        for item in self.__mapping.items():
            yield deepcopy(item)

    def methods(self):
        """Get a mapping of methods to tags.

        Yields:
            (str, set): method name and tags
        """
        for _, classes in self.__mapping.items():
            for _, methods in classes.items():
                for method_name, tags in methods.items():
                    yield (method_name, tags)

    def unique_tags(self, exclude=None):
        """Get the set of unique tags, excluding one or more paths.

        Args:
            exclude (list, optional): path(s) to exclude from the unique set.
                Defaults to None.

        Returns:
            set: the set of unique tags
        """
        exclude = list(map(self.__norm_path, exclude or []))

        unique_tags = set()
        for file_path, classes in self.__mapping.items():
            if file_path in exclude:
                continue
            for functions in classes.values():
                for tags in functions.values():
                    unique_tags.update(tags)
        return unique_tags

    def minimal_tags(self, include_paths=None):
        """Get the minimal tags representing files in the mapping.

        This computes an approximate minimal - not the absolute minimal.

        Args:
            include_paths (list, optional): path(s) to include in the mapping.
                Defaults to None, which includes all paths

        Returns:
            list: list of sets of tags
        """
        include_paths = list(map(self.__norm_path, include_paths or []))

        minimal_sets = []

        for file_path, classes in self.__mapping.items():
            if include_paths and file_path not in include_paths:
                continue
            # Keep track of recommended tags for each method
            file_recommended = []
            for class_name, functions in classes.items():
                for function_name, tags in functions.items():
                    # Try the class name and function name first
                    if class_name in tags:
                        file_recommended.append(set([class_name]))
                        continue
                    if function_name in tags:
                        file_recommended.append(set([function_name]))
                        continue
                    # Try using a set of tags globally unique to this test
                    globally_unique_tags = tags - self.unique_tags(exclude=file_path)
                    if globally_unique_tags and globally_unique_tags.issubset(tags):
                        file_recommended.append(globally_unique_tags)
                        continue
                    # Fallback to just using all of this test's tags
                    file_recommended.append(tags)

            if not file_recommended:
                continue

            # If all functions in the file have a common set of tags, use that set
            file_recommended_intersection = set.intersection(*file_recommended)
            if file_recommended_intersection:
                minimal_sets.append(file_recommended_intersection)
                continue

            # Otherwise, use tags unique to each function
            file_recommended_unique = []
            for tags in file_recommended:
                if tags not in file_recommended_unique:
                    file_recommended_unique.append(tags)
            minimal_sets.extend(file_recommended_unique)

        # Combine the minimal sets into a single set representing what avocado expects
        avocado_set = set(','.join(tags) for tags in minimal_sets)

        return avocado_set

    def is_test_subset(self, tags1, tags2):
        """Determine whether a set of tags is a subset with respect to tests.

        Args:
            tags1 (list): list of sets of tags
            tags2 (list): list of sets of tags

        Returns:
            bool: whether tags1's tests is a subset of tags2's tests
        """
        tests1 = set(self.__tags_to_tests(tags1))
        tests2 = set(self.__tags_to_tests(tags2))
        return tests1.issubset(tests2)

    def __tags_to_tests(self, tags):
        """Convert a list of tags to the tests they would run.

        Args:
            tags (list): list of sets of tags
        """
        tests = []
        for method_name, test_tags in self.methods():
            for tag_set in tags:
                if tag_set.issubset(test_tags):
                    tests.append(method_name)
                    break
        return tests

    def __update_from_path(self, path):
        """Update the mapping from a path.

        Args:
            path (str): the file or dir path to update from

        Raises:
            ValueError: if a path is not a file
        """
        path = self.__norm_path(path)

        if os.path.isdir(path):
            for __path in all_python_files(path):
                self.__parse_file(__path)
            return

        if os.path.isfile(path):
            self.__parse_file(path)
            return

        raise ValueError(f'Expected file or directory: {path}')

    def __parse_file(self, path):
        """Parse a file and update the internal mapping from avocado tags.

        Args:
            path (str): file to parse
        """
        with open(path, 'r') as file:
            file_data = file.read()

        module = ast.parse(file_data)
        for class_def in filter(lambda val: isinstance(val, ast.ClassDef), module.body):
            for func_def in filter(lambda val: isinstance(val, ast.FunctionDef), class_def.body):
                if not func_def.name.startswith('test_'):
                    continue
                tags = self.__parse_avocado_tags(ast.get_docstring(func_def))
                self.__update(path, class_def.name, func_def.name, tags)

    @staticmethod
    def __norm_path(path):
        """Convert to "realpath" and replace .yaml paths with .py equivalents.

        Args:
            path (str): path to normalize

        Returns:
            str: the normalized path
        """
        path = os.path.realpath(path)
        if path.endswith('.yaml'):
            path = re.sub(r'\.yaml$', '.py', path)
        return path

    def __update(self, file_name, class_name, test_name, tags):
        """Update the internal mapping by appending the tags.

        Args:
            file_name (str): file name
            class_name (str): class name
            test_name (str): test name
            tags (set): set of tags to update
        """
        if file_name not in self.__mapping:
            self.__mapping[file_name] = {}
        if class_name not in self.__mapping[file_name]:
            self.__mapping[file_name][class_name] = {}
        if test_name not in self.__mapping[file_name][class_name]:
            self.__mapping[file_name][class_name][test_name] = set()
        self.__mapping[file_name][class_name][test_name].update(tags)

    @staticmethod
    def __parse_avocado_tags(text):
        """Parse avocado tags from a string.

        Args:
            text (str): the string to parse for tags

        Returns:
            set: the set of tags
        """
        tag_strings = re.findall(':avocado: tags=(.*)', text)
        if not tag_strings:
            return set()
        return set(','.join(tag_strings).split(','))


def sorted_tags(tags):
    """Get a sorted list of tags.

    Args:
        tags (set): original tags

    Returns:
        list: sorted tags
    """
    tags_tmp = set(tags)
    new_tags = []
    for tag in ('all', 'vm', 'hw', 'medium', 'large', 'pr', 'daily_regression', 'full_regression'):
        if tag in tags_tmp:
            new_tags.append(tag)
            tags_tmp.remove(tag)
    new_tags.extend(sorted(tags_tmp))
    return new_tags


def run_linter(paths=None):
    """Run the ftest tag linter.

    Args:
        paths (list, optional): paths to lint. Defaults to all ftest python files

    Raises:
        LintFailure: if linting fails
    """
    if not paths:
        paths = all_python_files(FTEST_DIR)
    all_files = []
    all_classes = defaultdict(int)
    all_methods = defaultdict(int)
    test_wo_tags = []
    tests_wo_class_as_tag = []
    tests_wo_method_as_tag = []
    tests_wo_hw_vm_manual = []
    tests_w_empty_tag = []
    tests_wo_a_feature_tag = []
    non_feature_tags = set([
        'all', 'vm', 'hw', 'medium', 'large', 'pr', 'daily_regression', 'full_regression'])
    ftest_tag_map = FtestTagMap(paths)
    for file_path, classes in iter(ftest_tag_map):
        all_files.append(file_path)
        for class_name, functions in classes.items():
            all_classes[class_name] += 1
            for method_name, tags in functions.items():
                all_methods[method_name] += 1
                if len(tags) == 0:
                    test_wo_tags.append(method_name)
                if class_name not in tags:
                    tests_wo_class_as_tag.append(method_name)
                if method_name not in tags:
                    print(file_path)
                    tests_wo_method_as_tag.append(method_name)
                if not set(tags).intersection(set(['vm', 'hw', 'manual'])):
                    tests_wo_hw_vm_manual.append(method_name)
                if '' in tags:
                    tests_w_empty_tag.append(method_name)
                if not set(tags).difference(non_feature_tags | set([class_name, method_name])):
                    tests_wo_a_feature_tag.append(method_name)

    non_unique_classes = list(name for name, num in all_classes.items() if num > 1)
    non_unique_methods = list(name for name, num in all_methods.items() if num > 1)

    print('ftest tag lint')

    def _error_handler(_list, message, required=True):
        """Exception handler for each class of failure."""
        _list_len = len(_list)
        req_str = '(required)' if required else '(optional)'
        print(f'  {req_str} {_list_len} {message}')
        if _list_len == 0:
            return None
        for _test in _list:
            print(f'    {_test}')
        if _list_len > 3:
            remaining = _list_len - 3
            _list = _list[:3] + [f"... (+{remaining})"]
        _list_str = ", ".join(_list)
        if not required:
            # Print but do not fail
            return None
        return LintFailure(f"{_list_len} {message}: {_list_str}")

    # Lint fails if any of the lists contain entries
    errors = list(filter(None, [
        _error_handler(non_unique_classes, 'non-unique test classes'),
        _error_handler(non_unique_methods, 'non-unique test methods'),
        _error_handler(test_wo_tags, 'tests without tags'),
        _error_handler(tests_wo_class_as_tag, 'tests without class as tag'),
        _error_handler(tests_wo_method_as_tag, 'tests without method name as tag'),
        _error_handler(tests_wo_hw_vm_manual, 'tests without HW, VM, or manual tag'),
        _error_handler(tests_w_empty_tag, 'tests with an empty tag'),
        _error_handler(tests_wo_a_feature_tag, 'tests without a feature tag')]))
    if errors:
        raise errors[0]


def run_dump(paths=None):
    """Dump the tags per test.

    Formatted as
        <file path>:
          <class name>:
            <method name> - <tags>

    Args:
        paths (list, optional): path(s) to get tags for. Defaults to all ftest python files
    """
    if not paths:
        paths = all_python_files(FTEST_DIR)
    for file_path, classes in iter(FtestTagMap(paths)):
        short_file_path = re.findall(r'ftest/(.*$)', file_path)[0]
        print(f'{short_file_path}:')
        for class_name, functions in classes.items():
            print(f'  {class_name}:')
            all_methods = []
            longest_method_name = 0
            for method_name, tags in functions.items():
                longest_method_name = max(longest_method_name, len(method_name))
                all_methods.append((method_name, tags))
            for method_name, tags in all_methods:
                method_name_fm = method_name.ljust(longest_method_name, " ")
                tags_fm = ",".join(sorted_tags(tags))
                print(f'    {method_name_fm} - {tags_fm}')


def files_to_tags(paths):
    """Get the unique tags for paths.

    Args:
        paths (list): paths to get tags for

    Returns:
        set: set of test tags representing paths
    """
    # Get tags for ftest paths
    ftest_tag_map = FtestTagMap(all_python_files(FTEST_DIR))
    ftest_tag_set = ftest_tag_map.minimal_tags(paths)

    # Future work will also get recommended tags for non-ftest files
    return ftest_tag_set


def run_list(paths):
    """List unique tags for paths.

    Args:
        paths (list): paths to list tags of
    """
    tags = files_to_tags(paths)
    print(' '.join(sorted(tags)))


def test_tags_util(verbose=False):
    """Run unit tests for FtestTagMap.

    Can be ran directly as:
        tags.py unit
    Or with pytest as:
        python3 -m pytest tags.py

    Args:
        verbose (bool): whether to print verbose output for debugging
    """
    # pylint: disable=protected-access
    print('Ftest Tags Utility Unit Tests')
    tag_map = FtestTagMap([])
    os.chdir('/')

    def print_step(*args):
        """Print a step."""
        print('  ', *args)

    def print_verbose(*args):
        """Conditionally print if verbose is True."""
        if verbose:
            print('    ', *args)

    print_step('__norm_path')
    assert tag_map._FtestTagMap__norm_path('foo') == '/foo'
    assert tag_map._FtestTagMap__norm_path('/foo') == '/foo'

    print_step('__parse_avocado_tags')
    assert tag_map._FtestTagMap__parse_avocado_tags('') == set()
    assert tag_map._FtestTagMap__parse_avocado_tags('not tags') == set()
    assert tag_map._FtestTagMap__parse_avocado_tags('avocado tags=foo') == set()
    assert tag_map._FtestTagMap__parse_avocado_tags(':avocado: tags=foo') == set(['foo'])
    assert tag_map._FtestTagMap__parse_avocado_tags(':avocado: tags=foo,bar') == set(['foo', 'bar'])
    assert tag_map._FtestTagMap__parse_avocado_tags(
        ':avocado: tags=foo,bar\n:avocado: tags=foo2,bar2') == set(['foo', 'bar', 'foo2', 'bar2'])

    print_step('__update')
    tag_map._FtestTagMap__update('/foo1', 'class_1', 'test_1', set(['class_1', 'test_1', 'foo1']))
    tag_map._FtestTagMap__update('/foo2', 'class_2', 'test_2', set(['class_2', 'test_2', 'foo2']))
    print_verbose(tag_map._FtestTagMap__mapping)
    assert tag_map._FtestTagMap__mapping == {
        '/foo1': {'class_1': {'test_1': {'foo1', 'test_1', 'class_1'}}},
        '/foo2': {'class_2': {'test_2': {'foo2', 'class_2', 'test_2'}}}}

    print_step('__iter__')
    print_verbose(list(iter(tag_map)))
    assert list(iter(tag_map)) == [
        ('/foo1', {'class_1': {'test_1': {'class_1', 'test_1', 'foo1'}}}),
        ('/foo2', {'class_2': {'test_2': {'class_2', 'test_2', 'foo2'}}})
    ]

    print_step('__tags_to_tests')
    assert tag_map._FtestTagMap__tags_to_tests(
        [set(['foo1']), set(['foo2'])]) == ['test_1', 'test_2']
    assert tag_map._FtestTagMap__tags_to_tests([set(['foo1', 'class_1'])]) == ['test_1']

    print('Ftest Tags Utility Unit Tests PASSED')


def main():
    """main function execution"""
    description = '\n'.join([
        'Commands',
        '  lint - lint ftest avocado tags',
        '  list - list ftest avocado tags associated with test files',
        '  dump - dump the file/class/method/tag structure for test files',
        '  unit - run self unit tests'
    ])

    parser = ArgumentParser(formatter_class=RawDescriptionHelpFormatter, description=description)
    parser.add_argument(
        "command",
        choices=("lint", "list", "dump", "unit"),
        help="command to run")
    parser.add_argument(
        "-v", "--verbose",
        action='store_true',
        help="print verbose output for some commands")
    parser.add_argument(
        "paths",
        nargs="*",
        help="file paths")
    args = parser.parse_args()
    args.paths = list(map(os.path.realpath, args.paths))

    if args.command == "lint":
        try:
            run_linter(args.paths)
        except LintFailure as err:
            print(err)
            sys.exit(1)
        sys.exit(0)

    if args.command == "dump":
        run_dump(args.paths)
        sys.exit(0)

    if args.command == "list":
        run_list(args.paths)
        sys.exit(0)

    if args.command == "unit":
        test_tags_util(args.verbose)
        sys.exit(0)


if __name__ == '__main__':
    main()
