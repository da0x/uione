// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// The components a generated screen is made of. Each one does the work, like running
// a command, asking before it does, or hiding a value that can't be trusted, and
// then hands the drawing to the app's component set.

import { useEffect, useState } from "react";
import type { ReactNode } from "react";
import { useParams } from "react-router";
import { fill, useConfirmContext } from "./app.js";
import type { FieldProps } from "./contract.js";
import { useRunner } from "./data.js";
import type { CommandInput, ViewState } from "./data.js";
import { action, label, show, useLinks, useUI } from "./ui.js";

export function Hero({ title, children }: { title: string; children: ReactNode }) {
  const ui = useUI();
  return <ui.Hero title={title}>{children}</ui.Hero>;
}

export function Section({ title, id, children }: { title: string; id?: string; children: ReactNode }) {
  const ui = useUI();
  return (
    <ui.Section title={title} id={id}>
      {children}
    </ui.Section>
  );
}

export function Text({ children }: { children: ReactNode }) {
  const ui = useUI();
  return <ui.Text>{children}</ui.Text>;
}

export function Link({ to, children }: { to: string; children: ReactNode }) {
  const ui = useUI();
  const link = useLinks();
  return <ui.Link {...link(to)}>{children}</ui.Link>;
}

export function Code({ lang, source }: { lang: string; source: string }) {
  const ui = useUI();
  return <ui.Code lang={lang} source={withoutLicense(source)} />;
}

