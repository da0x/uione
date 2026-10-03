// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

import { act, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { useState } from "react";
import type { ReactNode } from "react";
import {
  App,
  Command,
  Confirm,
  Form,
  Live,
  Table,
  Text,
  memorySource,
  screen as defineScreen,
  useParam,
  useView,
} from "../src/index.js";
import type { MemorySource } from "../src/index.js";
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
