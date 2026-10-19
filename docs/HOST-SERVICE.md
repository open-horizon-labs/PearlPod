# Host media service

The maintained host service lives in the private [media-prep-service repository](https://github.com/open-horizon-labs/media-prep-service). Its source, preparation, device and delivery adapters serve PearlPod and Book Reader Thingee. The previous Python syncer and its host tests were moved there; firmware, FTP receiver code and native decoder/receiver harnesses stay here.

Run firmware checks here with `tools/check.sh` and `tools/check_sync.sh` using ESP-IDF 5.5.5. Run host-service checks from its checkout with `bash tools/check.sh`; set `PEARLPOD_ROOT` if this firmware checkout is not its sibling. Private real-media fixtures remain outside version control. The host service compiles this repository's actual receiver for integration checks rather than maintaining a second firmware copy.

The migration keeps ordinary readable card paths and the existing open household sync protocol. Selection-driven preparation happens on the NAS; device sync transfers completed files. Deployment cutover and device acceptance are tracked in the service's `docs/EXECUTION.md`. Historical sync design/verification documents in this repository describe their recorded revision; their old `syncer/` commands are superseded by the service README. Git history retains the previous standalone source for recovery; runtime rollback containers/cache are separate from source ownership.
