// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Cards that take pictures: each dropped, pasted or chosen made a record by their
// command, its file in the picture and its own size, name, bytes, date and camera in
// the fields that keep them; several at once one step to undo, and a drop target while empty.

import { fireEvent, render, waitFor } from "@testing-library/react";
import { App, Cards, DatedTimeline, Link, Messages, PaperButton, Picture, StepLink, Table, Toggle, columnsPlace, memorySource, mosaicRows, mosaicTile, tallOf, screen as defineScreen, dayTitle, markLength, marksAlong, rulerOf, timeGroups, useView } from "../src/index.js";
import { useNavigate, useParams } from "react-router";
import type { MemorySource } from "../src/index.js";
import { plain } from "../src/plain.js";
import { alreadyIn, alreadySaid, setToggled, takeInTurn } from "../src/workspace.js";
import { vi } from "vitest";
import { exif, jpeg } from "./photos.js";

const paste = {
  command: "studio::piece::create",
  fields: { picture: "image", picture_width: "picture_width", picture_height: "picture_height", name: "name", picture_bytes: "picture_bytes", picture_date: "picture_date", picture_camera: "picture_camera" },
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
    expect(first).toEqual({ room: "r1", image: expect.any(String), picture_width: 640, picture_height: 480, name: "harbor", picture_bytes: 4, picture_date: "2026-10-12T08:30:15Z", picture_camera: "" });
    expect(second).toMatchObject({ name: "Pasted image", picture_bytes: 2, "one:history": "joined" });
    expect(source.uploads.map((u) => u.file.name)).toEqual(["harbor.jpg", "image.png"]);
  });

  it("keep when a photo was taken and with what, as its camera says, in place of when its file changed", async () => {
    const source = memorySource({ files: true, commands: { "studio::piece::create": () => ({ id: "n1" }) } });
    source.set("studio::room_page", { pieces: [] }, "r1");
    const drawn = renderCards(source);
    await waitFor(() => expect(drawn.container.querySelector(".one-cards-taking")).toBeTruthy());
    const photo = jpeg(exif({ orientation: 6, make: "Canon", model: "Canon EOS R5", taken: "2026:10:12 08:30:15", offset: "+02:00" }));
    const files = [new File([photo], "harbor.jpg", { type: "image/jpeg", lastModified: Date.UTC(2026, 9, 20) })];
    fireEvent.drop(drawn.container.querySelector(".one-cards-taking")!, { dataTransfer: { files, getData: () => "", types: ["Files"] } });
    await waitFor(() => expect(source.runs.some((r) => r.command === "studio::piece::create")).toBe(true));
    expect(source.runs.find((r) => r.command === "studio::piece::create")!.input).toMatchObject({ picture_date: "2026-10-12T06:30:15Z", picture_camera: "Canon EOS R5" });
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

describe("cards that keep each picture once", () => {
  beforeEach(() => {
    globalThis.createImageBitmap = (async () => ({ width: 640, height: 480, close() {} })) as unknown as typeof createImageBitmap;
  });

  it("make no record of a picture whose file is a card's already, or one before it in the same batch, and say how many were", async () => {
    const hex = async (text: string) => [...new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text)))].map((b) => b.toString(16).padStart(2, "0")).join("");
    // Kept by its content, as a local store keeps files.
    const own = async (file: File) => `files/${await hex(await new Promise<string>((done) => {
      const reader = new FileReader();
      reader.onload = () => done(String(reader.result));
      reader.readAsText(file);
    }))}`;
    const source = memorySource({ files: { upload: own }, commands: { "studio::piece::create": () => ({ id: "n1" }) } });
    source.set("studio::room_page", { pieces: [{ id: "n0", name: "Harbor", image: `files/${await hex("harbor")}` }] }, "r1");
    function Pieces() {
      const page = useView("studio::room_page", "r1");
      return (
        <>
          <Cards view={page} list="pieces" columns={{ name: "Name" }} picture="image" paste={paste} name="/ cards pieces" />
          <Messages />
        </>
      );
    }
    const only = defineScreen({ title: "Pieces", route: "/" }, () => <Pieces />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.getByText("Harbor")).toBeTruthy());
    const files = [new File(["harbor"], "harbor.jpg", { type: "image/jpeg" }), new File(["dusk"], "dusk.jpg", { type: "image/jpeg" }), new File(["dusk"], "dusk copy.jpg", { type: "image/jpeg" }), new File(["pier"], "pier.jpg", { type: "image/jpeg" })];
    fireEvent.drop(drawn.container.querySelector(".one-cards-taking")!, { dataTransfer: { files, getData: () => "", types: ["Files"] } });
    await waitFor(() => expect(drawn.getByText("Added 2 pictures, 2 were already in.")).toBeTruthy());
    expect(source.runs.filter((r) => r.command === "studio::piece::create").map((r) => (r.input as Record<string, unknown>).name)).toEqual(["dusk", "pier"]);
    expect(source.uploads.map((u) => u.file.name)).toEqual(["dusk.jpg", "pier.jpg"]);
  });

  it("say so whatever came in", async () => {
    const said: unknown[] = [];
    await takeInTurn(["harbor", "dusk", "notes.txt"], (s) => said.push(s), async (name) => (name === "harbor" ? alreadyIn : name.endsWith(".txt") ? `${name} isn't a picture this can read.` : undefined));
    expect(said.at(-1)).toEqual({ text: "Added 1 picture, 1 was already in. notes.txt isn't a picture this can read.", problem: true });
    expect([alreadySaid(0, 1), alreadySaid(0, 9), alreadySaid(31, 9)]).toEqual(["That picture was already in.", "All 9 pictures were already in.", "Added 31 pictures, 9 were already in."]);
  });
});

