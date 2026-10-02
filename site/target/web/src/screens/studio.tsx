// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from studio.one by one. Do not edit.

import { Form, Table, screen, useView } from "@uione/react";

export const studio = screen({ title: "Studio", route: "/studio", nav: "Studio" }, () => {
  const projects = useView("studio::projects");
  return (
    <>
      <Table view={projects} columns={{ name: "Name", created_at: "Created" }} />
      <Form command="studio::project::create" fields={["name"]} button />
    </>
  );
});
