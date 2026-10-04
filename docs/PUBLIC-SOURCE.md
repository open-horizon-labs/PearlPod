# Public source scrub

The public repository retains sanitized development history and tags. Original personal artwork, runtime configuration, device identifiers, household profile names, personal paths and historical screenshots are excluded. Public documentation uses example network addresses and anonymized identifiers; measured timings remain historical observations. Runtime credentials remain in ignored local files or host-mounted secrets.

The original repository and releases are preserved in a private archive. A fresh public repository avoids exposing orphaned objects from the original history. Existing clones must not merge or push their old history into this repository; use a fresh clone or the already-updated working copy.

Firmware releases are binary assets, never committed into Git history. Public packages contain neutral firmware and original theme packs. Recovery refuses ambiguous USB interfaces, accepts an explicit port, retries at most twenty times by default and stops on success. It does not identify the DAC variant from USB.

Verification passed a complete all-ref secret scan, targeted personal-data scan, host firmware regression suite, actual LVGL renderer, syncer regression tests (26 cases, three environment-dependent skips) and public download checksum validation. These checks reduce disclosure risk but cannot prove the absence of every possible personal detail.
