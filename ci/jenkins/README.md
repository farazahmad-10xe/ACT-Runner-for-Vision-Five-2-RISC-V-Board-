# VF2 weekly Jenkins pipeline

## Cross-job validation portal

The `vf2-validation-dashboard` job provides one filterable portal across the
sanity, weekly and ACT-update jobs. It refreshes automatically every 15 minutes
and can also be run manually. The portal shows each job's build result, trigger,
start time, duration, Sail version, runner and ACT revisions, VF2 pass/fail
totals, and links to the original Jenkins dashboard, artifacts and console. Its
test table can be filtered by job, VF2 status, Sail status, test, suite or
failure category.

The portal reads retained Jenkins build metadata and archived `cases.json`
files, then stores normalized records under
`/home/lpt-10xe/jenkins-dashboard-data/records`. Those compact records are not
inside a disposable Jenkins workspace and are not removed by source-job build
retention. Completed records are immutable and cached, so after the initial
backfill normal refreshes inspect only new or still-running builds. The portal
does not copy ELFs, signatures, UART logs or credentials; it links back to the
source build for detailed evidence while that build's artifacts remain
retained.

The stable published report is available from the Jenkins project page under
**VF2 Validation Dashboard**. `alwaysLinkToLastBuild` keeps that project-level
link pointed at the newest successful portal publication.

## Five-test sanity job

The `vf2-privileged-sanity` Pipeline is the short hardware gate to run before
the complete weekly regression. It runs in the dedicated disposable workspace
`/home/lpt-10xe/jenkins-workspaces/vf2-privileged-sanity`; it never cleans,
checks out, or modifies the developer workspace. On every build it first
fetches `RUNNER_BRANCH` from the firmware GitHub repository, resolves one exact
runner SHA, checks it out detached, and initializes the ACT submodule. It then
restores the machine-local ignored `tools/` tree from
`/home/lpt-10xe/vf2_mmode_fw_Final_version_Verified/tools` and validates the
runtime helpers required by the job. This copy occurs after every workspace
cleanup because those tools are intentionally not committed to the firmware
repository. It also restores the ignored local `devices.json` as mode `0600`
and validates that a complete Tuya device entry exists without printing its
credentials. Neither local source is included in Git or the archived artifact
patterns. The tools source path and copied file count are recorded with the
runner resolution. It then
fetches and resolves the latest requested `sifive_u74` branch head, checks that
ACT SHA out detached, and freshly generates exactly these five tests:

- `ExceptionsF-00`
- `ExceptionsS-00`
- `ExceptionsSm-00`
- `ExceptionsU-00`
- `ExceptionsZc-00`

The job then runs Sail, verifies that the hardware pack contains exactly those
five names, optionally runs Spike, builds and flashes the VF2 runner, executes
all five cases across persistent SD progress/reboots, and publishes a separate
`Sanity Result Summary`. A stale ELF cannot enter this job: generation is
mandatory and preparation fails if a requested generator or expected hardware
ELF is missing. The SD-card movement still has the same two explicit operator
gates as the weekly hardware job. Passwordless flashing continues to execute
the trusted helper scripts from the development checkout; sudoers permits only
the exact fixed staging paths under
`/home/lpt-10xe/jenkins-hardware-staging/vf2-privileged-sanity` and `/dev/sda`,
not arbitrary commands or workspace scripts. The job copies and byte-verifies
each build's images into this stable location, so Jenkins workspace suffixes
such as `@2` never affect sudo authorization. If the configured SD device is absent, the flash stage makes
three availability attempts ten seconds apart. A write failure while the card
is still present is treated as a real error and is not retried.

Install or refresh only the exact hardware sudo rules without running the
Jenkins plugin manager:

```sh
sudo bash ci/jenkins/install_vf2_sudoers.sh
```

