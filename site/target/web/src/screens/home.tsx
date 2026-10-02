// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from home.one by one. Do not edit.

import { Code, Form, Hero, Link, Live, Section, Text, screen, useView } from "@uione/react";
import library from "../../../../../examples/library/main.one?raw";

export const home = screen({ title: "uione", route: "/" }, () => {
  const signups = useView("waitlist::signups");
  return (
    <>
      <Hero title="Write the feature once.">
        <Text>One short file describes a whole feature: its data, its rules, the commands that change it, the live views that show it, who may do what, and its screens. uione generates the React app, the Go backend and the Firestore rules from it.</Text>
        <Link to="#waitlist">Join the waitlist</Link>
        <Link to="/docs/language">Read the docs</Link>
      </Hero>
      <Section title="A whole feature">
        <Text>A lending library: two entities, five commands, three live views, a role and three screens. This is all of it.</Text>
        <Code lang="uione" source={library} />
      </Section>
      <Section title="Join the waitlist" id="waitlist">
        <Form command="waitlist::signup::create" fields={[{ name: "email", type: "email" }]} />
        <Text><Live view={signups} field="total" /> people are waiting.</Text>
      </Section>
    </>
  );
});
