#!/usr/bin/env python

"""Utility to parse build.config files in to a shell script"""

from __future__ import print_function
import argparse
import configparser
import sys

DEFAULT_CONFIG = 'utils/build.config'

def parse_cfg_files(cfgparser, file_name, level):
    """Recursively parse config files"""

    if level > 5:
        print("Too many config file levels seen.", file=sys.stderr)
        return

    level += 1

    # cppr build.config has duplicate keys, so need strict=False for now.
    dep_cfgparser = configparser.ConfigParser(strict=False)

    dep_cfgparser.read(file_name)

    if not cfgparser.has_option('component', 'component'):
        if dep_cfgparser.has_section('component'):
            for part in dep_cfgparser.items('component'):
                cfgparser.set('component', part[0], part[1])

    if dep_cfgparser.has_section('commit_versions'):
        for part in dep_cfgparser.items('commit_versions'):
            cfgparser.set('commit_versions', part[0], part[1])

    if dep_cfgparser.has_section('configs'):
        for part in dep_cfgparser.items('configs'):
            cfgparser.set('configs', part[0], part[1])
            parse_cfg_files(cfgparser, "%s/%s" % (part[0], part[1]), level)


def main():
    """main"""
    parser = argparse.ArgumentParser(
        description='Parse build.config files for a shell script')
    parser.add_argument('--build-config',
                        dest='build_config',
                        default=DEFAULT_CONFIG,
                        help='Config file to read.  ' \
                             "Default: %s" % DEFAULT_CONFIG)
    parser.add_argument('--prefix',
                        default='',
                        help='Prefix for generated symbols.  ' \
                             'Default is no prefix.')

    args = parser.parse_args()

    cfgparser = configparser.ConfigParser()
    cfgparser.add_section('component')
    cfgparser.add_section('commit_versions')
    cfgparser.add_section('configs')

    parse_cfg_files(cfgparser, args.build_config, 0)

    if cfgparser.has_section('component'):
        for part in cfgparser.items('component'):
            print("%s%s=%s" % (args.prefix, part[0], part[1]))

    comp_names = cfgparser.options('commit_versions')
    print("%sdepend_names=\"%s\"" % (args.prefix, ' '.join(comp_names)))
    for part in cfgparser.items('commit_versions'):
        print("%s%s_git_hash=%s" % (args.prefix, part[0], part[1]))
    if cfgparser.has_section('configs'):
        cfg_names = cfgparser.options('configs')
        print("%sconfig_filekeys=%s" % (args.prefix, ' '.join(cfg_names)))
        for part in cfgparser.items('configs'):
            print("%sconfig_file_%s=%s" % (args.prefix, part[0], part[1]))

if __name__ == "__main__":
    main()
