// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    environment: "jsdom",
    globals: true,
    include: ["test/**/*.test.tsx"],
  },
});
