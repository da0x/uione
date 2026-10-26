// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The plain component set, as someone using a screen reader or a keyboard meets it.

import { act, createEvent, fireEvent, render, screen, within } from "@testing-library/react";
import type { ReactNode } from "react";
import { afterEach, beforeEach, vi } from "vitest";
import { App, Board, Canvas, Cards, Details, Diagram, Form, Grid, Hero, Landing, Link, Live, Overline, Section, Statement, Text, Timeline, Steps, Switched, Table, allows, listChoices, memorySource, screen as defineScreen, useView } from "../src/index.js";
import type { LandingProps, MemorySource } from "../src/index.js";
import { plain } from "../src/plain.js";

function renderScreen(source: MemorySource, body: () => ReactNode) {
  const only = defineScreen({ title: "Test", route: "/" }, body);
  return render(<App name="app" screens={[only]} ui={plain} data={source} location="/" />);
}

afterEach(() => {
  delete (HTMLDialogElement.prototype as Partial<HTMLDialogElement>).showModal;
});

describe("a dialog", () => {
  it("opens modally, closes on Escape, and gives focus back to what opened it", () => {
    // Like a browser's, it moves focus into the dialog.
    const showModal = vi.fn(function (this: HTMLDialogElement) {
      this.setAttribute("open", "");
      this.querySelector("input")?.focus();
    });
    HTMLDialogElement.prototype.showModal = showModal;
    renderScreen(memorySource(), () => <Form command="studio::project::create" fields={["name"]} button />);
    const opener = screen.getByRole("button", { name: "Create" });
    opener.focus();
    fireEvent.click(opener);
    const dialog = screen.getByRole("dialog", { name: "Create" });
    expect(dialog.tagName).toBe("DIALOG");
    expect(showModal).toHaveBeenCalledTimes(1);
    expect(document.activeElement).toBe(screen.getByLabelText("Name"));

    fireEvent(dialog, new Event("cancel", { cancelable: true }));
    expect(screen.queryByRole("dialog")).toBeNull();
    expect(document.activeElement).toBe(opener);
  });
});

describe("a table's row actions", () => {
  it("name the row they act on, under a column header", () => {
    const source = memorySource({ views: { "library::shelf": { rows: [{ id: "b1", title: "Dune" }] } } });
    renderScreen(source, () => <Table view={useView("library::shelf")} columns={{ title: "Title" }} actions={["library::book::withdraw"]} />);
    expect(screen.getByRole("button", { name: "Withdraw Dune" })).toBeTruthy();
    expect(screen.getByRole("columnheader", { name: "Actions" })).toBeTruthy();
  });

  it("say what the table calls them, and aren't there for someone not allowed them", () => {
    const source = memorySource({ views: { "projects::people": { rows: [{ id: "m1", name: "Ada" }] } } });
    renderScreen(source, () => (
      <Table
        view={useView("projects::people")}
        columns={{ name: "Name" }}
        actions={[
          { name: "projects::member::delete", label: "Remove", allowed: true },
          { name: "projects::member::promote", allowed: false },
        ]}
      />
    ));
    expect(screen.getByRole("button", { name: "Remove Ada" })).toBeTruthy();
    expect(screen.queryByRole("button", { name: /Promote/ })).toBeNull();
  });

  it("open their command's form when it asks first, started from the row, and send it with the row's id", async () => {
    HTMLDialogElement.prototype.showModal = function (this: HTMLDialogElement) {
      this.setAttribute("open", "");
    };
    const source = memorySource({ views: { "projects::workflow": { rows: [{ id: "p1", title: "Open" }] } } });
    renderScreen(source, () => (
      <Table
        view={useView("projects::workflow")}
        columns={{ title: "Stage" }}
        actions={[{ name: "projects::stage::update", label: "Rename", form: { fields: [{ name: "title", label: "Called" }], submit: "Save" } }]}
      />
    ));
    fireEvent.click(screen.getByRole("button", { name: "Rename Open" }));
    expect(source.runs).toEqual([]);
    const called = screen.getByLabelText("Called") as HTMLInputElement;
    expect(called.value).toBe("Open");
    fireEvent.change(called, { target: { value: "Triage" } });
    await act(async () => fireEvent.click(screen.getByRole("button", { name: "Save" })));
    expect(source.runs).toEqual([{ command: "projects::stage::update", input: { title: "Triage", id: "p1" } }]);
    expect(screen.queryByRole("dialog")).toBeNull();
  });

  it("don't offer a row as a pick in a form about itself", () => {
    HTMLDialogElement.prototype.showModal = function (this: HTMLDialogElement) {
      this.setAttribute("open", "");
    };
    const source = memorySource({ views: { "projects::workflow": { rows: [{ id: "p1", title: "Triage" }, { id: "p2", title: "Done" }] } } });
    const stages: [string, string][] = [["p1", "Triage"], ["p2", "Done"]];
    renderScreen(source, () => (
      <Table
        view={useView("projects::workflow")}
        columns={{ title: "Stage" }}
        actions={[{ name: "projects::stage::delete", label: "Remove", form: { fields: [{ name: "into", label: "Move its issues to", type: "pick", choices: stages }] } }]}
      />
    ));
    fireEvent.click(screen.getByRole("button", { name: "Remove Triage" }));
    const into = screen.getByLabelText("Move its issues to");
    expect(Array.from(into.querySelectorAll("option")).map((o) => o.textContent)).toEqual(["Choose one", "Done"]);
  });

  it("are only on the rows their when holds for, each row keeping its cells", () => {
    const source = memorySource({
      views: {
        "projects::people": {
          rows: [
            { id: "m1", name: "Ada", person: "ada" },
            { id: "m2", name: "Bob", person: "bob" },
          ],
        },
      },
    });
    renderScreen(source, () => (
      <Table
        view={useView("projects::people")}
        columns={{ name: "Name" }}
        actions={[{ name: "projects::member::delete", label: "Remove", when: (row) => row.person !== "ada" }]}
      />
    ));
    expect(screen.queryByRole("button", { name: "Remove Ada" })).toBeNull();
    expect(screen.getByRole("button", { name: "Remove Bob" })).toBeTruthy();
    const [ada, bob] = screen.getAllByRole("row").slice(1);
    expect(ada.children.length).toBe(bob.children.length);
  });

  it("leave no actions column when none are allowed", () => {
    const source = memorySource({ views: { "projects::people": { rows: [{ id: "m1", name: "Ada" }] } } });
    renderScreen(source, () => <Table view={useView("projects::people")} columns={{ name: "Name" }} actions={[{ name: "projects::member::delete", allowed: false }]} />);
    expect(screen.queryByRole("columnheader", { name: "Actions" })).toBeNull();
  });
});

