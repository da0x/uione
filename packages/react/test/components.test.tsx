// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

import { render, screen } from "@testing-library/react";
import { App, Code, Hero, Link, Pages, Section, Text, memorySource, screen as defineScreen } from "../src/index.js";
import type { DocPage } from "../src/index.js";
import { label } from "../src/index.js";
import { plain } from "../src/plain.js";

describe("content", () => {
  it("draws heroes, sections, text, links and code through the component set", () => {
    const home = defineScreen({ title: "uione", route: "/" }, () => (
      <>
        <Hero title="Write the feature once.">
          <Text>One short file.</Text>
          <Link to="#waitlist">Join the waitlist</Link>
        </Hero>
        <Section title="A whole feature" id="example">
          <Code lang="uione" source="entity book {}" />
        </Section>
      </>
    ));
    render(<App name="uione" screens={[home]} ui={plain} data={memorySource()} location="/" />);
    expect(screen.getByRole("heading", { level: 1, name: "Write the feature once." })).toBeTruthy();
    expect(screen.getByRole("link", { name: "Join the waitlist" }).getAttribute("href")).toBe("#waitlist");
    expect(screen.getByRole("heading", { level: 2, name: "A whole feature" }).closest("section")!.id).toBe("example");
    expect(screen.getByText("entity book {}").getAttribute("data-lang")).toBe("uione");
  });
});

describe("pages", () => {
  const pages: DocPage[] = [
    { slug: "overview", title: "Overview", html: "<h1>Overview</h1>" },
    { slug: "language", title: "Language", html: "<h1>Language</h1><p>Blocks use braces.</p>" },
  ];
  const docs = defineScreen({ title: "Docs", route: "/docs/:page?", nav: "Docs" }, () => <Pages base="/docs" pages={pages} />);
  const renderAt = (location: string) =>
    render(<App name="uione" screens={[docs]} ui={plain} data={memorySource()} location={location} />);

  it("shows the page the address names", () => {
    renderAt("/docs/language");
    expect(screen.getByText("Blocks use braces.")).toBeTruthy();
    expect(screen.getByRole("link", { name: "Language" }).getAttribute("aria-current")).toBe("page");
  });

  it("shows the first page when the address names none", () => {
    renderAt("/docs");
    expect(screen.getByRole("article", { name: "Overview" })).toBeTruthy();
  });

  it("says so when the named page doesn't exist", () => {
    renderAt("/docs/nothing");
    expect(screen.getByText("There's no page here.")).toBeTruthy();
  });
});

describe("labels", () => {
  it("turns snake_case names into readable labels", () => {
    expect(label("created_at")).toBe("Created at");
    expect(label("email")).toBe("Email");
    expect(label("checkin")).toBe("Checkin");
  });
});
