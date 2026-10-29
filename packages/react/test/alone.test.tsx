// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A picture alone: all of its screen on black, its bars over it until the pointer's
// been still, zoomed and moved about as it's looked at, and fitted again on the next.

import { act, fireEvent, render, waitFor } from "@testing-library/react";
import { App, CollageButton, Layout, Picture, Region, StepLink, memorySource, screen as defineScreen, useParam, useView } from "../src/index.js";
import { actual, drawnAt, fitted, moved, rest, zoomed } from "../src/alone.js";
import { plain } from "../src/plain.js";

// A picture 1800 × 1000 in a place 900 × 600: fitted, it's 900 × 500, 50 from the top.
const seen = { w: 900, h: 600, wide: 1800, high: 1000 };

describe("a picture alone, looked at", () => {
  it("is fitted in its place, zoomed about a point that stays where it is, and moved no further than its edges", () => {
    expect(drawnAt(fitted, seen)).toEqual({ x: 0, y: 50, w: 900, h: 500 });
    // At 100% on a screen of one pixel to the page's, it's twice as large as fitted.
    expect(actual(seen, 1)).toBe(2);
    expect(actual(seen, 2)).toBe(1);
    const twice = zoomed(fitted, 2, { x: 200, y: 300 }, seen, 1);
    expect(twice.times).toBe(2);
    expect(drawnAt(twice, seen)).toEqual({ x: -200, y: -200, w: 1800, h: 1000 });
    // Dragged down past its top, it stops there.
    expect(drawnAt(moved(twice, 0, -900, seen), seen)).toEqual({ x: -200, y: 0, w: 1800, h: 1000 });
    // Never smaller than fitted, nor larger than eight times 100%.
    expect(zoomed(fitted, 0.5, { x: 450, y: 300 }, seen, 1)).toEqual(fitted);
    expect(zoomed(fitted, 100, { x: 450, y: 300 }, seen, 1).times).toBe(16);
    // Fitted, there's nowhere to move it.
    expect(moved(fitted, 300, 300, seen)).toEqual(fitted);
  });
});