`RUNNER_REVISION_OVERRIDE` and `ACT_REVISION_OVERRIDE` allow an older pair of
exact commits to be reproduced. With both overrides empty, the job tests the
latest firmware `main` and ACT `sifive_u74` heads available when preflight
starts. The resolved runner and ACT commits are stored in the archived
provenance and resolution files.

The sanity implementation is a constrained wrapper around
`ci/jenkins/weekly_vf2.sh`, so ACT resolution, Sail validation, packaging,
flashing, execution, evidence collection, and final reporting use the same
code as the weekly job. Its outputs are isolated under
`logs/jenkins/sanity/jenkins_sanity_<build>/`,
`logs/runs/jenkins_sanity_<build>/`, and the ACT workdir
`work-vf2-jenkins-sanity-priv`. The wrapper derives `REPO_ROOT` and its runner
resolution path from its own checked-out location, so Jenkins workspace suffixes
such as `@2` cannot redirect a build into another build's checkout.

## Complete weekly job

The `vf2-privileged-weekly` Pipeline is started manually from Jenkins and has
no automatic timer trigger. It runs in the dedicated disposable workspace
`/home/lpt-10xe/jenkins-workspaces/vf2-privileged-weekly`; it never cleans or
modifies the developer checkout. Every build fetches `RUNNER_BRANCH`, resolves
one exact firmware SHA, checks it out detached, and restores the same ignored
machine-local `tools/` and `devices.json` inputs used by the sanity job. It then
fetches `ACT_BRANCH`, resolves one exact ACT SHA, and checks that revision out
detached. `RUNNER_REVISION_OVERRIDE` and `ACT_REVISION_OVERRIDE` reproduce an
older pair when required. The job regenerates every testgen-backed
privileged suite, stages all official generator-backed and ACT-tracked static
privileged suites from clean source trees, and lets the SiFive U74 YAML/UDB
configuration select the applicable tests. Local untracked debug/probe files
are intentionally not included.
Sail uses the matching `sail.json` to produce reference signatures. The job constructs a hardware pack
only from resulting non-`.sig.elf` self-checking ELFs, optionally runs Spike,
flashes the VF2 runner and pack, captures UART, and archives ACT Agent summaries.
The current job is pinned to Sail RISC-V `0.13`; preflight verifies both the
resolved executable and the absolute reference-model path in the ACT config
before any tests are generated.

The hardware jobs resolve the firmware and ACT branches once at checkout and
use those exact detached commits for the rest of the run. The resolved SHAs are
archived in the run provenance. Because both weekly source trees are created
after `deleteDir()`, neither tracked nor untracked files from an earlier weekly
build can enter a new run.

Updating ACT is intentionally a separate operation. The
`vf2-act-update-validation` job fetches the requested branch directly from
`https://github.com/riscv/riscv-arch-test.git`, merges it with the reviewed VF2
ACT revision in a disposable Git worktree, regenerates official privileged
tests, and optionally runs the complete Sail validation. It never changes the
reviewed checkout and has no hardware/flash stage. A successful candidate must
still be reviewed, committed to the VF2 fork, and selected explicitly as the
hardware job's new `ACT_REVISION`.

Every completed build publishes a **Result Summary** HTML dashboard on the
Jenkins build page. It shows Sail, optional Spike, and VF2 status for every test
and a `PASS / executed total` summary for every extension. Each extension links
to its own page containing all tests and their status. Per-test pages link to
the report, UART log, evidence JSON, objdump, signatures, and other archived
files.

The summary also links to an **Execution History** dashboard assembled from
all retained `logs/jenkins/weekly/jenkins_weekly_*` results. It provides:

- an overall Jenkins run timeline with Sail, Spike, and VF2 pass totals;
- one history page per suite/extension, including its per-run pass totals and
  the latest status of every test;
- one history page per test case, showing each execution date/time, target
  statuses, classification, and links back to that build's result and report.

