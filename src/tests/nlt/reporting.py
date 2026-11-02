"""NLT: results collection, warnings/JUnit output and the summary report.

(C) Copyright 2020-2024 Intel Corporation.
(C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
(C) Copyright 2025 Google LLC
(C) Copyright 2025 Enakta Labs Ltd

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import json
import socket
import sys
import time

import junit_xml

from .base import get_active_test


class WarningsFactory():
    """Class to parse warnings, and save to JSON output file

    Take a list of failures, and output the data in a way that is best
    displayed according to
    https://github.com/jenkinsci/warnings-ng-plugin/blob/master/doc/Documentation.md
    """

    # Error levels supported by the reporting are LOW, NORMAL, HIGH, ERROR.

    def __init__(self,
                 filename,
                 junit=False,
                 class_id=None,
                 post=False,
                 post_error=False,
                 check=None):
        # pylint: disable=consider-using-with
        self._fd = open(filename, 'w')
        self.filename = filename
        self.post = post
        self.post_error = post_error
        self.check = check
        self.issues = []
        self._class_id = class_id
        self.pending = []
        self._running = True
        # Records of every reported test case (for the human-readable summary) and any other
        # WarningsFactory objects whose findings should be folded into this object's summary.
        self.test_records = []
        self.linked_factories = []
        self._summary_ctx = None
        self._summary_written = False
        # Save the filename of the object, as __file__ does not
        # work in __del__
        self._file = __file__.lstrip('./')
        self._flush()

        if junit:
            # Insert a test-case and force it to failed.  Save this to file
            # and keep it there, until close() method is called, then remove
            # it and re-save.  This means any crash will result in there
            # being a results file with an error recorded.
            tc_startup = junit_xml.TestCase('Startup', classname=self._class_name('core'))
            tc_sanity = junit_xml.TestCase('Sanity', classname=self._class_name('core'))
            tc_sanity.add_error_info('NLT exited abnormally')
            self.test_suite = junit_xml.TestSuite('Node Local Testing',
                                                  test_cases=[tc_startup, tc_sanity])
            self._write_test_file()
        else:
            self.test_suite = None

    def _class_name(self, class_name):
        """Return a formatted ID string for class"""
        if self._class_id:
            return f'NLT.{self._class_id}.{class_name}'
        return f'NLT.{class_name}'

    def __del__(self):
        """Ensure the file is flushed on exit.

        If it hasn't already been closed then mark an error
        """
        if not self._fd:
            return

        entry = {}
        entry['fileName'] = self._file
        # pylint: disable=protected-access
        entry['lineStart'] = sys._getframe().f_lineno
        entry['message'] = 'Tests exited without shutting down properly'
        entry['severity'] = 'ERROR'
        self.issues.append(entry)

        # Do not try and write the junit file here, as that does not work
        # during teardown.
        self.test_suite = None
        self.close()

    def add_test_case(self, name, failure=None, test_class='core', output=None, duration=None,
                      stdout=None, stderr=None):
        """Add a test case to the results

        class and other metadata will be set automatically,
        if failure is set the test will fail with the message
        provided.  Saves the state to file after each update.
        """
        if not self.test_suite:
            return

        test_case = junit_xml.TestCase(name, classname=self._class_name(test_class),
                                       elapsed_sec=duration, stdout=stdout, stderr=stderr)
        if failure:
            test_case.add_failure_info(failure, output=output)
        self.test_suite.test_cases.append(test_case)

        # Keep a lightweight record for the human-readable summary report.
        self.test_records.append({'name': name,
                                  'test_class': test_class,
                                  'failure': failure,
                                  'duration': duration})

        self._write_test_file()

    def link(self, other):
        """Fold another WarningsFactory's findings into this object's summary report"""
        self.linked_factories.append(other)

    def _write_test_file(self):
        """Write test results to file"""
        with open('nlt-junit.xml', 'w') as file:
            junit_xml.TestSuite.to_file(file, [self.test_suite], prettyprint=True)

    def explain(self, line, log_file, esignal):
        """Log an error, along with the other errors it caused

        Log the line as an error, and reference everything in the pending
        array.
        """
        count = len(self.pending)
        symptoms = set()
        locs = set()
        mtype = 'Fault injection'

        sev = 'LOW'
        if esignal:
            symptoms.add(f'Process died with signal {esignal}')
            sev = 'ERROR'
            mtype = 'Fault injection caused crash'
            count += 1

        if count == 0:
            return

        for (sline, smessage) in self.pending:
            locs.add(f'{sline.filename}:{sline.lineno}')
            symptoms.add(smessage)

        preamble = f'Fault injected here caused {count} errors, logfile {log_file}:'

        message = f"{preamble} {' '.join(sorted(symptoms))} {' '.join(sorted(locs))}"

        self.add(line, sev, message, cat='Fault injection location', mtype=mtype)
        self.pending = []

    def add(self, line, sev, message, cat=None, mtype=None):
        """Log an error

        Describe an error and add it to the issues array.
        Add it to the pending array, for later clarification
        """
        entry = {}
        entry['fileName'] = line.filename
        if mtype:
            entry['type'] = mtype
        else:
            entry['type'] = message
        if cat:
            entry['category'] = cat
        entry['lineStart'] = line.lineno
        # Jenkins no longer seems to display the description.
        entry['description'] = message
        entry['message'] = f'{line.get_anon_msg()}\n{message}'
        entry['severity'] = sev
        entry['nlt_test'] = get_active_test()
        self.issues.append(entry)
        if self.pending and self.pending[0][0].pid != line.pid:
            self.reset_pending()
        self.pending.append((line, message))
        self._flush()
        if self.post or (self.post_error and sev in ('HIGH', 'ERROR')):
            # https://docs.github.com/en/actions/reference/workflow-commands-for-github-actions
            if self.post_error:
                message = line.get_msg()
            print(f'::warning file={line.filename},line={line.lineno},::{self.check}, {message}')

    def reset_pending(self):
        """Reset the pending list

        Should be called before iterating on each new file, so errors
        from previous files aren't attributed to new files.
        """
        self.pending = []

    def _flush(self):
        """Write the current list to the json file

        This is done just in case of crash.  This function might get called
        from the __del__ method of DaosServer, so do not use __file__ here
        either.
        """
        self._fd.seek(0)
        self._fd.truncate(0)
        data = {}
        data['issues'] = list(self.issues)
        if self._running:
            # When the test is running insert an error in case of abnormal
            # exit, so that crashes in this code can be identified.
            entry = {}
            entry['fileName'] = self._file
            # pylint: disable=protected-access
            entry['lineStart'] = sys._getframe().f_lineno
            entry['severity'] = 'ERROR'
            entry['message'] = 'Tests are still running'
            data['issues'].append(entry)
        json.dump(data, self._fd, indent=2)
        self._fd.flush()

    def close(self):
        """Save, and close the log file"""
        self._running = False
        self._flush()
        self._fd.close()
        self._fd = None
        print(f'Closed JSON file {self.filename} with {len(self.issues)} errors')
        if self.test_suite:
            # This is a controlled shutdown, so wipe the error saying forced exit.
            self.test_suite.test_cases[1].errors = []
            self.test_suite.test_cases[1].error_message = []
            self._write_test_file()

    def arm_summary(self, filename, args, conf, start_time):
        """Record what write_summary needs so it can be emitted from any exit path.

        This lets the caller guarantee a summary even when a test, startup or teardown raises,
        which is exactly when the summary is most useful.
        """
        self._summary_ctx = (filename, args, conf, start_time)

    def write_summary(self, filename=None, args=None, conf=None, duration=None):
        """Write a single human-readable summary of the whole run.

        Consolidates the test-case results held by this object with the log-analysis findings
        from this object and every linked object, so a developer can find a failure and the log
        lines that caused it without opening the several separate xml/json/log artifacts.

        With no arguments the values passed to arm_summary() are used; the summary is written at
        most once regardless of how many exit paths call this.
        """
        if filename is None and self._summary_ctx is not None:
            filename, args, conf, start_time = self._summary_ctx
            duration = time.perf_counter() - start_time
        if self._summary_written or not filename:
            return
        self._summary_written = True

        # Aggregate findings from this object and all linked factories (server/client leaks).
        sev_rank = {'ERROR': 0, 'HIGH': 1, 'NORMAL': 2, 'LOW': 3}
        findings = []
        for source in [self] + self.linked_factories:
            label = getattr(source, 'check', None) or source.filename
            for issue in source.issues:
                findings.append((label, issue))
        findings.sort(key=lambda x: sev_rank.get(x[1].get('severity', 'LOW'), 4))

        # Group findings by the test that produced them; findings with no owning test are
        # from shared/server logs and are reported under a "server-wide" heading.
        by_test = {}
        for label, issue in findings:
            by_test.setdefault(issue.get('nlt_test') or None, []).append((label, issue))

        def _finding_lines(items, indent=''):
            out = []
            for label, issue in items:
                sev = issue.get('severity', 'LOW')
                loc = f'{issue.get("fileName", "?")}:{issue.get("lineStart", "?")}'
                msg = str(issue.get('message', '')).replace('\n', ' — ')
                out.append(f'{indent}- **{sev}** [{label}] `{loc}`')
                out.append(f'{indent}  - {msg}')
            return out

        failures = [r for r in self.test_records if r['failure']]
        passes = [r for r in self.test_records if not r['failure']]
        timed = sorted((r for r in self.test_records if r['duration']),
                       key=lambda r: r['duration'], reverse=True)

        verdict = 'FAILED' if failures or any(
            f[1].get('severity') in ('HIGH', 'ERROR') for f in findings) else 'PASSED'

        lines = []
        lines.append(f'# NLT summary: {verdict}')
        lines.append('')
        if args is not None:
            build = 'valgrind/memcheck' if getattr(args, 'memcheck', 'no') != 'no' else 'standard'
            lines.append(f'- Mode: `{getattr(args, "mode", None)}`  '
                         f'Class: `{getattr(args, "class_name", None)}`  Build: {build}')
        lines.append(f'- Host: `{socket.gethostname()}`')
        if duration is not None:
            lines.append(f'- Total run time: {duration:.0f}s')
        lines.append(f'- Tests: {len(passes)} passed, {len(failures)} failed  |  '
                     f'Findings: {len(findings)}')
        lines.append('')

        if failures:
            lines.append('## Failed tests')
            lines.append('')
            for rec in failures:
                dur = f' ({rec["duration"]:.1f}s)' if rec['duration'] else ''
                first = str(rec['failure']).splitlines()[0] if rec['failure'] else ''
                lines.append(f'- **{rec["name"]}**{dur}: {first}')
                # Show the log lines this test produced right next to the failure.
                lines.extend(_finding_lines(by_test.get(rec['name'], []), indent='  '))
            lines.append('')

        failed_names = {r['name'] for r in failures}
        has_extra = any(items for name, items in by_test.items()
                        if name not in failed_names and items)
        if has_extra:
            lines.append('## Log-analysis findings')
            lines.append('')
            # Per-test groups first (skip tests already shown in the failures section), then the
            # shared/server-wide findings that could not be tied to a single test.
            for test_name in sorted(k for k in by_test if k is not None):
                if test_name in failed_names:
                    continue
                lines.append(f'### {test_name}')
                lines.extend(_finding_lines(by_test[test_name]))
                lines.append('')
            if by_test.get(None):
                lines.append('### server-wide / shared logs')
                lines.extend(_finding_lines(by_test[None]))
                lines.append('')

        if conf is not None and getattr(conf, 'valgrind_errors', False):
            lines.append('## Valgrind')
            lines.append('')
            lines.append('- Valgrind/memcheck errors detected; see `*memcheck.xml`.')
            lines.append('')

        if timed:
            lines.append('## Slowest tests')
            lines.append('')
            for rec in timed[:10]:
                lines.append(f'- {rec["name"]}: {rec["duration"]:.1f}s')
            lines.append('')

        text = '\n'.join(lines) + '\n'
        with open(filename, 'w') as sfd:
            sfd.write(text)
        print(f'\n===== NLT summary ({verdict}) written to {filename} =====')
        print(text)
