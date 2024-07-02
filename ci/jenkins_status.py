#!/usr/bin/env python3

"""Parse Jenkins build test results"""

import argparse
import json
import sys
import urllib
from urllib.request import urlopen

JENKINS_HOME = "https://build.hpdd.intel.com/job/daos-stack"


class TestResult:
    """Represents a single Jenkins test result"""

    def __init__(self, data, blocks):
        name = data["name"]
        if "-" in name:
            try:
                (num, full) = name.split("-", 1)
                test_num = int(num)
                if full[-5] == "-":
                    full = full[:-5]
                name = f"{full} ({test_num})"
            except ValueError:
                pass
        self.name = name
        self.cname = data["className"]
        self.skipped = False
        self.passed = False
        self.failed = False
        assert data["status"] in ("PASSED", "FIXED", "SKIPPED", "FAILED", "REGRESSION")
        if data["status"] in ("PASSED", "FIXED"):
            self.passed = True
        elif data["status"] in ("FAILED", "REGRESSION"):
            self.failed = True
        elif data["status"] == "SKIPPED":
            self.skipped = True
        self.data = data
        self.blocks = blocks

    def info(self, prefix=""):
        """Return a string describing the test"""
        return f"{prefix}{self.cname}\t\t{self.name}"

    def full_info(self):
        """Return a longer string describing the test"""

        tcl = []
        if self.blocks is not None:
            tcl.extend(reversed(self.blocks))

        tcl.append(f"{self.cname}.{self.name}")
        details = self.data["errorDetails"]
        if details:
            return " / ".join(tcl) + "\n" + details.replace("\\n", "\n")
        return self.info()

    # Needed for set operations to compare results across sets.
    def __eq__(self, other):
        return self.name == other.name and self.cname == other.cname

    # Needed to be able to add results to sets.
    def __hash__(self):
        return hash((self.name, self.cname))

    def __str__(self):
        return self.name

    def __repr__(self):
        return f"Test result of {self.cname}"


def je_load(job_name, jid=None, what=None, tree=None):
    """Fetch something from Jenkins and return as native type."""

    url = f"{JENKINS_HOME}/job/daos/job/{job_name}"

    if jid:
        url += f"/{jid}"
        if what:
            url += f"/{what}"

    url += "/api/json"

    if tree:
        url += f"?tree={tree}"

    with urlopen(url) as f:  # nosec
        return json.load(f)


def show_job(job_name, jid, timed=None):
    """Parse one job

    timed: None - check all builds,
           True - only check timed builds.
           False - only check non-timed builds.

    Return a list of failed test objects"""

    if timed is not None:
        jdata = je_load(job_name, jid=jid, tree="actions[causes]")
        if (
            "causes" not in jdata["actions"][0]
            or jdata["actions"][0]["causes"][0]["_class"]
            != "hudson.triggers.TimerTrigger$TimerTriggerCause"
        ):
            if timed:
                return None
        else:
            if not timed:
                return None

    try:
        jdata = je_load(job_name, jid=jid, what="testReport")
    except urllib.error.HTTPError:
        print(f"Job {jid} of {job_name} has no test results")
        return None

    print(f"Checking job {jid} of {job_name}")

    failed = []

    assert not jdata["testActions"]
    for suite in jdata["suites"]:
        for k in suite["cases"]:
            tr = TestResult(k, suite["enclosingBlockNames"])
            if not tr.failed:
                continue
            failed.append(tr)
    return failed


def test_against_job(all_failed, job_name, count, timed=True):
    """Check for failures in existing test runs

    Takes set of failed tests, returns set of unexplained tests
    """
    data = je_load(job_name)
    lcb = data["lastCompletedBuild"]["number"]
    main_failed = set()
    ccount = 0
    for build in data["builds"]:
        jid = build["number"]
        if jid > lcb:
            print(f"Job {jid} is of {job_name} is still running, skipping")
        failed = show_job(job_name, jid, timed=timed)
        if not isinstance(failed, list):
            continue
        for test in failed:
            main_failed.add(test)
        ccount += 1
        if count == ccount:
            break

        unexplained = all_failed.difference(main_failed)
        if not unexplained:
            print(f"Stopping checking at {ccount} builds, all failures explained")
            break

    ignore = all_failed.intersection(main_failed)
    if ignore:
        print(
            f"Tests which failed in the PR and have also failed in {job_name} builds."
        )
        for test in ignore:
            print(test.full_info())

    return all_failed.difference(main_failed)


def main():
    """Check the results of a PR"""

    parser = argparse.ArgumentParser(description="Check Jenkins test results")
    parser.add_argument("--pr", type=int, required=True)
    parser.add_argument("--target", default="master", choices=["master", "release/2.4"])
    parser.add_argument("--more", action="store_true")

    args = parser.parse_args()

    job_name = f"PR-{args.pr}"

    try:
        data = je_load(job_name)
    except urllib.error.HTTPError as error:
        if error.code == 404:
            print("Unable to query Jenkins, invalid PR number")
            sys.exit(1)

    if not data["lastCompletedBuild"]:
        print("PR has no completed jobs")
        sys.exit(1)
    lcb = data["lastCompletedBuild"]["number"]

    all_failed = set()
    for build in data["builds"]:
        jid = build["number"]
        if jid > lcb:
            print(f"Job {jid} is of {job_name} is still running, skipping")
            continue
        failed = show_job(job_name, jid)
        if not isinstance(failed, list):
            continue
        for test in failed:
            all_failed.add(test)
        break
    if not all_failed:
        print("No failed tests in PR, returning")
        return

    build_count = 14
    weekly_build_count = 4

    if args.more:
        build_count = 60
        weekly_build_count = 10

    print(f"PR had failed {len(all_failed)} tests, checking against landings builds")

    if args.target == "master":
        print("Checking daily builds")
        all_failed = test_against_job(all_failed, "daily-testing", build_count)

        if all_failed:
            print("Checking weekly builds")
            all_failed = test_against_job(all_failed, "weekly-testing", weekly_build_count)

        if all_failed:
            print("Checking landing builds")
            all_failed = test_against_job(all_failed, "master", build_count, timed=False)
    else:
        print("Checking daily builds")
        all_failed = test_against_job(all_failed, "release%252F2.4", build_count)

        if all_failed:
            print("Checking weekly builds")
            all_failed = test_against_job(all_failed, "weekly-2.4-testing", weekly_build_count)

        if all_failed:
            print("Checking landing builds")
            all_failed = test_against_job(
                all_failed, "release%252F2.4", build_count, timed=False
            )

    if all_failed:
        print("Tests which only failed in the PR")
        for test in all_failed:
            print(test.full_info())
        sys.exit(1)


if __name__ == "__main__":
    main()
