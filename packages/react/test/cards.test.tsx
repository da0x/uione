// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Cards that take pictures: each dropped, pasted or chosen made a record by their
// command, its file in the picture and its own size, name, bytes and date in the fields
// that keep them; several at once one step to undo, and a drop target while empty.

import { fireEvent, render, waitFor } from "@testing-library/react";
import { App, Cards, Link, PaperButton, StepLink, memorySource, mosaicRows, mosaicTile, screen as defineScreen, useView } from "../src/index.js";
import { useNavigate } from "react-router";
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

describe("cards as a mosaic", () => {
  it("place every eight as two blocks of a tall tile, a wide one and two small, the second mirrored, by their place alone", () => {
    const at = (n: number) => Object.values(mosaicTile(n));
    expect([0, 1, 2, 3].map(at)).toEqual([[0, 0, 1, 2], [1, 0, 2, 1], [1, 1, 1, 1], [2, 1, 1, 1]]);
    expect([4, 5, 6, 7].map(at)).toEqual([[2, 2, 1, 2], [0, 2, 2, 1], [0, 3, 1, 1], [1, 3, 1, 1]]);
    expect(at(8)).toEqual([0, 4, 1, 2]);
    expect(at(13)).toEqual([0, 6, 2, 1]);
    expect([0, 1, 4, 5, 11].map(mosaicRows)).toEqual([0, 2, 2, 4, 6]);
  });

  it("set each card in its tile, opened by its link, and still take pictures dropped on them", async () => {
    const source = memorySource({ files: true, commands: { "studio::piece::create": () => ({ id: "n1" }) } });
    source.set("studio::room_page", { pieces: Array.from({ length: 11 }, (_, i) => ({ id: `c${i + 1}`, name: `Clipping ${i + 1}`, image: "" })) }, "r1");
    function Pieces() {
      const page = useView("studio::room_page", "r1");
      return <Cards view={page} list="pieces" columns={{ name: "Name" }} picture="image" mosaic link="/pieces/:piece" paste={paste} name="/ cards pieces" />;
    }
    const only = defineScreen({ title: "Pieces", route: "/" }, () => <Pieces />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.getAllByRole("article").length).toBe(11));
    const tiles = drawn.getAllByRole("article");
    expect([tiles[0]!.style.gridColumn, tiles[0]!.style.gridRow]).toEqual(["1 / span 1", "1 / span 2"]);
    expect([tiles[4]!.style.gridColumn, tiles[4]!.style.gridRow]).toEqual(["3 / span 1", "3 / span 2"]);
    expect([tiles[10]!.style.gridColumn, tiles[10]!.style.gridRow]).toEqual(["2 / span 1", "6 / span 1"]);
    expect(drawn.getByRole("link", { name: "Clipping 11" }).getAttribute("href")).toBe("/pieces/c11");
    fireEvent.drop(drawn.container.querySelector(".one-cards-taking")!, { dataTransfer: { files: [new File(["abcd"], "harbor.jpg", { type: "image/jpeg" })], getData: () => "", types: ["Files"] } });
    await waitFor(() => expect(source.runs.some((r) => r.command === "studio::piece::create")).toBe(true));
  });
});

