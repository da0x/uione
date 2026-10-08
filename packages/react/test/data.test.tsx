// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

import { act, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { useState } from "react";
import type { ReactNode } from "react";
import { useLocation } from "react-router";
import {
  App,
  Command,
  Confirm,
  Details,
  Form,
  Live,
  Table,
  Text,
  Thread,
  Timeline,
  changed,
  done,
  holds,
  keptByTime,
  markdownOf,
  phrase,
  memorySource,
  shortAddress,
  screen as defineScreen,
  useParam,
  useView,
} from "../src/index.js";
import type { DataSource, MemorySource, Person, ViewState } from "../src/index.js";
import { plain } from "../src/plain.js";

function renderScreen(source: MemorySource, body: () => ReactNode) {
  const only = defineScreen({ title: "Test", route: "/" }, body);
  return render(<App name="app" screens={[only]} ui={plain} data={source} location="/" />);
}

function Count() {
  const signups = useView("waitlist::signups");
  return (
    <Text>
      <Live view={signups} field="total" /> waiting
    </Text>
  );
}

describe("live views", () => {
  it("shows a value only while its view is live", () => {
    const source = memorySource();
    renderScreen(source, () => <Count />);
    const placeholder = () => screen.queryByText("not available right now");

    expect(placeholder()).toBeTruthy(); // loading: nothing has arrived

    act(() => source.set("waitlist::signups", { total: 12 }));
    expect(screen.getByText("12")).toBeTruthy();
    expect(placeholder()).toBeNull();

    act(() => source.status("waitlist::signups", "stale"));
    expect(screen.queryByText("12")).toBeNull(); // an old number that looks current is worse than none
    expect(placeholder()).toBeTruthy();

    act(() => source.set("waitlist::signups", { total: 13 }));
    expect(screen.getByText("13")).toBeTruthy();

    act(() => source.status("waitlist::signups", "denied"));
    expect(screen.queryByText("13")).toBeNull();
  });

  it("reads one document of a view that has one per person", () => {
    const source = memorySource();
    function Mine() {
      const mine = useView("library::mine", "reader-1");
      return <Live view={mine} field="count" />;
    }
    renderScreen(source, () => <Mine />);
    act(() => source.set("library::mine", { count: 2 }, "reader-2"));
    expect(screen.queryByText("2")).toBeNull();
    act(() => source.set("library::mine", { count: 3 }, "reader-1"));
    expect(screen.getByText("3")).toBeTruthy();
  });

  it("starts a view opened again from what it last showed, for the same person only", () => {
    // A source whose views answer only when told to, as a network does.
    let person: Person | null = { uid: "ada", name: "Ada" };
    const answers: ((state: ViewState) => void)[] = [];
    const slow: DataSource = {
      subscribe(_view, _subject, emit) {
        answers.push(emit);
        return () => {};
      },
      run: async () => {},
      auth: { person: () => person, watch: () => () => {}, signIn: async () => {}, signOut: async () => {} },
    };
    const first = renderScreen(slow as MemorySource, () => <Count />);
    expect(screen.queryByText("12")).toBeNull();
    act(() => answers.at(-1)!({ status: "live", data: { total: 12 } }));
    expect(screen.getByText("12")).toBeTruthy();
    first.unmount();

    const again = renderScreen(slow as MemorySource, () => <Count />);
    expect(screen.getByText("12")).toBeTruthy(); // before the view answers again
    act(() => answers.at(-1)!({ status: "live", data: { total: 13 } }));
    expect(screen.getByText("13")).toBeTruthy();
    again.unmount();

    person = { uid: "grace", name: "Grace" };
    renderScreen(slow as MemorySource, () => <Count />);
    expect(screen.queryByText("13")).toBeNull(); // what Ada saw isn't Grace's
  });
});

describe("commands", () => {
  it("runs a form's command with what was typed, then clears it", async () => {
    const source = memorySource();
    renderScreen(source, () => <Form command="waitlist::signup::create" fields={[{ name: "email", type: "email" }]} />);
    const email = screen.getByLabelText("Email") as HTMLInputElement;
    fireEvent.change(email, { target: { value: "ada@example.com" } });
    fireEvent.click(screen.getByRole("button", { name: "Create" }));
    await waitFor(() => expect(source.runs).toEqual([{ command: "waitlist::signup::create", input: { email: "ada@example.com" } }]));
    await waitFor(() => expect(email.value).toBe(""));
  });

  it("shows why a command failed, and keeps what was typed", async () => {
    const source = memorySource({
      commands: {
        "waitlist::signup::create": () => {
          throw new Error("that isn't an email address");
        },
      },
    });
    renderScreen(source, () => <Form command="waitlist::signup::create" fields={["email"]} />);
    const email = screen.getByLabelText("Email") as HTMLInputElement;
    fireEvent.change(email, { target: { value: "not an email" } });
    fireEvent.click(screen.getByRole("button", { name: "Create" }));
    expect((await screen.findByRole("alert")).textContent).toBe("that isn't an email address");
    expect(email.value).toBe("not an email");
  });

  it("opens a form in a dialog when it has a button", async () => {
    const source = memorySource();
    renderScreen(source, () => <Form command="studio::project::create" fields={["name"]} button />);
    expect(screen.queryByRole("dialog")).toBeNull();
    fireEvent.click(screen.getByRole("button", { name: "Create" }));
    expect(screen.getByRole("dialog", { name: "Create" })).toBeTruthy();
    fireEvent.change(screen.getByLabelText("Name"), { target: { value: "uione.io" } });
    fireEvent.submit(screen.getByLabelText("Name").closest("form")!);
    await waitFor(() => expect(screen.queryByRole("dialog")).toBeNull());
    expect(source.runs[0]).toEqual({ command: "studio::project::create", input: { name: "uione.io" } });
  });

  it("asks first when the screen has a Confirm for the command, and does nothing on cancel", async () => {
    const source = memorySource();
    renderScreen(source, () => (
      <>
        <Command name="library::book::withdraw" />
        <Confirm command="library::book::withdraw" question="Withdraw it?" />
      </>
    ));
    fireEvent.click(screen.getByRole("button", { name: "Withdraw" }));
    expect(screen.getByRole("dialog").textContent).toContain("Withdraw it?");
    fireEvent.click(screen.getByRole("button", { name: "Cancel" }));
    await waitFor(() => expect(screen.queryByRole("dialog")).toBeNull());
    expect(source.runs).toEqual([]);

    fireEvent.click(screen.getByRole("button", { name: "Withdraw" }));
    fireEvent.click(screen.getByRole("button", { name: "Yes" }));
    await waitFor(() => expect(source.runs).toEqual([{ command: "library::book::withdraw", input: {} }]));
  });
});

describe("tables", () => {
  function Shelf() {
    const shelf = useView("library::shelf");
    return (
      <>
        <Table view={shelf} columns={{ title: "Title", author: "Author" }} actions={["library::book::withdraw"]} />
        <Confirm command="library::book::withdraw" question="Withdraw {title}?" />
      </>
    );
  }

  it("shows a row per item, with each action on every row", async () => {
    const source = memorySource({
      views: {
        "library::shelf": {
          rows: [
            { id: "b1", title: "The Hobbit", author: "Tolkien" },
            { id: "b2", title: "Dune", author: "Herbert" },
          ],
        },
      },
    });
    renderScreen(source, () => <Shelf />);
    expect(screen.getAllByRole("row")).toHaveLength(3);
    expect(screen.getByText("Herbert")).toBeTruthy();

    fireEvent.click(screen.getByRole("button", { name: "Withdraw Dune" }));
    expect(screen.getByRole("dialog").textContent).toContain("Withdraw Dune?");
    fireEvent.click(screen.getByRole("button", { name: "Yes" }));
    await waitFor(() => expect(source.runs).toEqual([{ command: "library::book::withdraw", input: { id: "b2" } }]));
  });
});

describe("when something fails, the person is told", () => {
  it("shows why a command button's command failed", async () => {
    const source = memorySource({
      commands: {
        "library::shelf::restock": () => {
          throw new Error("you can't restock the shelf");
        },
      },
    });
    renderScreen(source, () => <Command name="library::shelf::restock" />);
    fireEvent.click(screen.getByRole("button", { name: "Restock" }));
    expect((await screen.findByRole("alert")).textContent).toBe("you can't restock the shelf");
  });

  it("runs a command on the entity it's given", async () => {
    const source = memorySource();
    renderScreen(source, () => <Command name="tracker::issue::close" id="uione-12" />);
    fireEvent.click(screen.getByRole("button", { name: "Close" }));
    await waitFor(() => expect(source.runs).toEqual([{ command: "tracker::issue::close", input: { id: "uione-12" } }]));
  });

  it("says what a button does, and leaves it out while it doesn't apply", () => {
    const source = memorySource();
    const { rerender } = renderScreen(source, () => <Command name="tracker::issue::close" id="uione-12" label="Close issue" when={true} />);
    expect(screen.getByRole("button", { name: "Close issue" })).toBeTruthy();
    rerender(<App name="app" screens={[defineScreen({ title: "Test", route: "/" }, () => <Command name="tracker::issue::close" label="Close issue" when={false} />)]} ui={plain} data={source} location="/" />);
    expect(screen.queryByRole("button", { name: "Close issue" })).toBeNull();
  });

  it("opens a form from a button named for it, and leaves both out while they don't apply", () => {
    const shown = renderScreen(memorySource(), () => <Form command="tracker::issue::create" fields={["title"]} button opener="New issue" />);
    fireEvent.click(screen.getByRole("button", { name: "New issue" }));
    expect(screen.getByRole("dialog", { name: "New issue" })).toBeTruthy();
    shown.unmount();
    renderScreen(memorySource(), () => <Form command="tracker::issue::create" fields={["title"]} button opener="New issue" when={false} />);
    expect(screen.queryByRole("button", { name: "New issue" })).toBeNull();
  });

  it("knows who holds a role where, once their roles arrive", () => {
    const roles = { status: "live" as const, data: { rows: [{ id: "a", project: "uione", role: "maintainer" }, { id: "b", project: "neotrac", role: "reporter" }] } };
    expect(holds(roles, "project", "uione", ["maintainer"])).toBe(true);
    expect(holds(roles, "project", "neotrac", ["maintainer"])).toBe(false);
    expect(holds(roles, "project", "elsewhere", ["maintainer", "reporter"])).toBe(false);
    expect(holds({ status: "loading", data: undefined }, "project", "uione", ["maintainer"])).toBe(false);
    expect(holds(roles, "project", undefined, ["maintainer"])).toBe(false);
  });

  it("leaves out a button and a form the person reading may not use", () => {
    renderScreen(memorySource({ person: { uid: "ada", name: "Ada" } }), () => (
      <>
        <Command name="tracker::issue::close" allowed={false} />
        <Form command="tracker::issue::create" fields={["title"]} button opener="New issue" authenticated allowed={false} />
      </>
    ));
    expect(screen.queryByRole("button", { name: "Close" })).toBeNull();
    expect(screen.queryByRole("button", { name: "New issue" })).toBeNull();
  });

  it("shows why an action on a table row failed", async () => {
    const source = memorySource({
      views: { "library::shelf": { rows: [{ id: "b1", title: "Dune", author: "Herbert" }] } },
      commands: {
        "library::book::withdraw": () => {
          throw new Error("that book is on loan");
        },
      },
    });
    renderScreen(source, () => (
      <Table view={useView("library::shelf")} columns={{ title: "Title" }} actions={["library::book::withdraw"]} />
    ));
    fireEvent.click(screen.getByRole("button", { name: "Withdraw Dune" }));
    expect((await screen.findByRole("alert")).textContent).toBe("that book is on loan");
  });
});

describe("a view that changes", () => {
  it("never shows one subject's data under another, even for one render", () => {
    const source = memorySource();
    act(() => source.set("library::loans", { rows: [{ id: "l1" }] }, "ada"));
    const seen: [string, unknown][] = [];
    function Loans({ member }: { member: string }) {
      const loans = useView("library::loans", member);
      seen.push([member, loans.data]);
      return null;
    }
    function Members() {
      const [member, setMember] = useState("ada");
      return (
        <>
          <Loans member={member} />
          <button type="button" onClick={() => setMember("grace")}>
            Grace
          </button>
        </>
      );
    }
    renderScreen(source, () => <Members />);
    expect(seen).toContainEqual(["ada", { rows: [{ id: "l1" }] }]);
    fireEvent.click(screen.getByRole("button", { name: "Grace" }));
    const leaked = seen.filter(([member, data]) => member === "grace" && data !== undefined);
    expect(leaked).toEqual([]);
  });
});

describe("a table whose rows open a page", () => {
  it("links each row's first cell to the page for that row", () => {
    const source = memorySource({ views: { "library::shelf": { rows: [{ id: "b 1", title: "Dune" }] } } });
    renderScreen(source, () => <Table view={useView("library::shelf")} link="/books/:book" columns={{ title: "Title" }} />);
    expect(screen.getByRole("link", { name: "Dune" }).getAttribute("href")).toBe("/books/b%201");
  });

  it("reads a route's parameter, for a view per entity", () => {
    function Page() {
      const id = useParam("book");
      return <Text>{`book ${id}`}</Text>;
    }
    const page = defineScreen({ title: "Book", route: "/books/:book" }, () => <Page />);
    render(<App name="app" screens={[page]} ui={plain} data={memorySource()} location="/books/b1" />);
    expect(screen.getByText("book b1")).toBeTruthy();
  });
});

describe("a view with several lists", () => {
  it("shows the list a table names, and the rows otherwise", () => {
    const source = memorySource({
      views: { "library::book_page": { title: "Dune", rows: [{ id: "r1", member: "row" }], loans: [{ id: "l1", member: "Ada" }] } },
    });
    renderScreen(source, () => (
      <>
        <Table view={useView("library::book_page")} list="loans" columns={{ member: "Member" }} />
        <Table view={useView("library::book_page")} columns={{ member: "Member" }} />
      </>
    ));
    expect(screen.getByText("Ada")).toBeTruthy();
    expect(screen.getByText("row")).toBeTruthy();
  });
});

describe("a table with people in it", () => {
  it("shows a person's picture, and only from an https address", () => {
    const source = memorySource({
      views: {
        "forum::posts": {
          rows: [
            { id: "p1", "author.name": "Ada", "author.picture": "https://example.com/ada.png" },
            { id: "p2", "author.name": "Eve", "author.picture": "javascript:alert(1)" },
          ],
        },
      },
    });
    const { container } = renderScreen(source, () => (
      <Table view={useView("forum::posts")} columns={{ "author.picture": "", "author.name": "Author" }} pictures={["author.picture"]} />
    ));
    const pictures = container.querySelectorAll("img");
    expect(pictures).toHaveLength(1);
    expect(pictures[0].getAttribute("src")).toBe("https://example.com/ada.png");
    expect(screen.queryByText("javascript:alert(1)")).toBeNull();
  });
});

describe("a web address in a table", () => {
  it("is a link to wherever it is, which says it leaves the app, with the address shortened", () => {
    const log = "https://console.cloud.google.com/cloud-build/builds/a2d3012f-883f-4aef-b810-5e9611c75899?project=39906949747";
    const source = memorySource({
      views: {
        "studio::history": {
          rows: [
            { id: "d1", status: "live", log_url: log },
            { id: "d2", status: "failed", log_url: `${window.location.origin}/logs/d2` },
            { id: "d3", status: "queued", log_url: "not an address" },
          ],
        },
      },
    });
    const { container } = renderScreen(source, () => <Table view={useView("studio::history")} columns={{ status: "Status", log_url: "Log" }} />);
    const links = container.querySelectorAll("td a");
    expect(links).toHaveLength(2);
    expect(links[0].getAttribute("href")).toBe(log);
    expect(links[0].getAttribute("target")).toBe("_blank");
    expect(links[0].getAttribute("rel")).toBe("noreferrer");
    expect(links[0].textContent).toContain("console.cloud.google.com/…");
    expect(links[0].textContent).toContain("↗");
    // The app's own address is a link like any other, in the same tab.
    expect(links[1].getAttribute("target")).toBeNull();
    expect(screen.getByText("not an address")).toBeTruthy();
  });

  it("keeps a short address whole, and a long one's host and end", () => {
    expect(shortAddress("https://neotrac.org/")).toBe("neotrac.org");
    expect(shortAddress("https://example.com/a/very/long/path/that/goes/on/and/on/to/the/end.html")).toBe("example.com/…/on/and/on/to/the/end.html");
    // A build's log: its project, after the ?, isn't shown, and its id is too long to.
    expect(shortAddress("https://console.cloud.google.com/cloud-build/builds;region=us-east4/7877c18a-a4d3-4c35-a91d-79ba1b2859e5?project=39906949747")).toBe(
      "console.cloud.google.com/…",
    );
    expect(shortAddress("https://neotrac.org/projects?sort=name#top")).toBe("neotrac.org/projects");
  });
});

describe("a form that makes something inside something else", () => {
  it("sends what it's given without asking for it", async () => {
    const source = memorySource();
    renderScreen(source, () => <Form command="tracker::issue::create" fields={["title"]} given={{ project: "uione" }} />);
    expect(screen.queryByLabelText("Project")).toBeNull();
    fireEvent.change(screen.getByLabelText("Title"), { target: { value: "Keys" } });
    fireEvent.click(screen.getByRole("button", { name: "Create" }));
    await waitFor(() => expect(source.runs).toEqual([{ command: "tracker::issue::create", input: { title: "Keys", project: "uione" } }]));
  });
});

describe("a list in a form and a table", () => {
  it("is written separated by commas, sent as a list, and shown that way", async () => {
    const source = memorySource({ views: { "tracker::issue_page": { labels: ["bug", "ui"] } } });
    renderScreen(source, () => (
      <Form command="tracker::issue::update" fields={[{ name: "labels", type: "list" }]} from={useView("tracker::issue_page")} id="uione-1" />
    ));
    const labels = screen.getByLabelText("Labels") as HTMLInputElement;
    expect(labels.value).toBe("bug, ui");
    fireEvent.change(labels, { target: { value: "bug, , docs ,ui" } });
    fireEvent.click(screen.getByRole("button", { name: "Update" }));
    await waitFor(() =>
      expect(source.runs).toEqual([{ command: "tracker::issue::update", input: { labels: ["bug", "docs", "ui"], id: "uione-1" } }]),
    );
  });

  it("shows a list in a table cell separated by commas", () => {
    const source = memorySource({ views: { "tracker::board": { rows: [{ id: "i1", "assignees.name": ["Ada", "Grace"] }] } } });
    renderScreen(source, () => <Table view={useView("tracker::board")} columns={{ "assignees.name": "Assigned" }} />);
    expect(screen.getByText("Ada, Grace")).toBeTruthy();
  });
});

describe("a form that changes something already stored", () => {
  it("starts from what's stored, keeps what's typed, and sends which one it changes", async () => {
    const source = memorySource({ views: { "library::book_page": { title: "Dune", author: "Herbert", due_at: new Date(2026, 9, 30) } } });
    renderScreen(source, () => (
      <Form command="library::book::update" fields={["title", "author", { name: "due_at", type: "date" }]} from={useView("library::book_page")} id="b1" />
    ));
    const title = screen.getByLabelText("Title") as HTMLInputElement;
    expect(title.value).toBe("Dune");
    expect((screen.getByLabelText("Due at") as HTMLInputElement).value).toBe("2026-10-30");

    fireEvent.change(title, { target: { value: "Dune Messiah" } });
    // A change arriving from elsewhere doesn't overwrite what's being typed.
    act(() => source.set("library::book_page", { title: "Dune (2nd)", author: "Herbert", due_at: new Date(2026, 9, 30) }));
    expect(title.value).toBe("Dune Messiah");

    fireEvent.click(screen.getByRole("button", { name: "Update" }));
    await waitFor(() =>
      expect(source.runs).toEqual([
        { command: "library::book::update", input: { title: "Dune Messiah", author: "Herbert", due_at: "2026-10-30", id: "b1" } },
      ]),
    );
    expect(title.value).toBe("Dune Messiah"); // an update keeps its values, unlike a create

    // Once the view has a newer document, the form follows it again.
    act(() => source.set("library::book_page", { title: "Dune Messiah", author: "Frank Herbert", due_at: new Date(2026, 9, 30) }));
    expect((screen.getByLabelText("Author") as HTMLInputElement).value).toBe("Frank Herbert");
  });
});

describe("why a command failed", () => {
  const failing = () =>
    memorySource({
      commands: {
        "studio::project::create": () => {
          throw new Error("that name is taken");
        },
      },
    });

  it("goes once a field is edited", async () => {
    renderScreen(failing(), () => <Form command="studio::project::create" fields={["name"]} />);
    fireEvent.click(screen.getByRole("button", { name: "Create" }));
    await screen.findByRole("alert");
    fireEvent.change(screen.getByLabelText("Name"), { target: { value: "another" } });
    expect(screen.queryByRole("alert")).toBeNull();
  });

  it("isn't there when a form's dialog is opened again", async () => {
    renderScreen(failing(), () => <Form command="studio::project::create" fields={["name"]} button />);
    fireEvent.click(screen.getByRole("button", { name: "Create" }));
    fireEvent.submit(screen.getByLabelText("Name").closest("form")!);
    await screen.findByRole("alert");
    fireEvent(screen.getByRole("dialog"), new Event("cancel", { cancelable: true }));
    expect(screen.queryByRole("dialog")).toBeNull();
    fireEvent.click(screen.getByRole("button", { name: "Create" }));
    expect(screen.getByRole("dialog")).toBeTruthy();
    expect(screen.queryByRole("alert")).toBeNull();
  });

  it("goes when the person declines to run it again", async () => {
    const source = memorySource({
      commands: {
        "library::book::withdraw": () => {
          throw new Error("that book is on loan");
        },
      },
    });
    renderScreen(source, () => (
      <>
        <Command name="library::book::withdraw" />
        <Confirm command="library::book::withdraw" question="Withdraw it?" />
      </>
    ));
    fireEvent.click(screen.getByRole("button", { name: "Withdraw" }));
    fireEvent.click(screen.getByRole("button", { name: "Yes" }));
    expect((await screen.findByRole("alert")).textContent).toBe("that book is on loan");
    fireEvent.click(screen.getByRole("button", { name: "Withdraw" }));
    fireEvent.click(screen.getByRole("button", { name: "Cancel" }));
    await waitFor(() => expect(screen.queryByRole("alert")).toBeNull());
  });
});

describe("a table whose rows open a page inside another", () => {
  it("fills the row's parameter with its id, and the ones before it from the address", () => {
    const source = memorySource({ views: { "tracker::issues": { rows: [{ id: "i 1", title: "Keys" }] } } });
    function Issues() {
      return <Table view={useView("tracker::issues")} link="/projects/:project/issues/:issue" columns={{ title: "Title" }} />;
    }
    const page = defineScreen({ title: "Project", route: "/projects/:project" }, () => <Issues />);
    render(<App name="app" screens={[page]} ui={plain} data={source} location="/projects/uione" />);
    expect(screen.getByRole("link", { name: "Keys" }).getAttribute("href")).toBe("/projects/uione/issues/i%201");
  });

  it("links a row to what it holds, when it names it", () => {
    const source = memorySource({
      views: { "studio::mine": { rows: [{ id: "uione-ada", project: "uione", "project.name": "uione" }] } },
    });
    renderScreen(source, () => <Table view={useView("studio::mine")} link="/projects/:project" columns={{ "project.name": "Project" }} />);
    expect(screen.getByRole("link", { name: "uione" }).getAttribute("href")).toBe("/projects/uione");
  });

  it("names what a row opens by its key's parts, when the link does", () => {
    const source = memorySource({
      views: { "studio::mine": { rows: [{ id: "my-name-neo%2Dtrac-ada", project: "my-name-neo%2Dtrac", "project.name": "neo-trac" }] } },
    });
    renderScreen(source, () => (
      <Table view={useView("studio::mine")} link="/:owner/:project" keyed={["owner"]} columns={{ "project.name": "Project" }} />
    ));
    expect(screen.getByRole("link", { name: "neo-trac" }).getAttribute("href")).toBe("/my-name/neo-trac");
  });

  it("links to another row's page from a row's page", () => {
    const source = memorySource({ views: { "library::related": { rows: [{ id: "b2", title: "Emma" }] } } });
    const page = defineScreen({ title: "Book", route: "/books/:book" }, () => (
      <Table view={useView("library::related")} link="/books/:book" columns={{ title: "Title" }} />
    ));
    render(<App name="app" screens={[page]} ui={plain} data={source} location="/books/b1" />);
    expect(screen.getByRole("link", { name: "Emma" }).getAttribute("href")).toBe("/books/b2");
  });
});

describe("a view whose data isn't what a table expects", () => {
  it("shows a list that isn't a list as empty", () => {
    const source = memorySource({ views: { "library::shelf": { rows: "nothing" } } });
    renderScreen(source, () => <Table view={useView("library::shelf")} columns={{ title: "Title" }} />);
    expect(screen.getAllByRole("row")).toHaveLength(1);
  });

  it("leaves out rows that aren't objects or have no id", () => {
    const rows = [null, 5, "row", ["b0"], { title: "No id" }, { id: 3, title: "Number id" }, { id: "b1", title: "Kept" }];
    const source = memorySource({ views: { "library::shelf": { rows } } });
    renderScreen(source, () => <Table view={useView("library::shelf")} columns={{ title: "Title" }} actions={["library::book::withdraw"]} />);
    expect(screen.getAllByRole("row")).toHaveLength(2);
    expect(screen.getByText("Kept")).toBeTruthy();
  });
});

describe("choices", () => {
  it("are picked from a list in a form, and shown by their labels in a table", () => {
    const source = memorySource({ views: { "studio::all": { rows: [{ id: "a", name: "neotrac", license: "apache_2_0" }] } } });
    renderScreen(source, () => (
      <>
        <Table view={useView("studio::all")} columns={{ name: "Name", license: "License" }} choices={{ license: { mit: "MIT", apache_2_0: "Apache-2.0" } }} />
        <Form command="studio::project::create" fields={["name", { name: "license", type: "choice", choices: [["mit", "MIT"], ["apache_2_0", "Apache-2.0"]] }]} />
      </>
    ));
    expect(screen.getByRole("cell", { name: "Apache-2.0" })).toBeTruthy();
    const pick = screen.getByRole("combobox", { name: "License" }) as HTMLSelectElement;
    expect([...pick.options].map((o) => o.textContent)).toEqual(["Choose one", "MIT", "Apache-2.0"]);
  });
});

describe("a form's button", () => {
  it("says what the form says it does, or the command's name", () => {
    renderScreen(memorySource(), () => (
      <>
        <Form command="studio::project::update" fields={["description"]} submit="Save changes" />
        <Form command="studio::project::create" fields={["name"]} />
      </>
    ));
    expect(screen.getByRole("button", { name: "Save changes" })).toBeTruthy();
    expect(screen.getByRole("button", { name: "Create" })).toBeTruthy();
  });

  it("opens a form behind a button by what the form says it does", () => {
    renderScreen(memorySource(), () => <Form command="studio::project::create" fields={["name"]} submit="Create project" button />);
    fireEvent.click(screen.getByRole("button", { name: "Create project" }));
    expect(screen.getByRole("dialog", { name: "Create project" })).toBeTruthy();
  });
});

describe("what needs someone signed in", () => {
  it("asks someone signed out to sign in instead of showing a form, and shows it once they have", async () => {
    const source = memorySource({ person: null });
    renderScreen(source, () => <Form command="tracker::project::create" fields={["name"]} authenticated />);
    expect(screen.queryByLabelText("Name")).toBeNull();
    fireEvent.click(screen.getByRole("button", { name: "Sign in to create" }));
    await waitFor(() => expect(screen.getByLabelText("Name")).toBeTruthy());
  });

  it("signs in at once with one way, and lets the person choose with several", async () => {
    const one = memorySource({ person: null, methods: [{ id: "github", name: "GitHub" }] });
    const first = renderScreen(one, () => <Form command="tracker::project::create" fields={["name"]} authenticated />);
    fireEvent.click(screen.getByRole("button", { name: "Sign in to create" }));
    await waitFor(() => expect(screen.getByLabelText("Name")).toBeTruthy());
    expect(one.signIns).toEqual(["github"]);
    expect(screen.queryByRole("dialog")).toBeNull();
    first.unmount();

    const several = memorySource({
      person: null,
      methods: [
        { id: "google", name: "Google" },
        { id: "github", name: "GitHub" },
        { id: "microsoft", name: "Microsoft" },
      ],
    });
    renderScreen(several, () => <Form command="tracker::project::create" fields={["name"]} authenticated />);
    fireEvent.click(screen.getByRole("button", { name: "Sign in to create" }));
    const choices = screen.getByRole("dialog", { name: "Sign in" });
    expect(Array.from(choices.querySelectorAll("button")).map((b) => b.textContent?.trim())).toEqual(
      expect.arrayContaining(["Continue with Google", "Continue with GitHub", "Continue with Microsoft"]),
    );
    fireEvent.click(screen.getByRole("button", { name: "Continue with Microsoft" }));
    await waitFor(() => expect(screen.getByLabelText("Name")).toBeTruthy());
    expect(several.signIns).toEqual(["microsoft"]);
    await waitFor(() => expect(screen.queryByRole("dialog", { name: "Sign in" })).toBeNull());
  });

  it("doesn't ask someone signed out to sign in for what takes a role", () => {
    renderScreen(memorySource({ person: null }), () => (
      <>
        <Form command="tracker::issue::update" fields={["title"]} button authenticated allowed={false} />
        <Form command="tracker::project::create" fields={["name"]} button authenticated />
      </>
    ));
    expect(screen.queryByRole("button", { name: /update/i })).toBeNull();
    expect(screen.getByRole("button", { name: "Sign in to create" })).toBeTruthy();
  });

  it("shows a form anyone may send to everyone", () => {
    renderScreen(memorySource({ person: null }), () => <Form command="waitlist::signup::create" fields={["email"]} />);
    expect(screen.getByLabelText("Email")).toBeTruthy();
  });

  it("leaves out what's refused to someone signed out, and shows it refused to someone signed in", () => {
    const signedOut = renderScreen(memorySource({ person: null, views: {} }), () => (
      <Table view={{ status: "denied", data: undefined }} columns={{ name: "Name" }} />
    ));
    expect(signedOut.container.querySelector("table")).toBeNull();
    signedOut.unmount();
    const signedIn = renderScreen(memorySource({ person: { uid: "ada", name: "Ada" } }), () => (
      <Table view={{ status: "denied", data: undefined }} columns={{ name: "Name" }} />
    ));
    expect(signedIn.container.querySelector("table")).not.toBeNull();
  });
});

describe("a yes or no", () => {
  it("is a box to tick, sent as true or false", async () => {
    const source = memorySource();
    renderScreen(source, () => <Form command="projects::project::update" fields={[{ name: "takes_reports", type: "boolean" }]} id="p1" />);
    const box = screen.getByLabelText("Takes reports") as HTMLInputElement;
    expect(box.type).toBe("checkbox");
    expect(box.checked).toBe(false);
    fireEvent.click(box);
    fireEvent.click(screen.getByRole("button", { name: "Update" }));
    await waitFor(() => expect(source.runs).toEqual([{ command: "projects::project::update", input: { takes_reports: true, id: "p1" } }]));
  });
});

describe("threads and timelines", () => {
  it("says what each change did", () => {
    expect(changed("", null, null)).toBe("created this");
    expect(changed("status", "open", "closed")).toBe("changed status from open to closed");
    expect(changed("due_at", "", "Friday")).toBe("set due at to Friday");
    expect(changed("labels", ["bug"], [])).toBe("cleared labels");
    expect(changed("status", "open", "closed", "tracker::issue::close")).toBe("closed this");
    expect(changed("status", "closed", "open", "tracker::issue::reopen")).toBe("reopened this");
    expect(changed("title", "a", "b", "tracker::issue::update")).toBe("changed title from a to b");
    expect(changed("", null, null, "tracker::issue::create")).toBe("created this");
    expect(changed("comment", null, null, "tracker::comment::create")).toBe("added a comment to this");
    expect(phrase("comment", null, null, "tracker::comment::create")).toEqual(["added a comment to", ""]);
    expect([done("take"), done("drop"), done("assign"), done("copy"), done("take_over")]).toEqual(["took", "dropped", "assigned", "copied", "took over"]);
    expect([done("transfer"), done("offer"), done("submit"), done("visit")]).toEqual(["transferred", "offered", "submitted", "visited"]);
  });

  it("shows what people wrote, each with who and when, and an entity's changes as sentences", () => {
    const view = {
      status: "live" as const,
      data: {
        comments: [{ id: "c1", "author.name": "Ada", body: "Reproduced **here**.", created_at: "2026-10-06T10:00:00Z" }],
        history: [
          { id: "h1", "created_by.name": "Ada", field: "", created_at: "2026-10-06T09:00:00Z" },
          { id: "h2", "created_by.name": "Grace", field: "status", before: "open", after: "closed", created_at: "2026-10-06T11:00:00Z" },
        ],
      },
    };
    renderScreen(memorySource(), () => (
      <>
        <Thread view={view} list="comments" />
        <Timeline view={view} list="history" />
      </>
    ));
    expect(screen.getByText("Ada", { selector: "strong" })).toBeTruthy();
    expect(screen.getByText(/Reproduced/)).toBeTruthy();
    expect(screen.getByText(/Grace changed status from open to closed/)).toBeTruthy();
    expect(screen.getByText(/Ada created this/)).toBeTruthy();
  });
});

describe("a table's tabs and labels", () => {
  it("shows the rows of one choice at a time, each tab with its count, and a list of words as labels", () => {
    const view = {
      status: "live" as const,
      data: {
        issues: [
          { id: "1", title: "Tabs too wide", status: "open", labels: ["phone", "editor"] },
          { id: "2", title: "Hover is empty", status: "open", labels: [] },
          { id: "3", title: "Old bug", status: "closed", labels: ["bug"] },
        ],
      },
    };
    renderScreen(memorySource(), () => (
      <Table view={view} list="issues" columns={{ title: "Title", labels: "Labels" }} labels={["labels"]} by="status" choices={{ status: { open: "Open", closed: "Closed" } }} />
    ));
    expect(screen.getByRole("tab", { name: "Open 2" }).getAttribute("aria-selected")).toBe("true");
    expect(screen.getByText("Tabs too wide")).toBeTruthy();
    expect(screen.queryByText("Old bug")).toBeNull();
    expect(screen.getByText("phone, editor")).toBeTruthy();
    fireEvent.click(screen.getByRole("tab", { name: "Closed 1" }));
    expect(screen.getByText("Old bug")).toBeTruthy();
    expect(screen.queryByText("Tabs too wide")).toBeNull();
  });
});

describe("copying what a page shows", () => {
  it("writes an issue as Markdown: its title, values, description, conversation and history", () => {
    const text = markdownOf(
      "#12 Copy an issue whole",
      {
        status: "open",
        labels: ["feature", "issues"],
        body: "One button copies **everything**.",
        comments: [{ id: "c", "author.name": "Ada", body: "Including comments.", created_at: "2026-10-06T10:00:00Z" }],
        history: [
          { id: "h", "created_by.name": "Ada", field: "", action: "projects::issue::create", created_at: "2026-10-06T09:00:00Z" },
          { id: "s1", "created_by.name": "Bob", field: "status", before: "open", after: "in_progress", action: "projects::issue::start", created_at: "2026-10-06T11:00:00Z" },
          { id: "s2", "created_by.name": "Bob", field: "implemented_by", before: null, after: "bob", action: "projects::issue::start", created_at: "2026-10-06T11:00:00Z" },
          // From nothing to nothing, as an older backend recorded it: no change at all.
          { id: "e1", "created_by.name": "Cy", field: "verified_by", before: null, after: "", action: "projects::issue::update", created_at: "2026-10-06T12:00:00Z" },
        ],
      },
      [["status", "Status"], ["labels", "Labels"], ["body", "Description", "markdown"]],
      [["comments", "Comments", "thread", ["author.name", "body", "created_at"]], ["history", "History", "changes", ["field"]]],
      { status: { open: "Open" } },
    );
    expect(text.startsWith("# #12 Copy an issue whole\n\n**Status:** Open  \n**Labels:** feature, issues  \n\n## Description\n\nOne button copies **everything**.\n\n## Comments\n\n**Ada** · ")).toBe(true);
    expect(text).toContain("\n\nIncluding comments.\n");
    expect(text).toContain("## History\n\n- Ada created this · ");
    // A command that changed two fields at once is one line, as on the page.
    expect(text.match(/- Bob /g)).toHaveLength(1);
    expect(text).not.toContain("Cy");
  });
});

describe("a timeline of many things", () => {
  it("names what each change was to, and links to it", () => {
    expect(phrase("status", "open", "closed", "tracker::issue::close")).toEqual(["closed", ""]);
    expect(phrase("title", "a", "b", "tracker::issue::update")).toEqual(["changed title of", "from a to b"]);
    const view = {
      status: "live" as const,
      data: { timeline: [{ id: "h", issue: "uione-12", "issue.number": 12, "issue.title": "Copy an issue whole", "created_by.name": "Ada", action: "tracker::issue::close", field: "status", created_at: "2026-10-06T10:00:00Z" }] },
    };
    renderScreen(memorySource(), () => <Timeline view={view} list="timeline" subject={["issue.number", "issue.title"]} link="/issues/:issue" />);
    const link = screen.getByRole("link", { name: "#12 Copy an issue whole" });
    expect(link.getAttribute("href")).toBe("/issues/uione-12");
    expect(link.parentElement?.textContent).toMatch(/^Ada closed #12 Copy an issue whole/);
  });
});

describe("details", () => {
  it("shows a view's values beside what they are, leaving out the empty ones", () => {
    const view = { status: "live" as const, data: { status: "in_progress", labels: ["editor"], implementer: "Ada", verifier: null } };
    renderScreen(memorySource(), () => (
      <Details view={view} fields={[["status", "Status"], ["labels", "Labels"], ["implementer", "Implemented by"], ["verifier", "Verified by"]]} choices={{ status: { in_progress: "In progress" } }} labels={["labels"]} />
    ));
    expect(screen.getByText("In progress")).toBeTruthy();
    expect(screen.getByText("Ada")).toBeTruthy();
    expect(screen.getByText("editor")).toBeTruthy();
    expect(screen.queryByText("Verified by")).toBeNull();
  });
});

describe("a table's search, order and pages", () => {
  it("finds rows by what's typed, puts them in order, and shows them a page at a time", () => {
    const issues = Array.from({ length: 5 }, (_, i) => ({ id: String(i + 1), number: i + 1, title: i % 2 ? "Editor tabs" : "Deploys hang" }));
    renderScreen(memorySource(), () => (
      <Table view={{ status: "live", data: { issues } }} list="issues" columns={{ number: "#", title: "Title" }} search={["title"]} sort="-number" page={2} />
    ));
    expect(screen.getAllByRole("row").slice(1).map((row) => row.textContent)).toEqual(["5Deploys hang", "4Editor tabs"]);
    expect(screen.getByText(/Page 1 of 3/)).toBeTruthy();
    fireEvent.change(screen.getByRole("searchbox", { name: "Search title" }), { target: { value: "editor" } });
    expect(screen.getAllByRole("row").slice(1).map((row) => row.textContent)).toEqual(["4Editor tabs", "2Editor tabs"]);
    expect(screen.queryByText(/Page 1 of/)).toBeNull();
  });
});

describe("filters by time", () => {
  it("keeps a time after or before one counted from now", () => {
    const now = Date.parse("2026-10-08T12:00:00Z");
    const twoDaysAgo = new Date(now - 2 * 24 * 3600 * 1000);
    const tenDaysAgo = new Date(now - 10 * 24 * 3600 * 1000);
    expect(keptByTime(twoDaysAgo, ">", "-7d", now)).toBe(true);
    expect(keptByTime(tenDaysAgo, ">", "-7d", now)).toBe(false);
    expect(keptByTime(tenDaysAgo.toISOString(), "<", "-1w", now)).toBe(true);
    expect(keptByTime(new Date(now + 3 * 3600 * 1000), "<=", "+4h", now)).toBe(true);
    expect(keptByTime(undefined, ">", "-7d", now)).toBe(false);
    expect(keptByTime(twoDaysAgo, ">", "seven days", now)).toBe(false);
  });
});

describe("what's new", () => {
  it("marks the changes since the person last looked, and says they've looked once", async () => {
    const source = memorySource();
    const looked = new Date("2026-10-08T10:00:00Z");
    const view: ViewState = {
      status: "live",
      data: {
        seen: looked,
        changes: [
          { id: "b", field: "", action: "tracker::issue::create", created_at: new Date("2026-10-08T11:00:00Z"), "created_by.name": "Ada" },
          { id: "a", field: "", action: "tracker::issue::create", created_at: new Date("2026-10-08T09:00:00Z"), "created_by.name": "Ada" },
        ],
      },
    };
    renderScreen(source, () => <Timeline view={view} list="changes" title="What's new" since={{ view, field: "seen" }} seen="tracker::reader::create" />);
    expect(screen.getByRole("list", { name: "What's new, 1 new" })).toBeTruthy();
    expect(screen.getAllByRole("listitem").map((item) => item.textContent?.startsWith("New: "))).toEqual([true, false]);
    await waitFor(() => expect(source.runs).toEqual([{ command: "tracker::reader::create", input: {} }]));
  });

  it("says nothing was looked at when nothing is new", async () => {
    const source = memorySource();
    const view: ViewState = { status: "live", data: { seen: new Date("2026-10-08T12:00:00Z"), changes: [{ id: "a", field: "", created_at: new Date("2026-10-08T09:00:00Z") }] } };
    renderScreen(source, () => <Timeline view={view} list="changes" title="What's new" since={{ view, field: "seen" }} seen="tracker::reader::create" />);
    expect(screen.getByRole("list", { name: "What's new" })).toBeTruthy();
    await new Promise((done) => setTimeout(done, 20));
    expect(source.runs).toEqual([]);
  });
});

describe("comments in a history", () => {
  it("says a comment was added, though nothing came before or after it", () => {
    const view: ViewState = {
      status: "live",
      data: { history: [{ id: "a", field: "comment", before: null, after: null, action: "tracker::comment::create", "created_by.name": "Ada", created_at: new Date("2026-10-08T09:00:00Z") }] },
    };
    renderScreen(memorySource(), () => <Timeline view={view} list="history" />);
    expect(screen.getByText(/added a comment to this/)).toBeTruthy();
  });
});

describe("what's on screen, in the path", () => {
  const view = {
    status: "live" as const,
    data: {
      issues: [
        { id: "1", title: "Tabs too wide", status: "open" },
        { id: "3", title: "Old bug", status: "closed" },
      ],
    },
  };
  function Where() {
    const { pathname, search } = useLocation();
    return <output aria-label="address">{pathname + search}</output>;
  }
  const table = () => (
    <>
      <Table view={view} list="issues" columns={{ title: "Title" }} by="status" choices={{ status: { open: "Open", closed: "Closed" } }} search={["title"]} />
      <Where />
    </>
  );
  const issues = defineScreen({ title: "Issues", route: "/issues", shown: true }, table);
  const open = (location: string) => render(<App name="app" screens={[issues]} ui={plain} data={memorySource()} location={location} />);

  it("puts the tab that's open in the path, and not the first, and what's typed in the query", async () => {
    open("/issues");
    fireEvent.click(screen.getByRole("tab", { name: "Closed 1" }));
    await waitFor(() => expect(screen.getByLabelText("address").textContent).toBe("/issues/closed"));
    fireEvent.change(screen.getByRole("searchbox"), { target: { value: "old" } });
    await waitFor(() => expect(screen.getByLabelText("address").textContent).toBe("/issues/closed?search=old"));
    fireEvent.click(screen.getByRole("tab", { name: "Open 0" }));
    await waitFor(() => expect(screen.getByLabelText("address").textContent).toBe("/issues?search=old"));
  });

  it("opens on the tab a path names, as a shared link does", () => {
    open("/issues/closed");
    expect(screen.getByText("Old bug")).toBeTruthy();
    expect(screen.queryByText("Tabs too wide")).toBeNull();
  });
});

describe("a site's foot", () => {
  it("says who it's by, linked, and the uione and commit it was built from", () => {
    const only = defineScreen({ title: "Test", route: "/" }, () => <Text>hi</Text>);
    render(
      <App name="app" screens={[only]} ui={plain} data={memorySource()} location="/" footer={{ copyright: "Ada Lovelace", link: "https://www.linkedin.com/in/ada", version: "0.7.0", commit: "d9d95fd0aaaa" }} />,
    );
    const foot = screen.getByRole("contentinfo");
    expect(foot.textContent).toBe(`© ${new Date().getFullYear()} Ada Lovelace · uione 0.7.0 · d9d95fd`);
    expect(screen.getByRole("link", { name: "Ada Lovelace" }).getAttribute("href")).toBe("https://www.linkedin.com/in/ada");
  });
});