// Code on a page is there to be read as an example, so a license header at the top
// of the file it comes from isn't shown: the header is about the file, not the idea.
export function withoutLicense(source: string): string {
  const lines = source.split("\n");
  if (!/^(\/\/|#) Copyright /.test(lines[0] ?? "")) return source;
  const end = lines.findIndex((line) => /SPDX-License-Identifier: /.test(line));
  if (end < 0) return source;
  let rest = end + 1;
  while (rest < lines.length && lines[rest]!.trim() === "") rest++;
  return lines.slice(rest).join("\n");
}

export interface DocPage {
  slug: string;
  title: string;
  html: string;
}

// A set of pages, one per address under `base`, like /docs/language. With no page in
// the address, the first one is shown.
export function Pages({ base, pages }: { base: string; pages: DocPage[] }) {
  const ui = useUI();
  const link = useLinks();
  const { page } = useParams();
  const current = page === undefined ? pages[0] : pages.find((p) => p.slug === page);
  return (
    <ui.Pages
      pages={pages.map((p) => ({ title: p.title, current: p === current, ...link(`${base}/${p.slug}`) }))}
      title={current?.title}
      html={current?.html}
    />
  );
}

// A Markdown field of a view, shown rendered by the component set.
export function Markdown({ view, field }: { view: ViewState; field: string }) {
  const ui = useUI();
  const value = view.data?.[field];
  return <ui.Markdown status={view.status} source={typeof value === "string" ? value : undefined} />;
}

// A value from a view, shown only while the view is live. While it's loading, stale
// or denied, the component set shows a placeholder instead, because an old number
// that looks current is worse than no number.
export function Live({ view, field }: { view: ViewState; field: string }) {
  const ui = useUI();
  const value = view.status === "live" ? show(view.data?.[field]) : "";
  return <ui.Live status={view.status} value={value} />;
}

function useConfirmedRunner() {
  const runner = useRunner();
  const { questions, ask } = useConfirmContext();
  const run = async (command: string, input: CommandInput = {}, about: CommandInput = input) => {
    const question = questions.get(command);
    if (question !== undefined && !(await ask(fill(question, about)))) return false;
    return runner.run(command, input);
  };
  return { run, busy: runner.busy, error: runner.error };
}

// Asks before `command` runs, wherever on the screen it's run from.
export function Confirm({ command, question }: { command: string; question: string }) {
  const { questions } = useConfirmContext();
  useEffect(() => {
    questions.set(command, question);
    return () => {
      questions.delete(command);
    };
  }, [questions, command, question]);
  return null;
}

// A button that runs a command on its own, with nothing to fill in.
export function Command({ name }: { name: string }) {
  const ui = useUI();
  const runner = useConfirmedRunner();
  return (
    <ui.Button kind="primary" disabled={runner.busy(name)} error={runner.error(name)} onClick={() => void runner.run(name)}>
      {label(action(name))}
    </ui.Button>
  );
}

type Row = Record<string, unknown> & { id: string };

// A view's rows, or one of its named lists, one column per field. Each command in
// `actions` becomes a button on every row, run with that row's id. A column in
// `pictures`, like member.picture, shows the person's picture rather than its
// address, and only when it's an https address.
export function Table({
  view,
  list = "rows",
  columns,
  actions = [],
  link,
  pictures = [],
}: {
  view: ViewState;
  list?: string; // which of the view's lists, like comments
  columns: Record<string, string>;
  actions?: string[];
  link?: string; // a route like /books/:book, which each row's id fills
  pictures?: string[];
}) {
  const ui = useUI();
  const links = useLinks();
  const rows = (view.data?.[list] as Row[] | undefined) ?? [];
  const runner = useConfirmedRunner();
  return (
    <ui.Table
      status={view.status}
      columns={Object.values(columns)}
      error={actions.map((name) => runner.error(name)).find((e) => e !== undefined)}
      rows={rows.map((row) => ({
        id: row.id,
        link: link ? links(link.replace(/:[A-Za-z_]\w*/, encodeURIComponent(row.id))) : undefined,
        cells: Object.keys(columns).map((key) => {
          if (!pictures.includes(key)) return show(row[key]);
          const source = row[key];
          return typeof source === "string" && source.startsWith("https://") ? <ui.Picture source={source} /> : "";
        }),
        actions: actions.map((name) => ({
          label: label(action(name)),
          disabled: runner.busy(name),
          onClick: () => void runner.run(name, { id: row.id }, row),
        })),
      }))}
    />
  );
}

export interface FieldSpec {
  name: string;
  label?: string;
  type?: string;
  hint?: string;
}

// What a field shows for a value from a view: a date as 2026-09-30, the way a date
// input takes it, a list separated by commas, and anything else as text.
function asField(value: unknown): string {
  if (value instanceof Date) {
    const pad = (n: number) => String(n).padStart(2, "0");
    return `${value.getFullYear()}-${pad(value.getMonth() + 1)}-${pad(value.getDate())}`;
  }
  if (Array.isArray(value)) return value.map(asField).join(", ");
  return value === null || value === undefined ? "" : String(value);
}

// What a form sends for a field: a list is written separated by commas, and sent
// as the list of what's between them.
function asInput(value: string, type: string | undefined): unknown {
  if (type !== "list") return value;
  return value
    .split(",")
    .map((item) => item.trim())
    .filter((item) => item !== "");
}

// A form for a command. Inline by default. With `button`, it's a button that opens
// the form in a dialog, which is how a screen offers to create something.
export function Form({
  command,
  fields,
  button = false,
  from,
  id,
  given = {},
}: {
  command: string;
  fields: (string | FieldSpec)[];
  button?: boolean;
  from?: ViewState; // for an update: the view whose document the fields start from
  id?: string; // for an update: the entity it changes
  given?: Record<string, string | undefined>; // sent without being asked for, like the project an issue is made in
}) {
  const ui = useUI();
  const runner = useConfirmedRunner();
  const specs = fields.map((f) => (typeof f === "string" ? { name: f } : f));
  const empty = () => Object.fromEntries(specs.map((f) => [f.name, ""]));
  const [values, setValues] = useState<Record<string, string>>(empty);
  const [open, setOpen] = useState(false);
  // An update form starts from what's stored, and follows it until someone types.
  // After an update it keeps what was sent until the view has a newer document,
  // rather than showing the old one for a moment.
  const [touched, setTouched] = useState(false);
  const [sent, setSent] = useState<ViewState["data"]>();
  const stored = from?.status === "live" ? from.data : undefined;
  useEffect(() => {
    if (!stored || touched || stored === sent) return;
    setValues(Object.fromEntries(specs.map((f) => [f.name, asField(stored[f.name])])));
    // The fields come from the screen and never change, so they aren't a dependency.
  }, [stored, touched, sent]);

  const submit = async () => {
    const typed = Object.fromEntries(specs.map((f) => [f.name, asInput(values[f.name] ?? "", f.type)]));
    const input = { ...typed, ...given };
    if (await runner.run(command, id === undefined ? input : { ...input, id })) {
      if (from) {
        setSent(stored);
        setTouched(false);
      } else {
        setValues(empty());
      }
      setOpen(false);
    }
  };

  const form = (
    <ui.Form
      fields={specs.map(
        (f): FieldProps => ({
          name: f.name,
          label: f.label ?? label(f.name),
          type: f.type ?? "text",
          value: values[f.name] ?? "",
          hint: f.hint,
          onChange: (value) => {
            setTouched(true);
            setValues((current) => ({ ...current, [f.name]: value }));
          },
        }),
      )}
      submit={label(action(command))}
      busy={runner.busy(command)}
      error={runner.error(command)}
      onSubmit={() => void submit()}
    />
  );

  if (!button) return form;
  return (
    <>
      <ui.Button kind="primary" onClick={() => setOpen(true)}>
        {label(action(command))}
      </ui.Button>
      <ui.Dialog open={open} title={label(action(command))} onClose={() => setOpen(false)}>
        {form}
      </ui.Dialog>
    </>
  );
}
