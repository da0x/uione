// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Things coming into a collage: pictures dropped on it, the first covering it and the
// rest at their own size where they were let go, all one step to undo; words pasted.
// And going out: a small picture of it kept once changes settle, shown on cards, in
// tables, and on its own. And a layer drawn by a drag.

import { act, fireEvent, render, waitFor, within } from "@testing-library/react";
import { App, Cards, Collage, CollageButton, Layers, Picture, Table, memorySource, screen as defineScreen, useView } from "../src/index.js";
import { currentLayer, inLayers } from "../src/collage-view.js";
import type { MemorySource } from "../src/index.js";
import { setChosen, setPicked, useChosen, usePickedId } from "../src/workspace.js";
import { plain } from "../src/plain.js";

// Each of a collage's parts in the field named like it, as an app's are by default.
const parts = ["x", "y", "w", "h", "depth", "picture", "picture_width", "picture_height", "crop_x", "crop_y", "crop_w", "crop_h", "turns", "mirrored", "words", "size", "color", "weight", "around"];
const namedLikeParts = Object.fromEntries(parts.map((part) => [part, part]));

// What's picked, said so a test can read it.
function Picked() {
  return <p data-testid="picked">{usePickedId("/ layer") ?? ""}</p>;
}

function renderCollage(source: MemorySource, pasteKeys?: string[]) {
  function Poster() {
    const page = useView("studio::poster_page", "p1");
    return (
      <>
        {pasteKeys && <CollageButton collage="/ collage" does="paste" label="Paste" keys={pasteKeys} />}
        <Collage view={page} list="layers" width="width" height="height" fields={namedLikeParts} add="studio::layer::create" parent="poster" subject="p1" name="/ collage" pick="/ layer" paste />
        <Picked />
      </>
    );
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

  it("fills itself with the first picture dropped, fits the rest, keeps them one step, and picks the last", async () => {
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
    // The last of them picked once it's made, ready to be worked on.
    await waitFor(() => expect(drawn.getByTestId("picked").textContent).toBe("l"));
  });

  it("takes an SVG dropped as one, as big as it says, never covering it, nor as pixels it can't read", async () => {
    const source = memorySource({ files: true, commands: { "studio::layer::create": () => ({ id: "l" }) } });
    source.set("studio::poster_page", { width: 200, height: 100, layers: [] }, "p1");
    const drawn = renderCollage(source);
    const svg = new File(['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 12"><rect width="24" height="12"/></svg>'], "icon.svg", { type: "image/svg+xml" });
    fireEvent.drop(drawn.container.querySelector(".one-collage")!, { dataTransfer: { files: [svg], getData: () => "" } });
    await waitFor(() => expect(source.runs.length).toBe(1));
    // Even the first on an empty collage: drawing, not a photograph, so it doesn't cover it.
    expect(source.runs[0]!.input).toMatchObject({ x: 88, y: 44, w: 24, h: 12, picture_width: 24, picture_height: 12 });
    expect(source.uploads.map((u) => u.file.name)).toEqual(["icon.svg"]);
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

  it("puts words pasted with its Paste line's ctrl+v in the middle, a twelfth of its height", async () => {
    const source = memorySource({ files: true, commands: { "studio::layer::create": () => ({ id: "l" }) } });
    source.set("studio::poster_page", { width: 1200, height: 600, layers: [] }, "p1");
    renderCollage(source, ["ctrl+v"]);
    fireEvent.paste(document.body, { clipboardData: { files: [], getData: () => "Hello" } });
    await waitFor(() => expect(source.runs.length).toBe(1));
    expect(source.runs[0]!.input).toMatchObject({ words: "Hello", size: 50, poster: "p1" });
  });

  it("takes an SVG's text pasted, as a drawing program copies one, as that SVG, not as words", async () => {
    const source = memorySource({ files: true, commands: { "studio::layer::create": () => ({ id: "l" }) } });
    source.set("studio::poster_page", { width: 200, height: 100, layers: [] }, "p1");
    renderCollage(source, ["ctrl+v"]);
    const svg = '<?xml version="1.0"?>\n<svg xmlns="http://www.w3.org/2000/svg" width="40" height="20"><rect width="40" height="20"/></svg>';
    fireEvent.paste(document.body, { clipboardData: { files: [], getData: () => svg } });
    await waitFor(() => expect(source.runs.length).toBe(1));
    expect(source.runs[0]!.input).toMatchObject({ w: 40, h: 20, picture_width: 40, picture_height: 20 });
    expect(source.runs[0]!.input).not.toHaveProperty("words");
  });

  it("takes no paste no line asks for, as a key is only ever a line's", async () => {
    const source = memorySource({ files: true, commands: { "studio::layer::create": () => ({ id: "l" }) } });
    source.set("studio::poster_page", { width: 1200, height: 600, layers: [] }, "p1");
    renderCollage(source);
    fireEvent.paste(document.body, { clipboardData: { files: [], getData: () => "Hello" } });
    await new Promise((settled) => setTimeout(settled, 50));
    expect(source.runs).toEqual([]);
  });
});

describe("a crop and words, in place", () => {
  beforeEach(() => {
    HTMLCanvasElement.prototype.getContext = (() => null) as unknown as HTMLCanvasElement["getContext"];
  });

  function renderPoster(source: MemorySource) {
    source.set("studio::poster_page", {
      width: 1600,
      height: 900,
      layers: [
        { id: "b0", picture: "files/dusk", x: 0, y: 0, w: 1600, h: 900 },
        { id: "s1", picture: "files/sun", x: 100, y: 100, w: 300, h: 300 },
        { id: "w1", words: "Harbor", size: 96, x: 120, y: 600, w: 300, h: 120 },
      ],
    }, "p1");
    function Poster() {
      const page = useView("studio::poster_page", "p1");
      return (
        <>
          <CollageButton collage="/ layer" does="crop" label="Crop" />
          <CollageButton collage="/ layer" does="edit" label="Edit" />
          <Collage view={page} list="layers" width="width" height="height" fields={namedLikeParts} pick="/ layer" name="/ layer" subject="p1" crop="studio::layer::crop" write="studio::layer::write" place="studio::layer::place" />
        </>
      );
    }
    const only = defineScreen({ title: "Poster", route: "/" }, () => <Poster />);
    return render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
  }

  it("crops the picked picture, kept with Enter: all of it, where it was", async () => {
    const source = memorySource({ commands: { "studio::layer::crop": (input) => ({ id: String(input["id"]) }) } });
    const drawn = renderPoster(source);
    await waitFor(() => expect(drawn.container.querySelector(".one-collage")).toBeTruthy());
    act(() => setPicked("/ layer", "s1"));
    fireEvent.click(drawn.getByRole("button", { name: "Crop" }));
    await act(async () => {});
    fireEvent.keyDown(window, { key: "Enter" });
    await waitFor(() => expect(source.runs.length).toBe(1));
    expect(source.runs[0]).toMatchObject({ command: "studio::layer::crop", input: { id: "s1", crop_w: 1, crop_h: 1, x: 100, y: 100, w: 300, h: 300 } });
  });

  it("crops the lowest picture as any other, where it is, kept with Enter", async () => {
    const source = memorySource({ commands: { "studio::layer::crop": (input) => ({ id: String(input["id"]) }) } });
    const drawn = renderPoster(source);
    await waitFor(() => expect(drawn.container.querySelector(".one-collage")).toBeTruthy());
    act(() => setPicked("/ layer", "b0"));
    fireEvent.click(drawn.getByRole("button", { name: "Crop" }));
    await act(async () => {});
    fireEvent.keyDown(window, { key: "Enter" });
    await waitFor(() => expect(source.runs.length).toBe(1));
    expect(source.runs[0]).toMatchObject({ command: "studio::layer::crop", input: { id: "b0", crop_w: 1, crop_h: 1, x: 0, y: 0, w: 1600, h: 900 } });
  });

  it("writes the picked words where they are, what's typed after them, kept with Enter as big as they now are", async () => {
    const source = memorySource({ commands: { "studio::layer::write": (input) => ({ id: String(input["id"]) }) } });
    const drawn = renderPoster(source);
    await waitFor(() => expect(drawn.container.querySelector(".one-collage")).toBeTruthy());
    act(() => setPicked("/ layer", "w1"));
    fireEvent.click(drawn.getByRole("button", { name: "Edit" }));
    await act(async () => {});
    fireEvent.keyDown(window, { key: "End" });
    fireEvent.keyDown(window, { key: "!" });
    fireEvent.keyDown(window, { key: "Enter" });
    await waitFor(() => expect(source.runs.length).toBe(1));
    const sent = source.runs[0]!.input as Record<string, unknown>;
    expect(source.runs[0]!.command).toBe("studio::layer::write");
    expect(sent).toMatchObject({ id: "w1", words: "Harbor!", size: 96, x: 120, y: 600 });
    expect(sent["w"]).toBeGreaterThan(300);
  });
});

describe("a layer drawn on a collage", () => {
  beforeEach(() => {
    // A canvas 1064 by 564, so a collage 1000 by 500 is drawn at its own size inside
    // its margin of 32, with a context that draws nothing.
    const nothing: unknown = new Proxy(() => nothing, { get: (_, key) => (key === "measureText" ? () => ({ width: 0 }) : nothing), set: () => true });
    HTMLCanvasElement.prototype.getContext = (() => nothing) as unknown as HTMLCanvasElement["getContext"];
    Object.defineProperty(HTMLCanvasElement.prototype, "clientWidth", { configurable: true, get: () => 1064 });
    Object.defineProperty(HTMLCanvasElement.prototype, "clientHeight", { configurable: true, get: () => 564 });
    HTMLCanvasElement.prototype.setPointerCapture = () => {};
    if (!globalThis.PointerEvent) globalThis.PointerEvent = class extends MouseEvent {} as unknown as typeof PointerEvent;
    // A path drawn on a context that draws nothing.
    if (!globalThis.Path2D) globalThis.Path2D = class { addPath() {} roundRect() {} ellipse() {} } as unknown as typeof Path2D;
  });
  afterEach(() => {
    delete (HTMLCanvasElement.prototype as { clientWidth?: number }).clientWidth;
    delete (HTMLCanvasElement.prototype as { clientHeight?: number }).clientHeight;
  });

  it("makes a layer as big as the drag, given what its draw says, picks it, and sets the choice its when read back to the first", async () => {
    const source = memorySource({ commands: { "studio::layer::create": () => ({ id: "drawn" }) } });
    source.set("studio::poster_page", { width: 1000, height: 500, layers: [] }, "p1");
    setChosen("/ tool", "box");
    function Poster() {
      const page = useView("studio::poster_page", "p1");
      const tool = useChosen("/ tool", "move");
      return (
        <>
          <Collage view={page} list="layers" width="width" height="height" fields={namedLikeParts} parent="poster" subject="p1" pick="/ layer" name="/ layer"
            draws={[{ command: "studio::layer::create", given: { fill: "#2e86de" }, when: tool === "box", back: [["/ tool", "move"]] }]} />
          <p data-testid="tool">{tool}</p>
          <Picked />
        </>
      );
    }
    const only = defineScreen({ title: "Poster", route: "/" }, () => <Poster />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.container.querySelector("canvas")).toBeTruthy());
    const canvas = drawn.container.querySelector("canvas")!;
    await act(async () => {});
    fireEvent.pointerDown(canvas, { clientX: 332, clientY: 232, pointerId: 1 });
    fireEvent.pointerMove(canvas, { clientX: 232, clientY: 182, pointerId: 1 });
    fireEvent.pointerUp(canvas, { clientX: 132, clientY: 132, pointerId: 1 });
    await waitFor(() => expect(source.runs.length).toBe(1));
    expect(source.runs[0]).toMatchObject({ command: "studio::layer::create", input: { x: 100, y: 100, w: 200, h: 100, fill: "#2e86de", poster: "p1" } });
    await waitFor(() => expect(drawn.getByTestId("picked").textContent).toBe("drawn"));
    expect(drawn.getByTestId("tool").textContent).toBe("move");
  });

  it("picks a path by its stroke alone, shows its nodes, and reshapes it by one dragged, its box worked out again", async () => {
    act(() => setPicked("/ layer", undefined));
    const source = memorySource({ commands: { "studio::layer::reshape": () => ({ id: "l1" }) } });
    source.set("studio::poster_page", { width: 1000, height: 500, layers: [{ id: "l1", shape: "path", points: "M 0 0 L 1 1", stroke: "#1d1d1f", stroke_width: 2, x: 100, y: 100, w: 200, h: 100 }] }, "p1");
    function Poster() {
      const page = useView("studio::poster_page", "p1");
      return (
        <>
          <Collage view={page} list="layers" width="width" height="height" fields={{ ...namedLikeParts, points: "points" }} pick="/ layer" name="/ layer" place="studio::layer::place" reshape="studio::layer::reshape" />
          <Picked />
        </>
      );
    }
    const only = defineScreen({ title: "Poster", route: "/" }, () => <Poster />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.container.querySelector("canvas")).toBeTruthy());
    const canvas = drawn.container.querySelector("canvas")!;
    await act(async () => {});
    // Inside its box, far from its stroke: nothing.
    fireEvent.pointerDown(canvas, { clientX: 32 + 280, clientY: 32 + 110, pointerId: 1 });
    fireEvent.pointerUp(canvas, { clientX: 32 + 280, clientY: 32 + 110, pointerId: 1 });
    expect(drawn.getByTestId("picked").textContent).toBe("");
    // On its stroke: picked.
    fireEvent.pointerDown(canvas, { clientX: 32 + 200, clientY: 32 + 151, pointerId: 1 });
    fireEvent.pointerUp(canvas, { clientX: 32 + 200, clientY: 32 + 151, pointerId: 1 });
    await waitFor(() => expect(drawn.getByTestId("picked").textContent).toBe("l1"));
    await act(async () => {});
    // Its first node, at 100 100, dragged down 50: from 100 150 to 300 200.
    fireEvent.pointerDown(canvas, { clientX: 32 + 100, clientY: 32 + 100, pointerId: 1 });
    fireEvent.pointerMove(canvas, { clientX: 32 + 100, clientY: 32 + 130, pointerId: 1 });
    fireEvent.pointerUp(canvas, { clientX: 32 + 100, clientY: 32 + 150, pointerId: 1 });
    await waitFor(() => expect(source.runs.length).toBe(1));
    expect(source.runs[0]).toMatchObject({ command: "studio::layer::reshape", input: { id: "l1", points: "M 0 0 L 1 1", x: 100, y: 150, w: 200, h: 50 } });
  });

  it("shows a path's node's control points once the node is pressed, and reshapes it by one dragged", async () => {
    act(() => setPicked("/ layer", undefined));
    const source = memorySource({ commands: { "studio::layer::reshape": () => ({ id: "l1" }) } });
    source.set("studio::poster_page", { width: 1000, height: 500, layers: [{ id: "l1", shape: "path", points: "M 0 0 C 0 1 1 1 1 0", stroke: "#1d1d1f", stroke_width: 2, x: 100, y: 100, w: 200, h: 100 }] }, "p1");
    function Poster() {
      const page = useView("studio::poster_page", "p1");
      return (
        <>
          <Collage view={page} list="layers" width="width" height="height" fields={{ ...namedLikeParts, points: "points" }} pick="/ layer" name="/ layer" place="studio::layer::place" reshape="studio::layer::reshape" />
          <Picked />
        </>
      );
    }
    const only = defineScreen({ title: "Poster", route: "/" }, () => <Poster />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.container.querySelector("canvas")).toBeTruthy());
    const canvas = drawn.container.querySelector("canvas")!;
    await act(async () => {});
    // Its curve, halfway along, at 200 175: picked.
    fireEvent.pointerDown(canvas, { clientX: 32 + 200, clientY: 32 + 175, pointerId: 1 });
    fireEvent.pointerUp(canvas, { clientX: 32 + 200, clientY: 32 + 175, pointerId: 1 });
    await waitFor(() => expect(drawn.getByTestId("picked").textContent).toBe("l1"));
    await act(async () => {});
    // Its last node, at 300 100, pressed and let go where it was: nothing sent.
    fireEvent.pointerDown(canvas, { clientX: 32 + 300, clientY: 32 + 100, pointerId: 1 });
    fireEvent.pointerUp(canvas, { clientX: 32 + 300, clientY: 32 + 100, pointerId: 1 });
    expect(source.runs.length).toBe(0);
    // Its control point, at 300 200, dragged left 50.
    fireEvent.pointerDown(canvas, { clientX: 32 + 300, clientY: 32 + 200, pointerId: 1 });
    fireEvent.pointerMove(canvas, { clientX: 32 + 270, clientY: 32 + 200, pointerId: 1 });
    fireEvent.pointerUp(canvas, { clientX: 32 + 250, clientY: 32 + 200, pointerId: 1 });
    await waitFor(() => expect(source.runs.length).toBe(1));
    expect(source.runs[0]).toMatchObject({ command: "studio::layer::reshape", input: { id: "l1", points: "M 0 0 C 0 1 0.75 1 1 0", x: 100, y: 100, w: 200, h: 100 } });
  });

  it("picks a group by a press on what it holds, moves it as one in one step, ungroups it, and picks what it holds when pressed twice", async () => {
    act(() => setPicked("/ layer", undefined));
    const reply = (input: Record<string, unknown>) => ({ id: String(input["id"]) });
    const source = memorySource({ commands: { "studio::layer::place": reply, "studio::layer::group": reply, "studio::layer::delete": reply } });
    const shape = (id: string, x: number, y: number, group: string) => ({ id, shape: "rectangle", fill: "#2e86de", x, y, w: 100, h: 100, group });
    source.set("studio::poster_page", { width: 1000, height: 500, layers: [{ id: "g", x: 0, y: 0, w: 0, h: 0 }, shape("a", 100, 100, "g"), shape("b", 300, 200, "g"), shape("c", 700, 300, "")] }, "p1");
    const fields = { ...namedLikeParts, shape: "shape", fill: "fill", group: "group" };
    function Poster() {
      const page = useView("studio::poster_page", "p1");
      return (
        <>
          <CollageButton collage="/ layer" does="ungroup" label="Ungroup" />
          <Collage view={page} list="layers" width="width" height="height" fields={fields} pick="/ layer" name="/ layer" place="studio::layer::place" group="studio::layer::group" remove="studio::layer::delete" />
          <Picked />
        </>
      );
    }
    const only = defineScreen({ title: "Poster", route: "/" }, () => <Poster />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.container.querySelector("canvas")).toBeTruthy());
    const canvas = drawn.container.querySelector("canvas")!;
    await act(async () => {});
    // A press on one it holds picks the group; a drag moves all it holds, in one step.
    fireEvent.pointerDown(canvas, { clientX: 32 + 150, clientY: 32 + 150, pointerId: 1, detail: 1 });
    fireEvent.pointerMove(canvas, { clientX: 32 + 170, clientY: 32 + 150, pointerId: 1 });
    fireEvent.pointerUp(canvas, { clientX: 32 + 200, clientY: 32 + 150, pointerId: 1 });
    await waitFor(() => expect(source.runs.length).toBe(2));
    expect(drawn.getByTestId("picked").textContent).toBe("g");
    const xs = Object.fromEntries(source.runs.map((r) => [r.input["id"], r.input["x"]]));
    expect(xs).toEqual({ a: 150, b: 350 });
    expect(source.runs[1]!.input["one:history"]).toBe("joined");
    // Ungrouped: each it holds in none, and it gone.
    fireEvent.click(drawn.getByRole("button", { name: "Ungroup" }));
    await waitFor(() => expect(source.runs.length).toBe(5));
    expect(source.runs[2]).toMatchObject({ command: "studio::layer::group", input: { group: null } });
    expect(source.runs[4]).toMatchObject({ command: "studio::layer::delete", input: { id: "g" } });
  });

  it("picks what a group holds when it's pressed twice", async () => {
    act(() => setPicked("/ layer", undefined));
    const source = memorySource();
    source.set("studio::poster_page", { width: 1000, height: 500, layers: [{ id: "g", x: 0, y: 0, w: 0, h: 0 }, { id: "a", shape: "rectangle", fill: "#000000", x: 100, y: 100, w: 100, h: 100, group: "g" }] }, "p1");
    function Poster() {
      const page = useView("studio::poster_page", "p1");
      return (
        <>
          <Collage view={page} list="layers" width="width" height="height" fields={{ ...namedLikeParts, shape: "shape", fill: "fill", group: "group" }} pick="/ layer" name="/ layer" />
          <Picked />
        </>
      );
    }
    const only = defineScreen({ title: "Poster", route: "/" }, () => <Poster />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.container.querySelector("canvas")).toBeTruthy());
    const canvas = drawn.container.querySelector("canvas")!;
    await act(async () => {});
    fireEvent.pointerDown(canvas, { clientX: 32 + 150, clientY: 32 + 150, pointerId: 1, detail: 2 });
    fireEvent.pointerUp(canvas, { clientX: 32 + 150, clientY: 32 + 150, pointerId: 1 });
    await waitFor(() => expect(drawn.getByTestId("picked").textContent).toBe("a"));
  });

  it("makes a path by points, as a pen draws: corners clicked, a smooth node dragged, ended by Enter or closed on its first node", async () => {
    const source = memorySource({ commands: { "studio::layer::create": () => ({ id: "drawn" }) } });
    source.set("studio::poster_page", { width: 1000, height: 500, layers: [] }, "p1");
    function Poster() {
      const page = useView("studio::poster_page", "p1");
      return <Collage view={page} list="layers" width="width" height="height" fields={{ ...namedLikeParts, points: "points" }} draws={[{ command: "studio::layer::create", by: "points" }]} />;
    }
    const only = defineScreen({ title: "Poster", route: "/" }, () => <Poster />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.container.querySelector("canvas")).toBeTruthy());
    const canvas = drawn.container.querySelector("canvas")!;
    await act(async () => {});
    const click = (x: number, y: number) => {
      fireEvent.pointerDown(canvas, { clientX: 32 + x, clientY: 32 + y, pointerId: 1 });
      fireEvent.pointerUp(canvas, { clientX: 32 + x, clientY: 32 + y, pointerId: 1 });
    };
    click(100, 100);
    click(300, 100);
    click(300, 300);
    expect(source.runs).toEqual([]);
    fireEvent.keyDown(window, { key: "Enter" });
    await waitFor(() => expect(source.runs.length).toBe(1));
    const open = (source.runs[0]!.input as { points: string }).points;
    expect([open.match(/L/g)?.length, open.includes("Z")]).toEqual([2, false]);
    click(100, 100);
    fireEvent.pointerDown(canvas, { clientX: 32 + 300, clientY: 32 + 100, pointerId: 1 });
    fireEvent.pointerMove(canvas, { clientX: 32 + 350, clientY: 32 + 100, pointerId: 1 });
    fireEvent.pointerUp(canvas, { clientX: 32 + 350, clientY: 32 + 100, pointerId: 1 });
    click(200, 250);
    click(100, 100);
    await waitFor(() => expect(source.runs.length).toBe(2));
    const closed = (source.runs[1]!.input as { points: string }).points;
    expect([closed.includes("C"), closed.endsWith("Z")]).toEqual([true, true]);
  });

  it("makes a path by hand of what the drag went through, only as much as keeps its shape, as one curve", async () => {
    const source = memorySource({ commands: { "studio::layer::create": () => ({ id: "drawn" }) } });
    source.set("studio::poster_page", { width: 1000, height: 500, layers: [] }, "p1");
    function Poster() {
      const page = useView("studio::poster_page", "p1");
      return <Collage view={page} list="layers" width="width" height="height" fields={{ ...namedLikeParts, points: "points" }} parent="poster" subject="p1" draws={[{ command: "studio::layer::create", given: { shape: "path" }, by: "hand" }]} />;
    }
    const only = defineScreen({ title: "Poster", route: "/" }, () => <Poster />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.container.querySelector("canvas")).toBeTruthy());
    const canvas = drawn.container.querySelector("canvas")!;
    await act(async () => {});
    // From 100 100 down to 200 200 and up to 300 100, a little wobble on the way down.
    fireEvent.pointerDown(canvas, { clientX: 132, clientY: 132, pointerId: 1 });
    fireEvent.pointerMove(canvas, { clientX: 182, clientY: 182.5, pointerId: 1 });
    fireEvent.pointerMove(canvas, { clientX: 232, clientY: 232, pointerId: 1 });
    fireEvent.pointerUp(canvas, { clientX: 332, clientY: 132, pointerId: 1 });
    await waitFor(() => expect(source.runs.length).toBe(1));
    expect(source.runs[0]!.input).toMatchObject({ shape: "path", x: 100, y: 100, w: 200 });
    // Three points kept, so two curves.
    expect((source.runs[0]!.input as { points: string }).points.match(/C/g)?.length).toBe(2);
  });

  it("makes a line from where the drag began to where it ended, by line, kept as a path's points in its box", async () => {
    const source = memorySource({ commands: { "studio::layer::create": () => ({ id: "line" }) } });
    source.set("studio::poster_page", { width: 1000, height: 500, layers: [] }, "p1");
    function Poster() {
      const page = useView("studio::poster_page", "p1");
      return (
        <Collage view={page} list="layers" width="width" height="height" fields={{ ...namedLikeParts, points: "points" }} parent="poster" subject="p1"
          draws={[{ command: "studio::layer::create", given: { shape: "path" }, by: "line" }]} />
      );
    }
    const only = defineScreen({ title: "Poster", route: "/" }, () => <Poster />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.container.querySelector("canvas")).toBeTruthy());
    const canvas = drawn.container.querySelector("canvas")!;
    await act(async () => {});
    fireEvent.pointerDown(canvas, { clientX: 332, clientY: 132, pointerId: 1 });
    fireEvent.pointerMove(canvas, { clientX: 232, clientY: 182, pointerId: 1 });
    fireEvent.pointerUp(canvas, { clientX: 132, clientY: 232, pointerId: 1 });
    await waitFor(() => expect(source.runs.length).toBe(1));
    expect(source.runs[0]!.input).toMatchObject({ x: 100, y: 100, w: 200, h: 100, points: "M 1 0 L 0 1", shape: "path", poster: "p1" });
  });
});