describe("a project's own roles", () => {
  it("allow what each role's record says, in that project only", () => {
    const roles = { status: "live" as const, data: { rows: [{ id: "m1", project: "ark", "role.may": ["job::create"] }] } };
    expect(allows(roles, "project", "ark", "job::create", "role.may")).toBe(true);
    expect(allows(roles, "project", "ark", "job::update", "role.may")).toBe(false);
    expect(allows(roles, "project", "raft", "job::create", "role.may")).toBe(false);
    expect(allows({ status: "loading", data: undefined }, "project", "ark", "job::create", "role.may")).toBe(false);
  });

  it("are picked from a list a view holds, by their titles", () => {
    const page = { status: "live" as const, data: { roles: [{ id: "ark-captain", title: "Captain" }, { id: "ark-deckhand", title: "Deckhand" }] } };
    expect(listChoices(page, "roles", "title")).toEqual([
      ["ark-captain", "Captain"],
      ["ark-deckhand", "Deckhand"],
    ]);
  });

  it("tick the commands a role allows, and send them as a list", async () => {
    const source = memorySource();
    renderScreen(source, () => (
      <Form command="crew::rank::create" fields={[{ name: "may", label: "Allows", type: "choices", choices: [["job::create", "Create job"], ["job::update", "Update job"]] }]} />
    ));
    fireEvent.click(screen.getByLabelText("Create job"));
    fireEvent.click(screen.getByLabelText("Update job"));
    fireEvent.click(screen.getByLabelText("Create job"));
    fireEvent.click(screen.getByRole("button", { name: "Create" }));
    await vi.waitFor(() => expect(source.runs.at(-1)).toEqual({ command: "crew::rank::create", input: { may: ["job::update"] } }));
  });
});

describe("a workflow's steps", () => {
  it("are buttons for the steps from where it is that the person's roles may take, each moving it on", async () => {
    const source = memorySource();
    const issue = { status: "live" as const, data: { stage: "ark-ready" } };
    const page = {
      status: "live" as const,
      data: {
        steps: [
          { id: "s1", from: "ark-ready", to: "ark-doing", title: "Start work", roles: ["ark-programmer"] },
          { id: "s2", from: "ark-ready", to: "ark-triage", "to.title": "Triage", roles: ["ark-owner"] },
          { id: "s3", from: "ark-doing", to: "ark-review", "to.title": "In review", roles: ["ark-programmer"] },
          { id: "s4", from: "ark-ready", to: "ark-closed", "to.title": "Closed", roles: ["ark-programmer"] },
        ],
      },
    };
    const roles = { status: "live" as const, data: { rows: [{ id: "m1", project: "ark", role: "ark-programmer" }] } };
    renderScreen(source, () => (
      <Steps command="projects::issue::move" id="ark-1" field="stage" current={issue} steps={page} list="steps" shown="title" to="to.title" held="roles" roles={roles} within="ark" />
    ));
    expect(screen.getAllByRole("button").map((b) => b.textContent)).toEqual(["Start work", "Move to Closed"]);
    fireEvent.click(screen.getByRole("button", { name: "Start work" }));
    await vi.waitFor(() => expect(source.runs.at(-1)).toEqual({ command: "projects::issue::move", input: { id: "ark-1", stage: "ark-doing" } }));
  });

  it("say where nothing leads on for the person's roles, and say nothing to someone who holds none", () => {
    const issue = { status: "live" as const, data: { stage: "ark-review" } };
    const page = { status: "live" as const, data: { steps: [{ id: "s1", from: "ark-review", "from.title": "In review", to: "ark-done", roles: ["ark-tester"] }] } };
    const programmer = { status: "live" as const, data: { rows: [{ id: "m1", project: "ark", role: "ark-programmer" }] } };
    const outsider = { status: "live" as const, data: { rows: [] } };
    const steps = (roles: typeof programmer | typeof outsider) => (
      <Steps command="projects::issue::move" id="ark-1" field="stage" current={issue} steps={page} list="steps" from="from.title" held="roles" roles={roles} within="ark" />
    );
    const { unmount } = renderScreen(memorySource(), () => steps(programmer));
    expect(screen.queryByRole("button")).toBeNull();
    expect(screen.getByText("No moves from In review for your roles")).toBeTruthy();
    unmount();
    renderScreen(memorySource(), () => steps(outsider));
    expect(screen.queryByText(/No moves/)).toBeNull();
  });
});

