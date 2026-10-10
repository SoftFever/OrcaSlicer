#!/usr/bin/env python3
"""Tests for scripts/ci_macos_admission.py (stdlib unittest, no external deps).

Run from the repo root:  python -m unittest discover -s scripts/tests -v
"""

import datetime
import os
import sys
import unittest
import urllib.error

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import ci_macos_admission as admission  # noqa: E402

UTC = datetime.timezone.utc
NOON = datetime.datetime(2026, 10, 10, 12, 0, tzinfo=UTC)


def job(name, status="completed", conclusion="success", labels=("macos-15",)):
    return {"name": name, "status": status, "conclusion": conclusion, "labels": list(labels)}


def gate(arch, status="completed", conclusion="success"):
    return job(f"build_macos_arch ({arch}) / Wait for a macOS runner", status, conclusion,
               labels=("ubuntu-24.04",))


def build(arch, status="in_progress"):
    return job(f"build_macos_arch ({arch}) / Build Deps / Build OrcaSlicer / Build OrcaSlicer",
               status, None if status != "completed" else "success")


CHECK_CACHE = job("build_macos_arch (arm64) / Check Cache", labels=("ubuntu-24.04",))
UNIVERSAL_DONE = job("Build macOS Universal / Build OrcaSlicer")
TESTS_DONE = job("macOS arm64 / Unit Tests")
PUSH = {"id": 1, "event": "push", "head_branch": "main", "status": "in_progress"}
PR = {"id": 2, "event": "pull_request", "head_branch": "topic", "status": "in_progress"}


class MacosDoneTest(unittest.TestCase):
    def test_not_done_before_the_final_jobs_are_listed(self):
        self.assertFalse(admission.macos_done([CHECK_CACHE, build("arm64", "completed")]))

    def test_not_done_while_a_final_job_runs(self):
        running = job("macOS arm64 / Unit Tests", "in_progress", None)
        self.assertFalse(admission.macos_done([UNIVERSAL_DONE, running]))

    def test_done_when_the_final_jobs_finished(self):
        self.assertTrue(admission.macos_done([build("arm64", "completed"), UNIVERSAL_DONE, TESTS_DONE]))

    def test_done_when_the_final_jobs_were_skipped(self):
        # A skipped caller job is listed under its own name, without labels.
        skipped = [job("Build macOS Universal", conclusion="skipped", labels=()),
                   job("macOS arm64", conclusion="skipped", labels=())]
        self.assertTrue(admission.macos_done(skipped))


class RunDemandTest(unittest.TestCase):
    def test_push_reserves_before_its_macos_jobs_are_listed(self):
        self.assertEqual(admission.run_demand(PUSH, []), admission.RESERVE)
        self.assertEqual(admission.run_demand(PUSH, [CHECK_CACHE]), admission.RESERVE)

    def test_push_keeps_its_reserve_between_stages(self):
        jobs = [build("arm64", "completed"), build("x86_64", "completed")]
        self.assertEqual(admission.run_demand(PUSH, jobs), admission.RESERVE)

    def test_push_releases_when_macos_is_done(self):
        jobs = [build("arm64", "completed"), UNIVERSAL_DONE, TESTS_DONE]
        self.assertEqual(admission.run_demand(PUSH, jobs), 0)

    def test_queued_jobs_count_as_busy(self):
        jobs = [build("arm64", "queued")]
        self.assertEqual(admission.run_demand(PR, jobs), 1)

    def test_pull_request_holds_nothing_before_it_is_let_in(self):
        self.assertEqual(admission.run_demand(PR, [CHECK_CACHE, gate("arm64", "in_progress", None)]), 0)

    def test_pull_request_holds_both_runners_once_let_in(self):
        # The other arch may still be waiting for deps, and goes straight through.
        self.assertEqual(admission.run_demand(PR, [gate("arm64"), gate("x86_64", "in_progress", None)]),
                         admission.RESERVE)

    def test_pull_request_keeps_its_runners_for_its_later_jobs(self):
        # Both arch builds are done and the universal build and tests are not
        # listed yet: they will need both runners without passing the line.
        jobs = [gate("arm64"), gate("x86_64"), build("arm64", "completed"), build("x86_64", "completed")]
        self.assertEqual(admission.run_demand(PR, jobs), 2)

    def test_pull_request_releases_when_macos_is_done(self):
        jobs = [gate("arm64"), gate("x86_64"), UNIVERSAL_DONE, TESTS_DONE]
        self.assertEqual(admission.run_demand(PR, jobs), 0)

    def test_pull_request_without_the_line_holds_what_it_runs(self):
        # A run started from a workflow without the line.
        self.assertEqual(admission.run_demand(PR, [build("arm64"), build("x86_64", "queued")]), 2)

    def test_finished_and_non_macos_jobs_are_not_counted(self):
        jobs = [build("arm64", "completed"), CHECK_CACHE,
                job("build_linux (ubuntu-24.04) / Check Cache", "in_progress", None, ("ubuntu-24.04",))]
        self.assertEqual(admission.run_demand(PR, jobs), 0)


class FakeApi:
    """Answers GET requests from a dict of path -> list of pages (or one body)."""

    def __init__(self, responses):
        self.responses = responses
        self.calls = []

    def get(self, path, **params):
        self.calls.append((path, params))
        if path.endswith("/runs") and "status" in params:
            runs = self.responses.get(("runs", params["status"]), [])
            start = (params["page"] - 1) * params["per_page"]
            return {"total_count": len(runs), "workflow_runs": runs[start:start + params["per_page"]]}
        if path.endswith("/runs"):
            return {"workflow_runs": self.responses.get(("runs", params.get("event")), [])[:1]}
        return {"jobs": self.responses[path]}


