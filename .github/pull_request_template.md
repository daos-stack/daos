### Before requesting gatekeeper:

* [ ] Two review approvals and any prior change requests have been resolved.
* [ ] Testing is complete and all tests passed or there is a reason documented in the PR why it should be force landed and forced-landing tag is set.
* [ ] `Features:` (or `Test-tag*`) commit pragma was used or there is a reason documented that there are no appropriate tags for this PR.
* [ ] Commit messages follows the guidelines outlined [here](https://daosio.atlassian.net/wiki/spaces/DC/pages/11133911069/Commit+Comments).
* [ ] Any tests skipped by the ticket being addressed have been run and passed in the PR.

### Gatekeeper:

* [ ] You are the appropriate gatekeeper to be landing the patch.
* [ ] The PR has 2 reviews by people familiar with the code, including appropriate owners.
* [ ] Githooks were used. If not, request that user install them and check copyright dates.
* [ ] Checkpatch issues are resolved.  Pay particular attention to ones that will show up on future PRs.
* [ ] All builds have passed.  Check non-required builds for any new compiler warnings.
* [ ] Sufficient testing is done. Check feature pragmas and test tags and that tests skipped for the ticket are run and now pass with the changes.
* [ ] If applicable, the PR has addressed any potential version compatibility issues.
* [ ] Check the target branch.   If it is master branch, should the PR go to a feature branch?  If it is a release branch, does it have merge approval in the JIRA ticket.
* [ ] Extra checks if forced landing is requested
  * [ ] Review comments are sufficiently resolved, particularly by prior reviewers that requested changes.
  * [ ] No new NLT or valgrind warnings.  Check the classic view.
  * [ ] Quick-build or Quick-functional is not used.
* [ ] Fix the commit message upon landing. Check the standard [here](https://daosio.atlassian.net/wiki/spaces/DC/pages/11133911069/Commit+Comments). Edit it to create a single commit. If necessary, ask submitter for a new summary.
