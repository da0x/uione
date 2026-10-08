// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

import { fireEvent, render, screen } from "@testing-library/react";
import { useState } from "react";
import { App, Code, Hero, Link, Menu, Pages, useParam, Section, Text, memorySource, screen as defineScreen } from "../src/index.js";
import { changed } from "../src/index.js";
import { show } from "../src/ui.js";
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

  it("fills a link's parameters from the page it's on", () => {
    const project = defineScreen({ title: "Project", route: "/projects/:project" }, () => (
      <Link to="/projects/:project/reports">Reports</Link>
    ));
    render(<App name="neotrac" screens={[project]} ui={plain} data={memorySource()} location="/projects/my%20app" />);
    expect(screen.getByRole("link", { name: "Reports" }).getAttribute("href")).toBe("/projects/my%20app/reports");
  });

  it("gives a last parameter like :file* the rest of the address, and keeps the page across it", () => {
    let mounted = 0;
    function Code() {
      useState(() => ++mounted);
      return (
        <>
          <Text>{`file ${useParam("file")} of ${useParam("project")}`}</Text>
          <Link to="/:project/code/components/chart.tsx">Chart</Link>
        </>
      );
    }
    const code = defineScreen({ title: "Code", route: "/:project/code/:file*" }, () => <Code />);
    render(<App name="studio" screens={[code]} ui={plain} data={memorySource()} location="/neotrac/code/issues.one" />);
    expect(screen.getByText("file issues.one of neotrac")).toBeTruthy();
    fireEvent.click(screen.getByRole("link", { name: "Chart" }));
    expect(screen.getByText("file components/chart.tsx of neotrac")).toBeTruthy();
    expect(mounted).toBe(1);
  });

  it("puts a menu's links beside the page, filled from its address, the page it's on marked", () => {
    const links = [
      { to: "/projects/:project/settings", label: "General" },
      { to: "/projects/:project/settings/deployments", label: "Deployments" },
    ];
    const settings = defineScreen({ title: "Settings", route: "/projects/:project/settings/deployments" }, () => (
      <Menu links={links}>
        <Text>Where it deploys.</Text>
      </Menu>
    ));
    render(<App name="studio" screens={[settings]} ui={plain} data={memorySource()} location="/projects/my%20app/settings/deployments" />);
    const general = screen.getByRole("link", { name: "General" });
    const deployments = screen.getByRole("link", { name: "Deployments" });
    expect(general.getAttribute("href")).toBe("/projects/my%20app/settings");
    expect(general.getAttribute("aria-current")).toBeNull();
    expect(deployments.getAttribute("aria-current")).toBe("page");
    expect(screen.getByText("Where it deploys.")).toBeTruthy();
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

  it("goes to the section the address names", () => {
    const sections: DocPage[] = [{ slug: "reference", title: "Reference", html: '<h1 id="reference">Reference</h1><h2 id="built-in-types">Built-in types</h2>' }];
    const reference = defineScreen({ title: "Docs", route: "/docs/:page?", nav: "Docs" }, () => <Pages base="/docs" pages={sections} />);
    const went: string[] = [];
    Element.prototype.scrollIntoView = function (this: Element) {
      went.push(this.id);
    };
    render(<App name="uione" screens={[reference]} ui={plain} data={memorySource()} location="/docs/reference#built-in-types" />);
    expect(went).toEqual(["built-in-types"]);
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

describe("what a change says", () => {
  it("says from what to what a command of its own moved something, unless what it did says it already", () => {
    expect(changed("phase", "Reported", "Triaged", "projects::issue::move")).toBe("moved this from Reported to Triaged");
    expect(changed("status", "open", "closed", "tracker::issue::close")).toBe("closed this");
    expect(changed("status", "closed", "open", "tracker::issue::reopen")).toBe("reopened this");
  });

  it("says only that something long changed, like a page's text", () => {
    expect(changed("body", "# Setup\n\nInstall it.", "# Setup\n\nInstall it, then run it.")).toBe("changed body");
    expect(changed("body", "", "# Setup\n\nInstall it.")).toBe("wrote body");
  });
});

describe("a date", () => {
  it("picked without a time shows as that day, wherever the reader is", () => {
    expect(show(new Date(Date.UTC(2026, 10, 1)))).toBe(new Date(2026, 10, 1).toLocaleDateString(undefined, { dateStyle: "medium" }));
  });
});
