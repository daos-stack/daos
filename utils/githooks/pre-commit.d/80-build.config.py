#!/usr/bin/env python3
#
# Copyright 2026 Google LLC
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Runs lint and auto-fix for utils/build.config for the DAOS project.

"""Git hook script to check and fix the format of utils/build.config."""

import os
import subprocess  # nosec B404
import sys
import unittest


def print_githook_header(name):
    """Print the standard git hook header."""
    print(f"{name + ':':<17} ", end='', flush=True)


def git_diff_cached_files(file_path):
    """Check if the file has staged changes."""
    target = os.environ.get('TARGET')
    if not target:
        # If TARGET is not set, we can't easily determine what to diff against
        # in the same way hook_base.sh does. For now, we'll return True to
        # force a check if we're not sure, or try to find a base.
        return True

    cmd = ['git', 'diff', target, '--cached', '--name-only', '--diff-filter=d', '--', file_path]
    result = subprocess.run(
        cmd, capture_output=True, text=True, check=False,
        shell=False)  # nosec B603
    return result.stdout.strip()


def process_build_config(lines):
    """Parse, sort, and normalize the build config lines."""
    global_lines = []
    sections = []
    current_section = None

    for line in lines:
        stripped = line.strip()
        if stripped.startswith('[') and stripped.endswith(']'):
            new_section = stripped[1:-1]
            current_section = {'name': new_section, 'header': line, 'lines': []}
            sections.append(current_section)
        elif current_section is None:
            global_lines.append(line)
        else:
            current_section['lines'].append(line)

    def get_units(lines):
        units = []
        current_comments = []
        for line in lines:
            stripped = line.strip()
            if not stripped:
                continue
            if stripped.startswith(('#', ';')):
                current_comments.append(line)
            elif '=' in stripped:
                key, val = line.split('=', 1)
                units.append({
                    'comments': current_comments,
                    'key': key.strip().lower(),
                    'val': val.strip()
                })
                current_comments = []
            else:
                current_comments.append(line)
        if current_comments:
            units.append({
                'comments': current_comments,
                'key': None,
                'val': None
            })
        return units

    def format_unit(unit):
        res = []
        res.extend(unit['comments'])
        if unit['key'] is not None:
            res.append(f"{unit['key']}={unit['val']}\n")
        return res

    def format_section(sec):
        res = []
        res.append(sec['header'])
        units = get_units(sec['lines'])
        key_units = sorted([u for u in units if u['key'] is not None],
                           key=lambda x: x['key'])
        other_units = [u for u in units if u['key'] is None]
        for u in key_units:
            res.extend(format_unit(u))
        for u in other_units:
            res.extend(format_unit(u))
        return res

    new_content = []
    global_filtered = [line for line in global_lines if line.strip()]
    new_content.extend(global_filtered)

    for i, sec in enumerate(sections):
        if i > 0:
            new_content.append("\n")
        new_content.extend(format_section(sec))

    return new_content


def run_tests():
    """Run unit tests for the functional bits."""
    class TestBuildConfig(unittest.TestCase):
        """Unit tests for build config processing."""

        def test_sorting(self):
            """Test that sections are sorted."""
            lines = [
                "[section]\n",
                "b=2\n",
                "a=1\n"
            ]
            expected = [
                "[section]\n",
                "a=1\n",
                "b=2\n"
            ]
            self.assertEqual(process_build_config(lines), expected)

        def test_casing(self):
            """Test that keys are lowercase."""
            lines = [
                "[section]\n",
                "Key=Val\n"
            ]
            expected = [
                "[section]\n",
                "key=Val\n"
            ]
            self.assertEqual(process_build_config(lines), expected)

        def test_comments_and_empty_lines(self):
            """Test that comments stay with following lines and empty lines are removed."""
            lines = [
                "[section]\n",
                "b=2\n",
                "\n",
                "# comment\n",
                "a=1\n"
            ]
            expected = [
                "[section]\n",
                "# comment\n",
                "a=1\n",
                "b=2\n"
            ]
            self.assertEqual(process_build_config(lines), expected)

        def test_section_spacing(self):
            """Test that sections are separated by exactly one empty line."""
            lines = [
                "[sec1]\n",
                "a=1\n",
                "[sec2]\n",
                "b=2\n"
            ]
            expected = [
                "[sec1]\n",
                "a=1\n",
                "\n",
                "[sec2]\n",
                "b=2\n"
            ]
            self.assertEqual(process_build_config(lines), expected)

        def test_no_empty_lines_at_ends(self):
            """Test that no empty lines are at start or end of file."""
            lines = [
                "\n",
                "[sec1]\n",
                "a=1\n",
                "\n"
            ]
            expected = [
                "[sec1]\n",
                "a=1\n"
            ]
            self.assertEqual(process_build_config(lines), expected)

    suite = unittest.TestLoader().loadTestsFromTestCase(TestBuildConfig)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)


def main():
    """Main entry point for the git hook."""
    if len(sys.argv) > 1 and sys.argv[1] == '--test':
        run_tests()

    print_githook_header("Build Config")

    filename = "utils/build.config"

    if not git_diff_cached_files(filename):
        print(f"No changes to {filename}. Skipping")
        sys.exit(0)

    print(f"Checking and fixing {filename} for sorting and casing")

    if not os.path.exists(filename):
        sys.exit(0)

    with open(filename, 'r', encoding='utf-8') as file_handle:
        lines = file_handle.readlines()

    new_content = process_build_config(lines)

    if lines != new_content:
        with open(filename, 'w', encoding='utf-8') as file_handle:
            file_handle.writelines(new_content)
        msg = (f"  ERROR: {filename} was not correctly formatted. "
               "Auto-fixes applied.")
        print(msg, file=sys.stderr)
        msg = (f"  Please review with 'git diff {filename}' and "
               "re-stage the changes.")
        print(msg, file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
