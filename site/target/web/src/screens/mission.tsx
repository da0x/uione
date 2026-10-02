// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from mission.one by one. Do not edit.

import { Hero, Section, Text, screen } from "@uione/react";

export const mission = screen({ title: "Mission", route: "/mission", nav: "Mission" }, () => (
  <>
    <Hero title="An application should be as short as what it does.">
      <Text>Today one feature is spread over a dozen files: a form, its types, its API route, its validation twice, its database rules, the query behind each screen and the code that keeps that screen fresh. They repeat each other, and they drift apart.</Text>
      <Text>uione's mission is to let a person say a feature once, plainly, and have everything else follow from it, correct and readable.</Text>
    </Hero>
    <Section title="Said once">
      <Text>A feature's data, rules, commands, views, permissions and screens live in one short file. Nothing is said twice, so nothing can disagree with itself.</Text>
    </Section>
    <Section title="One architecture">
      <Text>Every uione app works the same way: commands are the only writes, every write is an event, views are built from events, and every screen is live. A feature never has to say how, and a reader never has to guess.</Text>
    </Section>
    <Section title="Code you can read and keep">
      <Text>What the compiler writes is short, plain React, Go and Pulumi, built with the tools you already use. It belongs to whoever generated it. Anything it would repeat lives in a library instead.</Text>
    </Section>
    <Section title="Open, and yours">
      <Text>The compiler is free software under the AGPL, and the libraries apps are built on are under the LGPL, so an app of any kind can use them. Your app runs in your own Google Cloud project, and you can leave with all of it.</Text>
    </Section>
    <Section title="For everyone">
      <Text>Writing software shouldn't need a team to keep a dozen files in step. The studio puts the whole language in a browser, with nothing to install, so anyone who can describe what they want can build it.</Text>
    </Section>
  </>
));
