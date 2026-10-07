// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

import { act, fireEvent, render, screen } from "@testing-library/react";
import { vi } from "vitest";
import { App, Form, Link, Text, accentOf, memorySource, screen as defineScreen, show, useParam, useTitle, useView } from "../src/index.js";
import type { Person } from "../src/index.js";
import { useConfirmContext } from "../src/app.js";
import { withoutLicense } from "../src/components.js";
import { plain } from "../src/plain.js";

const home = defineScreen({ title: "uione", route: "/" }, () => <Text>Write the feature once.</Text>);
const studio = defineScreen({ title: "Studio", route: "/studio", nav: "Studio" }, () => <Text>Your projects.</Text>);
const docs = defineScreen({ title: "Docs", route: "/docs/:page?", nav: "Docs" }, () => <Text>The docs.</Text>);

function renderAt(location: string) {
  return render(<App name="uione" screens={[home, studio, docs]} ui={plain} data={memorySource()} location={location} />);
}

describe("an app built from screens", () => {
  it("shows the screen whose route matches the address", () => {
    renderAt("/studio");
    expect(screen.getByRole("heading", { name: "Studio" })).toBeTruthy();
    expect(screen.getByText("Your projects.")).toBeTruthy();
  });

  it("lists the screens with a nav label, and marks the current one", () => {
    renderAt("/docs/language");
    const nav = screen.getByRole("navigation");
    const links = Array.from(nav.querySelectorAll("a")).map((a) => [a.textContent, a.getAttribute("href"), a.getAttribute("aria-current")]);
    expect(links).toEqual([
      ["Studio", "/studio", null],
      ["Docs", "/docs", "page"],
    ]);
  });

  it("follows a link inside the app without reloading", () => {
    renderAt("/");
    expect(screen.getByText("Write the feature once.")).toBeTruthy();
    fireEvent.click(screen.getByRole("link", { name: "Studio" }));
    expect(screen.getByText("Your projects.")).toBeTruthy();
  });

  it("names the page after the screen", () => {
    renderAt("/studio");
    expect(document.title).toBe("Studio · uione");
  });

  it("says so when nothing is at the address", () => {
    renderAt("/nowhere");
    expect(screen.getByText("There's nothing at this address.")).toBeTruthy();
  });
});

describe("signing in", () => {
  function renderWith(person?: Person | null) {
    const source = memorySource(person === undefined ? {} : { person });
    return { source, ...render(<App name="uione" screens={[home]} ui={plain} data={source} location="/" />) };
  }

  it("isn't offered by an app without it", () => {
    renderWith();
    expect(screen.queryByRole("button", { name: "Sign in" })).toBeNull();
  });

  it("is offered to someone signed out, and shows who's signed in", async () => {
    renderWith(null);
    await act(async () => fireEvent.click(screen.getByRole("button", { name: "Sign in" })));
    expect(screen.getByText("You")).toBeTruthy();
    await act(async () => fireEvent.click(screen.getByRole("button", { name: "Sign out" })));
    expect(screen.getByRole("button", { name: "Sign in" })).toBeTruthy();
  });

  it("shows nothing until it's known whether anyone is signed in", () => {
    const person: Person | null | undefined = undefined;
    const source = memorySource({ person: null });
    source.auth!.person = () => person;
    render(<App name="uione" screens={[home]} ui={plain} data={source} location="/" />);
    expect(screen.queryByRole("button")).toBeNull();
  });
});

describe("values from a view", () => {
  it("show a date as a date, and anything else as text", () => {
    expect(show(new Date(2026, 8, 30))).toBe(new Date(2026, 8, 30).toLocaleDateString(undefined, { dateStyle: "medium" }));
    expect(show(12)).toBe("12");
    expect(show(undefined)).toBe("");
  });
});

describe("signing in that doesn't work", () => {
  function renderSignedOut(failure: unknown) {
    const source = memorySource({ person: null });
    source.auth!.signIn = async () => {
      throw failure;
    };
    render(<App name="uione" screens={[home]} ui={plain} data={source} location="/" />);
  }

  it("says what to do when the browser blocks the sign-in window", async () => {
    renderSignedOut({ code: "auth/popup-blocked" });
    fireEvent.click(screen.getByRole("button", { name: "Sign in" }));
    expect((await screen.findByRole("alert")).textContent).toContain("allow pop-ups for this site");
  });

  it("says nothing when the person closes the sign-in window", async () => {
    renderSignedOut({ code: "auth/popup-closed-by-user" });
    await act(async () => fireEvent.click(screen.getByRole("button", { name: "Sign in" })));
    expect(screen.queryByRole("alert")).toBeNull();
  });
});