describe("a grid", () => {
  const page = {
    status: "live" as const,
    data: {
      stages: [
        { id: "p1", title: "Triage" },
        { id: "p2", title: "Done" },
      ],
      steps: [{ id: "s1", from: "p1", to: "p2", "roles.title": ["Member"], roles: ["r1"] }],
    },
  };

  beforeEach(() => {
    HTMLDialogElement.prototype.showModal = function (this: HTMLDialogElement) {
      this.setAttribute("open", "");
    };
  });

  it("has a cell for each pair, a dash for each with itself, and creates what an empty one is pressed for", async () => {
    const source = memorySource();
    renderScreen(source, () => (
      <Grid
        view={page}
        list="steps"
        from="from"
        to="to"
        cell="roles.title"
        over={page}
        overList="stages"
        create={{ name: "projects::step::create", fields: ["title"], submit: "Allow", given: { project: "ark" } }}
        update={{ name: "projects::step::update", fields: ["title"] }}
        remove={{ name: "projects::step::delete" }}
      />
    ));
    expect(screen.getAllByRole("row").map((r) => r.textContent)).toEqual(["From, toTriageDone", "Triage—Member", "Done·—"]);
    fireEvent.click(screen.getByRole("button", { name: "Add Done to Triage" }));
    fireEvent.change(screen.getByLabelText("Title"), { target: { value: "Reopen" } });
    await act(async () => fireEvent.click(screen.getByRole("button", { name: "Allow" })));
    expect(source.runs).toEqual([{ command: "projects::step::create", input: { title: "Reopen", project: "ark", from: "p2", to: "p1" } }]);
  });

  it("opens update for a full cell, with remove beside it, and nothing for what the person may not change", async () => {
    const source = memorySource();
    renderScreen(source, () => (
      <Grid view={page} list="steps" from="from" to="to" cell="roles.title" over={page} overList="stages" create={{ name: "projects::step::create", allowed: false }} remove={{ name: "projects::step::delete" }} />
    ));
    expect(screen.queryByRole("button", { name: "Add Done to Triage" })).toBeNull();
    fireEvent.click(screen.getByRole("button", { name: "Triage to Done: Member" }));
    await act(async () => fireEvent.click(screen.getByRole("button", { name: "Remove" })));
    expect(source.runs).toEqual([{ command: "projects::step::delete", input: { id: "s1" } }]);
  });
});

describe("a timeline", () => {
  it("says a change no one made, like a migration's, was made by the system", () => {
    const view = { status: "live" as const, data: { history: [{ id: "c1", field: "board", before: null, after: "Product", created_at: new Date(2026, 9, 8) }] } };
    renderScreen(memorySource(), () => <Timeline view={view} list="history" />);
    expect(screen.getByText(/System/)).toBeTruthy();
  });
});

describe("cards", () => {
  it("sum up what each holds, and link to it filtered", () => {
    const page = {
      status: "live" as const,
      data: {
        boards: [{ id: "ark-main", title: "Product" }],
        issues: [
          { id: "i1", board: "ark-main", stage: "p1", "stage.title": "Triage", "stage.position": 1, "stage.status": "inactive" },
          { id: "i2", board: "ark-main", stage: "p2", "stage.title": "Done", "stage.position": 3, "stage.status": "success" },
          { id: "i3", board: "ark-main", stage: "p1", "stage.title": "Triage", "stage.position": 1, "stage.status": "inactive" },
          { id: "i4", board: "ark-main", stage: "p3", "stage.title": "Review", "stage.position": 2, "stage.status": "inactive" },
        ],
      },
    };
    renderScreen(memorySource(), () => (
      <Cards
        view={page}
        list="boards"
        columns={{ title: "Title" }}
        link="/:project/boards/:board"
        keyed={["project"]}
        tally={{ view: page, list: "issues", by: "board", and: "stage", shown: "stage.title", order: "stage.position", status: "stage.status", noun: "issues" }}
        filters={[{ label: "High priority", query: { priority: "high" } }]}
      />
    ));
    // The first stage hasn't started, one in between is under way, and one that's a success is done.
    expect(screen.getByText("4 issues, Triage 2 not started, Review 1 under way, Done 1 done")).toBeTruthy();
    expect(screen.getByRole("link", { name: "High priority" }).getAttribute("href")).toBe("/ark/boards/main?is=High+priority&priority=high");
  });
});

describe("a table keeping only some rows", () => {
  it("leaves out those holding a value, by a field read through what they point at", () => {
    const view = {
      status: "live" as const,
      data: {
        rows: [
          { id: "m1", "person.name": "Maya", "person.service": false },
          { id: "m2", "person.name": "publisher", "person.service": true },
          { id: "m3", "person.name": "Ade" },
        ],
      },
    };
    renderScreen(memorySource(), () => <Table view={view} columns={{ "person.name": "Name" }} only={{ "person.service!": "true" }} />);
    expect(screen.getAllByRole("row").slice(1).map((r) => r.textContent)).toEqual(["Maya", "Ade"]);
  });
});

describe("a table hidden when empty", () => {
  it("isn't there while it has no rows, and is once it has", () => {
    const empty = { status: "live" as const, data: { rows: [] } };
    const one = { status: "live" as const, data: { rows: [{ id: "r1", title: "Lost" }] } };
    const { unmount } = renderScreen(memorySource(), () => <Table view={empty} columns={{ title: "Your reports" }} hideEmpty />);
    expect(screen.queryByRole("table")).toBeNull();
    unmount();
    renderScreen(memorySource(), () => <Table view={one} columns={{ title: "Your reports" }} hideEmpty />);
    expect(screen.getByRole("table")).toBeTruthy();
  });

  it("isn't drawn while it loads, so it doesn't show for a moment and go", () => {
    const loading = { status: "loading" as const, data: undefined };
    renderScreen(memorySource(), () => <Table view={loading} columns={{ title: "Due soon" }} hideEmpty />);
    expect(screen.queryByRole("table")).toBeNull();
  });
});

