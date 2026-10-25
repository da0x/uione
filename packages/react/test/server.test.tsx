// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only
// @vitest-environment node

// An app drawn on a server, as a site's pages are drawn ahead of time: once, with no
// window, so with nothing that needs one.

import { renderToString } from "react-dom/server";
import { App, Text, memorySource, screen as defineScreen, useFill } from "../src/index.js";
import { plain } from "../src/plain.js";

describe("an app drawn on a server", () => {
  it("draws a screen that would fill the window in its page, since there's no window", () => {
    function Workspace() {
      useFill();
      return <Text>Only the workspace.</Text>;
    }
    const studio = defineScreen({ title: "Studio", route: "/", nav: "Studio" }, () => <Workspace />);
    const html = renderToString(<App name="uione" screens={[studio]} ui={plain} data={memorySource()} location="/" />);
    expect(html).toContain("Only the workspace.");
    expect(html).toContain("<nav");
  });
});