describe("questions before a command", () => {
  it("answers no to a question that a newer one replaces, so nothing waits on it forever", async () => {
    const answers: boolean[] = [];
    function Asker() {
      const { ask } = useConfirmContext();
      return (
        <button type="button" onClick={() => void ask("first?").then((yes) => answers.push(yes))}>
          ask
        </button>
      );
    }
    function Second() {
      const { ask } = useConfirmContext();
      return (
        <button type="button" onClick={() => void ask("second?").then((yes) => answers.push(yes))}>
          ask again
        </button>
      );
    }
    const page = defineScreen({ title: "Test", route: "/" }, () => (
      <>
        <Asker />
        <Second />
      </>
    ));
    render(<App name="uione" screens={[page]} ui={plain} data={memorySource()} location="/" />);
    fireEvent.click(screen.getByRole("button", { name: "ask" }));
    fireEvent.click(screen.getByRole("button", { name: "ask again" }));
    await act(async () => fireEvent.click(screen.getByRole("button", { name: "Yes" })));
    expect(answers).toEqual([false, true]);
  });
});

describe("the app's icon", () => {
  it("is shown beside its name, and only when it has one", () => {
    const { unmount } = render(<App name="uione" icon="/icon.svg" screens={[home]} ui={plain} data={memorySource()} location="/" />);
    expect(screen.getByRole("link", { name: "uione" }).querySelector("img")?.getAttribute("src")).toBe("/icon.svg");
    unmount();
    render(<App name="uione" screens={[home]} ui={plain} data={memorySource()} location="/" />);
    expect(screen.getByRole("link", { name: "uione" }).querySelector("img")).toBeNull();
  });
});

describe("code shown on a page", () => {
  it("leaves out the license header of the file it comes from", () => {
    const file = "// Copyright 2026 Someone\n// SPDX-License-Identifier: AGPL-3.0-only\n\nnamespace library {\n  entity book {}\n}\n";
    expect(withoutLicense(file)).toBe("namespace library {\n  entity book {}\n}\n");
    expect(withoutLicense("namespace library {}\n")).toBe("namespace library {}\n");
    expect(withoutLicense("// Copyright, but no license after it\nx\n")).toBe("// Copyright, but no license after it\nx\n");
  });
});


describe("a screen for one thing, opened for another", () => {
  it("starts over, so what was typed about one is never sent as the other's", async () => {
    const source = memorySource();
    source.set("library::book_page", { title: "Dune" }, "a");
    source.set("library::book_page", { title: "Emma" }, "b");
    function Book() {
      const book = useParam("book");
      return (
        <>
          <Form command="library::book::update" fields={["title"]} from={useView("library::book_page", book)} id={book} />
          <Link to="/books/b">Next book</Link>
        </>
      );
    }
    const page = defineScreen({ title: "Book", route: "/books/:book" }, () => <Book />);
    render(<App name="app" screens={[page]} ui={plain} data={source} location="/books/a" />);
    fireEvent.change(screen.getByLabelText("Title"), { target: { value: "Dune, edited" } });
    fireEvent.click(screen.getByRole("link", { name: "Next book" }));
    await act(async () => fireEvent.click(screen.getByRole("button", { name: "Update" })));
    expect(source.runs).toEqual([{ command: "library::book::update", input: { title: "Emma", id: "b" } }]);
  });
});

describe("a screen that breaks while it's drawn", () => {
  it("shows a short message, and leaves the rest of the app working", () => {
    vi.spyOn(console, "error").mockImplementation(() => {});
    function Broken(): never {
      throw new Error("a bug in the screen");
    }
    const broken = defineScreen({ title: "Broken", route: "/broken", nav: "Broken" }, () => <Broken />);
    render(<App name="uione" screens={[home, studio, broken]} ui={plain} data={memorySource()} location="/broken" />);
    expect(screen.getByText(/Something went wrong showing this page/)).toBeTruthy();
    fireEvent.click(screen.getByRole("link", { name: "Studio" }));
    expect(screen.getByText("Your projects.")).toBeTruthy();
    vi.restoreAllMocks();
  });

  it("offers no sign-in when the project names no way of signing in", () => {
    const home = defineScreen({ title: "Home", route: "/" }, () => <Text>hello</Text>);
    const { unmount } = render(<App name="site" screens={[home]} ui={plain} data={memorySource({ person: null })} location="/" />);
    expect(screen.getByRole("button", { name: "Sign in" })).toBeTruthy();
    unmount();
    render(<App name="site" screens={[home]} ui={plain} data={memorySource({ person: null })} location="/" authentication={false} />);
    expect(screen.queryByRole("button", { name: "Sign in" })).toBeNull();
  });
});

