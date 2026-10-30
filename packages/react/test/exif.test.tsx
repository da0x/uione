// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What a camera wrote in a photo, read from a JPEG, PNG, WebP or TIFF made here as a
// camera would write it; and from one cut short or made wrong, as far as it's sound.

import { cameraOf, factsIn, factsOf, takenAt } from "../src/exif.js";
import { exif, jpeg, png, webp, words, type Said } from "./photos.js";

const camera: Said = { orientation: 6, make: "FUJIFILM", model: "X-T5", taken: "2026:10:12 08:30:15", offset: "+02:00" };

describe("a photo's EXIF", () => {
  it("is read from a JPEG, a PNG, a WebP and a TIFF, either way its numbers are written", async () => {
    for (const little of [true, false]) {
      const block = exif({ ...camera, little });
      for (const photo of [jpeg(block), png(block), webp(block, true), webp(block, false), block]) {
        expect(await factsIn(photo)).toEqual({ taken: "2026:10:12 08:30:15", offset: "+02:00", make: "FUJIFILM", model: "X-T5", orientation: 6 });
      }
    }
  });

  it("is read from a file, only as much of it as it takes", async () => {
    const photo = jpeg(exif(camera));
    const file = new File([photo, new Uint8Array(1 << 20)], "harbor.jpg", { type: "image/jpeg" });
    let read = 0;
    const slice = file.slice.bind(file);
    file.slice = (from?: number, to?: number) => {
      read += Math.min(to ?? file.size, file.size) - (from ?? 0);
      return slice(from, to);
    };
    expect((await factsOf(file)).orientation).toBe(6);
    expect(read).toBeLessThan(1024);
  });

  it("says nothing of a photo without it, and only what it says of one that says some", async () => {
    expect(await factsIn(jpeg(new Uint8Array()))).toEqual({ taken: "", offset: "", make: "", model: "", orientation: 1 });
    expect(await factsIn(Uint8Array.from(words("GIF89a")))).toMatchObject({ orientation: 1, make: "" });
    expect(await factsIn(png(exif({ orientation: 3 })))).toEqual({ taken: "", offset: "", make: "", model: "", orientation: 3 });
    // Short text in the entry itself, and padding a camera leaves.
    expect(await factsIn(exif({ make: "Abc", model: "  Z9  " }))).toMatchObject({ make: "Abc", model: "Z9" });
    // An orientation that isn't one of the eight is as it's stored.
    expect((await factsIn(exif({ orientation: 9 }))).orientation).toBe(1);
  });

  it("reads one cut short as far as it's sound, and one made wrong without failing", async () => {
    const whole = jpeg(exif(camera));
    for (let n = 0; n <= whole.length; n++) {
      const facts = await factsIn(whole.subarray(0, n));
      expect([1, 6]).toContain(facts.orientation);
      for (const said of [facts.make, facts.model, facts.taken, facts.offset]) expect(/^[\x20-\x7e]*$/.test(said)).toBe(true);
    }
    // Every byte changed, one at a time, in each kind; then many changed at once, at random but
    // the same each run.
    let seed = 1;
    const next = () => ((seed = (seed * 1103515245 + 12345) >>> 0) >>> 16) & 0xff;
    for (const photo of [whole, png(exif(camera)), webp(exif(camera), true), exif({ ...camera, little: false })]) {
      for (let at = 0; at < photo.length; at++) {
        for (const to of [0x00, 0xff, next()]) {
          const wrong = photo.slice();
          wrong[at] = to;
          const facts = await factsIn(wrong);
          expect(facts.orientation).toBeGreaterThanOrEqual(1);
          expect(facts.orientation).toBeLessThanOrEqual(8);
          expect(facts.make.length).toBeLessThanOrEqual(256);
        }
      }
      for (let tries = 0; tries < 2000; tries++) {
        const wrong = photo.slice();
        for (let k = 0; k < 8; k++) wrong[next() % wrong.length] = next();
        await factsIn(wrong);
      }
    }
    // Lengths and counts that point far past the end, and a directory that points back
    // at itself.
    const pointing = exif(camera);
    new DataView(pointing.buffer).setUint32(4, 0xfffffff0, true);
    expect(await factsIn(pointing)).toMatchObject({ orientation: 1 });
    const many = exif(camera);
    new DataView(many.buffer).setUint16(8, 0xffff, true);
    // Only the entries there are room for are read, which still say which way up.
    expect((await factsIn(many)).orientation).toBe(6);
    const huge = jpeg(exif(camera));
    huge[4] = 0xff;
    huge[5] = 0xff;
    expect((await factsIn(huge)).orientation).toBe(1);
    // Its fourth entry, the EXIF directory's, pointed back at the first.
    const back = exif(camera);
    new DataView(back.buffer).setUint32(8 + 2 + 3 * 12 + 8, 8, true);
    expect(await factsIn(back)).toMatchObject({ orientation: 6, taken: "" });
    // Bytes that are only noise.
    for (let tries = 0; tries < 500; tries++) {
      const noise = Uint8Array.from({ length: 64 + (next() % 200) }, next);
      noise.set([0xff, 0xd8, 0xff, 0xe1, 0, 0x40, ...words("Exif"), 0, 0, 0x49, 0x49, 42, 0], 0);
      await factsIn(noise);
    }
  });

  it("names the camera by its make and model, the make said once", () => {
    const of = (make: string, model: string) => cameraOf({ taken: "", offset: "", make, model, orientation: 1 });
    expect(of("FUJIFILM", "X-T5")).toBe("FUJIFILM X-T5");
    expect(of("Canon", "Canon EOS R5")).toBe("Canon EOS R5");
    expect(of("NIKON CORPORATION", "NIKON Z 9")).toBe("NIKON Z 9");
    expect(of("Apple", "")).toBe("Apple");
    expect(of("", "Pixel 9")).toBe("Pixel 9");
    expect(of("", "")).toBe("");
  });

  it("says when it was taken at its offset, or as the time is here, and nothing when it says it wrong", () => {
    const at = (taken: string, offset = "") => takenAt({ taken, offset, make: "", model: "", orientation: 1 });
    expect(at("2026:10:12 08:30:15", "+02:00")).toBe("2026-10-12T06:30:15Z");
    expect(at("2026:10:12 08:30:15", "-05:30")).toBe("2026-10-12T14:00:15Z");
    expect(at("2026:10:12 08:30:15")).toBe(new Date(2026, 9, 12, 8, 30, 15).toISOString().replace(".000Z", "Z"));
    expect(at("")).toBeUndefined();
    expect(at("0000:00:00 00:00:00")).toBeUndefined();
    expect(at("2026:02:30 08:30:15")).toBeUndefined();
    expect(at("2026:10:12 24:00:00")).toBeUndefined();
    expect(at("2026-10-12 08:30:15")).toBeUndefined();
  });
});
