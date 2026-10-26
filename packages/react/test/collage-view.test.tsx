// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Things coming into a collage: pictures dropped on it, the first covering it and the
// rest at their own size where they were let go, all one step to undo; words pasted.
// And going out: a small picture of it kept once changes settle, shown on cards and
// in tables.

import { act, fireEvent, render, waitFor } from "@testing-library/react";
import { App, Cards, Collage, Table, memorySource, screen as defineScreen, useView } from "../src/index.js";
import type { MemorySource } from "../src/index.js";
import { plain } from "../src/plain.js";

// Each of a collage's parts in the field named like it, as an app's are by default.
const parts = ["x", "y", "w", "h", "depth", "picture", "picture_width", "picture_height", "crop_x", "crop_y", "crop_w", "crop_h", "turns", "mirrored", "words", "size", "color", "weight", "background", "around", "zoom", "slide_x", "slide_y"];
const namedLikeParts = Object.fromEntries(parts.map((part) => [part, part]));

function renderCollage(source: MemorySource) {
  function Poster() {
    const page = useView("studio::poster_page", "p1");
    return <Collage view={page} list="layers" width="width" height="height" fields={namedLikeParts} add="studio::layer::create" parent="poster" subject="p1" paste />;
  }
  const only = defineScreen({ title: "Poster", route: "/" }, () => <Poster />);
  return render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
}

describe("things coming into a collage", () => {
  beforeEach(() => {
    // A picture's own size, as the browser reads it: 400 by 200, whatever it is.
    globalThis.createImageBitmap = (async () => ({ width: 400, height: 200, close() {} })) as unknown as typeof createImageBitmap;
    // Nothing is drawn here, only sent.
    HTMLCanvasElement.prototype.getContext = (() => null) as unknown as HTMLCanvasElement["getContext"];
  });

  it("covers itself with the first picture dropped, fits the rest, and keeps them one step", async () => {
    const source = memorySource({ files: true, commands: { "studio::layer::create": () => ({ id: "l" }) } });
    source.set("studio::poster_page", { width: 200, height: 100, layers: [] }, "p1");
    const drawn = renderCollage(source);
    const files = [new File(["a"], "a.png", { type: "image/png" }), new File(["b"], "b.png", { type: "image/png" })];
    fireEvent.drop(drawn.container.querySelector(".one-collage")!, { dataTransfer: { files, getData: () => "" } });
    await waitFor(() => expect(source.runs.length).toBe(2));
    const [first, second] = source.runs.map((r) => r.input as Record<string, unknown>);
    expect(first).toMatchObject({ x: 0, y: 0, w: 200, h: 100, poster: "p1", picture_width: 400, picture_height: 200 });
    expect(first!["one:history"]).toBeUndefined();
    expect(second).toMatchObject({ x: 0, y: 0, w: 200, h: 100, "one:history": "joined" });
    expect(source.uploads.map((u) => u.file.name)).toEqual(["a.png", "b.png"]);
  });

  it("fills in the own size of a picture kept before it was, as it's turned, never to be undone, in whichever fields keep it", async () => {
    globalThis.fetch = (async () => new Response(new Blob(["a"]))) as typeof fetch;
    const source = memorySource({ files: true, commands: { "studio::layer::update": () => ({ id: "l" }) } });
    source.set("studio::poster_page", {
      width: 200,
      height: 100,
      layers: [
        { id: "l1", image: "uploads/a.png", image_width: 0, image_height: 0, x: 0, y: 0, w: 200, h: 100 },
        { id: "l2", image: "uploads/b.png", image_width: 0, image_height: 0, turns: 1, x: 0, y: 0, w: 50, h: 100 },
        { id: "l3", image: "uploads/c.png", image_width: 30, image_height: 20, x: 0, y: 0, w: 30, h: 20 },
      ],
    }, "p1");
    function Poster() {
      const page = useView("studio::poster_page", "p1");
      // Its pictures kept in fields named otherwise, read and sent as those.
      const renamed = { ...namedLikeParts, picture: "image", picture_width: "image_width", picture_height: "image_height" };
      return <Collage view={page} list="layers" width="width" height="height" fields={renamed} subject="p1" paste update="studio::layer::update" />;
    }
    const only = defineScreen({ title: "Poster", route: "/" }, () => <Poster />);
    render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(source.runs.length).toBe(2));
    expect(source.runs.map((r) => r.input)).toEqual([
      { id: "l1", image_width: 400, image_height: 200, "one:history": "apart" },
      { id: "l2", image_width: 200, image_height: 400, "one:history": "apart" },
    ]);
  });

  it("puts words pasted in the middle, a twelfth of its height", async () => {
    const source = memorySource({ files: true, commands: { "studio::layer::create": () => ({ id: "l" }) } });
    source.set("studio::poster_page", { width: 1200, height: 600, layers: [] }, "p1");
    renderCollage(source);
    fireEvent.paste(document.body, { clipboardData: { files: [], getData: () => "Hello" } });
    await waitFor(() => expect(source.runs.length).toBe(1));
    expect(source.runs[0]!.input).toMatchObject({ words: "Hello", size: 50, poster: "p1" });
  });
});

