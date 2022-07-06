#!/usr/bin/env python3
"""Interface between CI and bug-tracking tools"""

import os
import sys
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


VALID_COMPONENTS = ('gha', 'object', 'dfs', 'tse', 'vea', 'test', 'doc', 'build', 'bio')


def set_output(key, value):
    """ Set a key-value pair in GitHub actions metadata"""

    print(f'::set-output name={key}::{value}')


def main():
    """Run the script"""

    priority = False
    errors = []

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
            if component not in VALID_COMPONENTS:
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

    # Check format of ticket_number using regexp?

    try:
        ticket = server.issue(ticket_number, fields='summary,issuetype,status,labels,fixVersions')
    except jira.exceptions.JIRAError:
        set_output('errors', f"Unable to load ticket data for '{ticket_number}'")
        print('Unable to load ticket data.  Ticket may be private, or may not exist')
        return
    print(ticket.fields.summary)
    print(ticket.fields.issuetype)
    print(ticket.fields.status)
    print(ticket.fields.labels)
    print(ticket.fields.fixVersions)

    set_output('summary', ticket.fields.summary)
    set_output('status', f'Ticket status%25%0A{ticket.fields.status}')
    set_output('summary', ticket.fields.summary)
    for version in ticket.fields.fixVersions:
        if str(version) in ('2.2 Community Release', '2.4 Community Release'):
            priority = True

    set_output('labels', ','.join(ticket.fields.labels))

    if priority:
        set_output('priority', 'elevated')
        print('Job should run at high priority')
    else:
        set_output('priority', 'standard')
    if errors:
        set_output('errors', f'Errors are {",".join(errors)}')
        sys.exit(1)


if __name__ == '__main__':
    main()
