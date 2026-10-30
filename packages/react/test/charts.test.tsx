// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Charts and monitors: runs of numbers read from a record and how they read, and
// records whose status changes as tiles, one unheard from going offline.

import { render, screen } from "@testing-library/react";
import { App, Chart, Monitor, memorySource, readValue, scaleTop, screen as defineScreen, seriesOf, toneOfState, useView, valueAt } from "../src/index.js";
import { plain } from "../src/plain.js";

describe("a run of numbers", () => {
  it("is read from a joined field whole, or by a name within data", () => {
    expect(valueAt({ "held_by.name": "npm" }, "held_by.name")).toBe("npm");
    expect(valueAt({ stats: { cpu: [1, 2] } }, "stats.cpu")).toEqual([1, 2]);
    expect(seriesOf([1, "two", 3])).toEqual([1, 3]);
  });

  it("reads in its unit, and its scale tops at a round number", () => {
    expect(readValue(42.4, "percent")).toBe("42%");
    expect(readValue(1536 * 1024 * 1024, "bytes")).toBe("1.5 GB");
    expect(readValue(3 * 1024 * 1024, "rate")).toBe("3 MB/s");
    expect(scaleTop([12, 70], "percent")).toBe(100);
    expect(scaleTop([700 * 1024 * 1024], "bytes")).toBe(1024 * 1024 * 1024);
    expect(scaleTop([37], "")).toBe(50);
  });

  it("says a status word as a dot", () => {
    expect([toneOfState("Working"), toneOfState("standby"), toneOfState("idle"), toneOfState("stopped"), toneOfState("offline")]).toEqual([
      "working",
      "waiting",
      "success",
      "failed",
      "inactive",
    ]);
  });
});

describe("a monitor", () => {
  const ago = (seconds: number) => new Date(Date.now() - seconds * 1000).toISOString();
  function Agents() {
    const agents = useView("agents");
    return (
      <>
        <Monitor
          view={agents}
          list="agents"
          status="status"
          doing="doing"
          step="step"
          steps="steps"
          seen="seen_at"
          seenSeconds={30}
          charts={[{ path: "stats.cpu", title: "CPU", unit: "percent" }]}
        />
        <Chart view={agents} series={[{ path: "fleet.memory", title: "Memory", unit: "bytes" }]} />
      </>
    );
  }
  const page = defineScreen({ title: "Agents", route: "/agents" }, Agents);

  it("shows each record's state, what it's doing and how far, and one unheard from as offline", () => {
    const data = memorySource({
      views: {
        agents: {
          fleet: { memory: [1024, 2048] },
          agents: [
            { id: "npm", title: "npm", status: "working", doing: "#12 Uploading", step: 3, steps: 5, seen_at: ago(2), stats: { cpu: [10, 64] } },
            { id: "sites", title: "sites", status: "idle", seen_at: ago(600), stats: { cpu: [50] } },
          ],
        },
      },
    });
    render(<App name="tracker" screens={[page]} ui={plain} data={data} location="/agents" />);
    const tiles = screen.getAllByRole("article");
    expect(tiles[0]!.textContent).toContain("Working");
    expect(tiles[0]!.textContent).toContain("#12 Uploading");
    expect(tiles[0]!.textContent).toContain("step 3 of 5");
    expect(tiles[0]!.textContent).toContain("CPU: 64%");
    expect(tiles[1]!.textContent).toContain("Offline");
    expect(tiles[1]!.textContent).toContain("CPU: –");
    expect(screen.getByText("Memory: 2 KB (of 2 KB)")).toBeTruthy();
  });
});