History times are displayed in Pakistan Standard Time and retain UTC as hover
text. A later retry for the same test in one run replaces the earlier
`cases.json` entry so each test has one hardware result per Jenkins build.

The dashboard also maintains an ACT/Sail GitHub ticket catalog derived from
the public tracking sheet's **Failure cause** column. Ticket numbers, current
open/closed state, and links are shown on the overall test table, extension
tables, current per-test reports, per-test history pages, and embedded
historical VF2 reports. The **Tracked ACT/Sail tickets** page lists each ticket
with all related tests.

During `finalize`, Jenkins downloads a fresh CSV snapshot of the public sheet
and creates `tracking_sheet_comparison.csv`, comparing its Spike/VF2 columns
with the current Jenkins results. Failure to reach Google is non-fatal and does
not change the test verdict. Ticket metadata and test mappings used by the
dashboard are maintained in `ci/jenkins/test_issue_links.json` so report
generation itself does not depend on network access.

Jenkins' **Artifacts** link provides browser access and downloads for the
complete raw run data. A
`jenkins_weekly_<build>-complete.zip` artifact provides a one-click download of
the dashboard/state folder and the complete per-case run with all logs and
evidence.

Each run also archives a `provenance/` directory containing:

- exact runner and ACT commit IDs, branches, remotes and dirty-state counts;
- the requested ACT revision and whether the checkout matched it;
- binary-applicable tracked worktree/index patches;
- the VF2 delta against the locally fetched official ACT reference;
- snapshots and SHA-256 hashes of Jenkins control scripts and U74
  configuration;
- hashes of every staged test input and every ELF placed in the hardware pack.

Freshly generated test sources are stored under the build's Jenkins state
directory and are included in the complete run archive. Together with the Git
revisions and patches, these records identify the exact source and binary input
used by each run.

Tests that fail or are blocked during Sail reference generation cannot produce
a final self-checking hardware ELF. They are listed in
`reference_failed_no_hardware_elf.txt`; the remaining tests continue to Spike
and VF2. Weekly ACT is run without full Sail instruction traces to prevent trace
snapshots from exhausting the workstation disk.

Two `input` gates are intentional: the present lab setup requires moving the SD
card from the host reader to the VF2 board. A fully unattended weekly hardware
run requires an SD mux or a non-removable loading transport such as JTAG, UART,
or network boot.

Install and provision on Ubuntu with:

```sh
sudo bash ci/jenkins/install_jenkins_ubuntu.sh
```

Jenkins listens on port `8080` on all host interfaces and advertises the host's
current LAN address. The installer creates the `vf2admin` password locally
under the private Jenkins home and prints the command used to retrieve it.
To expose an existing local-only installation without reinstalling Jenkins, run:

```sh
sudo bash ci/jenkins/enable_lan_access.sh
```

If UFW is active, the helper limits port 8080 access to the directly connected
LAN subnet. Jenkins authentication remains required; anonymous access is off.

After changing the repository's `Jenkinsfile` or plugin list, apply those
changes to the already-provisioned job with:

```sh
sudo bash ci/jenkins/apply_job_updates.sh
```

When plugins are already installed and only the job definitions changed,
they can be installed without restarting Jenkins or using `sudo`:

```sh
python3 ci/jenkins/apply_job_configs.py \
  --repo-root "$PWD" \
  --jenkins-home /home/lpt-10xe/.jenkins-vf2
```

For safety, passwordless privilege is limited to the two trusted flash helpers
from the developer checkout, fixed build-specific staging paths, and `/dev/sda`.
Weekly artifacts are copied and byte-verified under
`/home/lpt-10xe/jenkins-hardware-staging/vf2-privileged-weekly`, so a Jenkins
workspace suffix such as `@2` does not affect sudo authorization. Flashing makes
three SD-device availability attempts ten seconds apart. If `SD_DEV` or a
staging path is changed, update `/etc/sudoers.d/jenkins-vf2-hardware`
deliberately as root as well.