describe("a table opened filtered", () => {
  it("keeps only the rows the address picks, and says so in words that clear it", () => {
    const source = memorySource({ views: { "projects::issues": { rows: [{ id: "i1", title: "Crash", priority: "high" }, { id: "i2", title: "Typo", priority: "low" }] } } });
    const only = defineScreen({ title: "Test", route: "/" }, () => <Table view={useView("projects::issues")} columns={{ title: "Title" }} />);
    render(<App name="app" screens={[only]} ui={plain} data={source} location="/?is=High+priority&priority=high" />);
    expect(screen.getAllByRole("row").slice(1).map((r) => r.textContent)).toEqual(["Crash"]);
    expect(screen.getByText(/High priority/)).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "Clear" }));
    expect(screen.getAllByRole("row").slice(1).map((r) => r.textContent)).toEqual(["Crash", "Typo"]);
  });
});

describe("a diagram", () => {
  it("lists each arrow to press, and adds one between two picked", async () => {
    HTMLDialogElement.prototype.showModal = function (this: HTMLDialogElement) {
      this.setAttribute("open", "");
    };
    const page = {
      status: "live" as const,
      data: {
        stages: [
          { id: "p1", title: "Triage" },
          { id: "p2", title: "Done" },
        ],
        steps: [{ id: "s1", from: "p1", to: "p2", "roles.title": ["Member"] }],
      },
    };
    const source = memorySource();
    renderScreen(source, () => (
      <Diagram
        view={page}
        list="steps"
        from="from"
        to="to"
        cell="roles.title"
        over={page}
        overList="stages"
        create={{ name: "projects::step::create", fields: [], submit: "Allow" }}
        remove={{ name: "projects::step::delete" }}
      />
    ));
    expect(screen.getByRole("button", { name: "Triage to Done: Member" })).toBeTruthy();
    fireEvent.change(screen.getByLabelText("From"), { target: { value: "p2" } });
    fireEvent.change(screen.getByLabelText("to"), { target: { value: "p1" } });
    fireEvent.click(screen.getByRole("button", { name: "Add Done to Triage" }));
    await act(async () => fireEvent.click(screen.getByRole("button", { name: "Allow" })));
    expect(source.runs).toEqual([{ command: "projects::step::create", input: { from: "p2", to: "p1" } }]);
  });
});

describe("a board", () => {
  const page = {
    status: "live" as const,
    data: {
      stages: [
        { id: "ark-reported", title: "Reported" },
        { id: "ark-triaged", title: "Triaged" },
        { id: "ark-closed", title: "Closed" },
      ],
      steps: [{ id: "s1", from: "ark-reported", to: "ark-triaged", roles: ["ark-member"] }],
      issues: [
        { id: "ark-1", title: "Crash", stage: "ark-reported" },
        { id: "ark-2", title: "Typo", stage: "ark-triaged" },
      ],
    },
  };
  const roles = { status: "live" as const, data: { rows: [{ id: "m1", project: "ark", role: "ark-member" }] } };

  it("puts each card in its column, and offers only the moves the person's roles may take, showing the card there at once", async () => {
    const source = memorySource();
    renderScreen(source, () => (
      <Board
        view={page}
        list="issues"
        by="stage"
        over={page}
        overList="stages"
        columns={{ title: "Title" }}
        move={{ command: "projects::issue::move", steps: page, list: "steps", held: "roles", roles, within: "ark" }}
      />
    ));
    const column = (name: string) => screen.getByRole("region", { name });
    expect(column("Reported").textContent).toContain("Crash");
    expect(screen.getAllByRole("button").map((b) => b.textContent)).toEqual(["Move to Triaged"]);
    await act(async () => fireEvent.click(screen.getByRole("button", { name: "Move to Triaged" })));
    expect(source.runs).toEqual([{ command: "projects::issue::move", input: { id: "ark-1", stage: "ark-triaged" } }]);
    expect(column("Triaged").textContent).toContain("Crash");
  });
});

describe("a board's cards", () => {
  it("say a number as one, and the people working each, by name and picture, apart from the rest", () => {
    const page = {
      status: "live" as const,
      data: {
        stages: [{ id: "ark-reported", title: "Reported" }],
        issues: [
          { id: "ark-1", title: "Crash", stage: "ark-reported", number: 1, labels: ["bug"], "assignees.name": ["Ada"], "assignees.picture": ["https://example.com/ada.png"] },
        ],
      },
    };
    const { container } = renderScreen(memorySource(), () => (
      <Board
        view={page}
        list="issues"
        by="stage"
        over={page}
        overList="stages"
        columns={{ title: "Title", number: "#", labels: "Labels", "assignees.name": "Assigned to" }}
        labels={["labels"]}
        people={{ "assignees.name": "assignees.picture" }}
      />
    ));
    const card = screen.getByRole("region", { name: "Reported" }).querySelector("li");
    expect(card?.querySelector("[data-details]")?.textContent).toBe("#1bug");
    const people = card?.querySelector("[data-people]");
    expect(people?.textContent).toContain("Ada");
    expect(people?.querySelector("img")?.getAttribute("src")).toBe("https://example.com/ada.png");
    expect(container.textContent).not.toContain("assignees");
  });
});

