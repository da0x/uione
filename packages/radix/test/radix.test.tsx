// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

import { act, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { vi } from "vitest";
import { existsSync, readFileSync } from "node:fs";
import { resolve } from "node:path";
import type { ReactNode } from "react";
import { useState } from "react";
import {
  App,
  Board,
  Cards,
  Code,
  Command,
  DatedTimeline,
  Confirm,
  Form,
  Hero,
  Link,
  Live,
  Pages,
  Section,
  Table,
  Text,
  Thread,
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

  it("has a labeled close button that closes it without running anything", async () => {
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

describe("cards", () => {
  it("name each card in a heading set in the text's face, not the headings'", () => {
    const source = memorySource();
    source.set("studio::room_page", { pieces: [{ id: "c1", name: "Harbor" }] });
    function Pieces() {
      const page = useView("studio::room_page");
      return <Cards view={page} list="pieces" columns={{ name: "Name" }} />;
    }
    renderScreen(() => <Pieces />, source);
    const title = screen.getByRole("heading", { name: "Harbor" });
    expect(title.tagName).toBe("H2");
    expect(title.className.split(" ")).toContain("font-sans");
  });

  it("set as a mosaic, fill each card's tile with its picture, its title over its foot, a heading in the text's face", async () => {
    const source = memorySource();
    source.set("studio::room_page", { pieces: Array.from({ length: 5 }, (_, i) => ({ id: `c${i + 1}`, name: `Clipping ${i + 1}` })) });
    function Pieces() {
      const page = useView("studio::room_page");
      return <Cards view={page} list="pieces" columns={{ name: "Name" }} mosaic />;
    }
    renderScreen(() => <Pieces />, source);
    const tiles = screen.getAllByRole("article");
    expect(tiles.length).toBe(5);
    expect([tiles[1]!.style.gridColumn, tiles[1]!.style.gridRow]).toEqual(["2 / span 2", "1 / span 1"]);
    expect([tiles[4]!.style.gridColumn, tiles[4]!.style.gridRow]).toEqual(["3 / span 1", "3 / span 2"]);
    const title = within(tiles[0]!).getByRole("heading", { name: "Clipping 1" });
    expect(title.className.split(" ")).toContain("font-sans");
  });
});

describe("cards under a heading for each month", () => {
  it("head each in the text's face, strong and larger, each part laid out only near the view, and a timeline of them moved through by its keys", async () => {
    const source = memorySource();
    const at = (m: number, d: number) => new Date(Date.UTC(2026, m - 1, d, 12));
    source.set("studio::room_page", { pieces: [at(10, 30), at(10, 2), at(9, 14), at(9, 3), at(9, 1)].map((when, i) => ({ id: `c${i + 1}`, name: `Clipping ${i + 1}`, picture_date: when })) });
    function Pieces() {
      const page = useView("studio::room_page");
      return (
        <>
          <Cards view={page} list="pieces" columns={{ name: "Name" }} mosaic by={{ field: "picture_date", unit: "month" }} name="clippings" />
          <DatedTimeline view={page} list="pieces" by="picture_date" cards="clippings" />
        </>
      );
    }
    renderScreen(() => <Pieces />, source);
    const september = screen.getByRole("region", { name: "September 2026" });
    const heading = within(september).getByRole("heading", { name: "September 2026" });
    expect(heading.className.split(" ")).toEqual(expect.arrayContaining(["font-sans", "font-semibold", "text-2xl"]));
    expect(september.style.contentVisibility).toBe("auto");
    const tiles = within(september).getAllByRole("article");
    expect([tiles[0]!.style.gridColumn, tiles[0]!.style.gridRow]).toEqual(["1 / span 1", "1 / span 2"]);
    // The timeline, a strip down the side that's a slider to the keyboard, its year said on it.
    const ruler = screen.getByRole("slider", { name: "Timeline" });
    expect(ruler.hasAttribute("data-strip")).toBe(true);
    expect(ruler.className).toContain("focus-visible:ring-2");
    expect(ruler.textContent).toBe("2026");
    // jsdom lays nothing out, so every day is at its top: the last of them is said there.
    expect(ruler.getAttribute("aria-valuetext")).toBe("September 1, 2026");
    // Pressed, the day there is said beside it, until it's let go away from it.
    if (!globalThis.PointerEvent) globalThis.PointerEvent = class extends MouseEvent {} as unknown as typeof PointerEvent;
    fireEvent.pointerDown(ruler, { button: 0, clientX: 0, clientY: 0, pointerId: 1 });
    expect(ruler.textContent).toBe("2026September 1, 2026");
    fireEvent.pointerUp(ruler, { button: 0, clientX: -50, clientY: 0, pointerId: 1 });
    expect(ruler.textContent).toBe("2026");
    fireEvent.keyDown(ruler, { key: "End" });
  });
});

describe("a workspace's side", () => {
  it("is a panel beside the work, or, holding only a strip like a timeline, that strip alone at its edge", () => {
    const Layout = radix.Layout;
    const drawn = render(<Layout name="workspace" regions={{ main: <p>The work</p>, side: <div data-strip="" /> }} />);
    const side = drawn.container.querySelector("aside")!;
    expect(side.className.split(" ")).toEqual(expect.arrayContaining(["w-72", "has-[>[data-strip]:only-child]:w-auto", "has-[>[data-strip]:only-child]:border-0"]));
  });
});

describe("a workspace that's only its picture", () => {
  it("draws its picture on black across all of the window, its bars over it while they're awake, and hidden but there while they're not, nor the pointer", () => {
    const Layout = radix.Layout;
    const regions = { main: <p>The picture</p>, toolbar: <button type="button">Next</button>, status: <span>Harbor</span> };
    const drawn = render(<Layout name="workspace" regions={regions} alone={{ awake: true }} />);
    const window = drawn.container.firstElementChild as HTMLElement;
    expect(window.className.split(" ")).toContain("bg-black");
    expect(screen.getByText("The picture").closest("main")!.className).toContain("absolute inset-0");
    const bars = screen.getByRole("toolbar").parentElement!;
    expect(bars.className.split(" ")).not.toContain("invisible");
    expect(screen.getByRole("toolbar").className).toContain("bg-surface/85");
    drawn.rerender(<Layout name="workspace" regions={regions} alone={{ awake: false }} />);
    expect(screen.getByRole("toolbar", { hidden: true }).parentElement!.className.split(" ")).toContain("invisible");
    expect(screen.getByText("Next")).toBeTruthy();
    expect(window.style.cursor).toBe("none");
  });

  it("keeps an open drawer shown over it while its bars rest", () => {
    const Layout = radix.Layout;
    const regions = { main: <p>The picture</p>, toolbar: <button type="button">About</button>, side: <span>1.5 MB</span> };
    const drawn = render(<Layout name="workspace" regions={regions} alone={{ awake: false }} />);
    expect(screen.getByText("1.5 MB").closest("aside")!.className.split(" ")).not.toContain("visible");
    drawn.rerender(<Layout name="workspace" regions={regions} alone={{ awake: false, drawers: ["side"] }} />);
    expect(screen.getByText("1.5 MB").closest("aside")!.className.split(" ")).toContain("visible");
    expect(screen.getByRole("toolbar", { hidden: true }).parentElement!.className.split(" ")).toContain("invisible");
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

  it("sets $…$ as math in its line, an equation alone and a math block on their own lines, and leaves money as words", () => {
    const { container } = render(<MarkdownText source={"Roots of $x^2 - 1$ cost $5 and $10.\n\n$a / b$\n\n```math\nsqrt(2)\n```"} />);
    const shown = [...container.querySelectorAll("svg[role=img]")].map((svg) => svg.getAttribute("aria-label"));
    expect(shown).toEqual(["x^2 - 1", "a / b", "sqrt(2)"]);
    expect(container.querySelector("p")?.textContent).toContain("cost $5 and $10.");
    expect(container.querySelectorAll(".one-math")).toHaveLength(2);
    expect(container.querySelector("pre")).toBeNull();
  });

  it("colors a fenced block's code as its language reads it, and leaves one in a language it doesn't know as it is", () => {
    const { container } = render(<MarkdownText source={"```cpp\nint main() { return 0; }\n```\n\n```brainfuck\n+[-]\n```"} />);
    const colored = [...container.querySelectorAll<HTMLElement>("pre code span")].map((s) => [s.textContent, s.style.color]);
    expect(colored).toEqual([
      ["int", "var(--syntax-type)"],
      ["main", "var(--syntax-function)"],
      ["return", "var(--syntax-keyword)"],
      ["0", "var(--syntax-number)"],
    ]);
    expect(container.querySelectorAll("pre")[1]?.textContent).toBe("+[-]\n");
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

describe("a card's progress", () => {
  it("shows while its holder works it, and not once it's let go or moved on", () => {
    const later = new Date(Date.now() + 60_000).toISOString();
    const source = memorySource({
      views: {
        "projects::board_page": {
          stages: [{ id: "ark-deploying", title: "Deploying" }, { id: "ark-live", title: "Live" }],
          issues: [
            { id: "ark-1", title: "studio", stage: "ark-deploying", held_by: "deployer", held_until: later, data: { step: 2, steps: 5 } },
            { id: "ark-2", title: "tracker", stage: "ark-live", data: { step: 4, steps: 5 } },
          ],
        },
      },
    });
    renderScreen(() => {
      const page = useView("projects::board_page");
      return <Board view={page} list="issues" by="stage" over={page} overList="stages" columns={{ title: "Title" }} />;
    }, source);
    expect(screen.getByRole("img", { name: "step 2 of 5" })).toBeTruthy();
    // Its holder's last word from the stage it left isn't how far it is in this one.
    expect(screen.queryByRole("img", { name: "step 4 of 5" })).toBeNull();
  });
});

describe("a thread of comments", () => {
  it("draws each comment as one box, its text inside it with no frame of its own", () => {
    const source = memorySource({
      views: { "tracker::issue_page": { comments: [{ id: "c1", "author.name": "Ann", body: "Looks **good**.", created_at: "2026-10-17T10:00:00Z" }] } },
    });
    renderScreen(() => <Thread view={useView("tracker::issue_page")} list="comments" />, source);
    const comment = screen.getByRole("article");
    expect(within(comment).getByText("good").tagName).toBe("STRONG");
    // The comment is the box; nothing inside it draws a border and a card of its own.
    expect(comment.querySelector(".rounded-box, .shadow-panel")).toBeNull();
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
        <radix.Link href="https://github.com/someone/tracker" external>
          someone/tracker
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

  it("shows a board's stages a page at a time, and moves a card with a picker", () => {
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
    const stages = screen.getByRole("tablist", { name: "Stages" });
    expect(within(stages).getAllByRole("tab").map((tab) => tab.textContent)).toEqual(["To Do1", "Doing0"]);
    fireEvent.click(within(stages).getByRole("tab", { name: "Doing 0" }));
    expect(within(stages).getByRole("tab", { name: "Doing 0" }).getAttribute("aria-selected")).toBe("true");
    fireEvent.change(screen.getByLabelText("Move Copy an issue whole"), { target: { value: "doing" } });
    expect(onMove).toHaveBeenCalledWith("a", "doing");
    vi.unstubAllGlobals();
  });
});

describe("a board's cards, as the relay front page draws them", () => {
  it("say how urgent they are by their edge alone, and who's working each by face, apart from the rest", () => {
    const Board = radix.Board;
    render(
      <Board
        status="live"
        columns={[
          {
            id: "todo",
            title: "To Do",
            cards: [
              {
                id: "a",
                title: "Proof the menu",
                details: ["#12", "bug"],
                people: [{ name: "Ada Lovelace", picture: "https://example.com/ada.png" }, { name: "Grace Hopper" }],
                reaches: [],
                tone: 1,
              },
            ],
          },
        ]}
      />,
    );
    const card = screen.getByRole("region", { name: "To Do" }).querySelector("li")!;
    // The card stays the page's own color; only its edge is the tone's.
    expect(card.className).not.toMatch(/bg-tone/);
    expect(card.querySelector("[data-edge]")?.getAttribute("style")).toContain("--color-tone-1-edge");
    expect(card.querySelector("[data-details]")?.textContent).toBe("#12bug");
    // Each person a face: their picture, or their initial, named for whoever can't see it.
    const people = card.querySelector("[data-people]")!;
    expect(people.querySelector("img")?.getAttribute("src")).toBe("https://example.com/ada.png");
    expect(within(people as HTMLElement).getByText("G")).toBeTruthy();
    expect(within(people as HTMLElement).getByTitle("Grace Hopper")).toBeTruthy();
    expect(people.textContent).toContain("Ada Lovelace");
  });

  it("colors a table's rows by their edge alone", () => {
    const Table = radix.Table;
    render(<Table status="live" columns={["Title"]} rows={[{ id: "a", cells: ["Proof the menu"], actions: [], tone: 2 }]} />);
    const row = screen.getByRole("row", { name: /Proof the menu/ });
    expect(row.className).not.toMatch(/bg-tone/);
    expect(row.className).toMatch(/--color-tone-2-edge/);
  });
});

describe("icons", () => {
  it("draws a link as an icon, still named by its words", () => {
    const Link = radix.Link;
    render(
      <Link href="/tracker/boards/main/workflow" icon="workflow">
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
    // It opens as everything over the page does, in a dialog.
    const panel = screen.getByRole("dialog", { name: "Display settings" });
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

  it("closes with Escape, and gives focus back to its button", async () => {
    render(<DisplaySettings />);
    const button = screen.getByRole("button", { name: "Display settings" });
    button.focus();
    fireEvent.click(button);
    fireEvent.keyDown(document.activeElement!, { key: "Escape" });
    expect(screen.queryByRole("dialog", { name: "Display settings" })).toBeNull();
    // Given back once it's closed.
    await waitFor(() => expect(document.activeElement).toBe(button));
  });
});

describe("a dialog", () => {
  // The z-index a Tailwind class like z-20 gives.
  const layer = (element: Element | null) => Number(/(?:^|\s)z-(\d+)(?:\s|$)/.exec(element?.getAttribute("class") ?? "")?.[1] ?? 0);

  it("opens above the page's header, its background too, so its × can be reached on a phone", () => {
    renderScreen(() => (
      <radix.Dialog open title="New project" onClose={() => {}}>
        <p>Its name.</p>
      </radix.Dialog>
    ));
    // Hidden from the accessibility tree while the dialog is open, as it should be.
    const header = screen.getByRole("banner", { hidden: true });
    const dialog = screen.getByRole("dialog", { name: "New project" });
    const overlay = document.querySelector("[data-state='open']:not([role='dialog'])");
    expect(layer(header)).toBeGreaterThan(0);
    expect(layer(dialog)).toBeGreaterThan(layer(header));
    expect(layer(overlay)).toBeGreaterThan(layer(header));
  });

  it("is the whole screen on a phone, and a dialog in the middle of a wider one", () => {
    renderScreen(() => (
      <radix.Dialog open title="New project" onClose={() => {}}>
        <p>Its name.</p>
      </radix.Dialog>
    ));
    const classes = screen.getByRole("dialog", { name: "New project" }).getAttribute("class")!.split(/\s+/);
    expect(classes).toEqual(expect.arrayContaining(["inset-0", "h-[100dvh]", "w-full", "sm:top-1/2", "sm:left-1/2", "sm:w-[min(28rem,calc(100vw-2rem))]"]));
  });

  it("keeps its title and × in sight, and scrolls only what's under them", () => {
    renderScreen(() => (
      <radix.Dialog open title="New project" onClose={() => {}}>
        <p>Its name.</p>
      </radix.Dialog>
    ));
    const dialog = screen.getByRole("dialog", { name: "New project" });
    const scrolls = Array.from(dialog.querySelectorAll(".overflow-y-auto"));
    expect(scrolls).toHaveLength(1);
    expect(scrolls[0]!.textContent).toContain("Its name.");
    expect(within(scrolls[0] as HTMLElement).queryByRole("heading", { name: "New project" })).toBeNull();
    expect(within(scrolls[0] as HTMLElement).queryByRole("button", { name: "Close" })).toBeNull();
  });
});

describe("a file field", () => {
  it("asks for a file, shows it on its way up, and offers to take it away", async () => {
    let arrive: (path: string) => void = () => {};
    const source = memorySource({
      files: {
        upload: (_file, progress) => {
          progress(0.25);
          return new Promise((done) => (arrive = done));
        },
      },
    });
    renderScreen(() => <Form command="press::order::create" fields={[{ name: "proof", type: "file" }]} />, source);
    expect(screen.getByText("Choose a file")).toBeTruthy();
    await act(async () => void fireEvent.change(screen.getByLabelText("Proof"), { target: { files: [new File(["%PDF"], "proof.pdf")] } }));
    const progress = screen.getByRole("progressbar", { name: "proof.pdf, uploading" }) as HTMLProgressElement;
    expect(progress.value).toBe(0.25);
    await act(async () => arrive("files/you/1/proof.pdf"));
    expect(screen.queryByRole("progressbar")).toBeNull();
    expect(screen.getByText("Choose another")).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "Remove proof.pdf" }));
    expect(screen.queryByText("proof.pdf")).toBeNull();
  });
});

describe("a front page in relay", () => {
  const Landing = radix.Landing;
  const page = (design: string) => (
    <Landing
      design={design}
      name="Press"
      title="Print that arrives on time."
      text={["Orders, proofs and shipping for a print shop."]}
      links={[{ href: "/signin", label: "Sign in" }]}
      features={[{ title: "You approve every proof", text: "Nothing goes to press unseen.", features: [] }]}
      steps={[]}
      asks={[{ title: "Ask for a quote", id: "quote", children: <p>Leave your address.</p> }]}
      before={[<table key="orders" aria-label="Your orders" />]}
    />
  );
  const follows = (first: Element, second: Element) => Boolean(first.compareDocumentPosition(second) & Node.DOCUMENT_POSITION_FOLLOWING);

  it("puts the screen's other items, like a table, under the hero's board, where the other designs put them first", () => {
    const { unmount } = render(page("relay"));
    const title = screen.getByRole("heading", { level: 1, name: "Print that arrives on time." });
    expect(screen.getByRole("link", { name: "Sign in" }).getAttribute("href")).toBe("/signin");
    const orders = screen.getByRole("table", { name: "Your orders" });
    expect(follows(title, orders)).toBe(true);
    expect(follows(orders, screen.getByRole("heading", { level: 2, name: "You approve every proof" }))).toBe(true);
    expect(document.getElementById("quote")?.textContent).toContain("Leave your address.");
    unmount();
    render(page("grid"));
    expect(follows(screen.getByRole("table", { name: "Your orders" }), screen.getByRole("heading", { level: 1 }))).toBe(true);
  });

  it("draws a board whose cards are what the site offers, one worked by an agent, hidden from a screen reader", () => {
    const { container } = render(page("relay"));
    const board = container.querySelector(".one-landing [aria-hidden=true]")!;
    expect(board.textContent).toContain("You approve every proof");
    expect(board.textContent).toContain("Review");
    expect(board.querySelector(".one-landing-carry")?.textContent).toContain("agent");
  });

  it("moves its card once, and not for whoever asks for less motion", () => {
    const text = readFileSync(resolve(process.cwd(), "dist/styles.css"), "utf8");
    expect(text).toMatch(/\.one-landing-carry\{animation:[^}]*1\.2s both one-landing-carry\}/);
    expect(text).toMatch(/prefers-reduced-motion:\s*reduce\)\{\.one-landing \[class\*=one-landing-\]\{animation:none\}/);
  });
});

describe("a front page's new parts", () => {
  const Landing = radix.Landing;
  const page = (design: string) => (
    <Landing
      design={design}
      name="xr soft"
      overline="Software modernization"
      title="Modern software, without stopping the business."
      text={[]}
      links={[{ href: "#talk", label: "Talk to us" }]}
      features={[
        {
          title: "Five pillars.",
          overline: "What we do",
          text: "Ten services.",
          features: [{ title: "Data", icon: "database", text: "Moved and put to work.", features: [{ title: "Data Migration", text: "Moved safely.", features: [] }] }],
        },
      ]}
      statement="We keep what your systems know."
      steps={[{ title: "One step at a time.", overline: "How we work", text: [], steps: [{ title: "Understand", text: "We learn." }, { title: "Move", text: "A piece at a time." }] }]}
      asks={[{ title: "Start with a conversation.", id: "talk", overline: "Contact", children: <a href="mailto:hello@example.com">hello@example.com</a> }]}
    />
  );

  it("are drawn by every design: the overlines, a group's title over its offerings, what an offering holds, the statement and the steps", () => {
    for (const design of ["waveform", "studio", "aurora", "grid", "relay", "constellation"]) {
      const { container, unmount } = render(page(design));
      const said = container.textContent ?? "";
      for (const words of ["Software modernization", "What we do", "Five pillars.", "Data Migration", "We keep what your systems know.", "How we work", "Understand", "Contact"]) {
        expect([design, said.includes(words)]).toEqual([design, true]);
      }
      expect(container.querySelector("svg")).toBeTruthy();
      unmount();
    }
  });

  it("opens what an offering links to from anywhere on it, in every design", () => {
    const linked = (design: string) => (
      <Landing
        design={design}
        name="apps"
        title="The apps."
        text={[]}
        links={[]}
        features={[
          { title: "The apps", text: [], features: [{ title: "mural", icon: "layers", text: "Pictures and words.", features: [], link: { href: "https://mural.example.com", external: true, label: "Open mural" } }] },
          { title: "notes", icon: "text", text: "Words.", features: [], link: { href: "/notes", label: "Open notes" } },
        ]}
        steps={[]}
        asks={[]}
      />
    );
    for (const design of ["folio", "waveform", "studio", "aurora", "grid", "relay", "constellation"]) {
      const { unmount } = render(linked(design));
      for (const [name, href] of [["mural", "https://mural.example.com"], ["notes", "/notes"]] as const) {
        const link = screen.queryByRole("link", { name });
        expect([design, name, link !== null]).toEqual([design, name, true]);
        if (!link) continue;
        expect([design, link.getAttribute("href")]).toEqual([design, href]);
        // Stretched over whatever holds it, which is placed for it.
        expect(link.className).toContain("after:inset-0");
        expect([design, link.parentElement?.closest(".relative, .one-constellation-card, [style*=relative]") !== null]).toEqual([design, true]);
      }
      expect(screen.getByRole("link", { name: "mural" }).getAttribute("target")).toBe("_blank");
      unmount();
    }
  });

  it("in constellation, names the site in two tones, lights its statement a word at a time, and numbers its steps", () => {
    const { container } = render(page("constellation"));
    expect(screen.getByRole("heading", { level: 1 }).getAttribute("aria-label")).toBe("xr soft");
    expect(container.querySelector(".one-constellation-first")?.textContent).toBe("xr");
    expect(container.querySelector(".one-constellation-rest")?.textContent).toBe("soft");
    expect(container.querySelectorAll(".one-constellation-statement span")).toHaveLength(6);
    expect(Array.from(container.querySelectorAll(".one-constellation-step-number")).map((n) => n.textContent)).toEqual(["01", "02"]);
    expect(container.querySelector(".one-constellation-band")?.textContent).toContain("Data");
    expect(document.getElementById("talk")?.querySelector("a")?.getAttribute("href")).toBe("mailto:hello@example.com");
  });
});

describe("a workspace's toolbar", () => {
  const Group = radix.Group;
  const tools = (n: number) => Array.from({ length: n }, (_, at) => <button key={at} type="button">{at}</button>);

  it("marks a group that's a single row of small tools, and none that's more", () => {
    const { container } = render(
      <>
        <Group name="Reading" small across={4}>{tools(4)}</Group>
        <Group name="View" small across={2}>{tools(2)}</Group>
        <Group name="Align" small>{tools(2)}</Group>
        <Group name="Style">{tools(2)}</Group>
      </>,
    );
    const marked = Array.from(container.querySelectorAll("[role=group]")).map((g) => [g.getAttribute("aria-label"), g.hasAttribute("data-one-row")]);
    expect(marked).toEqual([["Reading", true], ["View", true], ["Align", false], ["Style", false]]);
  });

  it("is a thin strip, its groups' names hidden, only while every group is a single row of small tools", () => {
    const text = readFileSync(resolve(process.cwd(), "dist/styles.css"), "utf8");
    expect(text).toContain(":not(:has(>[role=group]:not([data-one-row])))>[role=group]>span{display:none}");
  });
});
