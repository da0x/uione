// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// The plain component set, as someone using a screen reader or a keyboard meets it.

import { act, fireEvent, render, screen } from "@testing-library/react";
import type { ReactNode } from "react";
import { afterEach, beforeEach, vi } from "vitest";
import { App, Board, Form, Grid, Live, Steps, Switched, Table, allows, listChoices, memorySource, screen as defineScreen, useView } from "../src/index.js";
import type { MemorySource } from "../src/index.js";
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
        columns={{ title: "Phase" }}
        actions={[{ name: "projects::phase::update", label: "Rename", form: { fields: [{ name: "title", label: "Called" }], submit: "Save" } }]}
      />
    ));
    fireEvent.click(screen.getByRole("button", { name: "Rename Open" }));
    expect(source.runs).toEqual([]);
    const called = screen.getByLabelText("Called") as HTMLInputElement;
    expect(called.value).toBe("Open");
    fireEvent.change(called, { target: { value: "Triage" } });
    await act(async () => fireEvent.click(screen.getByRole("button", { name: "Save" })));
    expect(source.runs).toEqual([{ command: "projects::phase::update", input: { title: "Triage", id: "p1" } }]);
    expect(screen.queryByRole("dialog")).toBeNull();
  });

  it("don't offer a row as a pick in a form about itself", () => {
    HTMLDialogElement.prototype.showModal = function (this: HTMLDialogElement) {
      this.setAttribute("open", "");
    };
    const source = memorySource({ views: { "projects::workflow": { rows: [{ id: "p1", title: "Triage" }, { id: "p2", title: "Done" }] } } });
    const phases: [string, string][] = [["p1", "Triage"], ["p2", "Done"]];
    renderScreen(source, () => (
      <Table
        view={useView("projects::workflow")}
        columns={{ title: "Phase" }}
        actions={[{ name: "projects::phase::delete", label: "Remove", form: { fields: [{ name: "into", label: "Move its issues to", type: "pick", choices: phases }] } }]}
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
    const issue = { status: "live" as const, data: { phase: "ark-ready" } };
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
      <Steps command="projects::issue::move" id="ark-1" field="phase" current={issue} steps={page} list="steps" shown="title" to="to.title" held="roles" roles={roles} within="ark" />
    ));
    expect(screen.getAllByRole("button").map((b) => b.textContent)).toEqual(["Start work", "Move to Closed"]);
    fireEvent.click(screen.getByRole("button", { name: "Start work" }));
    await vi.waitFor(() => expect(source.runs.at(-1)).toEqual({ command: "projects::issue::move", input: { id: "ark-1", phase: "ark-doing" } }));
  });

  it("say where nothing leads on for the person's roles, and say nothing to someone who holds none", () => {
    const issue = { status: "live" as const, data: { phase: "ark-review" } };
    const page = { status: "live" as const, data: { steps: [{ id: "s1", from: "ark-review", "from.title": "In review", to: "ark-done", roles: ["ark-tester"] }] } };
    const programmer = { status: "live" as const, data: { rows: [{ id: "m1", project: "ark", role: "ark-programmer" }] } };
    const outsider = { status: "live" as const, data: { rows: [] } };
    const steps = (roles: typeof programmer | typeof outsider) => (
      <Steps command="projects::issue::move" id="ark-1" field="phase" current={issue} steps={page} list="steps" from="from.title" held="roles" roles={roles} within="ark" />
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
      phases: [
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
        overList="phases"
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
      <Grid view={page} list="steps" from="from" to="to" cell="roles.title" over={page} overList="phases" create={{ name: "projects::step::create", allowed: false }} remove={{ name: "projects::step::delete" }} />
    ));
    expect(screen.queryByRole("button", { name: "Add Done to Triage" })).toBeNull();
    fireEvent.click(screen.getByRole("button", { name: "Triage to Done: Member" }));
    await act(async () => fireEvent.click(screen.getByRole("button", { name: "Remove" })));
    expect(source.runs).toEqual([{ command: "projects::step::delete", input: { id: "s1" } }]);
  });
});

describe("a board", () => {
  const page = {
    status: "live" as const,
    data: {
      phases: [
        { id: "ark-reported", title: "Reported" },
        { id: "ark-triaged", title: "Triaged" },
        { id: "ark-closed", title: "Closed" },
      ],
      steps: [{ id: "s1", from: "ark-reported", to: "ark-triaged", roles: ["ark-member"] }],
      issues: [
        { id: "ark-1", title: "Crash", phase: "ark-reported" },
        { id: "ark-2", title: "Typo", phase: "ark-triaged" },
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
        by="phase"
        over={page}
        overList="phases"
        columns={{ title: "Title" }}
        move={{ command: "projects::issue::move", steps: page, list: "steps", held: "roles", roles, within: "ark" }}
      />
    ));
    const column = (name: string) => screen.getByRole("region", { name });
    expect(column("Reported").textContent).toContain("Crash");
    expect(screen.getAllByRole("button").map((b) => b.textContent)).toEqual(["Move to Triaged"]);
    await act(async () => fireEvent.click(screen.getByRole("button", { name: "Move to Triaged" })));
    expect(source.runs).toEqual([{ command: "projects::issue::move", input: { id: "ark-1", phase: "ark-triaged" } }]);
    expect(column("Triaged").textContent).toContain("Crash");
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
});

describe("a table put in order", () => {
  it("moves a row with Alt and an arrow, giving it a place between its new neighbors", async () => {
    const source = memorySource({
      views: {
        "projects::workflow": {
          phases: [
            { id: "p1", title: "Triage", position: 1 },
            { id: "p2", title: "Ready", position: 2 },
            { id: "p3", title: "Done", position: 3 },
          ],
        },
      },
    });
    renderScreen(source, () => (
      <Table view={useView("projects::workflow")} list="phases" columns={{ title: "Phase" }} reorder={{ command: "projects::phase::update", field: "position" }} />
    ));
    const handle = screen.getByRole("button", { name: "Move Done" });
    await act(async () => fireEvent.keyDown(handle, { key: "ArrowUp", altKey: true }));
    expect(source.runs).toEqual([{ command: "projects::phase::update", input: { id: "p3", position: 1.5 } }]);
    await act(async () => fireEvent.keyDown(screen.getByRole("button", { name: "Move Triage" }), { key: "ArrowUp", altKey: true }));
    expect(source.runs.length).toBe(1);
  });

  it("numbers every row again when a row is put between two at the same place", async () => {
    const source = memorySource({
      views: {
        "projects::workflow": {
          phases: [
            { id: "p1", title: "Triage", position: 1 },
            { id: "p2", title: "Ready", position: 1000 },
            { id: "p3", title: "Done", position: 1000 },
            { id: "p4", title: "Closed", position: 1000 },
          ],
        },
      },
    });
    renderScreen(source, () => (
      <Table view={useView("projects::workflow")} list="phases" columns={{ title: "Phase" }} reorder={{ command: "projects::phase::update", field: "position" }} />
    ));
    await act(async () => fireEvent.keyDown(screen.getByRole("button", { name: "Move Closed" }), { key: "ArrowUp", altKey: true }));
    expect(source.runs.map((run) => run.input)).toEqual([
      { id: "p2", position: 2 },
      { id: "p4", position: 3 },
      { id: "p3", position: 4 },
    ]);
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
