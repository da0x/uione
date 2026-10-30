// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Markdown that people write, like a book's summary or an issue's description,
// drawn as React elements rather than as HTML, so nothing in it can add markup or
// scripts to the page. Raw HTML in the source is dropped, images are shown as links
// rather than loaded, and links leave the page without carrying anything with them.

import { useId, useRef, useState } from "react";
import type { KeyboardEvent } from "react";
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import { ColoredCode, DiagramView, Equation } from "@uione/react";

// Markdown's tree, as much of it as math needs.
type MarkdownNode = { type: string; value?: string; lang?: string | null; children?: MarkdownNode[]; data?: object };

// Equations in the words, $x^2$, and ```math blocks, read as one::markdown reads them: a
// `$` opens before something other than a space and closes after one, not before a
// digit, so "$5 and $10" stays money.
function mathOf(words: string): MarkdownNode[] {
  const out: MarkdownNode[] = [];
  let from = 0;
  for (let i = 0; i < words.length; i++) {
    if (words[i] !== "$" || i + 1 >= words.length || words[i + 1] === " " || words[i + 1] === "$") continue;
    let close = i + 1;
    while ((close = words.indexOf("$", close + 1)) !== -1) {
      if (words[close - 1] !== " " && !/[0-9]/.test(words[close + 1] ?? "")) break;
    }
    if (close === -1) continue;
    if (i > from) out.push({ type: "text", value: words.slice(from, i) });
    const written = words.slice(i + 1, close);
    out.push({ type: "math", value: written, data: { hName: "span", hProperties: { dataMath: written } } });
    from = i = close + 1;
    i--;
  }
  if (from < words.length) out.push({ type: "text", value: words.slice(from) });
  return out;
}

function remarkMath() {
  const walk = (node: MarkdownNode) => {
    if (!node.children) return;
    node.children = node.children.flatMap((child) => {
      if (child.type === "text") return mathOf(child.value ?? "");
      // A math block set as an equation; a chem block as a chemical one, as ce( ) reads it.
      if (child.type === "code" && (child.lang === "math" || child.lang === "chem")) {
        const written = child.lang === "chem" ? `ce(${(child.value ?? "").trim()})` : (child.value ?? "").trim();
        return [{ type: "math", value: written, data: { hName: "div", hProperties: { dataMath: written, dataDisplayed: true }, hChildren: [] } }];
      }
      // A diagram block drawn as its blocks and wires.
      if (child.type === "code" && child.lang === "diagram") {
        const written = child.value ?? "";
        return [{ type: "diagram", value: written, data: { hName: "div", hProperties: { dataDiagram: written }, hChildren: [] } }];
      }
      walk(child);
      return [child];
    });
  };
  return walk;
}

// An equation alone in a paragraph is set on its own line, as a ```math block is.
function alone(children: unknown) {
  const all = Array.isArray(children) ? children : [children];
  const shown = all.filter((c) => !(typeof c === "string" && c.trim() === ""));
  return shown.length === 1 && typeof shown[0] === "object" && shown[0] !== null && "props" in shown[0] && (shown[0].props as Record<string, unknown>)["data-math"] !== undefined
    ? String((shown[0].props as Record<string, unknown>)["data-math"])
    : undefined;
}

export function MarkdownText({ source }: { source: string }) {
  return (
    <div className="one-prose">
      <ReactMarkdown
        remarkPlugins={[remarkGfm, remarkMath]}
        skipHtml
        components={{
          span: ({ node: _, ...props }) => {
            const written = (props as Record<string, unknown>)["data-math"];
            return typeof written === "string" ? <Equation written={written} displayed={false} /> : <span {...props} />;
          },
          div: ({ node: _, ...props }) => {
            const drawn = (props as Record<string, unknown>)["data-diagram"];
            if (typeof drawn === "string") {
              return (
                <div className="one-math">
                  <DiagramView written={drawn} />
                </div>
              );
            }
            const written = (props as Record<string, unknown>)["data-math"];
            return typeof written === "string" ? (
              <div className="one-math">
                <Equation written={written} displayed />
              </div>
            ) : (
              <div {...props} />
            );
          },
          p: ({ node: _, children, ...props }) => {
            const written = alone(children);
            return written !== undefined ? (
              <div className="one-math">
                <Equation written={written} displayed />
              </div>
            ) : (
              <p {...props}>{children}</p>
            );
          },
          // A fenced block's code colored as one::syntax reads its language, like ```cpp.
          code: ({ node: _, className, children, ...props }) => {
            const language = /\blanguage-(\S+)/.exec(className ?? "")?.[1];
            return (
              <code className={className} {...props}>
                {language && typeof children === "string" ? <ColoredCode words={children} language={language} /> : children}
              </code>
            );
          },
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
