#!/usr/bin/env python3
"""Interface between CI and bug-tracking tools"""

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


def set_output(key, value):
    """ Set a key-value pair in GitHub actions metadata"""

    print(f'::set-output name={key}::{value}')


def main():
    """Run the script"""

    priority = False

    options = {'server': 'https://daosio.atlassian.net/'}

    server = jira.JIRA(options)

    my_issue = 'DAOS-7821'

    try:
        ticket = server.issue(my_issue)
    except jira.exceptions.JIRAError:
        print('Unable to load ticket data.  Ticket may be private, or may not exist')
        return
    print(ticket.fields.summary)
    print(ticket.fields.issuetype)
    print(ticket.fields.status)
    print(ticket.fields.labels)
    print(ticket.fields.fixVersions)

    set_output('status', ticket.fields.status)
    for version in ticket.fields.fixVersions:
        if str(version) in ('2.2 Community Release', '2.4 Community Release'):
            priority = True

    set_output('labels', ','.join(ticket.fields.labels))

    if priority:
        set_output('priority', 'elevated')
        print('Job should run at high priority')
    else:
        set_output('priority', 'standard')


if __name__ == '__main__':
    main()