describe("a record's screen stepping through its list", () => {
  function renderArchive(source: MemorySource, from: string) {
    function Posters() {
      const archive = useView("studio::archive");
      return <Cards view={archive} list="posters" columns={{ title: "Title" }} link="/posters/:poster" />;
    }
    function Poster() {
      const archive = useView("studio::archive");
      const navigate = useNavigate();
      return (
        <>
          <StepLink view={archive} list="posters" to="/posters/:poster" icon="previous" keys={["left"]}>
            Previous
          </StepLink>
          <StepLink view={archive} list="posters" to="/posters/:poster" next icon="next" keys={["right"]}>
            Next
          </StepLink>
          <Link to="/posters" keys={["escape"]}>
            Posters
          </Link>
          <button type="button" onClick={() => void navigate(-1)}>
            Back
          </button>
        </>
      );
    }
    const screens = [defineScreen({ title: "Posters", route: "/posters" }, () => <Posters />), defineScreen({ title: "Poster", route: "/posters/:poster" }, () => <Poster />)];
    return render(<App name="studio" screens={screens} ui={plain} data={source} location={from} />);
  }

  function archive(count: number) {
    const source = memorySource();
    source.set("studio::archive", { posters: Array.from({ length: count }, (_, i) => ({ id: `p${i}`, title: `Poster ${i}` })) });
    return source;
  }

  // The page's scroll, which jsdom doesn't lay out: as tall as a long list.
  let scrolled = 0;
  beforeEach(() => {
    scrolled = 0;
    const page = document.scrollingElement ?? document.documentElement;
    Object.defineProperty(page, "scrollTop", { configurable: true, get: () => scrolled, set: (to: number) => void (scrolled = to) });
    Object.defineProperty(page, "scrollHeight", { configurable: true, get: () => 5000 });
    Object.defineProperty(page, "clientHeight", { configurable: true, get: () => 600 });
  });

  it("steps to the one before or after it in its list's own order, by its buttons or keys, and not past either end", async () => {
    const drawn = renderArchive(archive(3), "/posters/p0");
    await waitFor(() => expect(drawn.getByText("Next").closest("button")!.disabled).toBe(false));
    expect(drawn.getByText("Previous").closest("button")!.disabled).toBe(true);
    fireEvent.keyDown(window, { key: "ArrowLeft" });
    expect(drawn.getByText("Previous").closest("button")!.disabled).toBe(true);
    fireEvent.click(drawn.getByText("Next"));
    await waitFor(() => expect(drawn.getByText("Previous").closest("button")!.disabled).toBe(false));
    fireEvent.keyDown(window, { key: "ArrowRight" });
    // At the last, Next can't be followed, as it doesn't go around.
    await waitFor(() => expect(drawn.getByText("Next").closest("button")!.disabled).toBe(true));
    fireEvent.keyDown(window, { key: "ArrowRight" });
    fireEvent.keyDown(window, { key: "ArrowLeft" });
    await waitFor(() => expect(drawn.getByText("Next").closest("button")!.disabled).toBe(false));
    fireEvent.keyDown(window, { key: "ArrowLeft" });
    await waitFor(() => expect(drawn.getByText("Previous").closest("button")!.disabled).toBe(true));
  });

  it("goes back, or by a link, to the list scrolled where it was left, the card opened or stepped to last given the keyboard", async () => {
    const drawn = renderArchive(archive(30), "/posters");
    await waitFor(() => expect(drawn.getByText("Poster 12")).toBeTruthy());
    scrolled = 900;
    fireEvent.scroll(window);
    fireEvent.click(drawn.getByText("Poster 12"));
    await waitFor(() => expect(drawn.getByText("Next")).toBeTruthy());
    // A record opened afresh starts at its top.
    expect(scrolled).toBe(0);
    fireEvent.keyDown(window, { key: "ArrowRight" });
    fireEvent.keyDown(window, { key: "ArrowRight" });
    await new Promise((done) => setTimeout(done, 0));
    // By the window's own back.
    fireEvent.click(drawn.getByText("Back"));
    await waitFor(() => expect(drawn.getByText("Poster 14")).toBeTruthy());
    await waitFor(() => expect(document.activeElement?.textContent).toBe("Poster 14"));
    expect(scrolled).toBe(900);
    // And by a link to it, as Escape is on the record's screen.
    fireEvent.click(drawn.getByText("Poster 14"));
    await waitFor(() => expect(drawn.getByText("Next")).toBeTruthy());
    fireEvent.keyDown(window, { key: "ArrowRight" });
    await new Promise((done) => setTimeout(done, 0));
    fireEvent.keyDown(window, { key: "Escape" });
    await waitFor(() => expect(document.activeElement?.textContent).toBe("Poster 15"));
    expect(scrolled).toBe(900);
  });
});
