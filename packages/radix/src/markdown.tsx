// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// Markdown that people write, like a book's summary or an issue's description,
// drawn as React elements rather than as HTML, so nothing in it can add markup or
// scripts to the page. Raw HTML in the source is dropped, images are shown as links
// rather than loaded, and links leave the page without carrying anything with them.

import { useId, useRef, useState } from "react";
import type { KeyboardEvent } from "react";
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";

export function MarkdownText({ source }: { source: string }) {
  return (
    <div className="one-prose">
      <ReactMarkdown
        remarkPlugins={[remarkGfm]}
        skipHtml
        components={{
          // A link whose address was blocked arrives with none, and an empty href
          // would reload the page, so it's shown as its text.
          a: ({ href, children }) =>
            href ? (
              <a href={href} rel="noopener noreferrer nofollow">
                {children}
              </a>
            ) : (
              <span>{children}</span>
            ),
          img: ({ src, alt }) =>
            typeof src === "string" && src ? (
              <a href={src} rel="noopener noreferrer nofollow">
                {alt || "image"}
              </a>
            ) : (
              <span>{alt || "image"}</span>
            ),
        }}
      >
        {source}
      </ReactMarkdown>
    </div>
  );
}

// A form field for Markdown: write it, then see how it will look. The textarea stays
// a textarea, with the field's id, so the form's label names it; each tab's panel
// wraps it or the preview.
export function MarkdownField({
  id,
  name,
  value,
  labelledBy,
  describedBy,
  onChange,
}: {
  id: string;
  name: string;
  value: string;
  labelledBy: string;
  describedBy: string | undefined;
  onChange: (value: string) => void;
}) {
  const [previewing, setPreviewing] = useState(false);
  const tabs = useId();
  const buttons = useRef<Record<string, HTMLButtonElement | null>>({});
  const tabId = (preview: boolean) => `${tabs}-${preview ? "preview" : "write"}`;
  const panelId = (preview: boolean) => `${tabId(preview)}-panel`;
  // With two tabs, either arrow moves to the other one; Home and End go to the first
  // and the last.
  const move = (event: KeyboardEvent<HTMLButtonElement>) => {
    if (!["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) return;
    event.preventDefault();
    const next = event.key === "Home" ? false : event.key === "End" ? true : !previewing;
    setPreviewing(next);
    buttons.current[tabId(next)]?.focus();
  };
  const tab = (preview: boolean, label: string) => (
    <button
      type="button"
      role="tab"
      id={tabId(preview)}
      ref={(element) => {
        buttons.current[tabId(preview)] = element;
      }}
      aria-selected={previewing === preview}
      aria-controls={panelId(preview)}
      tabIndex={previewing === preview ? 0 : -1}
      onClick={() => setPreviewing(preview)}
      onKeyDown={move}
      className={`px-3 py-1.5 text-xs ${previewing === preview ? "border-b-2 border-accent font-medium text-ink" : "text-muted hover:text-ink"}`}
    >
      {label}
    </button>
  );
  return (
    <div className="rounded-box border border-control-line">
      <div role="tablist" className="flex gap-1 border-b border-line px-1">
        {tab(false, "Write")}
        {tab(true, "Preview")}
      </div>
      <div role="tabpanel" id={panelId(false)} aria-labelledby={tabId(false)} hidden={previewing}>
        <textarea
          id={id}
          name={name}
          value={value}
          rows={6}
          onChange={(event) => onChange(event.target.value)}
          aria-labelledby={labelledBy}
          aria-describedby={describedBy}
          className="block w-full resize-y rounded-b-box bg-page px-3 py-2 font-mono text-sm focus:outline-hidden focus-visible:ring-2 focus-visible:ring-accent focus-visible:ring-inset"
        />
      </div>
      <div
        role="tabpanel"
        id={panelId(true)}
        aria-labelledby={`${labelledBy} ${tabId(true)}`}
        hidden={!previewing}
        // The preview may hold nothing that takes focus, so the panel itself does.
        tabIndex={0}
        className="min-h-28 px-3 py-2 text-sm"
      >
        {previewing &&
          (value.trim() === "" ? <p className="text-muted">Nothing to preview yet.</p> : <MarkdownText source={value} />)}
      </div>
    </div>
  );
}