describe("a folder of pictures", () => {
  let before: typeof createImageBitmap;
  afterEach(() => {
    globalThis.createImageBitmap = before;
  });
  beforeEach(() => {
    before = globalThis.createImageBitmap;
    // A picture is one whose bytes say so; the rest aren't.
    globalThis.createImageBitmap = (async (file: Blob) => {
      const said = await new Promise<string>((done) => {
        const reader = new FileReader();
        reader.onload = () => done(String(reader.result));
        reader.readAsText(file);
      });
      if (!said.startsWith("picture")) throw new Error("not a picture");
      return { width: 640, height: 480, close() {} };
    }) as unknown as typeof createImageBitmap;
  });

  it("is chosen in the browser's own chooser, and every picture in it comes in, past hidden folders and what isn't a picture, saying how many", async () => {
    const source = memorySource({ files: true, commands: { "studio::piece::create": () => ({ id: "n1" }) } });
    source.set("studio::room_page", { pieces: [] }, "r1");
    function Pieces() {
      const page = useView("studio::room_page", "r1");
      return (
        <>
          <PaperButton paper="/ cards pieces" label="Folder" chooses folder />
          <Cards view={page} list="pieces" columns={{ name: "Name" }} picture="image" paste={paste} name="/ cards pieces" />
          <Messages />
        </>
      );
    }
    const only = defineScreen({ title: "Pieces", route: "/" }, () => <Pieces />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.getByRole("button", { name: "Folder" })).toBeTruthy());
    let opened: HTMLInputElement | undefined;
    const click = HTMLInputElement.prototype.click;
    HTMLInputElement.prototype.click = function (this: HTMLInputElement) {
      opened = this;
    };
    try {
      fireEvent.click(drawn.getByRole("button", { name: "Folder" }));
    } finally {
      HTMLInputElement.prototype.click = click;
    }
    expect(opened?.hasAttribute("webkitdirectory")).toBe(true);
    const at = (path: string, words: string) => {
      const file = new File([words], path.split("/").pop()!, { type: "" });
      Object.defineProperty(file, "webkitRelativePath", { value: path });
      return file;
    };
    const files = [at("Summer/harbor.jpg", "picture harbor"), at("Summer/beach/dusk", "picture dusk"), at("Summer/.thumbnails/harbor.jpg", "picture small"), at("Summer/notes.txt", "notes"), at("Summer/.hidden.jpg", "picture hidden")];
    Object.defineProperty(opened!, "files", { value: files });
    fireEvent.change(opened!);
    await waitFor(() => expect(drawn.getByText("Added 2 pictures.")).toBeTruthy());
    expect(source.runs.filter((r) => r.command === "studio::piece::create").map((r) => (r.input as Record<string, unknown>).name)).toEqual(["harbor", "dusk"]);
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

describe("cards in columns", () => {
  it("lay each in the shortest column so far, the leftmost of those, by its own shape, one of no size square", () => {
    const laid = columnsPlace([0.5, 1.5, 1, 0.75, 1], 3);
    expect(laid.places.map((p) => [p.column, p.above, p.before])).toEqual([[0, 0, 0], [1, 0, 0], [2, 0, 0], [0, 0.5, 1], [2, 1, 1]]);
    expect(laid.heights).toEqual([1.25, 1.5, 2]);
    expect(tallOf({ picture_width: 400, picture_height: 300 })).toBe(0.75);
    expect(tallOf({})).toBe(1);
  });

  it("set each card in its column, in turn, under its heading, the columns level again under each", async () => {
    const source = memorySource();
    const when = (m: number, d: number) => new Date(Date.UTC(2026, m - 1, d, 12));
    const pieces = [when(10, 30), when(10, 2), when(10, 1), when(9, 3)].map((date, i) => ({ id: `c${i + 1}`, name: `Clipping ${i + 1}`, image: "", picture_date: date, picture_width: 100, picture_height: 100 * (i + 1) }));
    source.set("studio::room_page", { pieces }, "r1");
    function Pieces() {
      const page = useView("studio::room_page", "r1");
      return <Cards view={page} list="pieces" columns={{ name: "Name" }} picture="image" across={2} by={{ field: "picture_date", unit: "month" }} name="/ cards pieces" />;
    }
    const only = defineScreen({ title: "Pieces", route: "/" }, () => <Pieces />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.getAllByRole("article").length).toBe(4));
    const october = drawn.getByRole("region", { name: "October 2026" });
    const columns = [...october.firstElementChild!.nextElementSibling!.children].map((c) => [...c.querySelectorAll("article")].map((a) => a.textContent));
    // The first two across, the third under the shorter, the first.
    expect(columns).toEqual([["Clipping 1", "Clipping 3"], ["Clipping 2"]]);
    const september = drawn.getByRole("region", { name: "September 2026" });
    expect([...september.querySelectorAll("article")].map((a) => a.textContent)).toEqual(["Clipping 4"]);
  });
});

describe("cards under a heading for each month", () => {
  const at = (y: number, m: number, d: number) => new Date(Date.UTC(y, m - 1, d, 12));
  // Newest first: three in October 2026, two in September, one in March, two in 2025, one with no date.
  const dates = [at(2026, 10, 30), at(2026, 10, 12), at(2026, 10, 1), at(2026, 9, 20), at(2026, 9, 2), at(2026, 3, 8), at(2025, 12, 31), at(2025, 7, 4), undefined];
  const pieces = dates.map((when, i) => ({ id: `c${i + 1}`, name: `Clipping ${i + 1}`, image: "", picture_date: when }));

  it("are worked out in one pass in the list's order, and listed by year and month with their counts", () => {
    const groups = timeGroups(pieces, "picture_date", "month");
    expect(groups.map((g) => [g.key, g.title, g.first, g.count])).toEqual([
      ["2026-10", "October 2026", 0, 3],
      ["2026-09", "September 2026", 3, 2],
      ["2026-03", "March 2026", 5, 1],
      ["2025-12", "December 2025", 6, 1],
      ["2025-07", "July 2025", 7, 1],
      ["none", "No date", 8, 1],
    ]);
    const ruler = rulerOf(pieces, "picture_date", "month");
    expect(ruler.days.map((d) => `${dayTitle(d.day)} ${d.group} ${d.along.toFixed(2)} ${d.count}`)).toEqual([
      "October 30, 2026 0 0.00 1",
      "October 12, 2026 0 0.33 1",
      "October 1, 2026 0 0.67 1",
      "September 20, 2026 1 0.00 1",
      "September 2, 2026 1 0.50 1",
      "March 8, 2026 2 0.00 1",
      "December 31, 2025 3 0.00 1",
      "July 4, 2025 4 0.00 1",
    ]);
    expect(ruler.years).toEqual([
      { year: 2026, first: 0 },
      { year: 2025, first: 6 },
    ]);
    // Under a heading for each year, a day is as far into its year.
    expect(rulerOf(pieces, "picture_date", "year").days[6]!.group).toBe(1);
    const days = timeGroups(pieces, "picture_date", "day");
    expect(days[0]!.title).toBe("October 30, 2026");
    // A day of one is a short mark, one of hundreds no wider than the ruler, and more is longer.
    expect([markLength(1, 40), markLength(300, 40), markLength(3, 40) < markLength(10, 40)]).toEqual([8, 40, true]);
    // Days closer than two pixels are one mark of them all.
    expect(marksAlong([{ at: 0, count: 1 }, { at: 0.001, count: 2 }, { at: 0.5, count: 1 }], 1000)).toEqual([
      { y: 0, count: 3 },
      { y: 500, count: 1 },
    ]);
  });

  it("head each month, a mosaic's tiles starting again under each, and a timeline drawn along them says the day at the top, and scrolls them as it's pressed and by its keys", async () => {
    const source = memorySource({ files: true });
    source.set("studio::room_page", { pieces }, "r1");
    function Pieces() {
      const page = useView("studio::room_page", "r1");
      return (
        <>
          <Cards view={page} list="pieces" columns={{ name: "Name" }} picture="image" mosaic by={{ field: "picture_date", unit: "month" }} link="/pieces/:piece" name="/ cards pieces" />
          <DatedTimeline view={page} list="pieces" by="picture_date" unit="month" cards="/ cards pieces" />
        </>
      );
    }
    // Each heading a thousand pixels under the one before, its part of the cards to 32
    // short of the next, the page scrolled, all of it six thousand tall.
    let scrolled = 0;
    const order = () => [...document.querySelectorAll("section > h2")];
    const box = Element.prototype.getBoundingClientRect;
    const page = document.documentElement;
    Element.prototype.getBoundingClientRect = function (this: Element) {
      const n = order().indexOf(this.tagName === "SECTION" ? this.querySelector("h2")! : this);
      const top = n < 0 ? 0 : n * 1000 - scrolled;
      const bottom = n < 0 ? 0 : this.tagName === "SECTION" ? top + 968 : top + 40;
      return { top, left: 0, right: 0, bottom, width: 0, height: bottom - top, x: 0, y: top, toJSON() {} } as DOMRect;
    };
    Object.defineProperty(page, "scrollTop", { configurable: true, get: () => scrolled, set: (to: number) => void (scrolled = to) });
    Object.defineProperty(page, "scrollHeight", { configurable: true, get: () => 6000 });
    try {
      const only = defineScreen({ title: "Pieces", route: "/" }, () => <Pieces />);
      const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
      await waitFor(() => expect(drawn.getAllByRole("article").length).toBe(9));
      expect(order().map((h) => h.textContent)).toEqual(["October 2026", "September 2026", "March 2026", "December 2025", "July 2025", "No date"]);
      // September's first tile is the tall one at the left, as a mosaic's first is.
      const september = drawn.getByRole("region", { name: "September 2026" }).querySelectorAll("article");
      expect([september[0]!.style.gridColumn, september[0]!.style.gridRow, september[1]!.style.gridColumn]).toEqual(["1 / span 1", "1 / span 2", "2 / span 2"]);
      const ruler = drawn.getByRole("slider", { name: "Timeline" });
      expect(ruler.textContent).toBe("2026 2025 ");
      await waitFor(() => expect(ruler.getAttribute("aria-valuetext")).toBe("October 30, 2026"));
      // Scrolled past October and September, March's day is at the top.
      scrolled = 2010;
      fireEvent.scroll(document);
      await waitFor(() => expect(ruler.getAttribute("aria-valuetext")).toBe("March 8, 2026"));
      // The next heading, then back to the year's start, and to the end.
      fireEvent.keyDown(ruler, { key: "ArrowDown" });
      expect(scrolled).toBe(3000);
      fireEvent.keyDown(ruler, { key: "PageUp" });
      expect(scrolled).toBe(0);
      fireEvent.keyDown(ruler, { key: "End" });
      expect(scrolled).toBe(6000);
      fireEvent.keyDown(ruler, { key: "k" });
      expect(scrolled).toBe(5000);
      // Pressed halfway down it, the cards are scrolled halfway.
      if (!globalThis.PointerEvent) globalThis.PointerEvent = class extends MouseEvent {} as unknown as typeof PointerEvent;
      fireEvent.pointerDown(ruler, { button: 0, clientY: 0.5, pointerId: 1 });
      expect(scrolled).toBe(3000);
    } finally {
      Element.prototype.getBoundingClientRect = box;
      delete (page as { scrollTop?: number }).scrollTop;
      delete (page as { scrollHeight?: number }).scrollHeight;
    }
  });

  it("work out the days of twenty thousand rows quickly", () => {
    const many = Array.from({ length: 20000 }, (_, i) => ({ id: `c${i}`, picture_date: new Date(Date.UTC(2026, 9, 30) - i * 30 * 3600 * 1000) }));
    const started = performance.now();
    const ruler = rulerOf(many, "picture_date", "month");
    const took = performance.now() - started;
    expect(ruler.days.reduce((n, d) => n + d.count, 0)).toBe(20000);
    expect(ruler.years.length).toBe(69);
    expect(took).toBeLessThan(500);
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

  it("reads the whole pictures of the records before and after the one shown ahead, so a step shows the whole one at once, never a small copy first", async () => {
    const read: string[] = [];
    vi.stubGlobal(
      "Image",
      class {
        src = "";
        decode() {
          read.push(this.src);
          return Promise.resolve();
        }
      },
    );
    const source = memorySource({ files: true });
    source.files!.small = async (path: string, longest: number) => `small:${path}:${longest}`;
    const posters = ["Arches", "Bridges", "Canyons", "Dunes"].map((title, i) => ({ id: `p${i}`, title, image: `uploads/p${i}.png` }));
    source.set("studio::archive", { posters });
    function Posters() {
      const archive = useView("studio::archive");
      return <Cards view={archive} list="posters" columns={{ title: "Title" }} picture="image" link="/posters/:poster" />;
    }
    function Poster() {
      const archive = useView("studio::archive");
      const { poster } = useParams();
      return (
        <>
          <p>Showing {poster}</p>
          <StepLink view={archive} list="posters" to="/posters/:poster" opened picture="image" keys={["left"]}>
            Previous
          </StepLink>
          <StepLink view={archive} list="posters" to="/posters/:poster" next opened picture="image" keys={["right"]}>
            Next
          </StepLink>
          <Picture source={`uploads/${poster}.png`} fit alone />
        </>
      );
    }
    const screens = [defineScreen({ title: "Posters", route: "/posters" }, () => <Posters />), defineScreen({ title: "Poster", route: "/posters/:poster" }, () => <Poster />)];
    const drawn = render(<App name="studio" screens={screens} ui={plain} data={source} location="/posters" />);
    await waitFor(() => expect(drawn.getByText("Bridges")).toBeTruthy());
    fireEvent.click(drawn.getByText("Bridges"));
    await waitFor(() => expect(drawn.container.querySelector("p")?.textContent).toBe("Showing p1"));
    // The ones before and after, whole, decoded.
    await waitFor(() => expect(read).toEqual(expect.arrayContaining(["memory:uploads/p0.png", "memory:uploads/p2.png"])));
    // Every picture shown from here on.
    const shown: string[] = [];
    const watching = new MutationObserver(() => {
      const src = drawn.container.querySelector(".one-picture-alone img")?.getAttribute("src");
      if (src && shown.at(-1) !== src) shown.push(src);
    });
    watching.observe(drawn.container, { subtree: true, childList: true, attributes: true, attributeFilter: ["src"] });
    fireEvent.keyDown(window, { key: "ArrowRight" });
    // The first picture drawn on the next one's screen is its whole one.
    await waitFor(() => expect(drawn.container.querySelector("p")?.textContent).toBe("Showing p2"));
    expect(drawn.container.querySelector(".one-picture-alone img")?.getAttribute("src")).toBe("memory:uploads/p2.png");
    watching.disconnect();
    expect(shown).toEqual(["memory:uploads/p2.png"]);
    // And on from there, the next one's read ahead.
    await waitFor(() => expect(read).toContain("memory:uploads/p3.png"));
    drawn.unmount();
    vi.unstubAllGlobals();
  });

  it("steps by itself while a slideshow plays, reading the next picture ahead, and stops at the end, at a key or a click, and when it's opened afresh", async () => {
    const kept = new Map<string, string>();
    vi.stubGlobal("localStorage", { getItem: (key: string) => kept.get(key) ?? null, setItem: (key: string, value: string) => void kept.set(key, value) });
    const read: string[] = [];
    vi.stubGlobal(
      "Image",
      class {
        set src(to: string) {
          read.push(to);
        }
        decode() {
          return Promise.resolve();
        }
      },
    );
    const source = memorySource({ files: true });
    const posters = ["Arches", "Bridges", "Canyons", "Dunes"].map((title, i) => ({ id: `p${i}`, title, image: `uploads/p${i}.png` }));
    source.set("studio::archive", { posters });
    function Posters() {
      const archive = useView("studio::archive");
      return <Cards view={archive} list="posters" columns={{ title: "Title" }} picture="image" link="/posters/:poster" />;
    }
    function Poster() {
      const archive = useView("studio::archive");
      const { poster } = useParams();
      return (
        <>
          <p>Showing {poster}</p>
          <Toggle name="/posters/:poster show" label="Slideshow" keys={["s"]} />
          <StepLink view={archive} list="posters" to="/posters/:poster" next opened every={0.05} whileOn="/posters/:poster show">
            Next
          </StepLink>
          <Link to="/posters">Posters</Link>
        </>
      );
    }
    const screens = [defineScreen({ title: "Posters", route: "/posters" }, () => <Posters />), defineScreen({ title: "Poster", route: "/posters/:poster" }, () => <Poster />)];
    const drawn = render(<App name="studio" screens={screens} ui={plain} data={source} location="/posters" />);
    const showing = () => drawn.container.querySelector("p")?.textContent;
    const playing = () => kept.get("uione toggle /posters/:poster show");
    const opened = async (title: string, id: string) => {
      fireEvent.click(drawn.getByText("Posters"));
      await waitFor(() => expect(drawn.getByText(title)).toBeTruthy());
      fireEvent.click(drawn.getByText(title));
      await waitFor(() => expect(showing()).toBe(`Showing ${id}`));
    };
    await waitFor(() => expect(drawn.getByText("Bridges")).toBeTruthy());
    fireEvent.click(drawn.getByText("Bridges"));
    await waitFor(() => expect(showing()).toBe("Showing p1"));
    // On, it steps on by itself, the next picture read ahead, and goes off at the end.
    fireEvent.keyDown(window, { key: "s" });
    expect(playing()).toBe("on");
    await waitFor(() => expect(read.some((to) => to.includes("p2"))).toBe(true));
    await waitFor(() => expect(showing()).toBe("Showing p2"));
    await waitFor(() => expect(showing()).toBe("Showing p3"));
    await waitFor(() => expect(playing()).toBe("off"));
    // Any key stops it, and so does its own toggle's key, which doesn't start it again.
    await opened("Arches", "p0");
    fireEvent.keyDown(window, { key: "s" });
    await waitFor(() => expect(showing()).toBe("Showing p1"));
    fireEvent.keyDown(window, { key: "a" });
    expect(playing()).toBe("off");
    await new Promise((done) => setTimeout(done, 150));
    expect(showing()).toBe("Showing p1");
    fireEvent.keyDown(window, { key: "s" });
    expect(playing()).toBe("on");
    fireEvent.keyDown(window, { key: "s" });
    expect(playing()).toBe("off");
    await new Promise((done) => setTimeout(done, 150));
    expect(showing()).toBe("Showing p1");
    // And a click anywhere, its toggle's included.
    fireEvent.keyDown(window, { key: "s" });
    const toggle = drawn.getByText("Slideshow").closest("button")!;
    fireEvent.pointerDown(toggle);
    fireEvent.click(toggle);
    expect(playing()).toBe("off");
    await new Promise((done) => setTimeout(done, 150));
    expect(showing()).toBe("Showing p1");
    drawn.unmount();
    // Opened afresh, though it was left on, it starts off.
    setToggled("/posters/:poster show", true);
    const afresh = render(<App name="studio" screens={screens} ui={plain} data={source} location="/posters/p0" />);
    await waitFor(() => expect(afresh.container.querySelector("p")?.textContent).toBe("Showing p0"));
    await waitFor(() => expect(playing()).toBe("off"));
    afresh.unmount();
    vi.unstubAllGlobals();
  });

  it("steps through the list it was opened from, cards or a table in the order shown, and else through the one it names, not past either end", async () => {
    const source = memorySource();
    const posters = [
      { id: "p0", title: "Arches", year: 1990 },
      { id: "p1", title: "Bridges", year: 2010 },
      { id: "p2", title: "Canyons", year: 2000 },
      { id: "p3", title: "Dunes", year: 2020 },
    ];
    source.set("studio::archive", { posters });
    source.set("studio::by_year", { posters: [3, 1, 2, 0].map((i) => posters[i]!) });
    function Posters() {
      const archive = useView("studio::archive");
      return <Cards view={archive} list="posters" columns={{ title: "Title" }} link="/posters/:poster" />;
    }
    function Years() {
      const years = useView("studio::by_year");
      return <Table view={years} list="posters" columns={{ title: "Title", year: "Year" }} link="/posters/:poster" />;
    }
    function Poster() {
      const archive = useView("studio::archive");
      const { poster } = useParams();
      return (
        <>
          <p>Showing {poster}</p>
          <StepLink view={archive} list="posters" to="/posters/:poster" opened keys={["left"]}>
            Previous
          </StepLink>
          <StepLink view={archive} list="posters" to="/posters/:poster" next opened keys={["right"]}>
            Next
          </StepLink>
          <Link to="/years">Years</Link>
          <Link to="/posters">Posters</Link>
        </>
      );
    }
    const screens = [
      defineScreen({ title: "Posters", route: "/posters" }, () => <Posters />),
      defineScreen({ title: "Years", route: "/years" }, () => <Years />),
      defineScreen({ title: "Poster", route: "/posters/:poster" }, () => <Poster />),
    ];
    const drawn = render(<App name="studio" screens={screens} ui={plain} data={source} location="/years" />);
    const showing = () => drawn.container.querySelector("p")?.textContent;
    const next = () => drawn.getByText("Next").closest("button")!;
    // Opened from the years, newest first: Dunes, Bridges, Canyons, Arches.
    await waitFor(() => expect(drawn.getByText("Bridges")).toBeTruthy());
    fireEvent.click(drawn.getByText("Bridges"));
    await waitFor(() => expect(showing()).toBe("Showing p1"));
    await waitFor(() => expect(next().disabled).toBe(false));
    fireEvent.click(next());
    await waitFor(() => expect(showing()).toBe("Showing p2"));
    fireEvent.click(next());
    await waitFor(() => expect(showing()).toBe("Showing p0"));
    // The last of them, so it can't go on, though the archive has more after it.
    await waitFor(() => expect(next().disabled).toBe(true));
    // Opened from the cards, by title.
    fireEvent.click(drawn.getByText("Posters"));
    await waitFor(() => expect(drawn.getByText("Bridges")).toBeTruthy());
    fireEvent.click(drawn.getByText("Bridges"));
    await waitFor(() => expect(showing()).toBe("Showing p1"));
    fireEvent.keyDown(window, { key: "ArrowRight" });
    await waitFor(() => expect(showing()).toBe("Showing p2"));
    fireEvent.keyDown(window, { key: "ArrowRight" });
    await waitFor(() => expect(showing()).toBe("Showing p3"));
    await waitFor(() => expect(next().disabled).toBe(true));
    drawn.unmount();
    // Opened by its address alone, through the archive it names.
    const alone = render(<App name="studio" screens={screens} ui={plain} data={source} location="/posters/p0" />);
    await waitFor(() => expect(alone.getByText("Next").closest("button")!.disabled).toBe(false));
    fireEvent.click(alone.getByText("Next"));
    await waitFor(() => expect(alone.container.querySelector("p")?.textContent).toBe("Showing p1"));
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