// A table or a board as far as a switch is concerned: something with a toolbar.
function Shown({ name, tools }: { name: string; tools?: ReactNode }) {
  return (
    <div>
      <p>{name}</p>
      {tools}
    </div>
  );
}

describe("a switch between a table and a board", () => {
  it("shows the one picked, and remembers it", () => {
    const kept = new Map<string, string>();
    vi.stubGlobal("localStorage", { getItem: (key: string) => kept.get(key) ?? null, setItem: (key: string, value: string) => void kept.set(key, value) });
    const shown = () =>
      renderScreen(memorySource(), () => (
        <Switched id="projects::project_page.issues" label="Show issues as" options={["Table", "Board"]} icons={["table", "board"]}>
          <Shown name="the table" />
          <Shown name="the board" />
        </Switched>
      ));
    const { unmount } = shown();
    expect(screen.getByText("the table")).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "Board" }));
    expect(screen.getByText("the board")).toBeTruthy();
    expect(screen.queryByText("the table")).toBeNull();
    unmount();
    shown();
    expect(screen.getByRole("button", { name: "Board" }).getAttribute("aria-pressed")).toBe("true");
    expect(screen.getByText("the board")).toBeTruthy();
    vi.unstubAllGlobals();
  });
  it("remembers it for each page, like each board, not once for all of them", () => {
    const kept = new Map<string, string>();
    vi.stubGlobal("localStorage", { getItem: (key: string) => kept.get(key) ?? null, setItem: (key: string, value: string) => void kept.set(key, value) });
    const board = defineScreen({ title: "Board", route: "/boards/:board", shown: true }, () => (
      <Switched id="projects::board_page.issues" label="Show issues as" options={["Table", "Board"]}>
        <Shown name="the table" />
        <Shown name="the board" />
      </Switched>
    ));
    const at = (location: string) => render(<App name="app" screens={[board]} ui={plain} data={memorySource()} location={location} />);
    const first = at("/boards/releases");
    fireEvent.click(screen.getByRole("button", { name: "Board" }));
    first.unmount();
    const other = at("/boards/features");
    expect(screen.getByText("the table")).toBeTruthy();
    other.unmount();
    at("/boards/releases");
    expect(screen.getByText("the board")).toBeTruthy();
    vi.unstubAllGlobals();
  });
});

describe("a switch's track", () => {
  it("is drawn once above the switch, whichever is shown, each milestone's dot from its issues' stages", () => {
    vi.stubGlobal("localStorage", { getItem: () => null, setItem: () => undefined });
    const source = memorySource({
      views: {
        "projects::board_page": {
          stages: [
            { id: "s1", title: "To do", status: "inactive" },
            { id: "s3", title: "Published", status: "success" },
            { id: "s9", title: "Failed", status: "failed" },
          ],
          milestones: [
            { id: "m13", title: "0.13.2" },
            { id: "m14", title: "0.14.0" },
            { id: "m15", title: "0.14.1" },
          ],
          issues: [
            { id: "i1", title: "compiler 0.13.2", stage: "s3", milestone: "m13" },
            { id: "i2", title: "compiler 0.14.0", stage: "s3", milestone: "m14" },
            { id: "i3", title: "tracker 0.14.0", stage: "s9", milestone: "m14" },
            { id: "i4", title: "compiler 0.14.1", stage: "s1", milestone: "m15" },
          ],
        },
      },
    });
    renderScreen(source, () => {
      const page = useView("projects::board_page");
      const track = { field: "milestone", view: page, list: "milestones", shown: "title", columns: {}, above: true };
      return (
        <Switched id="projects::board_page.issues" label="Show issues as" options={["Table", "Board"]} track={{ ...track, by: "stage", over: page, overList: "stages", statusBy: "status" }} view={page} list="issues">
          <Table view={page} list="issues" columns={{ title: "Title" }} track={track} />
          <Board view={page} list="issues" by="stage" over={page} overList="stages" columns={{ title: "Title" }} track={track} statusBy="status" />
        </Switched>
      );
    });
    const once = () => {
      const tracks = screen.getAllByRole("group", { name: "Milestone" });
      expect(tracks.length).toBe(1);
      // Above the switch.
      const switcher = screen.getByRole("button", { name: "Table" });
      expect(tracks[0]!.compareDocumentPosition(switcher) & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy();
    };
    once();
    expect(screen.getByRole("button", { name: /0\.13\.2/ }).querySelector("[aria-label=Succeeded]")).toBeTruthy();
    expect(screen.getByRole("button", { name: /0\.14\.0/ }).querySelector("[aria-label=Failed]")).toBeTruthy();
    expect(screen.getByRole("button", { name: /0\.14\.1/ }).querySelector("[role=img]")).toBeNull();
    fireEvent.click(screen.getByRole("button", { name: "Board" }));
    once();
    vi.unstubAllGlobals();
  });
});

describe("a track that's picked last", () => {
  it("keeps to the newest milestone with issues until another is picked, and picked again shows them all", () => {
    const source = memorySource({
      views: {
        "projects::board_page": {
          milestones: [
            { id: "m13", title: "0.13.2" },
            { id: "m14", title: "0.14.0" },
            { id: "m99", title: "0.15.0" },
          ],
          issues: [
            { id: "i1", title: "compiler 0.13.2", milestone: "m13" },
            { id: "i2", title: "compiler 0.14.0", milestone: "m14" },
          ],
        },
      },
    });
    renderScreen(source, () => {
      const page = useView("projects::board_page");
      return <Table view={page} list="issues" columns={{ title: "Title" }} track={{ field: "milestone", view: page, list: "milestones", shown: "title", columns: {}, hideEmpty: true, pickedLast: true }} />;
    });
    // 0.15.0 has no issues, so it's hidden, and 0.14.0 is the newest.
    expect(screen.queryByText("compiler 0.13.2")).toBeNull();
    expect(screen.getByText("compiler 0.14.0")).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: /0\.13\.2/ }));
    expect(screen.queryByText("compiler 0.14.0")).toBeNull();
    fireEvent.click(screen.getByRole("button", { name: /0\.13\.2/ }));
    expect(screen.getByText("compiler 0.13.2")).toBeTruthy();
    expect(screen.getByText("compiler 0.14.0")).toBeTruthy();
  });
});

