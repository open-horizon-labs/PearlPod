# Browsing and Now Playing critique

Method: dual-agent (A: /root/critique_design · B: /root/critique_evidence). Independent source assessments; embedded LVGL, not a browser application. A reviewed design without detector evidence; B ran the detector independently. B's automatic completion arrived first, but A never received B's findings.

| Heuristic | Score /4 | Key issue |
|---|---:|---|
| System status | 2 | No selected route or active track marker |
| Real-world match | 3 | Familiar album and playback model |
| Control and freedom | 2 | Browse position lost on return |
| Consistency | 2 | Small route targets, equal route emphasis |
| Error prevention | 2 | Small controls invite mistaps |
| Recognition | 2 | No covers in album list |
| Efficiency | 2 | No horizontal browse navigation |
| Minimalist design | 3 | Quiet palette; repeated tap instructions |
| Recovery | 2 | Error overlaps transport |
| Help | 2 | Gestures undiscoverable |
| Total | 22/40 | Acceptable, substantial room to improve |

The welcome illustration and palette belong to Listener, but browsing becomes a generic file list. The emotional high point is startup, followed by a visual valley until playback. Carry album imagery through selection and give pause a clear physical hierarchy.

Priorities: reserve error space; enlarge navigation and play/pause; include cover thumbnails; preserve scroll position and distinguish swipe from selection; reveal long titles and mark active music. Duration and seeking should not be implied without decoder support.

Detector: real invocation on main/ui.c, exit 0, [] / zero findings. The markup-oriented detector has little coverage of C/LVGL. No ignore file, browser route, overlay, live server or browser temporary files. Local LVGL raster captures are the visual evidence for refinement. Questions skipped: user explicitly selected the improvements and requested implementation in this combined workflow.

Delight thesis: Listener should recognize her music by its cover and feel that each touch confidently follows her intent, with visual character supplied by the existing illustrated fallback.

Two passes per surface: browsing first adds cover recognition and meaningful swipe/back paths, then active-track accents and restored position; Now Playing first elevates artwork and pause, then expressive playing/paused state and readable long titles. These ship as useful details without delaying boot or adding sound effects.
