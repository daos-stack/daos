#!/usr/bin/env python3
#
# Copyright 2026 Google LLC
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#
# Runs lint and auto-fix for utils/build.config for the DAOS project.

"""Githook script to check and fix the format of utils/build.config."""

import os
import sys
import subprocess
import unittest


def print_githook_header(name):
    """Print the standard githook header."""
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
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    return result.stdout.strip()


def _handle_config_line(line, current_section, sections, new_content):
    """Handle a single config line during parsing."""
    stripped = line.strip()
    if not stripped or stripped.startswith(('#', ';')):
        if current_section:
            sections[-1]['items'].append(line)
        else:
            new_content.append(line)
        return current_section

    if stripped.startswith('[') and stripped.endswith(']'):
        new_section = stripped[1:-1]
        sections.append({'name': new_section, 'header': line, 'items': []})
        return new_section

    if '=' in stripped and current_section:
        key, val = line.split('=', 1)
        sections[-1]['items'].append((key.strip().lower(), val.strip()))
    elif current_section:
        sections[-1]['items'].append(line)
    else:
        new_content.append(line)
    return current_section


def process_build_config(lines):
    """Parse, sort, and normalize the build config lines."""
    new_content = []
    sections = []
    current_section = None

    for line in lines:
        current_section = _handle_config_line(line, current_section, sections, new_content)

    # Process sections
    for sec in sections:
        new_content.append(sec['header'])

        keys_only = [item for item in sec['items'] if isinstance(item, tuple)]
        others = [item for item in sec['items'] if not isinstance(item, tuple)]

        if sec['name'] != 'component':
            keys_only.sort(key=lambda x: x[0])

        for key, val in keys_only:
            new_content.append(f"{key}={val}\n")
        new_content.extend(others)

    return new_content


def run_tests():
    """Run unit tests for the functional bits."""
    class TestBuildConfig(unittest.TestCase):
        """Unit tests for build config processing."""

        def test_sorting(self):
            """Test that sections (except component) are sorted."""
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
            """Test that keys are lowercased."""
            lines = [
                "[section]\n",
                "Key=Val\n"
            ]
            expected = [
                "[section]\n",
                "key=Val\n"
            ]
            self.assertEqual(process_build_config(lines), expected)

        def test_component_not_sorted(self):
            """Test that [component] section is not sorted."""
            lines = [
                "[component]\n",
                "z=1\n",
                "a=2\n"
            ]
            expected = [
                "[component]\n",
                "z=1\n",
                "a=2\n"
            ]
            self.assertEqual(process_build_config(lines), expected)

        def test_comments_and_empty_lines(self):
            """Test that comments and empty lines are preserved at the end of section."""
            lines = [
                "[section]\n",
                "b=2\n",
                "\n",
                "# comment\n",
                "a=1\n"
            ]
            # Based on current logic, comments and empty lines go to the bottom of the section
            expected = [
                "[section]\n",
                "a=1\n",
                "b=2\n",
                "\n",
                "# comment\n"
            ]
            self.assertEqual(process_build_config(lines), expected)

    suite = unittest.TestLoader().loadTestsFromTestCase(TestBuildConfig)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)


def main():
    """Main entry point for the githook."""
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