describe("a table put in order", () => {
  it("moves a row with Alt and an arrow, giving it a place between its new neighbors", async () => {
    const source = memorySource({
      views: {
        "projects::workflow": {
          stages: [
            { id: "p1", title: "Triage", position: 1 },
            { id: "p2", title: "Ready", position: 2 },
            { id: "p3", title: "Done", position: 3 },
          ],
        },
      },
    });
    renderScreen(source, () => (
      <Table view={useView("projects::workflow")} list="stages" columns={{ title: "Stage" }} reorder={{ command: "projects::stage::update", field: "position" }} />
    ));
    const handle = screen.getByRole("button", { name: "Move Done" });
    await act(async () => fireEvent.keyDown(handle, { key: "ArrowUp", altKey: true }));
    expect(source.runs).toEqual([{ command: "projects::stage::update", input: { id: "p3", position: 1.5 } }]);
    await act(async () => fireEvent.keyDown(screen.getByRole("button", { name: "Move Triage" }), { key: "ArrowUp", altKey: true }));
    expect(source.runs.length).toBe(1);
  });

  it("numbers every row again when a row is put between two at the same place", async () => {
    const source = memorySource({
      views: {
        "projects::workflow": {
          stages: [
            { id: "p1", title: "Triage", position: 1 },
            { id: "p2", title: "Ready", position: 1000 },
            { id: "p3", title: "Done", position: 1000 },
            { id: "p4", title: "Closed", position: 1000 },
          ],
        },
      },
    });
    renderScreen(source, () => (
      <Table view={useView("projects::workflow")} list="stages" columns={{ title: "Stage" }} reorder={{ command: "projects::stage::update", field: "position" }} />
    ));
    await act(async () => fireEvent.keyDown(screen.getByRole("button", { name: "Move Closed" }), { key: "ArrowUp", altKey: true }));
    expect(source.runs.map((run) => run.input)).toEqual([
      { id: "p2", position: 2 },
      { id: "p4", position: 3 },
      { id: "p3", position: 4 },
    ]);
  });
});

describe("a list put in order across tabs and columns", () => {
  const issues = {
    issues: [
      { id: "a", title: "Alpha", status: "open", position: 1 },
      { id: "b", title: "Beta", status: "closed", position: 2 },
      { id: "c", title: "Gamma", status: "open", position: 3 },
      { id: "d", title: "Delta", status: "open", position: 4 },
    ],
  };

  it("moves a row within its tab between its neighbors there, and shows it in its place at once", async () => {
    const source = memorySource({ views: { "projects::board": issues } });
    renderScreen(source, () => (
      <Table
        view={useView("projects::board")}
        list="issues"
        by="status"
        choices={{ status: { open: "Open", closed: "Closed" } }}
        columns={{ title: "Title", status: "Status" }}
        reorder={{ command: "projects::issue::update", field: "position" }}
      />
    ));
    await act(async () => fireEvent.keyDown(screen.getByRole("button", { name: "Move Delta" }), { key: "ArrowUp", altKey: true }));
    expect(source.runs).toEqual([{ command: "projects::issue::update", input: { id: "d", position: 2 } }]);
    const names = screen.getAllByRole("button", { name: /^Move / }).map((b) => b.getAttribute("aria-label"));
    expect(names).toEqual(["Move Alpha", "Move Delta", "Move Gamma"]);
  });

  it("moves a board's card up and down its column, between the cards there", async () => {
    const page = {
      stages: [
        { id: "s-open", title: "Open" },
        { id: "s-closed", title: "Closed" },
      ],
      issues: issues.issues.map((issue) => ({ ...issue, stage: issue.status === "open" ? "s-open" : "s-closed" })),
    };
    const source = memorySource({ views: { "projects::board": page } });
    renderScreen(source, () => {
      const view = useView("projects::board");
      return (
        <Board
          view={view}
          list="issues"
          by="stage"
          over={view}
          overList="stages"
          columns={{ title: "Title" }}
          reorder={{ command: "projects::issue::update", field: "position" }}
        />
      );
    });
    const open = screen.getByRole("region", { name: "Open" });
    const card = (name: string) => within(open).getByText(name).closest("li")!;
    await act(async () => fireEvent.click(within(card("Alpha")).getByRole("button", { name: "Move down" })));
    expect(source.runs).toEqual([{ command: "projects::issue::update", input: { id: "a", position: 3.5 } }]);
    expect(Array.from(open.querySelectorAll("li")).map((li) => li.querySelector("a, span")?.textContent ?? li.firstChild?.textContent)).toEqual(["Gamma", "Alpha", "Delta"]);
    await act(async () => fireEvent.click(within(card("Delta")).getByRole("button", { name: "Move up" })));
    expect(source.runs[1]).toEqual({ command: "projects::issue::update", input: { id: "d", position: 3.25 } });
  });
});

