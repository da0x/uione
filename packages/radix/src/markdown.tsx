// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// Markdown that people write, like a book's summary or an issue's description,
// drawn as React elements rather than as HTML, so nothing in it can add markup or
// scripts to the page. Raw HTML in the source is dropped, images are shown as links
// rather than loaded, and links leave the page without carrying anything with them.

import { useId, useState } from "react";
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";

export function MarkdownText({ source }: { source: string }) {
  return (
    <div className="one-prose">
      <ReactMarkdown
        remarkPlugins={[remarkGfm]}
        skipHtml
        components={{
          a: ({ href, children }) => (
            <a href={href} rel="noopener noreferrer nofollow">
              {children}
            </a>
          ),
          img: ({ src, alt }) => (
            <a href={typeof src === "string" ? src : undefined} rel="noopener noreferrer nofollow">
              {alt || "image"}
            </a>
          ),
        }}
      >
        {source}
      </ReactMarkdown>
    </div>
  );
}

// A form field for Markdown: write it, then see how it will look.
export function MarkdownField({
  id,
  name,
  value,
  describedBy,
  onChange,
}: {
  id: string;
  name: string;
  value: string;
  describedBy: string | undefined;
  onChange: (value: string) => void;
}) {
  const [previewing, setPreviewing] = useState(false);
  const tabs = useId();
  const tab = (preview: boolean, label: string) => (
    <button
      type="button"
      role="tab"
      id={`${tabs}-${label}`}
      aria-selected={previewing === preview}
      onClick={() => setPreviewing(preview)}
      className={`px-3 py-1.5 text-xs ${previewing === preview ? "border-b-2 border-accent font-medium text-ink" : "text-muted hover:text-ink"}`}
    >
      {label}
    </button>
  );
  return (
    <div className="rounded-box border border-line">
      <div role="tablist" className="flex gap-1 border-b border-line px-1">
        {tab(false, "Write")}
        {tab(true, "Preview")}
      </div>
      {previewing ? (
        <div role="tabpanel" className="min-h-28 px-3 py-2 text-sm">
          {value.trim() === "" ? <p className="text-muted">Nothing to preview yet.</p> : <MarkdownText source={value} />}
        </div>
      ) : (
        <textarea
          role="tabpanel"
          id={id}
          name={name}
          value={value}
          rows={6}
          onChange={(event) => onChange(event.target.value)}
          aria-describedby={describedBy}
          className="block w-full resize-y rounded-b-box bg-page px-3 py-2 font-mono text-sm outline-none"
        />
      )}
    </div>
  );
}
