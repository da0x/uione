// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from language.one by one. Do not edit.

import { Pages, screen } from "@uione/react";
import pages from "../pages/language.generated";

export const language = screen({ title: "Language", route: "/language/:page?", nav: "Language" }, () => (
  <>
    <Pages base="/language" pages={pages} />
  </>
));
