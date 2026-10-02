// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from install.one by one. Do not edit.

import { Pages, screen } from "@uione/react";
import pages from "../pages/install.generated";

export const install = screen({ title: "Install", route: "/install/:page?", nav: "Install" }, () => (
  <>
    <Pages base="/install" pages={pages} />
  </>
));
