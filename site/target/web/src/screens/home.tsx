// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from home.one by one. Do not edit.

import { Code, Form, Hero, Link, Live, Section, Text, screen, useView } from "@uione/react";
import library from "../../../../../examples/library/main.one?raw";

export const home = screen({ title: "uione", route: "/" }, () => {
  const signups = useView("waitlist::signups");
  return (
    <>
      <Hero title="Write a whole app in your browser.">
        <Text>The uione studio is where you describe a feature in one short file: its data, its rules, who may do what, and its screens. It checks every line as you type, shows the React, Go and Firestore rules each line becomes, and deploys to your own Google Cloud project. There's nothing to install.</Text>
        <Link to="https://uione.io/signin">Open the studio</Link>
        <Link to="/language">Learn the language</Link>
      </Hero>
      <Section title="A whole feature">
        <Text>A lending library: two entities, five commands, three live views, a role and three screens. This is all of it.</Text>
        <Code lang="uione" source={library} />
      </Section>
      <Section title="One file in, a whole project out">
        <Text>one, the compiler, turns a folder of .one files into a React app, a Go backend, the Firestore rules that guard its data, and a Pulumi program for the cloud it runs on. What it writes is short enough to read and built with the tools you already use.</Text>
        <Text>Every app works the same way: commands are the only writes, every write is an event, views are built from events, and every screen is live.</Text>
      </Section>
      <Section title="Install">
        <Text>uione 0.6.6 is out: the compiler for Linux and macOS, in a container, and its libraries for Go and npm. One command installs it: curl -fsSL https://www.uione.io/install.sh | sh</Text>
        <Link to="/install">Install</Link>
        <Link to="/releases">What's new in 0.6.6</Link>
      </Section>
      <Section title="Free software">
        <Text>The compiler is under the AGPL, and the libraries apps are built on are under the LGPL. What the compiler generates belongs to you.</Text>
        <Link to="/mission">Why uione exists</Link>
      </Section>
      <Section title="Join the waitlist" id="waitlist">
        <Text>Leave your address to hear about new releases.</Text>
        <Form command="waitlist::signup::create" fields={[{ name: "email", type: "email" }]} />
        <Text><Live view={signups} field="total" /> people are waiting.</Text>
      </Section>
    </>
  );
});