describe("a value that isn't available", () => {
  it("says so in text, not in a label on an element with no role", () => {
    renderScreen(memorySource(), () => <Live view={useView("waitlist::signups")} field="total" />);
    const text = screen.getByText("not available right now");
    expect(text.closest("[aria-label]")).toBeNull();
  });
});

describe("a form's fields", () => {
  it("are described by their hint, and by the error once the form has one", async () => {
    const source = memorySource({
      commands: {
        "waitlist::signup::create": () => {
          throw new Error("that isn't an email address");
        },
      },
    });
    renderScreen(source, () => (
      <Form command="waitlist::signup::create" fields={[{ name: "email", type: "email", hint: "We only write once." }, "name"]} />
    ));
    const email = screen.getByLabelText("Email");
    const name = screen.getByLabelText("Name");
    const describedBy = (input: HTMLElement) =>
      (input.getAttribute("aria-describedby") ?? "")
        .split(" ")
        .filter(Boolean)
        .map((id) => document.getElementById(id)?.textContent);
    expect(describedBy(email)).toEqual(["We only write once."]);
    expect(name.getAttribute("aria-describedby")).toBeNull();
    expect(email.getAttribute("aria-invalid")).toBeNull();

    fireEvent.click(screen.getByRole("button", { name: "Create" }));
    await screen.findByRole("alert");
    expect(describedBy(email)).toEqual(["We only write once.", "that isn't an email address"]);
    expect(describedBy(name)).toEqual(["that isn't an email address"]);
    expect(email.getAttribute("aria-invalid")).toBe("true");
    expect(name.getAttribute("aria-invalid")).toBe("true");
  });
});

describe("files", () => {
  // Chosen, and what the choosing set off let finish.
  const choose = (input: HTMLElement, ...files: File[]) => act(async () => void fireEvent.change(input, { target: { files } }));

  it("are uploaded as they're chosen, and sent as their paths", async () => {
    const source = memorySource({ files: true });
    renderScreen(source, () => <Form command="press::order::create" fields={["title", { name: "proof", type: "file" }, { name: "pictures", type: "files" }]} />);
    await choose(screen.getByLabelText("Proof"), new File(["%PDF"], "proof.pdf", { type: "application/pdf" }));
    await choose(screen.getByLabelText("Pictures"), new File(["a"], "front.jpg"), new File(["b"], "back.jpg"));
    await vi.waitFor(() => expect(source.uploads.map((u) => u.file.name)).toEqual(["proof.pdf", "front.jpg", "back.jpg"]));
    await screen.findByText(/^back\.jpg/);
    // The one chosen first is taken away again before it's sent.
    fireEvent.click(screen.getByRole("button", { name: "Remove front.jpg" }));
    fireEvent.click(screen.getByRole("button", { name: "Create" }));
    await vi.waitFor(() =>
      expect(source.runs.at(-1)).toEqual({
        command: "press::order::create",
        input: { title: "", proof: "files/you/1/proof.pdf", pictures: ["files/you/3/back.jpg"] },
      }),
    );
  });

  it("aren't sent while one is still on its way, and say why one didn't arrive", async () => {
    let arrive: (path: string) => void = () => {};
    const source = memorySource({
      files: {
        upload: (file, progress) => {
          progress(0.5);
          if (file.name === "huge.mov") return Promise.reject(new Error("a file can hold at most 5 GB"));
          return new Promise((done) => (arrive = done));
        },
      },
    });
    renderScreen(source, () => <Form command="press::order::create" fields={[{ name: "pictures", type: "files" }]} />);
    await choose(screen.getByLabelText("Pictures"), new File(["a"], "front.jpg"), new File(["b"], "huge.mov"));
    await screen.findByText(/huge\.mov: a file can hold at most 5 GB/);
    expect(screen.getByText("front.jpg 50%")).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "Create" }));
    expect(source.runs).toEqual([]);
    await act(async () => arrive("files/you/9/front.jpg"));
    fireEvent.click(screen.getByRole("button", { name: "Remove huge.mov" }));
    await vi.waitFor(() => expect((screen.getByRole("button", { name: "Create" }) as HTMLButtonElement).disabled).toBe(false));
    fireEvent.click(screen.getByRole("button", { name: "Create" }));
    await vi.waitFor(() => expect(source.runs.at(-1)?.input).toEqual({ pictures: ["files/you/9/front.jpg"] }));
  });

  it("start from what's stored, a path a line", async () => {
    const source = memorySource({ files: true, views: { "press::order_page": { pictures: ["files/a/1/front.jpg", "files/a/2/back.jpg"] } } });
    renderScreen(source, () => {
      const page = useView("press::order_page");
      return <Form command="press::order::update" id="o1" from={page} fields={[{ name: "pictures", type: "files" }]} />;
    });
    fireEvent.click(await screen.findByRole("button", { name: "Remove front.jpg" }));
    fireEvent.click(screen.getByRole("button", { name: "Update" }));
    await vi.waitFor(() => expect(source.runs.at(-1)?.input).toEqual({ pictures: ["files/a/2/back.jpg"], id: "o1" }));
  });

  it("are shown as links that open each, a picture beside an image", async () => {
    const source = memorySource({ files: true, views: { "press::order_page": { proof: "files/a/1/proof.pdf", pictures: ["files/a/2/front.jpg"] } } });
    const opened = { location: { href: "" } };
    const open = vi.spyOn(window, "open").mockReturnValue(opened as unknown as Window);
    renderScreen(source, () => {
      const page = useView("press::order_page");
      return <Details view={page} fields={[["proof", "Proof"], ["pictures", "Pictures"]]} files={["proof", "pictures"]} />;
    });
    fireEvent.click(await screen.findByRole("button", { name: "proof.pdf" }));
    await vi.waitFor(() => expect(opened.location.href).toBe("memory:files/a/1/proof.pdf"));
    await vi.waitFor(() => expect(document.querySelector("img")?.getAttribute("src")).toBe("memory:files/a/2/front.jpg"));
    open.mockRestore();
  });
});

