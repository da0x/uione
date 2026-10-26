// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Things coming into a collage: pictures dropped on it, the first covering it and the
// rest at their own size where they were let go, all one step to undo; words pasted.
// And going out: a small picture of it kept once changes settle.

import { act, fireEvent, render, waitFor } from "@testing-library/react";
import { App, Cards, Collage, memorySource, screen as defineScreen, useView } from "../src/index.js";
import type { MemorySource } from "../src/index.js";
import { plain } from "../src/plain.js";

function renderCollage(source: MemorySource) {
  function Poster() {
    const page = useView("studio::poster_page", "p1");
    return <Collage view={page} list="layers" add="studio::layer::create" parent="poster" subject="p1" pasteInto="picture" sizedWidth="picture_width" sizedHeight="picture_height" />;
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
      return <Collage view={page} list="layers" subject="p1" preview="studio::poster::preview" previewInto="preview" previewWide={320} />;
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
