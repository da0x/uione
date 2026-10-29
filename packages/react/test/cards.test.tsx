// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Cards that take pictures: each dropped, pasted or chosen made a record by their
// command, its file in the picture and its own size, name, bytes, date and camera in
// the fields that keep them; several at once one step to undo, and a drop target while empty.

import { fireEvent, render, waitFor } from "@testing-library/react";
import { App, Cards, DatedTimeline, Link, Messages, PaperButton, StepLink, memorySource, mosaicRows, mosaicTile, screen as defineScreen, timeGroups, useView, within, yearsOf } from "../src/index.js";
import { useNavigate } from "react-router";
import type { MemorySource } from "../src/index.js";
import { plain } from "../src/plain.js";
import { alreadyIn, alreadySaid, takeInTurn } from "../src/workspace.js";
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
    expect(yearsOf(groups, "month").map((e) => `${e.title} ${e.count}`)).toEqual(["2026 6", "October 3", "September 2", "March 1", "2025 2", "December 1", "July 1", "No date 1"]);
    expect(yearsOf(timeGroups(pieces, "picture_date", "year"), "year").map((e) => `${e.title} ${e.count}`)).toEqual(["2026 6", "2025 2", "No date 1"]);
    const days = timeGroups(pieces, "picture_date", "day");
    expect(days[0]!.title).toBe("October 30, 2026");
    expect(yearsOf(days, "day").map((e) => `${e.title} ${e.count}`).slice(0, 3)).toEqual(["2026 6", "October 3", "September 2"]);
    expect([within("2026-10-12", "2026-10"), within("2026-10", "2026"), within("2026-10", "2026-1"), within("2025-10", "2026")]).toEqual([true, true, false, false]);
  });

  it("head each month, a mosaic's tiles starting again under each, and a timeline marks the one in view and scrolls to the one chosen", async () => {
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
    // Each heading a thousand pixels under the one before, scrolled to by scrollIntoView.
    let scrolled = 0;
    const order = () => [...document.querySelectorAll("section > h2")];
    const box = Element.prototype.getBoundingClientRect;
    const into = Element.prototype.scrollIntoView;
    Element.prototype.getBoundingClientRect = function (this: Element) {
      const n = order().indexOf(this);
      return { top: n < 0 ? 0 : n * 1000 - scrolled, left: 0, right: 0, bottom: 0, width: 0, height: 0, x: 0, y: 0, toJSON() {} } as DOMRect;
    };
    Element.prototype.scrollIntoView = function (this: Element) {
      scrolled = order().indexOf(this) * 1000;
    };
    try {
      const only = defineScreen({ title: "Pieces", route: "/" }, () => <Pieces />);
      const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
      await waitFor(() => expect(drawn.getAllByRole("article").length).toBe(9));
      expect(order().map((h) => h.textContent)).toEqual(["October 2026", "September 2026", "March 2026", "December 2025", "July 2025", "No date"]);
      // September's first tile is the tall one at the left, as a mosaic's first is.
      const september = drawn.getByRole("region", { name: "September 2026" }).querySelectorAll("article");
      expect([september[0]!.style.gridColumn, september[0]!.style.gridRow, september[1]!.style.gridColumn]).toEqual(["1 / span 1", "1 / span 2", "2 / span 2"]);
      const entries = () => drawn.getAllByRole("button").map((b) => `${b.textContent}${b.getAttribute("aria-current") ? " *" : ""}`);
      await waitFor(() => expect(entries()).toEqual(["2026 6", "October 3 *", "September 2", "March 1", "2025 2", "December 1", "July 1", "No date 1"]));
      // Scrolled past October and September, March is the one in view.
      scrolled = 2010;
      fireEvent.scroll(document);
      await waitFor(() => expect(entries()[3]).toBe("March 1 *"));
      // 2025 chosen scrolls its first month's heading to the top, and it's marked.
      fireEvent.click(drawn.getByRole("button", { name: "2025 2" }));
      expect(scrolled).toBe(3000);
      await waitFor(() => expect(entries()[5]).toBe("December 1 *"));
      fireEvent.click(drawn.getByRole("button", { name: "July 1" }));
      expect(scrolled).toBe(4000);
    } finally {
      Element.prototype.getBoundingClientRect = box;
      Element.prototype.scrollIntoView = into;
    }
  });

  it("work out the months of twenty thousand rows quickly", () => {
    const many = Array.from({ length: 20000 }, (_, i) => ({ id: `c${i}`, picture_date: new Date(Date.UTC(2026, 9, 30) - i * 3 * 3600 * 1000) }));
    const started = performance.now();
    const entries = yearsOf(timeGroups(many, "picture_date", "month"), "month");
    const took = performance.now() - started;
    expect(entries.filter((e) => !e.year).reduce((n, e) => n + e.count, 0)).toBe(20000);
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
