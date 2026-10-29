// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Cards that take pictures: each dropped, pasted or chosen made a record by their
// command, its file in the picture and its own size, name, bytes and date in the fields
// that keep them; several at once one step to undo, and a drop target while empty.

import { fireEvent, render, waitFor } from "@testing-library/react";
import { App, Cards, PaperButton, memorySource, screen as defineScreen, useView } from "../src/index.js";
import type { MemorySource } from "../src/index.js";
import { plain } from "../src/plain.js";
import { takeInTurn } from "../src/workspace.js";
import { vi } from "vitest";

const paste = {
  command: "studio::piece::create",
  fields: { picture: "image", picture_width: "picture_width", picture_height: "picture_height", name: "name", picture_bytes: "picture_bytes", picture_date: "picture_date" },
  given: { room: "r1" },
};

function renderCards(source: MemorySource) {
  function Pieces() {
    const page = useView("studio::room_page", "r1");
    return (
      <>
        <PaperButton paper="/ cards pieces" label="Pictures" chooses />
        <Cards view={page} list="pieces" columns={{ name: "Name" }} picture="image" paste={paste} name="/ cards pieces" />
      </>
    );
  }
  const only = defineScreen({ title: "Pieces", route: "/" }, () => <Pieces />);
  return render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
}

describe("cards that take pictures", () => {
  beforeEach(() => {
    globalThis.createImageBitmap = (async () => ({ width: 640, height: 480, close() {} })) as unknown as typeof createImageBitmap;
  });

  it("make a record of each picture dropped on them, even while they have none, all one step to undo", async () => {
    const source = memorySource({ files: true, commands: { "studio::piece::create": () => ({ id: "n1" }) } });
    source.set("studio::room_page", { pieces: [] }, "r1");
    const drawn = renderCards(source);
    await waitFor(() => expect(drawn.getByText("Nothing here yet. Drop pictures here to add them.")).toBeTruthy());
    const changed = Date.UTC(2026, 9, 12, 8, 30, 15, 250);
    const files = [new File(["abcd"], "harbor.jpg", { type: "image/jpeg", lastModified: changed }), new File(["ab"], "notes.txt", { type: "text/plain" }), new File(["xy"], "image.png", { type: "image/png", lastModified: changed })];
    fireEvent.drop(drawn.container.querySelector(".one-cards-taking")!, { dataTransfer: { files, getData: () => "", types: ["Files"] } });
    await waitFor(() => expect(source.runs.filter((r) => r.command === "studio::piece::create").length).toBe(2));
    const [first, second] = source.runs.filter((r) => r.command === "studio::piece::create").map((r) => r.input as Record<string, unknown>);
    expect(first).toEqual({ room: "r1", image: expect.any(String), picture_width: 640, picture_height: 480, name: "harbor", picture_bytes: 4, picture_date: "2026-10-12T08:30:15Z" });
    expect(second).toMatchObject({ name: "Pasted image", picture_bytes: 2, "one:history": "joined" });
    expect(source.uploads.map((u) => u.file.name)).toEqual(["harbor.jpg", "image.png"]);
  });

  it("make a record of each picture chosen by an add pictures button that names them", async () => {
    const source = memorySource({ files: true, commands: { "studio::piece::create": () => ({ id: "n1" }) } });
    source.set("studio::room_page", { pieces: [{ id: "n0", name: "Kept", image: "" }] }, "r1");
    const drawn = renderCards(source);
    await waitFor(() => expect(drawn.getByText("Kept")).toBeTruthy());
    let opened: HTMLInputElement | undefined;
    const click = HTMLInputElement.prototype.click;
    HTMLInputElement.prototype.click = function (this: HTMLInputElement) {
      opened = this;
    };
    try {
      fireEvent.click(drawn.getByRole("button", { name: "Pictures" }));
    } finally {
      HTMLInputElement.prototype.click = click;
    }
    Object.defineProperty(opened!, "files", { value: [new File(["a"], "beach.jpg", { type: "image/jpeg" })] });
    fireEvent.change(opened!);
    await waitFor(() => expect(source.runs.some((r) => r.command === "studio::piece::create")).toBe(true));
    expect(source.runs.find((r) => r.command === "studio::piece::create")!.input).toMatchObject({ room: "r1", name: "beach", picture_width: 640 });
  });
});

describe("pictures taken in turn", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("give the page back to the browser before each, saying how far they've got, and nothing once they're in", async () => {
    const happened: string[] = [];
    const later = globalThis.setTimeout;
    vi.spyOn(globalThis, "setTimeout").mockImplementation(((run: () => void, wait?: number) => later(() => (happened.push(`given back after ${wait}`), run()), wait)) as typeof setTimeout);
    const said: unknown[] = [];
    await takeInTurn(["harbor", "dusk", "pier"], (s) => said.push(s?.text), async (name) => void happened.push(name));
    expect(happened).toEqual(["given back after 0", "harbor", "given back after 0", "dusk", "given back after 0", "pier"]);
    expect(said).toEqual(["Adding 1 of 3 pictures…", "Adding 2 of 3 pictures…", "Adding 3 of 3 pictures…", undefined]);
  });

  it("say, once they're in, what couldn't be taken", async () => {
    const said: unknown[] = [];
    await takeInTurn(["notes.txt", "harbor.jpg"], (s) => said.push(s), async (name) => (name.endsWith(".txt") ? `${name} isn't a picture this can read.` : undefined));
    expect(said.at(-1)).toEqual({ text: "notes.txt isn't a picture this can read.", problem: true });
  });

  it("are drawn on cards from a smaller copy where the store makes them", async () => {
    const source = memorySource({ files: true });
    source.set("studio::room_page", { pieces: [{ id: "n0", name: "Kept", image: "files/abc" }] }, "r1");
    const asked: unknown[] = [];
    const files = { upload: async () => "files/abc", address: async (path: string) => `whole:${path}`, small: async (path: string, longest: number) => (asked.push([path, longest]), `small:${path}`) };
    function Pieces() {
      const page = useView("studio::room_page", "r1");
      return <Cards view={page} list="pieces" columns={{ name: "Name" }} picture="image" />;
    }
    const only = defineScreen({ title: "Pieces", route: "/" }, () => <Pieces />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={{ ...source, subscribe: source.subscribe, run: source.run, files }} location="/" />);
    await waitFor(() => expect(drawn.container.querySelector("img")?.getAttribute("src")).toBe("small:files/abc"));
    expect(asked).toEqual([["files/abc", 640]]);
  });
});
