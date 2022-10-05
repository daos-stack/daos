### Before requesting gatekeeper:

* [ ] Two review approvals and any prior change requests have been resolved.
* [ ] Testing is complete and all tests passed or there is a reason documented in the PR why it
      should be force landed and forced-landing tag is set.
* [ ] Commit messages follows the guidelines outlined
      [here](https://daosio.atlassian.net/wiki/spaces/DC/pages/11133911069/Commit+Comments).
* [ ] Any tests skipped by the ticket being addressed have been run and passed in the PR.

### Gatekeeper:

* [ ] You are the appropriate gatekeeper to be landing the patch.
* [ ] The PR has 2 reviews by people familiar with the code.
* [ ] Any appropriate watchers have had a chance to review the PR.
* [ ] Review comments are sufficiently resolved, particularly by prior reviewers that requested
      changes.
* [ ] Githooks were used. If not, there is a sufficient reason to move forward.  Check copyrights
      if githooks were not used.
* [ ] Checkpatch issues are resolved.  Pay particular attention to ones that will show up on future
      PRs.
* [ ] All builds have passed.  Check non-required builds to ensure they don't indicate any problem
      such as a compiler warning on extraneous platforms.
* [ ] No new NLT or valgrind warnings.  Check the classic view. This step should only matter if
      the build requires force landing.
* [ ] Ensure sufficent testing is done.   Check feature pragmas and test tags. Check that tests
      skipped for the ticket are run and now pass with the changes.
* [ ] Quick-build or Quick-functional is not used.
* [ ] Pay attention to PRs that may affect compatibility between versions and ensure it has been
      addressed.
* [ ] Check the target branch.   If it is master branch, should the PR go to a feature branch?  If
      it is a release branch, does it have merge approval in the JIRA ticket.
* [ ] Check the commit message when landing. Check the standard
      [here](https://daosio.atlassian.net/wiki/spaces/DC/pages/11133911069/Commit+Comments). Edit
      it to create a single commit. If necessary, ask submitter for a new summary.
