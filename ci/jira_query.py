#!/usr/bin/env python3
"""Interface between CI and bug-tracking tools"""

import json
import os
import random
import re
import string
import sys
import time
import urllib
from argparse import ArgumentParser

import jira

# Script to improve interaction with Jenkins, GitHub and Jira.  This is intended to work in several
# ways:

# Add comments to Pull Requests in GitHub to help developers and reviewers.
# Assist in keeping Jira up-to-date with development activities
# Set priorities of jobs in Jenkins.

# To do this it should  be run as a GitHub action which will apply comments (and possibly labels)
# to PRs, as well as warning and failing PR builds if any ticket metadata is incorrect.

# https://jira.readthedocs.io/api.html#module-jira.client
# https://github.com/marketplace/actions/comment-pull-request

# Expected components from the commit message, and directory in src/, src/client or utils/ is also
# valid.  We've never checked/enforced these before so there have been a lot of values used in the
# past.
VALID_COMPONENTS = ('agent', 'build', 'ci', 'csum', 'doc', 'gha', 'il', 'md', 'mercury',
                    'packaging', 'pil4dfs', 'swim', 'test', 'tools', 'ddb', 'dlck')

# Expected ticket prefix.
VALID_TICKET_PREFIX = ('DAOS', 'CORCI', 'SRE')

# 10044 is "Approved to Merge"
# 10045 is "Required for Version"
FIELDS = 'summary,status,labels,fixVersions,customfield_10044,customfield_10045'


def set_output(key, value):
    """Set a key-value pair in GitHub actions metadata"""
    env_file = os.getenv('GITHUB_OUTPUT')
    if not env_file:
        clean_value = value.replace('\n', '%0A')
        print(f'::set-output name={key}::{clean_value}')
        return

    delim = ''.join(random.choices(string.ascii_uppercase, k=7))  # nosec
    with open(env_file, 'a') as file:
        file.write(f'{key}<<{delim}\n{value}\n{delim}\n')


def valid_comp_from_dir(component):
    """Checks is a component is valid based on src tree"""
    return os.path.isdir(os.path.join('src', component)) \
        or os.path.isdir(os.path.join('src', 'client', component)) \
        or os.path.isdir(os.path.join('utils', component))


def fetch_pr_data(pr_number):
    """Query GitHub API and return PR metadata.

    Args:
        pr_number (int): pull request number

    Returns:
        dict: PR metadata from GitHub API
    """
    github_repo = os.environ.get('GITHUB_REPOSITORY', 'daos-stack/daos')
    github_url = f'https://api.github.com/repos/{github_repo}/pulls/{pr_number}'

    # We occasionally see this fail with rate-limit-exceeded, if that happens then wait for a
    # while and re-try once.
    try:
        with urllib.request.urlopen(github_url) as raw_pr_data:  # nosec
            return json.loads(raw_pr_data.read())
    except urllib.error.HTTPError as error:
        if error.code != 403:
            raise
        time.sleep(60 * 10)
        with urllib.request.urlopen(github_url) as raw_pr_data:  # nosec
            return json.loads(raw_pr_data.read())


def _get_targeted_branch_version():
    """Get the targeted DAOS branch version based on the VERSION file.

    Returns:
        tuple: the targeted version as a tuple of integers (major, minor, patch)
    """
    with open(os.path.join(os.path.dirname(__file__), '..', 'VERSION')) as version_file:
        raw_version = version_file.read().strip()
    version_major, version_minor, version_patch = _version_str_to_tuple(raw_version)

    # If the minor version is odd, it is a development version,
    # so bump it to the next even number for the release version.
    if version_minor % 2 == 1:
        version_minor += 1
        version_patch = 0

    # If the minor version is at 10, bump the major version and reset the minor version to 0.
    if version_minor == 10:
        version_major += 1
        version_minor = 0
        version_patch = 0

    return (version_major, version_minor, version_patch)


def _version_str_to_tuple(version_str):
    """Convert a version string to a tuple of integers.

    Args:
        version_str (str): version string, e.g. "2.8.1"

    Returns:
        tuple: the version as a tuple of integers (major, minor, patch)
    """
    version_parts = version_str.split('.')
    version_major = int(version_parts[0])
    version_minor = int(version_parts[1])
    version_patch = int(version_parts[2]) if len(version_parts) > 2 else 0
    return (version_major, version_minor, version_patch)


def _jira_version_to_tuple(jira_version):
    """Convert a Jira version string to a tuple of integers.

    Args:
        jira_version (str): Jira version string, e.g. "2.8.1 Community Release"

    Returns:
        tuple: the version as a tuple of integers (major, minor, patch)
    """
    match = re.match(r'^(\d+\.\d+(?:\.\d+)?)', str(jira_version))
    if not match:
        raise ValueError(f'Invalid Jira version string: {jira_version}')
    return _version_str_to_tuple(match.group(1))


def _jira_approved_to_merge(ticket):
    """Get the approved to merge status from a Jira ticket.

    Args:
        ticket (jira.Issue): Jira ticket object

    Returns:
        list: list of approved to merge versions as a tuple of integers (major, minor, patch)
    """
    return list(map(_jira_version_to_tuple, (ticket.fields.customfield_10044 or [])))


