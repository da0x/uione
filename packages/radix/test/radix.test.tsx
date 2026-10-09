// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

import { act, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { vi } from "vitest";
import { existsSync, readFileSync } from "node:fs";
import { resolve } from "node:path";
import type { ReactNode } from "react";
import { useState } from "react";
import {
  App,
  Code,
  Command,
  Confirm,
  Form,
  Hero,
  Link,
  Live,
  Pages,
  Section,
  Table,
  Text,
  memorySource,
  screen as defineScreen,
  useView,
} from "@uione/react";
import type { MemorySource } from "@uione/react";
import { highlight } from "../src/highlight.js";
import { restyle, setCodeDisplay } from "../src/display.js";
import { MarkdownText } from "../src/markdown.js";
import { DisplaySettings } from "../src/accessibility.js";
import { radix } from "../src/index.js";

function renderScreen(body: () => ReactNode, source: MemorySource = memorySource(), location = "/") {
  const screens = [
    defineScreen({ title: "uione", route: "/" }, body),
    defineScreen({ title: "Docs", route: "/docs/:page?", nav: "Docs" }, body),
  ];
  return render(<App name="uione" screens={screens} ui={radix} data={source} location={location} />);
}

describe("the page", () => {
  it("has the app's name linking home, and the navigation", () => {
    renderScreen(() => <Text>hello</Text>, memorySource(), "/docs");
    expect(screen.getByRole("link", { name: "uione" }).getAttribute("href")).toBe("/");
    expect(screen.getByRole("link", { name: "Docs" }).getAttribute("aria-current")).toBe("page");
    expect(screen.getByRole("heading", { level: 1, name: "Docs" })).toBeTruthy();
  });

  it("switches between light and dark, and keeps the reader's pick", () => {
    const kept = new Map<string, string>();
    vi.stubGlobal("localStorage", {
      getItem: (k: string) => kept.get(k) ?? null,
      setItem: (k: string, v: string) => void kept.set(k, v),
      removeItem: (k: string) => void kept.delete(k),
    });
    delete document.documentElement.dataset.theme;
    const { unmount } = renderScreen(() => <Text>hello</Text>);
    fireEvent.click(screen.getByRole("button", { name: "Switch to dark mode" }));
    expect(document.documentElement.dataset.theme).toBe("dark");
    expect(localStorage.getItem("uione-theme")).toBe("dark");
    fireEvent.click(screen.getByRole("button", { name: "Switch to light mode" }));
    expect(document.documentElement.dataset.theme).toBe("light");
    unmount();
    // A page opened later starts from the pick.
    renderScreen(() => <Text>hello</Text>);
    expect(screen.getByRole("button", { name: "Switch to dark mode" })).toBeTruthy();
    vi.unstubAllGlobals();
  });
});

describe("content", () => {
  it("draws a hero, a section with its anchor, text and a link", () => {
    renderScreen(() => (
      <>
        <Hero title="Write the feature once.">
          <Text>One short file.</Text>
          <Link to="#waitlist">Join the waitlist</Link>
        </Hero>
        <Section title="Join the waitlist" id="waitlist">
          <Text>Soon.</Text>
        </Section>
      </>
    ));
    expect(screen.getByRole("heading", { level: 1, name: "Write the feature once." })).toBeTruthy();
    expect(screen.getByRole("heading", { level: 2, name: "Join the waitlist" }).closest("section")!.id).toBe("waitlist");
    expect(screen.getByRole("link", { name: "Join the waitlist" }).getAttribute("href")).toBe("#waitlist");
  });

  it("draws docs pages beside their list", () => {
    const pages = [
      { slug: "overview", title: "Overview", html: "<h1>Overview</h1>" },
      { slug: "language", title: "Language", html: "<h1>Language</h1><p>Blocks use braces.</p>" },
    ];
    renderScreen(() => <Pages base="/docs" pages={pages} />, memorySource(), "/docs/language");
    expect(screen.getByText("Blocks use braces.")).toBeTruthy();
    expect(screen.getByRole("navigation", { name: "Pages" })).toBeTruthy();
  });
});

describe("code", () => {
  const source = "namespace library {\n\nentity book {\n  title  text  required\n}\n\n}";

  it("highlights .one code with the editors' grammar", () => {
    const html = highlight(source, "uione")!;
    expect(html).toContain('class="shiki');
    // The declaration keyword and the type get colors of their own, different from
    // plain text, in both themes.
    const colored = (word: string) => new RegExp(`<span style="color:(#[0-9A-Fa-f]{6});--shiki-dark:(#[0-9A-Fa-f]{6})">${word}</span>`).exec(html);
    const keyword = colored("entity");
    const type = colored("text");
    expect(keyword).not.toBeNull();
    expect(type).not.toBeNull();
    expect(keyword![1]).not.toBe(type![1]);
  });

  it("shows code in other languages plain", () => {
    expect(highlight("let x = 1", "js")).toBeUndefined();
    renderScreen(() => <Code lang="js" source="let x = 1" />);
    expect(screen.getByText("let x = 1").tagName).toBe("CODE");
  });

  it("draws highlighted code on the first render", () => {
    renderScreen(() => <Code lang="uione" source={source} />);
    expect(document.querySelector(".one-code .shiki")).not.toBeNull();
  });

  it("highlights the .one code in a docs page, and leaves other languages plain", () => {
    const html =
      '<h1>Language</h1><pre><code class="language-one">entity book {\n  title  text  required\n  note   text = &quot;a &amp; b&quot;\n}\n</code></pre>' +
      '<pre><code class="language-js">let x = 1\n</code></pre>';
    renderScreen(() => <Pages base="/docs" pages={[{ slug: "language", title: "Language", html }]} />, memorySource(), "/docs/language");
    const highlighted = document.querySelector("article .one-code .shiki");
    expect(highlighted).not.toBeNull();
    // What the markdown escaped is shown as written, once.
    expect(highlighted!.textContent).toContain('"a & b"');
    expect(document.querySelector("article code.language-js")?.textContent).toBe("let x = 1\n");
  });
});

describe("forms", () => {
  it("labels every field and describes it with its hint", () => {
    renderScreen(() => <Form command="studio::project::create" fields={[{ name: "name", hint: "Shown in your list" }]} />);
    const input = screen.getByLabelText("Name");
    expect(input.tagName).toBe("INPUT");
    const hint = document.getElementById(input.getAttribute("aria-describedby")!);
    expect(hint!.textContent).toBe("Shown in your list");
  });

  it("shows a command's error as an alert", async () => {
    const source = memorySource({ commands: { "a::b::create": () => Promise.reject(new Error("try again")) } });
    renderScreen(() => <Form command="a::b::create" fields={["name"]} />, source);
    fireEvent.click(screen.getByRole("button", { name: "Create" }));
    expect((await screen.findByRole("alert")).textContent).toBe("try again");
  });
});

describe("dialogs", () => {
  it("moves focus into the dialog, and closes on Escape without running anything", async () => {
    const source = memorySource();
    renderScreen(
      () => (
        <>
          <Command name="library::book::withdraw" />
          <Confirm command="library::book::withdraw" question="Withdraw it?" />
        </>
      ),
      source,
    );
    fireEvent.click(screen.getByRole("button", { name: "Withdraw" }));
    const dialog = await screen.findByRole("dialog", { name: "Are you sure?" });
    await waitFor(() => expect(dialog.contains(document.activeElement)).toBe(true));
    fireEvent.keyDown(document.activeElement!, { key: "Escape" });
    await waitFor(() => expect(screen.queryByRole("dialog")).toBeNull());
    expect(source.runs).toEqual([]);
  });

  it("has a labelled close button that closes it without running anything", async () => {
    const source = memorySource();
    renderScreen(
      () => (
        <>
          <Command name="library::book::withdraw" />
          <Confirm command="library::book::withdraw" question="Withdraw it?" />
        </>
      ),
      source,
    );
    fireEvent.click(screen.getByRole("button", { name: "Withdraw" }));
    const dialog = await screen.findByRole("dialog", { name: "Are you sure?" });
    fireEvent.click(within(dialog).getByRole("button", { name: "Close" }));
    await waitFor(() => expect(screen.queryByRole("dialog")).toBeNull());
    expect(source.runs).toEqual([]);
  });
});

describe("tables and live values", () => {
  function Shelf() {
    const shelf = useView("library::shelf");
    return <Table view={shelf} columns={{ title: "Title" }} />;
  }

  it("says what's happening when a table has no rows", () => {
    const source = memorySource();
    renderScreen(() => <Shelf />, source);
    expect(screen.getByText("Loading…")).toBeTruthy();
    act(() => source.set("library::shelf", { rows: [] }));
    expect(screen.getByText("Nothing here yet.")).toBeTruthy();
    act(() => source.status("library::shelf", "denied"));
    expect(screen.getByText("You can't see this.")).toBeTruthy();
  });

  it("names the column of row actions for screen readers", () => {
    const source = memorySource();
    function Books() {
      return <Table view={useView("library::shelf")} columns={{ title: "Title" }} actions={["library::book::withdraw"]} />;
    }
    renderScreen(() => <Books />, source);
    act(() => source.set("library::shelf", { rows: [{ id: "1", title: "Dune" }] }));
    expect(screen.getByRole("columnheader", { name: "Actions" })).toBeTruthy();
  });

  it("opens a row's page wherever the row is clicked, but not from its own buttons", () => {
    const source = memorySource();
    function Books() {
      return <Table view={useView("library::shelf")} link="/docs/:page" columns={{ title: "Title", author: "Author" }} actions={["library::book::withdraw"]} />;
    }
    renderScreen(() => <Books />, source);
    act(() => source.set("library::shelf", { rows: [{ id: "dune", title: "Dune", author: "Herbert" }] }));
    fireEvent.click(screen.getByRole("button", { name: "Withdraw Dune" }));
    expect(screen.queryByRole("heading", { name: "Docs" })).toBeNull();
    fireEvent.click(screen.getByText("Herbert"));
    expect(screen.getByRole("heading", { name: "Docs" })).toBeTruthy();
  });

  it("shows a placeholder instead of a value that isn't live", () => {
    const source = memorySource();
    function Count() {
      return <Live view={useView("waitlist::signups")} field="total" />;
    }
    renderScreen(() => <Count />, source);
    expect(screen.getByText("not available right now").className).toBe("sr-only");
    act(() => source.set("waitlist::signups", { total: 7 }));
    expect(screen.getByText("7")).toBeTruthy();
  });
});

describe("the stylesheet", () => {
  it("is built, with the theme's colors and the components' classes in it", () => {
    // A path rather than a URL: under jsdom, URL is jsdom's class, which Node's file
    // functions don't accept. Tests run from the package's own folder.
    const css = resolve(process.cwd(), "dist/styles.css");
    expect(existsSync(css), "run yarn build first, which compiles dist/styles.css").toBe(true);
    const text = readFileSync(css, "utf8");
    expect(text).toContain("--color-accent");
    expect(text).toContain("prefers-color-scheme:dark");
    expect(text).toContain(".bg-accent");
    expect(text).toContain(".one-prose");
  });

  it("squares corners when the page asks, except a person's picture, which stays a circle", () => {
    const text = readFileSync(resolve(process.cwd(), "dist/styles.css"), "utf8");
    expect(text).toMatch(/\[data-corners=(square|"square")\] \.rounded-full:not\(\.one-person\)\{border-radius:0\}/);
    // A section's colors in the shades of the theme the site's is from.
    expect(text).toMatch(/\[data-palette=(papercolor|"papercolor")\]\{--hue-blue:#005faf/);
  });

  it("draws what the reader asks for: text size, spacing, a legible font, less motion, links and focus", () => {
    const text = readFileSync(resolve(process.cwd(), "dist/styles.css"), "utf8");
    expect(text).toMatch(/\[data-text=(200|"200")\]\{font-size:200%\}/);
    expect(text).toMatch(/\[data-spacing=(comfortable|"comfortable")\] body\{letter-spacing:\.12em;word-spacing:\.16em;line-height:1\.5\}/);
    expect(text).toMatch(/\[data-font=(legible|"legible")\]\{--font-sans:"Atkinson Hyperlegible"/);
    expect(text).toMatch(/prefers-reduced-motion:\s*reduce/);
    expect(text).toMatch(/\[data-links=(underline|"underline")\] a\{[^}]*text-decoration-line:underline/);
    expect(text).toMatch(/forced-colors:\s*active/);
  });
});

describe("how code is shown", () => {
  const code = "entity loan {\n\tdue_at  date  required\n}\nfunction sort_title(title) {\n\treturn title\n}";

  afterEach(() => act(() => setCodeDisplay({ tabWidth: 4, names: "default" })));

  it("writes names in the style the reader picks, and nothing else", () => {
    expect(restyle("due_at", "camelCase")).toBe("dueAt");
    expect(restyle("due_at", "PascalCase")).toBe("DueAt");
    expect(restyle("due_at", "kebab-case")).toBe("due-at");
    expect(restyle("library::book", "PascalCase")).toBe("Library::Book");
    expect(restyle("due_at", "default")).toBe("due_at");

    const text = (html: string) => html.replace(/<[^>]+>/g, "");
    const camel = text(highlight(code, "uione", "camelCase")!);
    expect(camel).toContain("dueAt");
    expect(camel).toContain("sortTitle");
    // Keywords and types keep their own spelling, even in PascalCase.
    const pascal = text(highlight(code, "uione", "PascalCase")!);
    expect(pascal).toContain("entity Loan");
    expect(pascal).toContain("date");
    expect(pascal).toContain("required");
    expect(pascal).toContain("DueAt");
  });

  it("shows code plain when the highlighter can't be built, rather than failing to load", async () => {
    vi.resetModules();
    vi.doMock("shiki/core", async (original) => ({
      ...(await original<typeof import("shiki/core")>()),
      createHighlighterCoreSync: () => {
        throw new Error("broken grammar");
      },
    }));
    try {
      const fresh = await import("../src/highlight.js");
      expect(fresh.highlight(code, "uione")).toBeUndefined();
      expect(fresh.highlightCodeBlocks('<pre><code class="language-one">x</code></pre>', "box")).toBe(
        '<pre><code class="language-one">x</code></pre>',
      );
    } finally {
      vi.doUnmock("shiki/core");
      vi.resetModules();
    }
  });

    it("has a toolbar that sets the tab width and the names for every piece of code at once", () => {
    renderScreen(() => (
      <>
        <Code lang="uione" source={code} />
        <Code lang="uione" source={code} />
      </>
    ));
    const boxes = () => Array.from(document.querySelectorAll<HTMLElement>(".one-code [style*='tab-size']"));
    expect(boxes().map((b) => b.style.tabSize)).toEqual(["4", "4"]);
    const [first] = screen.getAllByRole("group", { name: "Tab width" });
    fireEvent.click(within(first!).getByRole("button", { name: "2" }));
    expect(boxes().map((b) => b.style.tabSize)).toEqual(["2", "2"]);

    const [names] = screen.getAllByLabelText("Names");
    expect((names as HTMLSelectElement).value).toBe("default");
    expect(screen.getAllByRole("option", { name: "Default" }).length).toBeGreaterThan(0);
    fireEvent.change(names!, { target: { value: "camelCase" } });
    expect(boxes().every((b) => b.textContent!.includes("dueAt"))).toBe(true);
  });
});

describe("markdown someone wrote", () => {
  it("is drawn with its structure, and can't add markup, scripts or images to the page", () => {
    const { container } = render(
      <MarkdownText
        source={"# Dune\n\nA **classic**.\n\n<script>alert(1)</script><b>bold?</b>\n\n![cover](https://example.com/c.png)\n\n[home](javascript:alert(1)) and [site](https://example.com)\n\n| a | b |\n|---|---|\n| 1 | 2 |"}
      />,
    );
    expect(container.querySelector("h1")?.textContent).toBe("Dune");
    expect(container.querySelector("strong")?.textContent).toBe("classic");
    expect(container.querySelector("script")).toBeNull();
    expect(container.querySelector("b")).toBeNull();
    expect(container.querySelector("img")).toBeNull();
    expect(container.querySelector('a[href="https://example.com/c.png"]')?.textContent).toBe("cover");
    expect(container.querySelector('a[href^="javascript"]')).toBeNull();
    expect(container.querySelector('a[href="https://example.com"]')?.getAttribute("rel")).toBe("noopener noreferrer nofollow");
    expect(container.querySelector("table")).not.toBeNull();
    // The blocked link keeps its text, without an empty href that would reload the page.
    expect(container.querySelector('a:not([href]), a[href=""]')).toBeNull();
    expect(container.textContent).toContain("home");
  });

  it("is written in a form with a preview of how it will look", () => {
    renderScreen(() => <Form command="library::book::create" fields={[{ name: "summary", type: "markdown" }]} />);
    fireEvent.change(screen.getByRole("textbox", { name: "Summary" }), { target: { value: "A **classic**." } });
    fireEvent.click(screen.getByRole("tab", { name: "Preview" }));
    expect(screen.getByText("classic").tagName).toBe("STRONG");
    fireEvent.click(screen.getByRole("tab", { name: "Write" }));
    expect((screen.getByRole("textbox", { name: "Summary" }) as HTMLTextAreaElement).value).toBe("A **classic**.");
  });

  it("is a textbox inside the tab panels, and its tabs move with the arrow keys", () => {
    renderScreen(() => <Form command="library::book::create" fields={[{ name: "summary", type: "markdown" }]} />);
    const textbox = screen.getByRole("textbox", { name: "Summary" });
    expect(textbox.tagName).toBe("TEXTAREA");
    const write = screen.getByRole("tab", { name: "Write" });
    const preview = screen.getByRole("tab", { name: "Preview" });
    const panel = screen.getByRole("tabpanel", { name: "Write" });
    expect(panel.contains(textbox)).toBe(true);
    expect(write.getAttribute("aria-controls")).toBe(panel.id);
    expect([write.tabIndex, preview.tabIndex]).toEqual([0, -1]);

    write.focus();
    fireEvent.keyDown(write, { key: "ArrowRight" });
    expect(preview.getAttribute("aria-selected")).toBe("true");
    expect(document.activeElement).toBe(preview);
    expect([write.tabIndex, preview.tabIndex]).toEqual([-1, 0]);
    const previewPanel = screen.getByRole("tabpanel", { name: "Summary Preview" });
    expect(preview.getAttribute("aria-controls")).toBe(previewPanel.id);
    expect(screen.queryByRole("tabpanel", { name: "Write" })).toBeNull();

    fireEvent.keyDown(preview, { key: "Home" });
    expect(document.activeElement).toBe(write);
    expect(screen.getByRole("textbox", { name: "Summary" })).toBe(textbox);
  });
});

describe("a choice of a few", () => {
  it("is a row of cards to pick from, starting on its own choice", () => {
    function Visibility() {
      const [value, setValue] = useState("private");
      const RadixForm = radix.Form;
      return (
        <RadixForm
          fields={[{ name: "visibility", label: "Visibility", type: "choice", choices: [["private", "Private"], ["public", "Public"]], value, onChange: setValue }]}
          submit="Create"
          busy={false}
          error={undefined}
          onSubmit={() => {}}
        />
      );
    }
    render(<Visibility />);
    const group = screen.getByRole("radiogroup", { name: "Visibility" });
    const cards = within(group).getAllByRole("radio") as HTMLInputElement[];
    expect(cards.map((c) => c.value)).toEqual(["private", "public"]);
    expect(cards[0]!.checked).toBe(true);
    fireEvent.click(screen.getByLabelText("Public"));
    expect((screen.getByLabelText("Public") as HTMLInputElement).checked).toBe(true);
  });
});

describe("a link to another site", () => {
  it("opens in a new tab and carries the link-external mark; one of the app's own doesn't", () => {
    const { container } = render(
      <>
        <radix.Link href="https://github.com/da0x/neotrac" external>
          da0x/neotrac
        </radix.Link>
        <radix.Link href="/da0x">da0x</radix.Link>
      </>,
    );
    const [away, home] = Array.from(container.querySelectorAll("a"));
    expect(away.getAttribute("target")).toBe("_blank");
    expect(away.getAttribute("rel")).toBe("noreferrer");
    expect(away.querySelector("svg")).not.toBeNull();
    expect(home.getAttribute("target")).toBeNull();
    expect(home.querySelector("svg")).toBeNull();
  });
});

describe("asking to count visits", () => {
  it("asks once, at the foot of the page, and says what the visitor answered", () => {
    const answers: boolean[] = [];
    render(<radix.Consent onAnswer={(agreed) => answers.push(agreed)} />);
    const ask = screen.getByRole("complementary", { name: "Counting visits" });
    expect(ask.textContent).toContain("Google Analytics");
    fireEvent.click(within(ask).getByRole("button", { name: "No thanks" }));
    fireEvent.click(within(ask).getByRole("button", { name: "Allow" }));
    expect(answers).toEqual([false, true]);
  });
});

describe("a table's rows", () => {
  it("sit closer with Compact rows, which every table follows and the browser keeps", () => {
    const kept = new Map<string, string>();
    vi.stubGlobal("localStorage", { getItem: (k: string) => kept.get(k) ?? null, setItem: (k: string, v: string) => kept.set(k, v) });
    const Table = radix.Table;
    const rows = [{ id: "a", cells: ["12", "Copy an issue whole"], actions: [] }];
    render(
      <>
        <Table status="live" columns={["#", "Title"]} rows={rows} tools={<span />} />
        <Table status="live" columns={["#", "Title"]} rows={rows} />
      </>,
    );
    const cells = () => screen.getAllByRole("cell").filter((cell) => cell.textContent === "12");
    expect(cells().every((cell) => cell.className.includes("py-1"))).toBe(true);
    const compact = screen.getByRole("button", { name: "Compact rows" });
    expect(compact.getAttribute("aria-pressed")).toBe("false");
    fireEvent.click(compact);
    expect(compact.getAttribute("aria-pressed")).toBe("true");
    expect(cells().every((cell) => cell.className.includes("py-0.5"))).toBe(true);
    expect(kept.get("uione-density")).toBe("compact");
    vi.unstubAllGlobals();
  });
});

describe("on a phone", () => {
  const phone = () =>
    vi.stubGlobal("matchMedia", (query: string) => ({ matches: query.includes("max-width"), addEventListener: () => {}, removeEventListener: () => {} }));

  it("lists a table's rows by their title, with the rest on a line under it", () => {
    phone();
    const Table = radix.Table;
    render(
      <Table
        status="live"
        columns={["#", "Title", "Labels", "Priority"]}
        rows={[{ id: "a", cells: ["12", "Copy an issue whole", "", "High"], actions: [] }]}
      />,
    );
    expect(screen.queryByRole("table")).toBeNull();
    const entry = screen.getByRole("listitem");
    expect(entry.textContent).toBe("#12Copy an issue wholePriority: High");
    vi.unstubAllGlobals();
  });

  it("shows a board's phases a page at a time, and moves a card with a picker", () => {
    phone();
    const Board = radix.Board;
    const onMove = vi.fn();
    render(
      <Board
        status="live"
        onMove={onMove}
        columns={[
          { id: "todo", title: "To Do", cards: [{ id: "a", title: "Copy an issue whole", details: [], reaches: ["doing"] }] },
          { id: "doing", title: "Doing", cards: [] },
        ]}
      />,
    );
    const phases = screen.getByRole("tablist", { name: "Phases" });
    expect(within(phases).getAllByRole("tab").map((tab) => tab.textContent)).toEqual(["To Do1", "Doing0"]);
    fireEvent.click(within(phases).getByRole("tab", { name: "Doing 0" }));
    expect(within(phases).getByRole("tab", { name: "Doing 0" }).getAttribute("aria-selected")).toBe("true");
    fireEvent.change(screen.getByLabelText("Move Copy an issue whole"), { target: { value: "doing" } });
    expect(onMove).toHaveBeenCalledWith("a", "doing");
    vi.unstubAllGlobals();
  });
});

describe("icons", () => {
  it("draws a link as an icon, still named by its words", () => {
    const Link = radix.Link;
    render(
      <Link href="/neotrac/boards/main/workflow" icon="workflow">
        Workflow
      </Link>,
    );
    const link = screen.getByRole("link", { name: "Workflow" });
    expect(link.getAttribute("title")).toBe("Workflow");
    expect(link.querySelector("svg")).toBeTruthy();
    expect(link.textContent).toBe("");
  });
});

describe("display settings", () => {
  const kept = new Map<string, string>();
  beforeEach(() => {
    vi.stubGlobal("localStorage", {
      getItem: (k: string) => kept.get(k) ?? null,
      setItem: (k: string, v: string) => void kept.set(k, v),
      removeItem: (k: string) => void kept.delete(k),
    });
  });
  afterEach(() => {
    kept.clear();
    vi.unstubAllGlobals();
    for (const name of ["contrast", "text", "spacing", "font", "motion", "transparency", "links", "focus"]) delete document.documentElement.dataset[name];
  });

  it("opens from a labeled button, sets what the reader picks on the page, and keeps it", () => {
    render(<DisplaySettings />);
    const button = screen.getByRole("button", { name: "Display settings" });
    expect(button.getAttribute("aria-expanded")).toBe("false");
    fireEvent.click(button);
    expect(button.getAttribute("aria-expanded")).toBe("true");
    const panel = screen.getByRole("group", { name: "Display settings" });
    // Each setting is a group of radio buttons under its name.
    fireEvent.click(within(panel).getByRole("radio", { name: "More" }));
    fireEvent.click(within(panel).getByRole("radio", { name: "200%" }));
    fireEvent.click(within(panel).getByRole("radio", { name: "Comfortable" }));
    fireEvent.click(within(panel).getByRole("radio", { name: "Underline every link" }));
    const root = document.documentElement;
    expect(root.dataset.contrast).toBe("more");
    expect(root.dataset.text).toBe("200");
    expect(root.dataset.spacing).toBe("comfortable");
    expect(root.dataset.links).toBe("underline");
    expect(JSON.parse(localStorage.getItem("uione-display")!)).toMatchObject({ contrast: "more", text: "200", spacing: "comfortable", links: "underline" });
    // A legible font is loaded the first time it's asked for.
    fireEvent.click(within(panel).getByRole("radio", { name: "Atkinson Hyperlegible" }));
    expect(document.getElementById("uione-legible")?.getAttribute("href")).toContain("Atkinson+Hyperlegible");
    // Back to the system's: nothing is set on the page.
    fireEvent.click(within(panel).getByRole("button", { name: "Back to my system's" }));
    expect(root.dataset.contrast).toBeUndefined();
    expect(root.dataset.text).toBeUndefined();
    expect(within(panel).getByRole("radio", { name: "100%" })).toHaveProperty("checked", true);
  });

  it("closes with Escape, and gives focus back to its button", () => {
    render(<DisplaySettings />);
    const button = screen.getByRole("button", { name: "Display settings" });
    fireEvent.click(button);
    fireEvent.keyDown(document, { key: "Escape" });
    expect(screen.queryByRole("group", { name: "Display settings" })).toBeNull();
    expect(document.activeElement).toBe(button);
  });
});
