// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

import type { ReactNode } from "react";

// The icons a button, a link or a front page's section can be drawn with, its words
// naming it.
export const buttonIcons: Record<string, ReactNode> = {
  edit: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinejoin="round" aria-hidden="true">
      <path d="M11.2 2.3a1.4 1.4 0 0 1 2 2L5.6 11.9 2.8 12.7l.8-2.8z" />
      <path d="M10 3.5l2 2" />
    </svg>
  ),
  add: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" aria-hidden="true">
      <path d="M8 3v10M3 8h10" />
    </svg>
  ),
  follow: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinejoin="round" aria-hidden="true">
      <path d="M4 11.5V7.2a4 4 0 0 1 8 0v4.3l1.2 1.2H2.8z" />
      <path d="M6.6 14a1.5 1.5 0 0 0 2.8 0" />
    </svg>
  ),
  following: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="currentColor" stroke="currentColor" strokeWidth="0.75" strokeLinejoin="round" aria-hidden="true">
      <path d="M4 11.5V7.2a4 4 0 0 1 8 0v4.3l1.2 1.2H2.8z" />
      <path d="M6.6 14a1.5 1.5 0 0 0 2.8 0" fill="none" />
    </svg>
  ),
  // A cog: a ring with teeth, and its hole.
  settings: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinejoin="round" aria-hidden="true">
      <path d="M6.9 1.5h2.2l.4 1.8 1.2.7 1.8-.6 1.1 1.9-1.4 1.3v1.4l1.4 1.3-1.1 1.9-1.8-.6-1.2.7-.4 1.8H6.9l-.4-1.8-1.2-.7-1.8.6-1.1-1.9 1.4-1.3V7.3L2.4 6l1.1-1.9 1.8.6 1.2-.7z" />
      <circle cx="8" cy="8" r="2" />
    </svg>
  ),
  workflow: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinejoin="round" aria-hidden="true">
      <rect x="1.5" y="2" width="4.5" height="4" rx="1" />
      <rect x="10" y="10" width="4.5" height="4" rx="1" />
      <path d="M6 4h2.5a1.5 1.5 0 0 1 1.5 1.5V10" strokeLinecap="round" />
    </svg>
  ),
  // A bin with its lid.
  delete: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2.5 4.5h11M6.5 4.5V3a1 1 0 0 1 1-1h1a1 1 0 0 1 1 1v1.5" />
      <path d="M3.8 4.5l.7 8.6a1 1 0 0 0 1 .9h5a1 1 0 0 0 1-.9l.7-8.6M6.7 7v4.5M9.3 7v4.5" />
    </svg>
  ),
  // A robot, for what programs do, like a project's agents.
  robot: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="3" y="5" width="10" height="8" rx="2" />
      <path d="M8 5V3M6 9h.01M10 9h.01M6.5 11h3M1.5 8.5v2M14.5 8.5v2" />
    </svg>
  ),
  // A box with its lid, what's put away.
  archive: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="1.5" y="2.5" width="13" height="3" rx="1" />
      <path d="M2.8 5.5v7a1 1 0 0 0 1 1h8.4a1 1 0 0 0 1-1v-7M6.5 8.5h3" />
    </svg>
  ),

  // A building with its windows and door, what's built and run.
  building: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="3" y="1.5" width="10" height="13" rx="1" />
      <path d="M6 4.5h1M9 4.5h1M6 7.5h1M9 7.5h1M7 14.5v-3h2v3" />
    </svg>
  ),
  // Three disks stacked, what's stored.
  database: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="2.5" y="2" width="11" height="3.5" rx="1.75" />
      <rect x="2.5" y="6.25" width="11" height="3.5" rx="1.75" />
      <rect x="2.5" y="10.5" width="11" height="3.5" rx="1.75" />
    </svg>
  ),
  // Angle brackets around a slash, what's written.
  code: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M5.5 4.5L2 8l3.5 3.5M10.5 4.5L14 8l-3.5 3.5M9.25 3l-2.5 10" />
    </svg>
  ),
  // Two rings joined, what's connected.
  connect: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <circle cx="4" cy="8" r="2.5" />
      <circle cx="12" cy="8" r="2.5" />
      <path d="M6.5 8h3" />
    </svg>
  ),
  // A shield with a check, what's kept safe.
  shield: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M8 1.5l5.5 2v4.5L8 14.5 2.5 8V3.5z" />
      <path d="M5.5 8l2 2 3-3.5" />
    </svg>
  ),
  // A cloud, what runs in it.
  cloud: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M4.5 12.5a3 3 0 0 1 0-6a4 4 0 0 1 7.5-1a3.5 3.5 0 0 1 0 7z" />
    </svg>
  ),
  // Bars over a line, what's measured.
  chart: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2.5 13.5h11M4.5 11V8M8 11V4M11.5 11V6.5" />
    </svg>
  ),
  // A four-pointed star, what's clever or new.
  spark: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M8 1.5l1.5 5 5 1.5-5 1.5L8 14.5 6.5 9.5l-5-1.5 5-1.5z" />
    </svg>
  ),
  // Two people, the ones it's for.
  people: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <circle cx="6" cy="5.5" r="2.25" />
      <path d="M1.5 13.5a4.5 4.5 0 0 1 9 0" />
      <circle cx="11.5" cy="6" r="1.75" />
      <path d="M11 9.5a3.5 3.5 0 0 1 3.5 4" />
    </svg>
  ),
  // A padlock, what's closed to others.
  lock: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="3" y="7" width="10" height="7" rx="1.5" />
      <path d="M5.5 7V5a2.5 2.5 0 0 1 5 0v2" />
    </svg>
  ),
  // A globe, what reaches everywhere.
  globe: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <circle cx="8" cy="8" r="6.25" />
      <path d="M1.75 8h12.5M8 1.75a9 9 0 0 1 0 12.5a9 9 0 0 1 0-12.5" />
    </svg>
  ),
  // Layers stacked, what's built on what.
  layers: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M8 2l6 3-6 3-6-3z" />
      <path d="M2 8l6 3 6-3M2 11l6 3 6-3" />
    </svg>
  ),
  // A month's page, its rings above; a bell that rings; a tray things come into; and
  // the ways back and on.
  calendar: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="2" y="3" width="12" height="11" rx="1.5" />
      <path d="M2 6.5h12M5.5 1.5v3M10.5 1.5v3" />
    </svg>
  ),
  bell: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M4 11V7a4 4 0 0 1 8 0v4l1.25 1.5H2.75L4 11zM6.5 14a1.5 1.5 0 0 0 3 0" />
    </svg>
  ),
  inbox: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2 9.5l2-6.5h8l2 6.5v3.5a1 1 0 0 1-1 1H3a1 1 0 0 1-1-1zM2 9.5h3.5l1 1.5h3l1-1.5H14" />
    </svg>
  ),
  previous: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M10 3L5 8l5 5" />
    </svg>
  ),
  next: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M6 3l5 5-5 5" />
    </svg>
  ),
  // A table of contents: headings, and what's under them set in.
  contents: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2.5 3.5h11M5 6.5h8.5M5 9.5h8.5M2.5 12.5h11" />
    </svg>
  ),
  crop: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M4.5 1.5v10h10M1.5 4.5h10v10" />
    </svg>
  ),
  copy: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="5.5" y="5.5" width="9" height="9" rx="1.5" />
      <path d="M10.5 5.5V3a1 1 0 0 0-1-1H3a1 1 0 0 0-1 1v6.5a1 1 0 0 0 1 1h2.5" />
    </svg>
  ),
  raise: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M8 14V5.5M4.5 9L8 5.5 11.5 9M3 2.5h10" />
    </svg>
  ),
  lower: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M8 2v8.5M4.5 7L8 10.5 11.5 7M3 13.5h10" />
    </svg>
  ),
  rotate_left: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M3.5 5.9L2.98 7.15 2.8 8.5 2.98 9.85 3.5 11.1 4.32 12.18 5.4 13 6.65 13.52 8 13.7 9.35 13.52 10.6 13 11.68 12.18 12.5 11.1 13.02 9.85 13.2 8.5 13.02 7.15 12.5 5.9 11.68 4.82 10.6 4 9.35 3.48 8 3.3 6.65 3.48 5.4 4M9.1 4.4L5.4 4 6.9 .59" />
    </svg>
  ),
  rotate_right: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M12.5 5.9L13.02 7.15 13.2 8.5 13.02 9.85 12.5 11.1 11.68 12.18 10.6 13 9.35 13.52 8 13.7 6.65 13.52 5.4 13 4.32 12.18 3.5 11.1 2.98 9.85 2.8 8.5 2.98 7.15 3.5 5.9 4.32 4.82 5.4 4 6.65 3.48 8 3.3 9.35 3.48 10.6 4M6.9 4.4L10.6 4 9.1 .59" />
    </svg>
  ),
  flip_horizontal: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M8 1.5v13" />
      <path d="M6 4L2.5 12H6z" />
      <path d="M10 4l3.5 8H10z" />
    </svg>
  ),
  flip_vertical: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M1.5 8h13" />
      <path d="M4 6l8-3.5V6z" />
      <path d="M4 10l8 3.5V10z" />
    </svg>
  ),
  check: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M3 8.5l3.2 3.2L13 4.8" />
    </svg>
  ),
  close: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M4 4l8 8M12 4l-8 8" />
    </svg>
  ),
  paste: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="3.5" y="3" width="9" height="11.5" rx="1.5" />
      <rect x="6" y="1.5" width="4" height="3" rx="0.75" />
      <path d="M6 8h4M6 10.5h4" />
    </svg>
  ),
  save: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M8 2v8M4.5 6.5L8 10l3.5-3.5" />
      <path d="M2.5 10.5V14h11v-3.5" />
    </svg>
  ),
  undo: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M3 6L9.5 6 10.41 6.12 11.25 6.47 11.97 7.03 12.53 7.75 12.88 8.59 13 9.5 12.88 10.41 12.53 11.25 11.97 11.97 11.25 12.53 10.41 12.88 9.5 13 6 13M6 3L3 6 6 9" />
    </svg>
  ),
  redo: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M13 6L6.5 6 5.59 6.12 4.75 6.47 4.03 7.03 3.47 7.75 3.12 8.59 3 9.5 3.12 10.41 3.47 11.25 4.03 11.97 4.75 12.53 5.59 12.88 6.5 13 10 13M10 3L13 6 10 9" />
    </svg>
  ),
  text: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M3 3.5h10M8 3.5V13M6 13h4" />
    </svg>
  ),
  screenshot: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="1.5" y="4.5" width="13" height="9.5" rx="1.5" />
      <circle cx="8" cy="9.25" r="2.6" />
      <path d="M5.5 4.5L6.75 2.5h2.5l1.25 2" />
    </svg>
  ),
  fullscreen: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2 6V2h4M10 2h4v4M14 10v4h-4M6 14H2v-4" />
    </svg>
  ),
  sun: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <circle cx="8" cy="8" r="3" />
      <path d="M8 1v1.5M8 13.5V15M1 8h1.5M13.5 8H15M3.05 3.05l1.06 1.06M11.89 11.89l1.06 1.06M3.05 12.95l1.06-1.06M11.89 4.11l1.06-1.06" />
    </svg>
  ),
  moon: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M13.5 9.5A5.5 5.5 0 1 1 6.5 2.5a4.5 4.5 0 0 0 7 7z" />
    </svg>
  ),
  bold: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M4.5 2.5h4.25a2.75 2.75 0 0 1 0 5.5H4.5zM4.5 8h5a3 3 0 0 1 0 6h-5z" />
    </svg>
  ),
  smaller: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M1.5 13l3.5-9 3.5 9M2.9 10h4.2M10.5 8.5h4" />
    </svg>
  ),
  larger: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M1.5 13l3.5-9 3.5 9M2.9 10h4.2M10.5 8.5h4M12.5 6.5v4" />
    </svg>
  ),
  fill: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="1.5" y="3" width="13" height="10" rx="1" />
      <path d="M3 4.5h10v7H3z" fill="currentColor" />
    </svg>
  ),
  fit: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="1.5" y="3" width="13" height="10" rx="1" />
      <path d="M5.5 4.5h5v7h-5z" fill="currentColor" />
    </svg>
  ),
  stretch: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="1.5" y="3" width="13" height="10" rx="1" />
      <path d="M4 8h8M6 6L4 8l2 2M10 6l2 2-2 2" />
    </svg>
  ),
  center: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="1.5" y="3" width="13" height="10" rx="1" />
      <path d="M6 6.5h4v3H6z" fill="currentColor" />
    </svg>
  ),
  color: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M8 1L11.96 7.65A4.6 4.6 0 1 1 4.04 7.65z" />
    </svg>
  ),
  edges: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="5" y="5" width="6" height="6" rx="1" />
      <path d="M1.5 8H5M11 8h3.5M8 1.5V5M8 11v3.5" />
    </svg>
  ),
  clear: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="5" y="5" width="6" height="6" rx="1" />
      <path d="M1.5 4V1.5H4M12 1.5h2.5V4M14.5 12v2.5H12M4 14.5H1.5V12" />
    </svg>
  ),
  magic: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2.5 13.5l8-8M12 1.5v2M12 7.5v2M8 5.5h2M14 5.5h2" />
    </svg>
  ),
  italic: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M7 3h5M4 13h5M10 3L6 13" />
    </svg>
  ),
  eye: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M1.5 8A9 9 0 0 1 14.5 8 9 9 0 0 1 1.5 8zM10 8A2 2 0 1 1 6 8 2 2 0 1 1 10 8" />
    </svg>
  ),
  chevron_right: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M6 4l4 4-4 4" />
    </svg>
  ),
  chevron_down: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M4 6l4 4 4-4" />
    </svg>
  ),
  eye_off: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M1.5 8A9 9 0 0 1 14.5 8 9 9 0 0 1 1.5 8zM10 8A2 2 0 1 1 6 8 2 2 0 1 1 10 8M2.5 2.5l11 11" />
    </svg>
  ),
  group: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M1.5 1.5h13v13h-13zM4 4h5v5H4zM7 7h5v5H7z" />
    </svg>
  ),
  ungroup: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2 2.5h6v6H2zM8 7.5h6v6H8z" />
    </svg>
  ),
  cut: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M5.5 4.5A2 2 0 1 1 1.5 4.5 2 2 0 1 1 5.5 4.5zM5.5 11.5A2 2 0 1 1 1.5 11.5 2 2 0 1 1 5.5 11.5zM5.2 5.6l9.3 6.4M5.2 10.4l9.3-6.4" />
    </svg>
  ),
  star: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M8.0 1.9 L9.72 6.03 L14.18 6.39 L10.78 9.3 L11.82 13.66 L8.0 11.33 L4.18 13.66 L5.22 9.3 L1.82 6.39 L6.28 6.03z" />
    </svg>
  ),
  polygon: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M8.0 1.9 L14.18 6.39 L11.82 13.66 L4.18 13.66 L1.82 6.39z" />
    </svg>
  ),
  zoom_in: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M11.5 7A4.5 4.5 0 1 1 2.5 7 4.5 4.5 0 1 1 11.5 7zM10.2 10.2l3.8 3.8M5 7h4M7 5v4" />
    </svg>
  ),
  zoom_fit: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M11.5 7A4.5 4.5 0 1 1 2.5 7 4.5 4.5 0 1 1 11.5 7zM10.2 10.2l3.8 3.8M5 5.5h4v3H5z" />
    </svg>
  ),
  zoom_out: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M11.5 7A4.5 4.5 0 1 1 2.5 7 4.5 4.5 0 1 1 11.5 7zM10.2 10.2l3.8 3.8M5 7h4" />
    </svg>
  ),
  // Any color: a ring of every hue, in its colors.
  spectrum: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" strokeWidth="3" aria-hidden="true">
      <path d="M8.0 3.0A5 5 0 0 1 9.36 3.19" stroke="hsl(0 100% 50%)" />
      <path d="M9.29 3.17A5 5 0 0 1 10.56 3.7" stroke="hsl(15 100% 50%)" />
      <path d="M10.5 3.67A5 5 0 0 1 11.58 4.51" stroke="hsl(30 100% 50%)" />
      <path d="M11.54 4.46A5 5 0 0 1 12.36 5.56" stroke="hsl(45 100% 50%)" />
      <path d="M12.33 5.5A5 5 0 0 1 12.85 6.77" stroke="hsl(60 100% 50%)" />
      <path d="M12.83 6.71A5 5 0 0 1 13.0 8.07" stroke="hsl(75 100% 50%)" />
      <path d="M13.0 8.0A5 5 0 0 1 12.81 9.36" stroke="hsl(90 100% 50%)" />
      <path d="M12.83 9.29A5 5 0 0 1 12.3 10.56" stroke="hsl(105 100% 50%)" />
      <path d="M12.33 10.5A5 5 0 0 1 11.49 11.58" stroke="hsl(120 100% 50%)" />
      <path d="M11.54 11.54A5 5 0 0 1 10.44 12.36" stroke="hsl(135 100% 50%)" />
      <path d="M10.5 12.33A5 5 0 0 1 9.23 12.85" stroke="hsl(150 100% 50%)" />
      <path d="M9.29 12.83A5 5 0 0 1 7.93 13.0" stroke="hsl(165 100% 50%)" />
      <path d="M8.0 13.0A5 5 0 0 1 6.64 12.81" stroke="hsl(180 100% 50%)" />
      <path d="M6.71 12.83A5 5 0 0 1 5.44 12.3" stroke="hsl(195 100% 50%)" />
      <path d="M5.5 12.33A5 5 0 0 1 4.42 11.49" stroke="hsl(210 100% 50%)" />
      <path d="M4.46 11.54A5 5 0 0 1 3.64 10.44" stroke="hsl(225 100% 50%)" />
      <path d="M3.67 10.5A5 5 0 0 1 3.15 9.23" stroke="hsl(240 100% 50%)" />
      <path d="M3.17 9.29A5 5 0 0 1 3.0 7.93" stroke="hsl(255 100% 50%)" />
      <path d="M3.0 8.0A5 5 0 0 1 3.19 6.64" stroke="hsl(270 100% 50%)" />
      <path d="M3.17 6.71A5 5 0 0 1 3.7 5.44" stroke="hsl(285 100% 50%)" />
      <path d="M3.67 5.5A5 5 0 0 1 4.51 4.42" stroke="hsl(300 100% 50%)" />
      <path d="M4.46 4.46A5 5 0 0 1 5.56 3.64" stroke="hsl(315 100% 50%)" />
      <path d="M5.5 3.67A5 5 0 0 1 6.77 3.15" stroke="hsl(330 100% 50%)" />
      <path d="M6.71 3.17A5 5 0 0 1 8.07 3.0" stroke="hsl(345 100% 50%)" />
    </svg>
  ),
  opacity: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M14 8A6 6 0 1 1 2 8 6 6 0 1 1 14 8zM8 2v12M8 6l4.5-2.6M8 9.5l5.6-3.2M8 13l4.4-2.5" />
    </svg>
  ),
  spread: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M1.5 3.5h5.5v9h-5.5zM9 3.5h5.5v9H9z" />
    </svg>
  ),
  coffee: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2.5 6.5h8.5v3.5a3.5 3.5 0 0 1-3.5 3.5H6A3.5 3.5 0 0 1 2.5 10zM11 7.5h1.25a1.75 1.75 0 0 1 0 3.5H11M5 2.5v2M8.5 2.5v2" />
    </svg>
  ),
  book: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M8 4.5A9 9 0 0 0 1.5 3v9.5A9 9 0 0 1 8 14 9 9 0 0 1 14.5 12.5V3A9 9 0 0 0 8 4.5zM8 4.5V14" />
    </svg>
  ),
  letters: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M1.5 13L5 3.5 8.5 13M2.8 9.8h4.4M14.5 8.5V13M14.5 11A2.1 2.1 0 1 1 10.3 11 2.1 2.1 0 1 1 14.5 11" />
    </svg>
  ),
  quill: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2.5 13.5L13.5 2.5M13.5 2.5A11 11 0 0 0 5.5 10.5M13.5 2.5A11 11 0 0 1 5.5 10.5" />
    </svg>
  ),
  typewriter: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M4.5 7V2.5h7V7M1.5 7h13v6.5h-13zM4 9.5h1M7.5 9.5h1M11 9.5h1M5 11.5h6" />
    </svg>
  ),
  align_left: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2.5 4h11M2.5 7h7M2.5 10h11M2.5 13h7" />
    </svg>
  ),
  align_center: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2.5 4h11M4.5 7h7M2.5 10h11M4.5 13h7" />
    </svg>
  ),
  align_right: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2.5 4h11M6.5 7h7M2.5 10h11M6.5 13h7" />
    </svg>
  ),
  title: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M3 2.5h10M3 4.5h10M8 4.5v9" />
    </svg>
  ),
  heading: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M4 3v10M12 3v10M4 8h8" />
    </svg>
  ),
  quote: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M3.5 3.5v9M6.5 5h7M6.5 8h7M6.5 11h5" />
    </svg>
  ),
  equation: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2.5 4.5h4M4.5 2.5v4M9.5 4.5h4M2.5 11.5l3-3M2.5 8.5l3 3M9.5 10h4M9.5 12.5h4" />
    </svg>
  ),
  square: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M3 13V3h10" />
    </svg>
  ),
  pointer: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M4 2.5v10.5l2.8-2.8 1.9 4.3 1.8-.8-1.9-4.3H13z" />
    </svg>
  ),
  rectangle: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2.5 4h11v8h-11z" />
    </svg>
  ),
  ellipse: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M14 8A6 4.5 0 1 1 2 8 6 4.5 0 1 1 14 8z" />
    </svg>
  ),
  line: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M3 13L13 3" />
    </svg>
  ),
  rounded: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M3 13V8a5 5 0 0 1 5-5h5" />
    </svg>
  ),
  blend: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <circle cx="5.5" cy="8" r="4" />
      <circle cx="10.5" cy="8" r="4" />
    </svg>
  ),
  harmonize: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <circle cx="8" cy="5.5" r="3.5" />
      <circle cx="5.5" cy="10" r="3.5" />
      <circle cx="10.5" cy="10" r="3.5" />
    </svg>
  ),
  picture: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="2" y="3" width="12" height="10" rx="1.5" />
      <circle cx="5.5" cy="6.5" r="1.2" />
      <path d="M2.5 12l3.5-3.5 2.5 2.5 2-2 3 3" />
    </svg>
  ),
  folder: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2 4.5a1 1 0 0 1 1-1h3.2l1.5 1.8H13a1 1 0 0 1 1 1V12a1 1 0 0 1-1 1H3a1 1 0 0 1-1-1z" />
    </svg>
  ),
  gallery: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="0.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="2" y="2" width="5" height="5" rx="1" />
      <rect x="9" y="2" width="5" height="5" rx="1" />
      <rect x="2" y="9" width="5" height="5" rx="1" />
      <rect x="9" y="9" width="5" height="5" rx="1" />
    </svg>
  ),
};