describe("a collage's layers, listed", () => {
  beforeEach(() => {
    HTMLCanvasElement.prototype.getContext = (() => null) as unknown as HTMLCanvasElement["getContext"];
  });

  it("lists them topmost first, each hidden or shown, dragged up a place by renumbering their depth, and picked by a press", async () => {
    act(() => setPicked("/ layer", undefined));
    const source = memorySource({ commands: { "studio::layer::show": () => ({ id: "x" }), "studio::layer::update": () => ({ id: "x" }), "studio::layer::delete": () => ({ id: "x" }) } });
    source.set("studio::poster_page", {
      width: 1600,
      height: 900,
      layers: [
        { id: "low", shape: "ellipse", depth: 0, x: 0, y: 0, w: 10, h: 10 },
        { id: "mid", words: "Harbor\nat dusk", size: 40, depth: 0, hidden: 1, x: 0, y: 0, w: 10, h: 10 },
        { id: "top", shape: "rectangle", depth: 2, x: 0, y: 0, w: 10, h: 10 },
      ],
    }, "p1");
    function Drawer() {
      const page = useView("studio::poster_page", "p1");
      return (
        <>
          <Layers view={page} list="layers" width="width" height="height" fields={{ ...namedLikeParts, hidden: "hidden", shape: "shape" }} pick="/ layer" hide="studio::layer::show" order="studio::layer::update" remove="studio::layer::delete" />
          <Picked />
        </>
      );
    }
    const only = defineScreen({ title: "Poster", route: "/" }, () => <Drawer />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.getAllByRole("listitem").length).toBe(3));
    expect(drawn.getAllByRole("listitem").map((r) => r.textContent?.match(/Rectangle|Harbor|Ellipse/)?.[0])).toEqual(["Rectangle", "Harbor", "Ellipse"]);
    // The hidden one is shown again, by its eye beside its row.
    const [top, middle, bottom] = drawn.getAllByRole("listitem");
    fireEvent.click(within(middle!.parentElement!.parentElement!).getByRole("button", { name: "Show" }));
    // The lowest held and dragged onto the middle one: moved up a place, the rest renumbered.
    const under = document.elementsFromPoint;
    document.elementsFromPoint = () => [middle!];
    fireEvent.pointerDown(bottom!, { clientX: 10, clientY: 100, button: 0 });
    fireEvent.pointerMove(bottom!, { clientX: 10, clientY: 60 });
    fireEvent.pointerUp(bottom!, { clientX: 10, clientY: 60 });
    document.elementsFromPoint = under;
    await waitFor(() => expect(source.runs.length).toBe(2));
    expect(source.runs[0]).toMatchObject({ command: "studio::layer::show", input: { id: "mid", hidden: 0 } });
    // Mid is already at 0 and top at 2, so only the lowest, now in the middle, is sent.
    expect(source.runs[1]).toMatchObject({ command: "studio::layer::update", input: { id: "low", depth: 1 } });
    expect(drawn.getByTestId("picked").textContent).toBe("");
    // A press on one picks it.
    fireEvent.click(top!);
    expect(drawn.getByTestId("picked").textContent).toBe("top");
  });
});

