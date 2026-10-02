// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from docs.one by one. Do not edit.

import { Pages, screen } from "@uione/react";
import pages from "../docs.generated";

export const docs = screen({ title: "Docs", route: "/docs/:page?", nav: "Docs" }, () => (
  <>
    <Pages base="/docs" pages={pages} />
  </>
));
