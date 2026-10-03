---
name: Listener Player
description: Listener's personal anime-inspired offline music companion
colors:
  night-navy: "#101827"
  warm-cream: "#fff6dc"
  sunshine-yellow: "#ffd75e"
  mint-teal: "#56ddc5"
  list-surface: "#1d2c40"
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
    height: "54px"
  list-row:
    backgroundColor: "{colors.list-surface}"
    textColor: "{colors.warm-cream}"
    rounded: "{rounded.row}"
    height: "64px"
    width: "424px"
  footer-route:
    backgroundColor: "{colors.sunshine-yellow}"
    textColor: "{colors.night-navy}"
    rounded: "{rounded.action}"
    height: "36px"
    width: "78px"
---

# Design System: Listener Player

## Overview

The embedded Operate surface is a personal anime music companion for Listener. Sample soundtrack and Sample collection are the user's confirmed imagery references. The current interface places expressive personal artwork within a quiet, readable listening surface.

This is an extraction from `main/ui.c`, rather than a screenshot audit or broad design review. The user confirmed rendering, button volume, audio playback, and a long-hold power cycle on the physical device. The previous brief is refreshed here under the current documentation task; aspirational behaviors are distinguished from implemented ones.

**Key Characteristics:**

- Album art is the center of the listening screen.
- Warm, bright actions stand apart from dark album rows.
- Persistent Albums and Playing routes connect browsing and listening.

## Colors

Sunshine yellow marks touch actions and status messages; mint teal supports row subtitles. Warm cream carries headings and music names on night navy. List surface provides tonal separation for album and track rows. The frontmatter owns exact color values.

## Typography

LVGL's built-in Montserrat fonts provide a consistent hierarchy: heading at 28 px, current track and empty-state title at 24 px, row titles and action labels at 20 px, and supporting album names, track counts, volume, and status at 16 px. Long headings and music names use ellipsis. There is no custom typeface or additional weight system in the implementation.

## Layout

The fixed 460×460 screen has 18 px horizontal insets and a 424 px content width. Heading begins at y=12; the vertically scrolling body begins at y=54 and is 302 px high. Album and track rows are 64 px high on a 72 px rhythm. Album tracks are paged in groups of 32, with Previous and Next page actions when required.

Now Playing centers 240×240 artwork, with the track title and album name below. Transport occupies y=360 with 54 px actions. Volume and the persistent Albums/Playing routes occupy y=420; these footer routes are only 36 px high, so a blanket 54 px minimum touch-target claim would be inaccurate. There are no responsive breakpoints.

## Elevation & Depth

Actions and list rows explicitly disable shadows. Tonal surfaces separate selectable rows from the background; body and transport containers are transparent. Other LVGL theme defaults have not been independently audited.

## Shapes

Action buttons have softly curved 14 px corners; list rows use 12 px corners. Artwork remains square. Shapes help identify actions without competing with album imagery.

## Components

**Actions:** Sunshine-yellow buttons with centered navy Montserrat labels. Play album spans the body; transport provides previous, play/pause, and next. The implementation registers click events and does not define custom hover, focus, or pressed styling.

**Album and track rows:** Full-width dark surfaces with an ellipsized title, teal subtitle, and a click target across the row. Albums show track counts; tracks say “Tap to play.”

**Navigation:** Albums and Playing persist at the footer. Albums is the return route; a separate Back control is not implemented. Album pages add Play album and, where necessary, track-page controls.

**Artwork:** The personal welcome illustration appears while scanning the card and as the Now Playing fallback. Album artwork loads asynchronously and replaces the fallback when ready. The empty album list is currently text-only; no mascot reactions or decorative animations are implemented.

**Playback status:** Now Playing shows elapsed time and toggles the play/pause symbol. Footer volume updates from audio state. Error strings appear in yellow; explicit retry buttons are not implemented. Status shares vertical space with transport, so error visibility during playback needs physical review.

## Do's and Don'ts

- **Do** keep album artwork and readable controls central to Listener's anime-inspired player.
- **Do** preserve immediate navigation while artwork loads in the background.
- **Do** distinguish observed implementation from planned accessibility and recovery improvements.
- **Don't** make decorative imagery a timed startup gate.
- **Don't** claim every touch target is at least 54 px or that a separate Back button exists.
- **Don't** introduce Wi-Fi requirements into everyday offline listening.
