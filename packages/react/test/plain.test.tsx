// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// The plain component set, as someone using a screen reader or a keyboard meets it.

import { fireEvent, render, screen } from "@testing-library/react";
import type { ReactNode } from "react";
import { afterEach, vi } from "vitest";
import { App, Form, Live, Table, memorySource, screen as defineScreen, useView } from "../src/index.js";
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
