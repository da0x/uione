// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from site/ by one. Do not edit.

import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

// Commands go to the Go backend, which listens on port 8081.
export default defineConfig({
  plugins: [react()],
  server: { proxy: { "/api": "http://localhost:8081" } },
});
