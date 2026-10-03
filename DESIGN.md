---
name: PearlPod
description: Listener's personal anime-inspired offline music companion
colors:
  night-navy: "#101827"
  warm-cream: "#fff6dc"
  sunshine-yellow: "#ffd75e"
  mint-teal: "#56ddc5"
  list-surface: "#1d2c40"
  row-pressed: "#294555"
  selected-track: "#284b50"
typography:
  heading:
    fontFamily: Montserrat
    fontSize: "28px"
  title:
    fontFamily: Montserrat
    fontSize: "24px"
  body:
    fontFamily: Montserrat
    fontSize: "20px"
  supporting:
    fontFamily: Montserrat
    fontSize: "16px"
rounded:
  action: "14px"
  row: "12px"
spacing:
  screen-inset: "18px"
  row-inset: "14px"
  row-gap: "8px"
components:
  action:
    backgroundColor: "{colors.sunshine-yellow}"
    textColor: "{colors.night-navy}"
    rounded: "{rounded.action}"
    height: "64px"
  list-row:
    backgroundColor: "{colors.list-surface}"
    textColor: "{colors.warm-cream}"
    rounded: "{rounded.row}"
    height: "92px"
    width: "424px"
  footer-route:
    backgroundColor: "{colors.sunshine-yellow}"
    textColor: "{colors.night-navy}"
    rounded: "{rounded.action}"
    height: "64px"
    width: "204px"
  track-row:
    backgroundColor: "{colors.list-surface}"
    textColor: "{colors.warm-cream}"
    rounded: "{rounded.row}"
    height: "72px"
    width: "424px"
  play-pause:
    backgroundColor: "{colors.sunshine-yellow}"
    textColor: "{colors.night-navy}"
    rounded: "{rounded.action}"
    height: "76px"
    width: "168px"
---

# Design System: PearlPod

## Overview

The embedded Operate surface is a personal anime music companion for Listener. Sample soundtrack and Sample collection are the user's confirmed imagery references. The current interface places expressive personal artwork within a quiet, readable listening surface.

This documents the current LVGL implementation in `main/ui.c`. Earlier physical checks confirmed rendering, audio, volume, and sleep/wake; physical touch accuracy and swipe behavior for this refinement remain pending. Source geometry and host rendering do not establish hardware gesture accuracy.

**Key Characteristics:**

- Recognizable album thumbnails lead browsing; large cover art leads listening.
- Generous touch controls prioritize play/pause and keep navigation predictable.
- Personal anime imagery provides fallback art while real covers load asynchronously.

## Colors

Sunshine yellow marks touch actions and status messages; mint teal supports row subtitles. Warm cream carries headings and music names on night navy. List surface provides tonal separation for album and track rows. The frontmatter owns exact color values.

## Typography

LVGL's built-in Montserrat hierarchy uses heading at 28 px, current track at 24 px, row titles and action labels at 20 px, and supporting labels at 16 px. Browse titles ellipsize. Now Playing scrolls long track and album titles horizontally at different controlled speeds so their full identity remains available. The central play/pause symbol uses the heading size.

## Layout

The fixed 460×460 screen uses 18 px horizontal insets and 424 px content width. Browse content begins at y=60 and is 304 px high. Album rows are 92 px high on a 100 px rhythm, with 64×64 thumbnails. Track rows are 72 px high on an 80 px rhythm beneath a 64 px Play album action. Album pages contain up to eight albums; track pages contain up to 32 tracks. Persistent header arrows handle page boundaries and disable unavailable directions. Footer routes are 204×64 with a 16 px gap.

Now Playing centers 240×240 artwork above scrolling track and album labels. Transport at y=378 contains a central 168×76 play/pause target between 112×68 previous/next targets. Elapsed time sits beside the album label; volume sits in the header. Errors occupy a separate narrow band above transport. Back and page controls are 64×52. There are no responsive breakpoints.

## Elevation & Depth

Actions and list rows explicitly disable shadows. Tonal surfaces separate selectable rows from the background; body and transport containers are transparent. Other LVGL theme defaults have not been independently audited.

## Shapes

Action buttons have softly curved 14 px corners; list rows use 12 px corners. Artwork remains square. Shapes help identify actions without competing with album imagery.

## Components

**Actions:** Yellow controls turn teal when pressed. Play/pause has the largest target, and its resting color becomes teal during active playback. The heading explicitly distinguishes Paused, Now playing, and Your music states. Rows use a brighter dark surface when pressed; the selected track has a distinct tonal surface and a text label.

**Browsing:** Albums pair thumbnails with music names, counts, and a selected-album label when relevant. Tracks show sequence numbers and identify the selected track. Vertical dragging explores each page. Album browsing supports left/right swipes for pages; on a track page, left advances pages and right returns to albums. Explicit page arrows and Back remain available. Now Playing also supports a right swipe to albums. Gesture handling consumes the touch release to avoid selecting a track after a swipe. Album page scroll positions and each album's track position are saved during navigation.

**Artwork:** Only visible album rows request thumbnails. Decoding runs asynchronously; generation tokens discard stale results after navigation. Personal anime artwork remains visible when covers are absent or fail to decode. Now Playing requests the album cover or embedded artwork from the selected track. The same illustration greets Listener during the quick startup scan.

**Navigation and feedback:** Browse footers offer Albums and Playing, with Albums selected in teal on the collection view. Now Playing replaces those routes with larger transport and a header Back button. Short browse hints invite exploration or explain the return gesture; errors replace hints. Empty-state copy explains how to add albums. No seek bar, decorative animation, custom focus treatment, or in-device settings surface is implemented.

## Do's and Don'ts

- **Do** preserve the navy, cream, yellow, and teal identity and let album artwork carry personality.
- **Do** keep tap alternatives for gestures and prioritize play/pause over secondary transport.
- **Do** preserve browsing position while covers load asynchronously.
- **Don't** introduce a timed decorative startup gate or Wi-Fi requirement for listening.
- **Don't** claim physical swipe accuracy from source inspection or host renders.
- **Don't** add decorative motion that competes with music names or controls.