describe("a picture alone on its screen", () => {
  function renderAlone(from: string) {
    const source = memorySource({ files: true });
    source.set("studio::archive", { clippings: ["c0", "c1", "c2"].map((id) => ({ id, name: id })) });
    for (const id of ["c0", "c1", "c2"]) source.set("studio::clipping_page", { name: id, image: `uploads/${id}.jpg` }, id);
    function Clipping({ id }: { id: string }) {
      const archive = useView("studio::archive");
      const page = useView("studio::clipping_page", id);
      return (
        <Layout name="workspace" alone>
          <Region name="toolbar">
            <StepLink view={archive} list="clippings" to="/clippings/:clipping" next icon="next" keys={["right"]}>
              Next
            </StepLink>
            <CollageButton collage="/clippings/:clipping collage" does="zoom in" label="Larger" />
          </Region>
          <Region name="main">
            <Picture source={page.data?.["image"] ?? null} fit alone zoom="/clippings/:clipping collage" />
          </Region>
          <Region name="status">{page.data?.["name"] as string}</Region>
        </Layout>
      );
    }
    function Shown() {
      return <Clipping id={useParam("clipping") ?? ""} />;
    }
    const screens = [defineScreen({ title: "Clippings", route: "/clippings" }, () => <p>All of them</p>), defineScreen({ title: "Clipping", route: "/clippings/:clipping" }, () => <Shown />)];
    return render(<App name="studio" screens={screens} ui={plain} data={source} location={from} />);
  }

  // Where it's laid out, which jsdom doesn't do: 900 × 600, and the picture 1800 × 1000.
  beforeEach(() => {
    if (!globalThis.PointerEvent) globalThis.PointerEvent = class extends MouseEvent {} as unknown as typeof PointerEvent;
    Object.defineProperty(HTMLElement.prototype, "clientWidth", { configurable: true, get: () => 900 });
    Object.defineProperty(HTMLElement.prototype, "clientHeight", { configurable: true, get: () => 600 });
    Object.defineProperty(HTMLImageElement.prototype, "naturalWidth", { configurable: true, get: () => 1800 });
    Object.defineProperty(HTMLImageElement.prototype, "naturalHeight", { configurable: true, get: () => 1000 });
  });
  afterEach(() => {
    for (const name of ["clientWidth", "clientHeight"]) delete (HTMLElement.prototype as unknown as Record<string, unknown>)[name];
    for (const name of ["naturalWidth", "naturalHeight"]) delete (HTMLImageElement.prototype as unknown as Record<string, unknown>)[name];
  });

  async function shown(drawn: ReturnType<typeof renderAlone>) {
    await waitFor(() => expect(drawn.container.querySelector(".one-picture-alone img")).toBeTruthy());
    const picture = drawn.container.querySelector<HTMLImageElement>(".one-picture-alone img")!;
    fireEvent.load(picture);
    return picture;
  }

  it("shows its bars over it until it's been still, then hides them and the pointer, and a step by its key leaves them hidden", async () => {
    const drawn = renderAlone("/clippings/c0");
    const picture = await shown(drawn);
    expect(picture.style.imageOrientation).toBe("from-image");
    await waitFor(() => expect(picture.style.top).toBe("50px"));
    expect(drawn.container.querySelector<HTMLElement>("[data-region=toolbar]")!.hidden).toBe(false);
    act(() => rest());
    expect(drawn.container.querySelector<HTMLElement>("[data-region=toolbar]")!.hidden).toBe(true);
    expect(drawn.container.querySelector<HTMLElement>("[data-region=status]")!.hidden).toBe(true);
    expect(drawn.container.querySelector<HTMLElement>(".one-picture-alone")!.style.cursor).toBe("none");
    // The picture's where it was.
    expect(picture.style.top).toBe("50px");
    // Ctrl alone doesn't bring them back; the pointer moving does.
    fireEvent.keyDown(window, { key: "Control", ctrlKey: true });
    expect(drawn.container.querySelector<HTMLElement>("[data-region=toolbar]")!.hidden).toBe(true);
    fireEvent.pointerMove(window);
    expect(drawn.container.querySelector<HTMLElement>("[data-region=toolbar]")!.hidden).toBe(false);
    // Hidden, a step by its key leaves them so.
    act(() => rest());
    fireEvent.keyDown(window, { key: "ArrowRight" });
    await waitFor(() => expect(drawn.container.querySelector("img")?.getAttribute("src")).toContain("c1"));
    expect(drawn.container.querySelector<HTMLElement>("[data-region=toolbar]")!.hidden).toBe(true);
    // Another key brings them back.
    fireEvent.keyDown(window, { key: "a" });
    expect(drawn.container.querySelector<HTMLElement>("[data-region=toolbar]")!.hidden).toBe(false);
  });

  it("is zoomed by Ctrl and its keys, its zoom buttons and the wheel with Ctrl, to 100% and back by a double click, and dragged about once it's larger", async () => {
    const drawn = renderAlone("/clippings/c0");
    const picture = await shown(drawn);
    const place = drawn.container.querySelector<HTMLElement>(".one-picture-alone")!;
    await waitFor(() => expect(picture.style.width).toBe("900px"));
    fireEvent.keyDown(window, { key: "=", ctrlKey: true });
    expect(picture.style.width).toBe("1125px");
    fireEvent.keyDown(window, { key: "0", ctrlKey: true });
    expect(picture.style.width).toBe("900px");
    fireEvent.click(drawn.getByText("Larger"));
    await waitFor(() => expect(picture.style.width).toBe("1125px"));
    fireEvent.keyDown(window, { key: "-", ctrlKey: true });
    expect(picture.style.width).toBe("900px");
    // The wheel with Ctrl, as a touchpad's pinch comes, zooms; without, fitted, it's left alone.
    fireEvent.wheel(place, { deltaY: -50, ctrlKey: true, clientX: 450, clientY: 300 });
    expect(parseFloat(picture.style.width)).toBeGreaterThan(900);
    fireEvent.keyDown(window, { key: "0", ctrlKey: true });
    // Twice pressed: 100%, twice as large as fitted here, about where it's pressed.
    fireEvent.doubleClick(place, { clientX: 200, clientY: 300 });
    expect(picture.style.width).toBe("1800px");
    expect(picture.style.left).toBe("-200px");
    expect(picture.style.top).toBe("-200px");
    expect(place.style.cursor).toBe("grab");
    // Dragged down, as far as its top.
    fireEvent.pointerDown(place, { button: 0, clientX: 450, clientY: 300, pointerId: 1 });
    expect(place.style.cursor).toBe("grabbing");
    fireEvent.pointerMove(place, { clientX: 450, clientY: 900, pointerId: 1 });
    fireEvent.pointerUp(place, { pointerId: 1 });
    expect(picture.style.top).toBe("0px");
    // Scrolled, it moves about too.
    fireEvent.wheel(place, { deltaX: 100, deltaY: 0 });
    expect(parseFloat(picture.style.left)).toBeCloseTo(-300);
    fireEvent.doubleClick(place, { clientX: 450, clientY: 300 });
    expect(picture.style.width).toBe("900px");
  });
});