describe("a collage's preview", () => {
  it("is drawn once changes settle, as wide as it says, and sent never to be undone", async () => {
    // A canvas that takes every drawing and makes a picture of nothing.
    const drawing = new Proxy({}, { get: (_, name) => (name === "measureText" ? () => ({ width: 10 }) : () => {}), set: () => true });
    HTMLCanvasElement.prototype.getContext = (() => drawing) as unknown as HTMLCanvasElement["getContext"];
    HTMLCanvasElement.prototype.toBlob = function (done: BlobCallback) {
      done(new Blob([`${this.width}x${this.height}`], { type: "image/png" }));
    };
    vi.useFakeTimers();
    const source = memorySource({ files: true, commands: { "studio::poster::preview": () => ({ id: "p1" }) } });
    source.set("studio::poster_page", { width: 1600, height: 900, layers: [{ id: "w1", words: "Harbor", size: 96, x: 0, y: 0, w: 300, h: 120 }] }, "p1");
    function Poster() {
      const page = useView("studio::poster_page", "p1");
      return <Collage view={page} list="layers" width="width" height="height" fields={namedLikeParts} subject="p1" preview="studio::poster::preview" previewInto="preview" previewWide={320} />;
    }
    const only = defineScreen({ title: "Poster", route: "/" }, () => <Poster />);
    render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await act(async () => {
      await vi.advanceTimersByTimeAsync(400);
    });
    expect(source.runs).toEqual([]);
    await act(async () => {
      await vi.advanceTimersByTimeAsync(600);
    });
    vi.useRealTimers();
    await waitFor(() => expect(source.runs.length).toBe(1));
    expect(source.runs[0]).toMatchObject({ command: "studio::poster::preview", input: { id: "p1", "one:history": "apart" } });
    const drawn = await new Promise<string>((done) => {
      const reading = new FileReader();
      reading.onload = () => done(String(reading.result));
      reading.readAsText(source.uploads[0]!.file);
    });
    expect(drawn).toBe("320x180");
  });
});

describe("cards with a picture", () => {
  it("show each row's file on top, fitted in", async () => {
    const source = memorySource({ files: true });
    source.set("studio::posters", { rows: [{ id: "p1", title: "Harbor", preview: "uploads/harbor.png" }, { id: "p2", title: "Blank" }] });
    function Gallery() {
      return <Cards view={useView("studio::posters")} list="rows" columns={{ title: "Title" }} picture="preview" fit />;
    }
    const only = defineScreen({ title: "Gallery", route: "/" }, () => <Gallery />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.container.querySelectorAll("img").length).toBe(1));
    expect(drawn.container.querySelector("img")!.getAttribute("src")).toContain("harbor.png");
  });
});

describe("a table with a picture", () => {
  it("shows each row's file small, before its cells, and leaves a row without one blank", async () => {
    const source = memorySource({ files: true });
    source.set("studio::posters", { rows: [{ id: "p1", title: "Harbor", preview: "uploads/harbor.png" }, { id: "p2", title: "Blank" }] });
    function Posters() {
      return <Table view={useView("studio::posters")} columns={{ title: "Title" }} picture="preview" fit />;
    }
    const only = defineScreen({ title: "Posters", route: "/" }, () => <Posters />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.container.querySelectorAll("tbody img").length).toBe(1));
    expect(drawn.container.querySelector("tbody img")!.getAttribute("src")).toContain("harbor.png");
    expect(drawn.container.querySelectorAll("tbody tr")[1]!.querySelectorAll("td").length).toBe(2);
  });
});
