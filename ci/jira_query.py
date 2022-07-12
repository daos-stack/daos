#!/usr/bin/env python3
"""Interface between CI and bug-tracking tools"""

import os
import sys
import json
import urllib
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
VALID_COMPONENTS = ('build', 'ci', 'doc', 'gha', 'il', 'mercury', 'test')

# 10044 is "Approved to Merge"
# 10045 is "Required for Version"
FIELDS = 'summary,status,labels,customfield_10044,customfield_10045'

# Expected values for Status.  Tickets which are closed should not be being worked on, and tickets
# which are Open or Reopened should be set to In Progress when being worked on.
STATUS_VALUES_ALLOWED = ('In Review', 'In Progress')

# Labels in GitHub which this script will set/clear based on the logic below.
MANAGED_LABELS = ('release-2.2', 'release-2.4', 'priority')


def set_output(key, value):
    """ Set a key-value pair in GitHub actions metadata"""

    clean_value = value.replace('\n', '%0A')
    print(f'::set-output name={key}::{clean_value}')
    print(value)


# pylint: disable=too-many-branches
def main():
    """Run the script"""

    priority = None
    errors = []
    gh_label = []

    options = {'server': 'https://daosio.atlassian.net/'}

    server = jira.JIRA(options)

    # Find the ticket number, either from the environment if possible or the command line.
    pr_title = os.getenv('PR_TITLE')
    if pr_title:
        # GitHub actions (or similar), perform some checks on the title of the PR.
        parts = pr_title.split(' ')
        ticket_number = parts[0]
        component = parts[1]
        if component.endswith(':'):
            component = component[:-1]
            col = component.lower()
            if col != component:
                errors.append('Component should be lower-case')
            if col not in VALID_COMPONENTS and not os.path.isdir(os.path.join('src', col)) \
               and not os.path.isdir(os.path.join('src', 'client', col)) \
               and not os.path.isdir(os.path.join('utils', col)):

                errors.append('Unknown component')
        else:
            errors.append('component not formatted correctly')
        if len(pr_title) > 80:
            errors.append('Title of PR is too long')
    else:
        if len(sys.argv) > 1:
            ticket_number = sys.argv[1]
        else:
            print('Set PR_TITLE or pass on command line')
            return

    # Check format of ticket_number.
    parts = ticket_number.split('-', maxsplit=1)
    if parts[0] not in ('DAOS', 'CORCI'):
        errors.append('Ticket number prefix incorrect')
    try:
        int(parts[1])
    except ValueError:
        errors.append('Ticket number suffix is not a number')

    try:
        ticket = server.issue(ticket_number, fields=FIELDS)
    except jira.exceptions.JIRAError:
        output = [f"Unable to load ticket data for '{ticket_number}'"]
        output.append(f'https://daosio.atlassian.net/browse/{ticket_number}')
        set_output('message', '\n'.join(output))
        print('Unable to load ticket data.  Ticket may be private, or may not exist')
        return
    print(ticket.fields.summary)
    print(ticket.fields.status)
    if str(ticket.fields.status) not in STATUS_VALUES_ALLOWED:
        errors.append('Ticket status value not as expected')

    # Highest priority, tickets with "Approved to Merge" set.
    if ticket.fields.customfield_10044:
        priority = 1

    # Elevated priority, PRs to master where ticket is "Required for Version" is set.
    if ticket.fields.customfield_10045:

        # Check the target branch here.  Can not be done from a ticket number alone, so only perform
        # this check if we can.

        set_rv_priority = True
        target_branch = os.getenv('GITHUB_BASE_REF')
        if target_branch:
            if target_branch.startswith('release/'):
                set_rv_priority = False

        rv_priority = None

        for version in ticket.fields.customfield_10045:
            if str(version) in ('2.0.3 Community Release', '2.0.3 Community Release',
                                '2.2 Community Release'):
                rv_priority = 2
            if rv_priority is None and str(version) in ('2.4 Community Release'):
                rv_priority = 3

            if str(version) in ('2.2 Community Release'):
                gh_label.append('release-2.2')
            if str(version) in ('2.4 Community Release'):
                gh_label.append('release-2.4')

        if set_rv_priority and priority is None:
            priority = rv_priority

    output = []

    output.append(f"Ticket title is '{ticket.fields.summary}'")
    output.append(f"Status is '{ticket.fields.status}'")

    if ticket.fields.labels:
        label_str = ','.join(ticket.fields.labels)
        output.append(f"Labels: '{label_str}'")

    if priority is not None:
        output.append(f'Job should run at elevated priority ({priority})')
        gh_label.append('priority')

    if errors:
        output.append(f'Errors are {",".join(errors)}')

    output.append(f'https://daosio.atlassian.net/browse/{ticket_number}')

    set_output('message', '\n'.join(output))

    if gh_label:
        set_output('label', '\n'.join(gh_label))

    gh_url = 'https://api.github.com/repos/daos-stack/daos/issues/9610/labels'
    with urllib.request.urlopen(gh_url) as gh_label_data:
        gh_labels = json.loads(gh_label_data.read())

    to_remove = gh_labels
    for label in gh_label:
        to_remove.remove(label)
    if to_remove:
        set_output('label-clear', '\n'.join(to_remove))

    if errors:
        sys.exit(1)


if __name__ == '__main__':
    main()
