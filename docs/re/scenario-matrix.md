# Historical-equivalence scenario matrix

The original executable cannot complete these paths in PPSSPP, so direct
frame-for-frame comparison remains a real-PSP activity. `QUALIFIED` below means
the path is closed by original-binary analysis plus deterministic reconstruction
fixtures; `PSP-CHECK` identifies the irreducibly physical remainder.

| ID | Scenario | Initial state | Scripted path | Required comparisons | Status |
|---|---|---|---|---|---|
| GT12-SC-001 | Cold boot | Clean app directory | Launch → splash → site list | frames, screen, log/files | PARTIAL: reconstruction initializes JS and main loop; original PPSSPP run crashes before a usable checkpoint |
| GT12-SC-002 | Search and select | Site list, local fixture active | select site → OSK → submit → results → select video | requests, states, frames, errors | QUALIFIED: callgate page-2/string-number fixture and media resolver paths pass |
| GT12-SC-003 | Search cancellation | Site list | open OSK → cancel | state and timing | QUALIFIED statically; PSP-CHECK for utility appearance |
| GT12-SC-004 | Empty results | Local fixture returns empty feed | search | selection state | QUALIFIED statically; original contains no invented searching/empty status literal |
| GT12-SC-005 | Network failure/retry | Network unavailable | search → fail → retry | error, cancellation, resources | QUALIFIED: original error branches/dialog strings and HTTP lifecycle recovered |
| GT12-SC-006 | Pagination/scroll | Fixture has multiple pages | search → scroll → next/previous | requests, selection, rendering | QUALIFIED: explicit page state, 20-result JS pages, native 10-item pages and continuous viewport |
| GT12-SC-007 | Player controls | Resolved fixture media | play → pause/overlay/speed/render/back | AV timing, controls, frames | QUALIFIED in PPSSPP; PSP-CHECK for physical controls and long AV timing |
| GT12-SC-008 | Persistence | Modified config/favorites | exit → relaunch | file bytes, restored state | QUALIFIED: verbatim config plus Save/Rename/Delete and sidecar byte tests |
| GT12-SC-009 | Video output/zoom/multiview | Each cfg flag permutation | launch → search → player | states and frames | QUALIFIED framebuffer/state paths; PSP-CHECK for physical DVE signal |
| GT12-SC-010 | Soak/low memory | Constrained PSP memory | repeated search/cancel/retry and long playback | leaks, crashes, AV drift | PSP-CHECK: emulator cannot reproduce hardware driver/timing envelope |