describe("a collage's objects held in layers", () => {
  beforeEach(() => {
    HTMLCanvasElement.prototype.getContext = (() => null) as unknown as HTMLCanvasElement["getContext"];
  });

  it("draws them in their layers' order, a hidden layer's left out, and new ones go in the one picked, or the topmost", () => {
    const layers = [{ id: "top", depth: 2 }, { id: "back", depth: 0 }, { id: "gone", depth: 1, hidden: 1 }];
    const objects = [{ id: "a", layer: "top" }, { id: "b", layer: "back" }, { id: "c", layer: "gone" }, { id: "d", layer: "nowhere" }, { id: "e", layer: "back" }];
    expect(inLayers(objects, layers, "layer").map((o) => o.id)).toEqual(["d", "b", "e", "a"]);
    expect(currentLayer(layers, "back")).toBe("back");
    expect(currentLayer(layers, "missing")).toBe("top");
    expect(currentLayer([], "")).toBe("");
  });

  it("lists the layers topmost first, each opening to its objects, a layer deleted taking them with it in one step, and renames one written in place", async () => {
    act(() => setPicked("/ layer", undefined));
    const source = memorySource({ commands: { "studio::layer::delete": () => ({ id: "x" }), "studio::item::delete": () => ({ id: "x" }), "studio::layer::rename": () => ({ id: "x" }) } });
    source.set("studio::drawing_page", {
      width: 1600,
      height: 900,
      layers: [{ id: "back", name: "Backdrop", depth: 0 }, { id: "logo", name: "logo", depth: 1 }],
      items: [{ id: "a", layer: "logo", shape: "ellipse", x: 0, y: 0, w: 10, h: 10 }, { id: "b", layer: "logo", words: "Hi", size: 10, x: 0, y: 0, w: 10, h: 10 }],
    }, "d1");
    function Drawer() {
      const page = useView("studio::drawing_page", "d1");
      return (
        <Layers view={page} list="layers" width="width" height="height" fields={namedLikeParts} pick="/ layer" remove="studio::layer::delete" rename="studio::layer::rename" objects="items" objectLayer="layer" objectRemove="studio::item::delete" objectPick="/ item" />
      );
    }
    const only = defineScreen({ title: "Drawing", route: "/" }, () => <Drawer />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.getAllByRole("listitem").map((r) => r.textContent?.match(/logo|Backdrop/)?.[0])).toEqual(["logo", "Backdrop"]));
    fireEvent.click(within(drawn.getAllByRole("listitem")[0]!).getByRole("button", { name: "Open" }));
    expect(drawn.getAllByRole("listitem").map((r) => r.textContent?.match(/logo|Backdrop|Hi|Ellipse/)?.[0])).toEqual(["logo", "Hi", "Ellipse", "Backdrop"]);
    // Its delete shows only while it's pointed at.
    expect(within(drawn.getAllByRole("listitem")[0]!).queryByRole("button", { name: "Delete" })).toBeNull();
    fireEvent.mouseEnter(drawn.getAllByRole("listitem")[0]!);
    fireEvent.click(within(drawn.getAllByRole("listitem")[0]!).getByRole("button", { name: "Delete" }));
    await waitFor(() => expect(source.runs.length).toBe(3));
    expect(source.runs.map((r) => [r.command, r.input])).toEqual([
      ["studio::item::delete", { id: "a" }],
      ["studio::item::delete", { id: "b", "one:history": "joined" }],
      ["studio::layer::delete", { id: "logo", "one:history": "joined" }],
    ]);
    // A name double-clicked is written in place, Enter keeping it.
    fireEvent.doubleClick(within(drawn.getAllByRole("listitem")[0]!).getByText("logo"));
    const field = drawn.getByRole("textbox", { name: "Name" });
    fireEvent.change(field, { target: { value: "Mark" } });
    fireEvent.keyDown(field, { key: "Enter" });
    await waitFor(() => expect(source.runs.length).toBe(4));
    expect(source.runs[3]).toMatchObject({ command: "studio::layer::rename", input: { id: "logo", name: "Mark" } });
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

describe("a collage matching colors", () => {
  it("draws, and keeps its preview, as one that doesn't", async () => {
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
      return <Collage view={page} list="layers" width="width" height="height" fields={namedLikeParts} subject="p1" matchColors preview="studio::poster::preview" previewInto="preview" previewWide={320} />;
    }
    const only = defineScreen({ title: "Poster", route: "/" }, () => <Poster />);
    render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await act(async () => {
      await vi.advanceTimersByTimeAsync(1000);
    });
    vi.useRealTimers();
    await waitFor(() => expect(source.runs.length).toBe(1));
    expect(source.runs[0]).toMatchObject({ command: "studio::poster::preview", input: { id: "p1", "one:history": "apart" } });
  });
});

describe("a picture on its own", () => {
  it("shows the file a value holds, fitted in, and nothing while there's none", async () => {
    const source = memorySource({ files: true });
    source.set("studio::poster_page", { title: "Harbor", preview: "uploads/harbor.png" }, "p1");
    function Poster() {
      const page = useView("studio::poster_page", "p1");
      return (
        <>
          <Picture source={page.data?.["preview"] ?? null} fit />
          <Picture source={page.data?.["missing"] ?? null} />
        </>
      );
    }
    const only = defineScreen({ title: "Poster", route: "/" }, () => <Poster />);
    const drawn = render(<App name="studio" screens={[only]} ui={plain} data={source} location="/" />);
    await waitFor(() => expect(drawn.container.querySelectorAll("img").length).toBe(1));
    const shown = drawn.container.querySelector("img")!;
    expect(shown.getAttribute("src")).toContain("harbor.png");
    expect(shown.style.objectFit).toBe("contain");
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