describe("counting visitors", () => {
  // The browser's storage, where the visitor's answer is kept.
  const kept = new Map<string, string>();
  vi.stubGlobal("localStorage", {
    getItem: (k: string) => kept.get(k) ?? null,
    setItem: (k: string, v: string) => void kept.set(k, v),
    removeItem: (k: string) => void kept.delete(k),
    clear: () => kept.clear(),
  });

  function counter() {
    const told: string[] = [];
    return { told, analytics: { consent: (agreed: boolean) => told.push(`consent ${agreed}`), page: (path: string) => told.push(`page ${path}`) } };
  }

  it("counts each screen, asks once whether it may use cookies, and keeps the answer", async () => {
    localStorage.clear();
    const { told, analytics } = counter();
    const first = render(<App name="uione" screens={[home, studio, docs]} ui={plain} data={memorySource()} location="/" analytics={analytics} />);
    await act(async () => {
      await new Promise((resolve) => setTimeout(resolve, 5));
    });
    expect(told).toEqual(["page /"]); // counted without cookies, before any answer
    fireEvent.click(screen.getByRole("link", { name: "Studio" }));
    await act(async () => {
      await new Promise((resolve) => setTimeout(resolve, 5));
    });
    expect(told).toEqual(["page /", "page /studio"]);
    fireEvent.click(screen.getByRole("button", { name: "Allow" }));
    expect(told).toContain("consent true");
    expect(screen.queryByRole("complementary", { name: "Counting visits" })).toBeNull();
    first.unmount();

    // The next visit isn't asked again, and starts with the answer it gave.
    const again = counter();
    render(<App name="uione" screens={[home]} ui={plain} data={memorySource()} location="/" analytics={again.analytics} />);
    expect(screen.queryByRole("complementary", { name: "Counting visits" })).toBeNull();
    expect(again.told[0]).toBe("consent true");
  });

  it("asks no one, and counts no one, without analytics", () => {
    localStorage.clear();
    render(<App name="uione" screens={[home]} ui={plain} data={memorySource()} location="/" />);
    expect(screen.queryByRole("complementary", { name: "Counting visits" })).toBeNull();
  });
});

describe("titles made from what a page shows", () => {
  it("has none until its views arrive, then shows them, in the page and its tab", () => {
    const source = memorySource();
    const issue = defineScreen({ title: "", route: "/issues/:issue" }, () => {
      const page = useView("tracker::issue_page", useParam("issue"));
      useTitle(["#", [page, "number"], " ", [page, "title"]]);
      return <Text>body</Text>;
    });
    render(<App name="tracker" screens={[issue]} ui={plain} data={source} location="/issues/12" />);
    expect(document.title).toBe("tracker");
    expect(screen.queryByRole("heading", { level: 1 })?.textContent ?? "").toBe("");
    act(() => source.set("tracker::issue_page", { number: 12, title: "Tabs are too wide" }, "12"));
    expect(screen.getByRole("heading", { level: 1 }).textContent).toBe("#12 Tabs are too wide");
    expect(document.title).toBe("#12 Tabs are too wide · tracker");
  });
});

describe("a site's own color", () => {
  it("draws buttons and links in it, with text that reads on it, lighter on a dark page", () => {
    const teal = accentOf("#0f766e");
    expect(teal).toContain(":root { --color-accent: #0f766e;");
    expect(teal).toContain("--color-accent-ink: #ffffff;");
    expect(teal).toContain(':root[data-theme="dark"] { --color-accent: color-mix(in oklab, #0f766e 55%, white);');
    expect(accentOf("#fde047")).toContain("--color-accent-ink: #0e1726;");
    expect(accentOf("red; } body { display: none")).toBe("");
  });
});
