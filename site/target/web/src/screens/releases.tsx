// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from releases.one by one. Do not edit.

import { Pages, screen } from "@uione/react";
import pages from "../pages/releases.generated";

export const releases = screen({ title: "Releases", route: "/releases/:page?", nav: "Releases" }, () => (
  <>
    <Pages base="/releases" pages={pages} />
  </>
));