describe("a landing page", () => {
  it("is its hero, what it offers, and what it asks, drawn in its design", () => {
    renderScreen(memorySource(), () => (
      <Landing design="grid">
        <Hero title="Print that arrives on time.">
          <Text>Where a print shop takes its orders.</Text>
          <Link to="/signin">Sign in</Link>
        </Hero>
        <Section title="You approve every proof">
          <Text>Nothing goes to press unseen.</Text>
        </Section>
        <Section title="Ask for a quote" id="quote">
          <Text>Leave your address.</Text>
          <Form command="quote::request::create" fields={["email"]} />
        </Section>
      </Landing>
    ));
    const page = document.querySelector("[data-landing=grid]")!;
    expect(page.querySelector("h1")?.textContent).toBe("Print that arrives on time.");
    expect(screen.getByRole("link", { name: "Sign in" }).getAttribute("href")).toBe("/signin");
    const headings = Array.from(page.querySelectorAll("h2")).map((h) => h.textContent);
    expect(headings).toEqual(["You approve every proof", "Ask for a quote"]);
    expect(document.getElementById("quote")?.querySelector("form")).toBeTruthy();
  });
});

describe("a front page's design", () => {
  it("is given its offerings as a tree, its overlines, its statement and its steps, and an email link as written", () => {
    let given: LandingProps | undefined;
    const Drawn = plain.Landing;
    const ui = { ...plain, Landing: (p: LandingProps) => ((given = p), <Drawn {...p} />) };
    const only = defineScreen({ title: "Test", route: "/" }, () => (
      <Landing design="constellation">
        <Hero title="Modern software.">
          <Overline>Consulting</Overline>
          <Link to="#talk">Talk to us</Link>
        </Hero>
        <Section title="Five pillars.">
          <Overline>What we do</Overline>
          <Text>Ten services.</Text>
          <Section title="Data" icon="database">
            <Text>Moved and put to work.</Text>
            <Section title="Data Migration">
              <Text>Moved safely.</Text>
            </Section>
          </Section>
        </Section>
        <Statement>We keep what your systems know.</Statement>
        <Section title="How we work" steps>
          <Overline>Our way</Overline>
          <Section title="Understand">
            <Text>We learn.</Text>
          </Section>
          <Section title="Move">
            <Text>A piece at a time.</Text>
          </Section>
        </Section>
        <Section title="Talk" id="talk">
          <Overline>Contact</Overline>
          <Link to="mailto:hello@example.com">hello@example.com</Link>
        </Section>
      </Landing>
    ));
    render(<App name="app" screens={[only]} ui={ui} data={memorySource()} location="/" />);
    expect(given?.overline).toBe("Consulting");
    expect(given?.features).toHaveLength(1);
    const group = given!.features[0]!;
    expect([group.title, group.overline, group.features[0]?.title, group.features[0]?.icon, group.features[0]?.features[0]?.title]).toEqual([
      "Five pillars.",
      "What we do",
      "Data",
      "database",
      "Data Migration",
    ]);
    expect(given?.statement).toBe("We keep what your systems know.");
    expect(given?.steps.map((s) => [s.title, s.overline, s.steps.map((step) => step.title)])).toEqual([["How we work", "Our way", ["Understand", "Move"]]]);
    expect(given?.asks.map((a) => [a.title, a.id, a.overline])).toEqual([["Talk", "talk", "Contact"]]);
    expect(screen.getByRole("link", { name: "hello@example.com" }).getAttribute("href")).toBe("mailto:hello@example.com");
  });
});

describe("a canvas", () => {
  it("makes a card of an image dropped on it, where it landed, and draws the picture its card holds", async () => {
    const source = memorySource({
      views: { "ideas::wall": { rows: [{ id: "n1", title: "Sketch", files: ["files/you/0/sketch.png"], x: 40, y: 40, w: 160, h: 160 }] } },
      files: true,
    });
    renderScreen(source, () => (
      <Canvas
        view={useView("ideas::wall")}
        list="rows"
        columns={{ title: "Title" }}
        x="x"
        y="y"
        w="w"
        h="h"
        grid={{ step: 160, x: 40, y: 40 }}
        picture="files"
        paste={{ command: "ideas::note::create", into: "files", many: true, named: "title", given: { wall: "w1" } }}
      />
    ));
    const surface = screen.getByRole("application");
    const shot = new File(["png"], "image.png", { type: "image/png" });
    // jsdom's drag events carry no position, so it's given to this one.
    const drop = createEvent.drop(surface, { dataTransfer: { files: [shot], types: ["Files"] } });
    Object.defineProperties(drop, { clientX: { value: 330 }, clientY: { value: 210 } });
    await act(async () => {
      fireEvent(surface, drop);
    });
    await act(async () => {});
    expect(source.uploads.map((u) => u.file.name)).toEqual(["image.png"]);
    expect(source.runs).toEqual([
      { command: "ideas::note::create", input: { wall: "w1", files: [source.uploads[0]!.path], title: "Pasted image", x: 360, y: 200, w: 160, h: 160 } },
    ]);
    expect(document.querySelector("img")?.getAttribute("src")).toContain("sketch.png");
  });
});
