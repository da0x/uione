// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from site.one by one. Do not edit.

import { Built, BuiltLink, Link, Text } from "@uione/react";

export function SiteFooter() {
  return (
    <>
      <Text>© <Built value="year" /> <Link to="https://www.linkedin.com/in/daheralfawares">Daher Alfawares</Link></Text>
      <BuiltLink to="release">uione <Built value="version" /></BuiltLink>
      <BuiltLink to="source"><Built value="commit" /></BuiltLink>
    </>
  );
}
