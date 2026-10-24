// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

import type { ReactNode } from "react";

// The icons a button, a link or a front page's section can be drawn with, its words
// naming it.
export const buttonIcons: Record<string, ReactNode> = {
  edit: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinejoin="round" aria-hidden="true">
      <path d="M11.2 2.3a1.4 1.4 0 0 1 2 2L5.6 11.9 2.8 12.7l.8-2.8z" />
      <path d="M10 3.5l2 2" />
    </svg>
  ),
  add: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" aria-hidden="true">
      <path d="M8 3v10M3 8h10" />
    </svg>
  ),
  follow: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinejoin="round" aria-hidden="true">
      <path d="M4 11.5V7.2a4 4 0 0 1 8 0v4.3l1.2 1.2H2.8z" />
      <path d="M6.6 14a1.5 1.5 0 0 0 2.8 0" />
    </svg>
  ),
  following: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="currentColor" stroke="currentColor" strokeWidth="1.5" strokeLinejoin="round" aria-hidden="true">
      <path d="M4 11.5V7.2a4 4 0 0 1 8 0v4.3l1.2 1.2H2.8z" />
      <path d="M6.6 14a1.5 1.5 0 0 0 2.8 0" fill="none" />
    </svg>
  ),
  // A cog: a ring with teeth, and its hole.
  settings: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinejoin="round" aria-hidden="true">
      <path d="M6.9 1.5h2.2l.4 1.8 1.2.7 1.8-.6 1.1 1.9-1.4 1.3v1.4l1.4 1.3-1.1 1.9-1.8-.6-1.2.7-.4 1.8H6.9l-.4-1.8-1.2-.7-1.8.6-1.1-1.9 1.4-1.3V7.3L2.4 6l1.1-1.9 1.8.6 1.2-.7z" />
      <circle cx="8" cy="8" r="2" />
    </svg>
  ),
  workflow: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinejoin="round" aria-hidden="true">
      <rect x="1.5" y="2" width="4.5" height="4" rx="1" />
      <rect x="10" y="10" width="4.5" height="4" rx="1" />
      <path d="M6 4h2.5a1.5 1.5 0 0 1 1.5 1.5V10" strokeLinecap="round" />
    </svg>
  ),
  // A bin with its lid.
  delete: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2.5 4.5h11M6.5 4.5V3a1 1 0 0 1 1-1h1a1 1 0 0 1 1 1v1.5" />
      <path d="M3.8 4.5l.7 8.6a1 1 0 0 0 1 .9h5a1 1 0 0 0 1-.9l.7-8.6M6.7 7v4.5M9.3 7v4.5" />
    </svg>
  ),
  // A robot, for what programs do, like a project's agents.
  robot: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="3" y="5" width="10" height="8" rx="2" />
      <path d="M8 5V3M6 9h.01M10 9h.01M6.5 11h3M1.5 8.5v2M14.5 8.5v2" />
    </svg>
  ),
  // A box with its lid, what's put away.
  archive: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="1.5" y="2.5" width="13" height="3" rx="1" />
      <path d="M2.8 5.5v7a1 1 0 0 0 1 1h8.4a1 1 0 0 0 1-1v-7M6.5 8.5h3" />
    </svg>
  ),

  // A building with its windows and door, what's built and run.
  building: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="3" y="1.5" width="10" height="13" rx="1" />
      <path d="M6 4.5h1M9 4.5h1M6 7.5h1M9 7.5h1M7 14.5v-3h2v3" />
    </svg>
  ),
  // Three disks stacked, what's stored.
  database: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="2.5" y="2" width="11" height="3.5" rx="1.75" />
      <rect x="2.5" y="6.25" width="11" height="3.5" rx="1.75" />
      <rect x="2.5" y="10.5" width="11" height="3.5" rx="1.75" />
    </svg>
  ),
  // Angle brackets around a slash, what's written.
  code: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M5.5 4.5L2 8l3.5 3.5M10.5 4.5L14 8l-3.5 3.5M9.25 3l-2.5 10" />
    </svg>
  ),
  // Two rings joined, what's connected.
  connect: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <circle cx="4" cy="8" r="2.5" />
      <circle cx="12" cy="8" r="2.5" />
      <path d="M6.5 8h3" />
    </svg>
  ),
  // A shield with a check, what's kept safe.
  shield: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M8 1.5l5.5 2v4.5L8 14.5 2.5 8V3.5z" />
      <path d="M5.5 8l2 2 3-3.5" />
    </svg>
  ),
  // A cloud, what runs in it.
  cloud: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M4.5 12.5a3 3 0 0 1 0-6a4 4 0 0 1 7.5-1a3.5 3.5 0 0 1 0 7z" />
    </svg>
  ),
  // Bars over a line, what's measured.
  chart: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M2.5 13.5h11M4.5 11V8M8 11V4M11.5 11V6.5" />
    </svg>
  ),
  // A four-pointed star, what's clever or new.
  spark: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M8 1.5l1.5 5 5 1.5-5 1.5L8 14.5 6.5 9.5l-5-1.5 5-1.5z" />
    </svg>
  ),
  // Two people, the ones it's for.
  people: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <circle cx="6" cy="5.5" r="2.25" />
      <path d="M1.5 13.5a4.5 4.5 0 0 1 9 0" />
      <circle cx="11.5" cy="6" r="1.75" />
      <path d="M11 9.5a3.5 3.5 0 0 1 3.5 4" />
    </svg>
  ),
  // A padlock, what's closed to others.
  lock: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <rect x="3" y="7" width="10" height="7" rx="1.5" />
      <path d="M5.5 7V5a2.5 2.5 0 0 1 5 0v2" />
    </svg>
  ),
  // A globe, what reaches everywhere.
  globe: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <circle cx="8" cy="8" r="6.25" />
      <path d="M1.75 8h12.5M8 1.75a9 9 0 0 1 0 12.5a9 9 0 0 1 0-12.5" />
    </svg>
  ),
  // Layers stacked, what's built on what.
  layers: (
    <svg viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d="M8 2l6 3-6 3-6-3z" />
      <path d="M2 8l6 3 6-3M2 11l6 3 6-3" />
    </svg>
  ),
};
