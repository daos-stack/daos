"""Analyze stack usage output"""
import os
import argparse
import atexit
from SCons.Script import Exit


def exit_handler(sa):
    """run analysis on exit"""
    sa.analyze()


class analyzer():
    """Class to parse .su files"""

    def __init__(self, env, basedir, arg=""):
        """Class for reading and printing stack usage statistics"""
        self.dir_exclusions = []
        self.dir_inclusions = []
        self.file_inclusions = []
        self.base_dir = basedir
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

    def analyze(self):
        """Run the analysis"""
        function_map = {}

        print("Analyzing %s" % self.base_dir)
        print("Options:")
        print("\texcluded directory strings: %s" % self.get_value(self.dir_exclusions, "none"))
        print("\tincluded directory strings: %s" % self.get_value(self.dir_inclusions, "all"))
        print("\tincluded file strings     : %s" % self.get_value(self.file_inclusions, "all"))
        print("\tcutoff                    : %d" % self.cutoff)

        if not os.path.exists(self.base_dir):
            print("No files in %s" % self.base_dir)
            return

        for root, _dirs, files in os.walk(self.base_dir):
            if self.excluded(root):
                continue
            if not self.included(root, self.dir_inclusions):
                continue
            for fname in files:
                if not self.included(fname, self.file_inclusions):
                    continue
                if fname.endswith(".su"):
                    with open(os.path.join(root, fname), "r") as sf:
                        for line in sf.readlines():
                            split = line.split()
                            if len(split) < 3:
                                continue
                            func = split[0]
                            usage = int(split[-2])
                            if usage < self.cutoff:
                                continue
                            if func not in function_map:
                                function_map[func] = usage
                            elif usage > function_map[func]:
                                function_map[func] = usage

        size_map = {}
        for key, value in function_map.items():
            if value not in size_map:
                size_map[value] = []

            size_map[value].append(key)

        for key in sorted(size_map.keys(), reverse=True):
            print("%d bytes:" % key)
            for value in size_map[key]:
                print("    %s" % value)
