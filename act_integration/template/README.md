# VF2 Runner ACT Target Skeleton

This template is a minimal starting point for wiring an external ACT target to
the VF2 runner repository.

Contents:

- `Makefrag`: adds the runner adapter include path and exposes runner build vars
- `target.env`: example shell environment for S-mode bring-up

Expected usage:

1. Set `VF2_RUNNER_REPO` to this repository.
2. Include `Makefrag` from your ACT target make path.
3. Ensure the ACT target include path can see `rvmodel.h`.
4. Build ACT payloads with the privilege profile you want the runner to use.

This skeleton does not replace a full ACT target port. It gives you a stable
starting point so the ACT tree can consume the runner adapter and keep the
runner build knobs close to the target configuration.
