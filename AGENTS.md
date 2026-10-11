# Working on the explicit-mask fork

- Integrate on `dev`; preserve `main` as the upstream baseline. Worker branches
  stay local. Use separate worktrees and disjoint file ownership.
- Commit as `McFredward <frederik@lissek.info>`. Speak German to the maintainer;
  source, comments and developer documentation are English.
- The intended input is real per-eye scene coverage, independent of scene RGB.
  Never derive mask coverage from a key colour in the explicit-mask path.
- A mask belongs to one source session, video frame, eye and extent. A compositor
  submit counter, approximate timestamp or latest received mask is not proof of
  that association. Missing/invalid associations must retain opaque rendering.
- Preserve native reprojection, distortion and foveation mapping. Do not activate
  a replacement shader until its descriptor and frame/UV bindings are established.
- Do not redistribute Valve binaries, shader source or disassemblies. Diagnostic
  captures go into ignored `build/` or a private output directory.
- Run focused tests for bounded changes; report software evidence separately from
  a headset test. Never describe this foundation as working Steam Link passthrough.
- Keep allocations, queued frames, reassembly and logging bounded. No file I/O,
  socket waits or shader compilation on a compositor rendering thread.
- Clean completed temporary test artifacts after every run (maintainer request,
  2026-10-11). Retain small validation records and only the build outputs still
  needed for review/deployment. Protect active workers, hardware logs, source
  checkouts and private reference captures; never purge `/tmp` wholesale.
