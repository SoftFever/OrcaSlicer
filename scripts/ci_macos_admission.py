#!/usr/bin/env python3
"""Holds a pull request's macOS build until a hosted macOS runner is free for it.

Runs from a Linux job in build_check_cache.yml, once per macOS arch, in a single
repo-wide line (a concurrency group with queue: max), so only the job at the
front of the line polls. A pull request run is let in whole: its first arch waits
until the runners every active Build all run holds or still needs, plus RESERVE
for this run, fit in MACOS_RUNNER_LIMIT, and its second arch then goes straight
through. Letting arches in one at a time would deadlock, with every runner held
by a run waiting for a second one for its other arch.

- A push, nightly or manual run holds RESERVE runners until its macOS work is
  done. The jobs API lists only the jobs a run has reached, so what it still
  needs cannot be counted and is reserved instead.
- A pull request run that was let in holds RESERVE runners until its macOS work
  is done, which covers its later app build, universal build and tests that
  never pass through the line.
- A run holds at least the macOS jobs it has queued or running.
- In the minutes around the nightly's cron time, RESERVE runners are held for it
  until its run appears.

Any API error repeated FAILURES_BEFORE_ADMIT times, and the WAIT_MINUTES limit,
let the arch in, so a fault here never blocks pull requests.

Environment: GH_TOKEN, REPO, WORKFLOW_REF, GITHUB_RUN_ID, and optionally MACOS_RUNNER_LIMIT,
WAIT_MINUTES, POLL_SECONDS and GITHUB_API_URL.
"""

import datetime
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

# A Build all run builds arm64 and x86_64 at the same time, then the universal
# build and the macOS tests at the same time.
RESERVE = 2

# Must match the job names in build_all.yml and build_check_cache.yml.
GATE_SUFFIX = " / Wait for a macOS runner"
FINAL_JOBS = {"Build macOS Universal", "macOS arm64"}
# Must match the cron in build_all.yml.
NIGHTLY_UTC = datetime.time(2, 15)
NIGHTLY_REPO = "OrcaSlicer/OrcaSlicer"
NIGHTLY_LEAD = datetime.timedelta(minutes=10)
NIGHTLY_GRACE = datetime.timedelta(minutes=45)

ACTIVE = {"queued", "in_progress", "waiting", "pending", "requested"}
FAILURES_BEFORE_ADMIT = 3


def is_macos(job):
    return any(label.startswith("macos-") for label in job.get("labels") or [])


def macos_done(jobs):
    """True once the universal build and the macOS tests have finished or been skipped.
    Both start together when the arch builds finish."""
    # A skipped caller job is listed under its own name, a started one as "<name> / <job>".
    final = [job for job in jobs if job["name"].split(" / ")[0] in FINAL_JOBS]
    return bool(final) and all(job["status"] == "completed" for job in final)


def admitted(jobs):
    """True once one of the run's arches was let in."""
    return any(job["name"].endswith(GATE_SUFFIX) and job["conclusion"] == "success" for job in jobs)


def run_demand(run, jobs):
    """macOS runners a run holds or still needs."""
    # A run with no jobs that is pending waits behind another run of its
    # concurrency group, which holds the runners for both.
    if not jobs and run["status"] in ("pending", "waiting"):
        return 0
    active = sum(1 for job in jobs if is_macos(job) and job["status"] in ACTIVE)
    if macos_done(jobs):
        return active
    if run["event"] == "pull_request" and not admitted(jobs):
        return active
    return max(active, RESERVE)



def nightly_window(now):
    """The window around today's nightly cron time, as (start, end) in UTC."""
    cron = datetime.datetime.combine(now.date(), NIGHTLY_UTC, tzinfo=datetime.timezone.utc)
    return cron - NIGHTLY_LEAD, cron + NIGHTLY_GRACE


def parse_time(value):
    return datetime.datetime.fromisoformat(value.replace("Z", "+00:00"))