def _jira_required_for_version(ticket):
    """Get the required for version from a Jira ticket.

    Args:
        ticket (jira.Issue): Jira ticket object

    Returns:
        list: list of required for versions as a tuple of integers (major, minor, patch)
    """
    return list(map(_jira_version_to_tuple, (ticket.fields.customfield_10045 or [])))


def _jira_fix_versions(ticket):
    """Get the fix versions from a Jira ticket.

    Args:
        ticket (jira.Issue): Jira ticket object

    Returns:
        list: list of fix versions as a tuple of integers (major, minor, patch)
    """
    return list(map(_jira_version_to_tuple, (ticket.fields.fixVersions or [])))


def _get_jira_release_versions(ticket):
    """Get the release versions from a Jira ticket.

    Args:
        ticket (jira.Issue): Jira ticket object

    Returns:
        list: list of release versions as a tuple of integers (major, minor, patch)
    """
    return _jira_required_for_version(ticket) + _jira_fix_versions(ticket)


def main():
    """Run the script"""
    # pylint: disable=too-many-branches
    parser = ArgumentParser(description='Query JIRA to automatically set PR metadata')
    parser.add_argument('pr_number', type=int, help='Pull request number')
    args = parser.parse_args()

    pr_data = fetch_pr_data(args.pr_number)

    # Labels to be removed from the PR
    labels_to_clear = set()

    # Labels to add to the PR
    labels_to_add = set()

    # Labels already on the PR
    labels_on_pr = set(label['name'] for label in pr_data['labels'])

    errors = []
    pr_title = pr_data['title']

    # Revert PRs can be auto-generated, so detect and handle this
    if pr_title.startswith('Revert "'):
        pr_title = pr_title[8:-1]

    parts = pr_title.split(' ')
    ticket_number = parts[0]
    component = parts[1]
    if component.endswith(':'):
        component = component[:-1]
        col = component.lower()
        if col != component:
            errors.append('Component should be lower-case')
        if col not in VALID_COMPONENTS and not valid_comp_from_dir(col):
            errors.append('Unknown component')
            print('Either amend PR title or add to ci/jira_query.py')
    else:
        errors.append('component not formatted correctly')
    if len(pr_title) > 80:
        errors.append('Title of PR is too long')

    # Check format of ticket_number.
    parts = ticket_number.split('-', maxsplit=1)
    if parts[0] not in VALID_TICKET_PREFIX:
        errors.append('Ticket number prefix incorrect')
    link = 'https://daosio.atlassian.net/wiki/spaces/DC/pages/11133911069/Commit+Comments'
    try:
        int(parts[1])
    except ValueError:
        errors.append(f'Ticket number suffix is not a number. See {link}')
    except IndexError:
        errors.append(f'PR title is malformatted. See {link}')

    try:
        server = jira.JIRA({'server': 'https://daosio.atlassian.net/'})
        ticket = server.issue(ticket_number, fields=FIELDS)
    except jira.exceptions.JIRAError:
        errors.append('Unable to load ticket data')
        output = [f'Errors are {",".join(errors)}',
                  f'https://daosio.atlassian.net/browse/{ticket_number}']
        set_output('message', '\n'.join(output))
        print('Unable to load ticket data.  Ticket may be private, or may not exist')
        return
    print(f'Ticket summary: {ticket.fields.summary}')
    print(f'Ticket status: {ticket.fields.status}')

    # Best effort to determine which release this PR is going into.
    # 1. Get all the versions from the ticket.
    # 2. Keep only those which match the major.minor of the branch version.
    # 3. Sort the remaining versions and pick the lowest one.
    jira_release_versions = _get_jira_release_versions(ticket)
    targeted_branch_version = _get_targeted_branch_version()
    candidate_versions = sorted([
        version
        for version in jira_release_versions
        if version[:2] == targeted_branch_version[:2]])

    # Add a GitHub label for the release version, if one was found
    if candidate_versions:
        release_version = candidate_versions[0]
        daos_version_str = '.'.join(map(str, release_version))
        labels_to_add.add(f'release-{daos_version_str}')
        # Also add a label if the ticket is approved to merge for this release version.
        # Or remove if it is not approved to merge for this release version.
        if release_version in _jira_approved_to_merge(ticket):
            labels_to_add.add('approved-to-merge')
        elif 'approved-to-merge' in labels_on_pr:
            labels_to_clear.add('approved-to-merge')

    output = [
        f"Ticket title is '{ticket.fields.summary}'",
        f"Status is '{ticket.fields.status}'"
    ]

    if ticket.fields.labels:
        label_str = ','.join(ticket.fields.labels)
        output.append(f"Labels: '{label_str}'")

    if errors:
        output.append(f'Errors are {",".join(errors)}')

    output.append(f'https://daosio.atlassian.net/browse/{ticket_number}')

    set_output('message', '\n'.join(output))

    if labels_to_add:
        set_output('label', '\n'.join(sorted(labels_to_add)))

    # Remove all managed labels which are not to be set.
    for label in labels_on_pr:
        # Remove any release-* labels which are not in the set of labels to be applied
        if label.startswith('release-') and label not in labels_to_add:
            labels_to_clear.add(label)
    if labels_to_clear:
        set_output('label-clear', '\n'.join(sorted(labels_to_clear)))

    if errors:
        sys.exit(1)


if __name__ == '__main__':
    main()