def jobs_path(run_id):
    return f"repos/o/r/actions/runs/{run_id}/jobs"


class MeasureTest(unittest.TestCase):
    def test_sums_in_progress_and_queued_runs(self):
        queued_pr = dict(PR, id=3, status="queued")
        api = FakeApi({
            ("runs", "in_progress"): [PUSH, PR],
            ("runs", "queued"): [queued_pr],
            jobs_path(1): [CHECK_CACHE],
            jobs_path(2): [gate("arm64"), build("arm64")],
            jobs_path(3): [gate("arm64"), gate("x86_64"), build("arm64", "queued")],
        })
        total, lines, here = admission.measure(api, "o/r", "build_all.yml", 99, NOON)
        self.assertEqual(total, 3 * admission.RESERVE)
        self.assertEqual(len(lines), 3)
        self.assertFalse(here)

    def test_reads_every_page_of_runs(self):
        runs = [dict(PR, id=i) for i in range(150)]
        responses = {("runs", "in_progress"): runs}
        responses.update({jobs_path(i): [gate("arm64")] for i in range(150)})
        total, _, _ = admission.measure(FakeApi(responses), "o/r", "build_all.yml", 999, NOON)
        self.assertEqual(total, 150 * admission.RESERVE)

    def test_reserves_for_the_nightly_until_its_run_appears(self):
        due = datetime.datetime(2026, 10, 10, 2, 10, tzinfo=UTC)
        yesterday = {"created_at": "2026-10-09T02:20:00Z"}
        total, lines, _ = admission.measure(
            FakeApi({("runs", "schedule"): [yesterday]}), "OrcaSlicer/OrcaSlicer", "build_all.yml", 99, due)
        self.assertEqual(total, admission.RESERVE)
        self.assertIn("nightly", lines[0])

    def test_no_nightly_reserve_once_its_run_exists(self):
        due = datetime.datetime(2026, 10, 10, 2, 30, tzinfo=UTC)
        today = {"created_at": "2026-10-10T02:21:00Z"}
        total, _, _ = admission.measure(
            FakeApi({("runs", "schedule"): [today]}), "OrcaSlicer/OrcaSlicer", "build_all.yml", 99, due)
        self.assertEqual(total, 0)

    def test_no_nightly_reserve_in_a_fork(self):
        due = datetime.datetime(2026, 10, 10, 2, 10, tzinfo=UTC)
        total, _, _ = admission.measure(FakeApi({}), "fork/OrcaSlicer", "build_all.yml", 99, due)
        self.assertEqual(total, 0)

    def test_counts_a_run_in_both_lists_once(self):
        api = FakeApi({
            ("runs", "in_progress"): [PUSH],
            ("runs", "queued"): [dict(PUSH, status="queued")],
            jobs_path(1): [],
        })
        total, _, _ = admission.measure(api, "o/r", "build_all.yml", 99, NOON)
        self.assertEqual(total, admission.RESERVE)

    def test_knows_when_its_own_run_was_let_in(self):
        # Every runner is held by runs that were let in whole, so none waits for
        # a runner for its second arch.
        responses = {("runs", "in_progress"): [dict(PR, id=i) for i in (1, 2)]}
        responses.update({jobs_path(i): [gate("arm64"), gate("x86_64", "in_progress", None)] for i in (1, 2)})
        total, _, here = admission.measure(FakeApi(responses), "o/r", "build_all.yml", 2, NOON)
        self.assertEqual(total, 2 * admission.RESERVE)
        self.assertTrue(here)

    def test_no_nightly_lookup_outside_its_window(self):
        api = FakeApi({})
        admission.measure(api, "o/r", "build_all.yml", 99, NOON)
        self.assertFalse(any("event" in params for _, params in api.calls))


class WaitTest(unittest.TestCase):
    def run_wait(self, results, limit=5, wait_minutes=60, poll_seconds=120):
        results = iter(results)
        now = [0.0]

        def measure_now():
            result = next(results)
            if isinstance(result, Exception):
                raise result
            if result == "here":
                return 99, [], True
            return result, [], False

        def sleep(seconds):
            now[0] += seconds

        reason = admission.wait(measure_now, limit, wait_minutes, poll_seconds,
                                clock=lambda: now[0], sleep=sleep, log=lambda _: None)
        return reason, now[0]

    def test_lets_in_when_both_runners_fit(self):
        self.assertEqual(self.run_wait([3]), ("a runner is free", 0))

    def test_waits_while_full(self):
        self.assertEqual(self.run_wait([4, 5, 3]), ("a runner is free", 240))

    def test_second_arch_goes_straight_through(self):
        self.assertEqual(self.run_wait(["here"]), ("this run already holds its runners", 0))

    def test_lets_in_after_repeated_api_errors(self):
        error = urllib.error.URLError("rate limited")
        reason, _ = self.run_wait([error, error, error])
        self.assertEqual(reason, "the queue could not be read")

    def test_a_good_read_resets_the_error_count(self):
        error = urllib.error.URLError("rate limited")
        reason, _ = self.run_wait([error, error, 5, error, error, 3])
        self.assertEqual(reason, "a runner is free")

    def test_lets_in_at_the_time_limit(self):
        reason, elapsed = self.run_wait([5] * 100, wait_minutes=10, poll_seconds=120)
        self.assertEqual(reason, "it waited 10 minutes")
        self.assertLess(elapsed, 600)


if __name__ == "__main__":
    unittest.main()