class Api:
    def __init__(self, token, url="https://api.github.com"):
        self.token = token
        self.url = url.rstrip("/")

    def get(self, path, **params):
        query = urllib.parse.urlencode(params)
        request = urllib.request.Request(f"{self.url}/{path}?{query}", headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {self.token}",
            "X-GitHub-Api-Version": "2022-11-28",
        })
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)


def list_runs(api, repo, workflow, **params):
    runs, page = [], 1
    while True:
        body = api.get(f"repos/{repo}/actions/workflows/{workflow}/runs",
                       per_page=100, page=page, **params)
        runs += body["workflow_runs"]
        if len(runs) >= body["total_count"] or not body["workflow_runs"]:
            return runs
        page += 1


def latest_run(api, repo, workflow, **params):
    runs = api.get(f"repos/{repo}/actions/workflows/{workflow}/runs",
                   per_page=1, **params)["workflow_runs"]
    return runs[0] if runs else None


def measure(api, repo, workflow, run_id, now):
    """Total macOS runners held or needed, one line per run that holds any, and
    whether run_id was already let in."""
    total, lines, here = 0, [], False
    # A run is listed as queued whenever one of its jobs waits for a runner, and
    # as pending whenever one waits in a concurrency group, so runs in any of these
    # can hold runners. A run can move between the lists between the calls.
    runs = {run["id"]: run for status in ("in_progress", "queued", "pending", "waiting")
            for run in list_runs(api, repo, workflow, status=status)}
    for run in runs.values():
        jobs = api.get(f"repos/{repo}/actions/runs/{run['id']}/jobs",
                       filter="latest", per_page=100)["jobs"]
        demand = run_demand(run, jobs)
        here = here or (run["id"] == run_id and admitted(jobs))
        if demand:
            total += demand
            lines.append(f"  {demand}  run {run['id']} ({run['event']}, {run['head_branch']})")

    # build_all.yml runs the nightly only in the main repository.
    start, end = nightly_window(now)
    if repo == NIGHTLY_REPO and start <= now < end:
        last = latest_run(api, repo, workflow, event="schedule")
        if not last or parse_time(last["created_at"]) < start:
            total += RESERVE
            lines.append(f"  {RESERVE}  the nightly, due at {NIGHTLY_UTC:%H:%M} UTC")
    return total, lines, here


def wait(measure_now, limit, wait_minutes, poll_seconds,
         clock=time.monotonic, sleep=time.sleep, log=print):
    """Polls until this run fits. Returns the reason it was let in."""
    deadline = clock() + wait_minutes * 60
    failures = 0
    while True:
        try:
            total, lines, here = measure_now()
        except (urllib.error.URLError, OSError, ValueError, KeyError, TypeError) as error:
            failures += 1
            log(f"::warning title=macOS admission::Could not read the queue ({error}).")
            if failures >= FAILURES_BEFORE_ADMIT:
                return "the queue could not be read"
        else:
            failures = 0
            if here:
                return "this run already holds its runners"
            log(f"{total} of {limit} macOS runners held or needed:")
            for line in lines:
                log(line)
            if total + RESERVE <= limit:
                return "a runner is free"
        if clock() + poll_seconds >= deadline:
            return f"it waited {wait_minutes} minutes"
        sleep(poll_seconds)


def main():
    repo = os.environ["REPO"]
    workflow = os.environ["WORKFLOW_REF"].split("@")[0].rsplit("/", 1)[-1]
    api = Api(os.environ["GH_TOKEN"], os.environ.get("GITHUB_API_URL", "https://api.github.com"))
    run_id = int(os.environ["GITHUB_RUN_ID"])
    limit = int(os.environ.get("MACOS_RUNNER_LIMIT") or 5)
    reason = wait(
        lambda: measure(api, repo, workflow, run_id, datetime.datetime.now(datetime.timezone.utc)),
        limit,
        wait_minutes=int(os.environ.get("WAIT_MINUTES") or 240),
        poll_seconds=int(os.environ.get("POLL_SECONDS") or 180),
    )
    print(f"Letting this macOS build in: {reason}.")


if __name__ == "__main__":
    sys.exit(main())
