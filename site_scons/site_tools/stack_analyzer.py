"""
(C) Copyright 2019-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent

Analyze stack usage output
"""
import argparse
import atexit
import os

from SCons.Script import Exit


def exit_handler(handle):
    """run analysis on exit"""
    handle.analyze()


class Analyzer():
    """Class to parse .su files"""

    def __init__(self, env, daos_prefix, comp_prefix, arg=""):
        """Class for reading and printing stack usage statistics"""
        self.dir_exclusions = []
        self.dir_inclusions = []
        self.file_inclusions = []
        self.daos_prefix = daos_prefix
        self.comp_prefix = comp_prefix
        self.cutoff = 0
        self.parse_args(arg)
        if '-fstack-usage' not in env["CCFLAGS"]:
            print("Stack analysis not supported with this compiler")
            Exit(1)

    def parse_args(self, arg_str):
        """Parse the arguments"""
        parser = argparse.ArgumentParser(description='Stack size analyzer')
        parser.add_argument('-x', '--exclude-dir', dest='xdirs', nargs='*', default=[],
                            help="string to match indicating directories to exclude")
        parser.add_argument('-I', '--include-dir', dest='dirs', nargs='*', default=[],
                            help="string to match indicating directories to include")
        parser.add_argument('-i', '--include-file', dest='files', nargs='*', default=[],
                            help="string to match indicating a directory to include")
        parser.add_argument('-c', '--cutoff', dest='cutoff', default=100, type=int,
                            help="Lower bound cutoff for entries to print")
        parser.add_argument('-e', '--exit', dest='exit', default=False, action="store_true",
                            help="Do not wait for build. Run the analysis immediately and exit.")
        args = parser.parse_args(arg_str.split())
        self.dir_exclusions = args.xdirs
        self.dir_inclusions = args.dirs
        self.file_inclusions = args.files
        self.cutoff = args.cutoff
        if args.exit:
            self.analyze()
            Exit(0)

    @staticmethod
    def included(name, inclusions):
        """Handle inclusions"""
        if inclusions == []:
            return True
        for inclusion in inclusions:
            if inclusion in name:
                return True
        return False

    @staticmethod
    def get_value(value, default):
        """Get the string value of a list option"""
        if value == []:
            return default
        return " ".join(value)

    def excluded(self, name):
        """Handle inclusions"""
        for exclusion in self.dir_exclusions:
            if exclusion in name:
                return True
        return False

    def analyze_on_exit(self):
        """Setup the analyzer to run on exit"""
        atexit.register(exit_handler, self)

    def _gather_path(self, comp, path, function_map):
        """Analyze a single component"""
        print(f'"Analyzing {comp} at {path}')
        if not os.path.exists(path):
            print('No files in {path}')
            return

        for root, _dirs, files in os.walk(path):
            if self.excluded(root):
                continue
            if not self.included(root, self.dir_inclusions):
                continue
            for fname in files:
                if not self.included(fname, self.file_inclusions):
                    continue
                if fname.endswith(".su"):
                    with open(os.path.join(root, fname), "r") as frame:
                        for line in frame.readlines():
                            split = line.split()
                            if len(split) < 3:
                                continue
                            func = f"{comp}:{split[0]}"
                            usage = int(split[-2])
                            if usage < self.cutoff:
                                continue
                            if func not in function_map:
                                function_map[func] = usage
                            elif usage > function_map[func]:
                                function_map[func] = usage

    def analyze(self):
        """Run the analysis"""
        function_map = {}

        self._gather_path('daos', self.daos_prefix, function_map)
        for path in os.listdir(self.comp_prefix):
            comp_path = os.path.join(self.comp_prefix, path)
            path = path.replace(".build", "")
            if os.path.isdir(comp_path):
                self._gather_path(path, comp_path, function_map)

        print('Options:')
        print('\texcluded directory strings: {self.get_value(self.dir_exclusions, "none")}')
        print('\tincluded directory strings: {self.get_value(self.dir_inclusions, "all")}')
        print('\tincluded file strings     : {self.get_value(self.file_inclusions, "all")}')
        print('\tcutoff                    : {self.cutoff}')

        size_map = {}
        for key, value in function_map.items():
            if value not in size_map:
                size_map[value] = []

            size_map[value].append(key)

        for key in sorted(size_map.keys(), reverse=True):
            print(f'{key:d} bytes:')
            for value in size_map[key]:
                print(f'    {value}')


def generate(env, daos_prefix, comp_prefix, args):
    """Add daos specific methods to environment"""
    analyzer = Analyzer(env, daos_prefix, comp_prefix, args)
    analyzer.analyze_on_exit()


def exists(_env):
    """Tell SCons we exist"""
    return True
