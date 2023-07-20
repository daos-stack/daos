#!/usr/bin/env python3
"""Interface between CI and bug-tracking tools"""

import os
import sys
import json
import time
import urllib
import random
import string
import jira

# Script to improve interaction with Jenkins, GitHub and Jira.  This is intended to work in several
# ways:

# Add comments to Pull Requests in GitHub to help developers and reviewers.
# Assist in keeping Jira up-to-date with development activities
# Set priorities of jobs in Jenkins.

# To do this it should  be run as a GitHub action which will apply comments (and possibly labels)
# to PRs, as well as warning and failing PR builds if any ticket metadata is incorrect.
# It should run inside Jenkins to set appropriate job priority for builds.

# https://jira.readthedocs.io/api.html#module-jira.client
# https://github.com/marketplace/actions/comment-pull-request

# Expected components from the commit message, and directory in src/, src/client or utils/ is also
# valid.  We've never checked/enforced these before so there have been a lot of values used in the
# past.
VALID_COMPONENTS = ('agent', 'build', 'ci', 'csum', 'doc', 'gha', 'il', 'md', 'mercury',
                    'packaging', 'pil4dfs', 'swim', 'test', 'tools')

# Expected ticket prefix.
VALID_TICKET_PREFIX = ('DAOS', 'CORCI', 'SRE')

# 10044 is "Approved to Merge"
# 10045 is "Required for Version"
FIELDS = 'summary,status,labels,customfield_10044,customfield_10045'

# Labels in GitHub which this script will set/clear based on the logic below.
MANAGED_LABELS = ('release-2.2', 'release-2.4', 'priority')


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


def fetch_pr_data():
    """Query GibHub API and return PR metadata"""
    pr_data = None
    if len(sys.argv) == 2:
        try:
            pr_number = int(sys.argv[1])
        except ValueError:
            print('argument must be a value')
            sys.exit(1)

        github_repo = os.environ.get('GITHUB_REPOSITORY', 'daos-stack/daos')
        gh_url = f'https://api.github.com/repos/{github_repo}/pulls/{pr_number}'

        # We occasionally see this fail with rate-limit-exceeded, if that happens then wait for a
        # while and re-try once.
        try:
            with urllib.request.urlopen(gh_url) as raw_pr_data:  # nosec
                pr_data = json.loads(raw_pr_data.read())
        except urllib.error.HTTPError as error:
            if error.code == 403:
                time.sleep(60 * 10)
                with urllib.request.urlopen(gh_url) as raw_pr_data:  # nosec
                    pr_data = json.loads(raw_pr_data.read())
            else:
                raise
    else:
        print('Pass PR number on command line')
        sys.exit(1)

    assert pr_data is not None
    return pr_data


def main():
    """Run the script"""
    # pylint: disable=too-many-branches
    pr_data = fetch_pr_data()

    priority = None
    errors = []
    gh_label = set()
    pr_title = pr_data['title']

    # Revert PRs can be auto-generated, detect and handle this, as well as
    # marking them a priority.
    if pr_title.startswith('Revert "'):
        pr_title = pr_title[8:-1]
        priority = 2

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

    # Highest priority, tickets with "Approved to Merge" set.
    if ticket.fields.customfield_10044:
        priority = 1

    # Elevated priority, PRs to master where ticket is "Required for Version" is set.
    if ticket.fields.customfield_10045:

        # Check the target branch here.  Can not be done from a ticket number alone, so only perform
        # this check if we can.

        rv_priority = None

        for version in ticket.fields.customfield_10045:
            if str(version) in ('2.0.3 Community Release', '2.0.3 Community Release',
                                '2.2 Community Release'):
                rv_priority = 2
            elif str(version) in ('2.4 Community Release'):
                rv_priority = 3

            if str(version) in ('2.2 Community Release'):
                gh_label.add('release-2.2')
            if str(version) in ('2.4 Community Release'):
                gh_label.add('release-2.4')

        # If a PR does not otherwise have priority then use custom values from above.
        if priority is None and not pr_data['base']['ref'].startswith('release'):
            priority = rv_priority

    output = []

    output.append(f"Ticket title is '{ticket.fields.summary}'")
    output.append(f"Status is '{ticket.fields.status}'")

    if ticket.fields.labels:
        label_str = ','.join(ticket.fields.labels)
        output.append(f"Labels: '{label_str}'")

    if priority is not None:
        output.append(f'Job should run at elevated priority ({priority})')
        gh_label.add('priority')

    if errors:
        output.append(f'Errors are {",".join(errors)}')

    output.append(f'https://daosio.atlassian.net/browse/{ticket_number}')

    set_output('message', '\n'.join(output))

    if gh_label:
        set_output('label', '\n'.join(sorted(gh_label)))

    # Remove all managed labels which are not to be set.
    to_remove = []
    for label in pr_data['labels']:
        name = label['name']
        if name in MANAGED_LABELS and name not in gh_label:
            to_remove.append(name)
    if to_remove:
        set_output('label-clear', '\n'.join(to_remove))

    if errors:
        sys.exit(1)


if __name__ == '__main__':
    main()
