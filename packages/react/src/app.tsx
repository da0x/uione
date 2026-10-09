// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// An app is a list of screens. Each screen knows its own title and route, so the
// app can build the routes and the navigation without anyone writing them twice.

import { Component, createContext, useCallback, useContext, useEffect, useMemo, useRef, useState } from "react";
import type { ComponentType, ReactNode } from "react";
import { createPortal } from "react-dom";
import { BrowserRouter, MemoryRouter, Route, Routes, matchPath, useLocation, useParams } from "react-router";
import type { Analytics, Build, ComponentSet } from "./contract.js";
import { buildLinks } from "./contract.js";
import { DataProvider, useAuth, useView } from "./data.js";
import type { CommandInput, DataSource, ViewState } from "./data.js";
import { partsOf } from "./keys.js";
import { Rest, UIContext, useLinks, useUI } from "./ui.js";

export interface ScreenInfo {
  title: string;
  route: string;
  nav?: string; // the label in the navigation; a screen without one isn't listed
  // It shows something its address says after its own path, like a tab or a board
  // rather than a table: /neotrac/boards/main/board/in_progress.
  shown?: boolean;
}

export type Screen = ComponentType & { info: ScreenInfo };

export function screen(info: ScreenInfo, body: () => ReactNode): Screen {
  function ScreenBody() {
    return <>{body()}</>;
  }
  return Object.assign(ScreenBody, { info });
}

export interface AppProps {
  name: string;
  screens: Screen[];
  ui: ComponentSet;
  data: DataSource;
  icon?: string; // the address of the app's icon, shown beside its name
  location?: string; // start at this address, in memory rather than the browser's
  authentication?: boolean; // whether people sign in here, so the page offers it; true unless the project says otherwise
  color?: string; // the site's own color, as #rrggbb, for its buttons and links in place of the component set's
  analytics?: Analytics; // counting visitors, once they agree; none counts no one, and asks no one
  // Numbers beside the app's name on every page, each a value of a view, like what's
  // new to the person reading; shown once they're signed in, when more than none.
  badges?: readonly Badge[];
  build?: Build; // what it was built from, which its footer can show
  footer?: SiteFooter; // at the foot of every page
  // Display settings beside light and dark: contrast, text size, spacing, a legible
  // font, motion, links and focus, each kept in the reader's browser.
  display?: boolean;
}

// The site's footer: how it's laid out, and what it holds, drawn on every page.
export interface SiteFooter {
  layout: "bar" | "columns";
  content: ComponentType;
}

export interface Badge {
  view: string; // like projects::news
  value: string; // like unread
}

const BadgeSpec = createContext<readonly Badge[]>([]);
const FooterSpec = createContext<SiteFooter | undefined>(undefined);
const DisplaySpec = createContext(false);
export const BuildSpec = createContext<Build | undefined>(undefined);

// A value of what the site was built from, in a footer's words: the year it's read
// in, its uione release, or its commit, shortened.
export function Built({ value }: { value: "year" | "version" | "commit" }) {
  const build = useContext(BuildSpec);
  if (value === "year") return <>{new Date().getFullYear()}</>;
  if (value === "version") return <>{build?.version ?? ""}</>;
  return <>{build?.commit?.slice(0, 7) ?? ""}</>;
}

// A link to what the site was built from: its release's notes, or its commit in its
// repository. Where that can't be told, like a build outside git, its words alone.
export function BuiltLink({ to, children }: { to: "release" | "source"; children: ReactNode }) {
  const ui = useUI();
  const build = useContext(BuildSpec);
  const href = build ? buildLinks(build)[to] : undefined;
  if (to === "source" && !build?.commit) return null;
  return href ? (
    <ui.Link href={href} external>
      {children}
    </ui.Link>
  ) : (
    <span>{children}</span>
  );
}

// Counts what's new to the person, and says how many to its parent.
function ReadBadge({ badge, at, onRead }: { badge: Badge; at: number; onRead: (at: number, n: number) => void }) {
  const view = useView(badge.view);
  const value = view.status === "live" ? view.data?.[badge.value] : undefined;
  const n = typeof value === "number" ? value : 0;
  useEffect(() => onRead(at, n), [at, n, onRead]);
  return null;
}

// The visitor's answer to being counted, kept in their browser: agreed, refused, or
// not asked yet.
const consentKey = "uione-analytics-consent";

function storedConsent(): boolean | undefined {
  try {
    const answer = localStorage.getItem(consentKey);
    return answer === "agreed" ? true : answer === "refused" ? false : undefined;
  } catch {
    return undefined;
  }
}

// Counts each screen the visitor sees, and asks them once whether they may be
// counted with cookies; until they agree, they're counted without.
function Counting({ analytics }: { analytics: Analytics }) {
  const ui = useUI();
  const { pathname } = useLocation();
  const [answer, setAnswer] = useState(storedConsent);
  useEffect(() => {
    if (answer !== undefined) analytics.consent(answer);
  }, [analytics, answer]);
  useEffect(() => {
    // After the screen has set its title.
    const timer = setTimeout(() => analytics.page(pathname, document.title), 0);
    return () => clearTimeout(timer);
  }, [analytics, pathname]);
  if (answer !== undefined) return null;
  return (
    <ui.Consent
      onAnswer={(agreed) => {
        try {
          localStorage.setItem(consentKey, agreed ? "agreed" : "refused");
        } catch {
          // Without storage, they're asked again next time.
        }
        setAnswer(agreed);
      }}
    />
  );
}

// Signing in, from the page's account area or from a form that needs it: at once,
// with one way to sign in, or by choosing one, with several. An app whose project
// names no way of signing in offers none, so it never shows a button that can't work.
interface SignInFlow {
  offered: boolean;
  begin: () => void;
  error: string | undefined; // why signing in just failed, shown beside the account
  clear: () => void;
}

const SignInContext = createContext<SignInFlow>({ offered: true, begin: () => {}, error: undefined, clear: () => {} });

export function useSignIn(): SignInFlow {
  return useContext(SignInContext);
}

function SignInProvider({ offered, children }: { offered: boolean; children: ReactNode }) {
  const ui = useUI();
  const auth = useAuth();
  const [choosing, setChoosing] = useState(false);
  const [busy, setBusy] = useState<string | undefined>();
  const [error, setError] = useState<string | undefined>();
  const methods = auth?.methods ?? [];
  const signIn = (method?: string) => {
    if (!auth) return;
    setError(undefined);
    setBusy(method);
    auth.signIn(method).then(
      () => setChoosing(false),
      (e: unknown) => setError(signInMessage(e)),
    ).finally(() => setBusy(undefined));
  };
  const flow: SignInFlow = {
    offered,
    begin: () => {
      if (methods.length > 1) {
        setError(undefined);
        setChoosing(true);
      } else signIn(methods[0]?.id);
    },
    error: choosing ? undefined : error,
    clear: () => setError(undefined),
  };
  return (
    <SignInContext.Provider value={flow}>
      {children}
      {methods.length > 1 && (
        <ui.Dialog
          open={choosing}
          title="Sign in"
          onClose={() => {
            setChoosing(false);
            setError(undefined);
          }}
        >
          <ui.SignIn methods={methods} busy={busy} error={error} onChoose={signIn} />
        </ui.Dialog>
      )}
    </SignInContext.Provider>
  );
}

// The site's own color as the accent tokens a component set draws with: the color
// itself on a light page, with a darker hover, a soft tint and text that reads on
// it; and on a dark page, a lighter one, so it stands out there too.
export function accentOf(color: string): string {
  if (!/^#[0-9a-f]{6}$/i.test(color)) return "";
  const [r, g, b] = [1, 3, 5].map((at) => {
    const c = parseInt(color.slice(at, at + 2), 16) / 255;
    return c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
  });
  const luminance = 0.2126 * r! + 0.7152 * g! + 0.0722 * b!;
  const ink = 1.05 / (luminance + 0.05) >= 4.5 ? "#ffffff" : "#0e1726";
  const light = `--color-accent: ${color}; --color-accent-hover: color-mix(in oklab, ${color} 82%, black); --color-accent-ink: ${ink}; --color-accent-soft: color-mix(in oklab, ${color} 12%, white); --color-grid: color-mix(in srgb, ${color} 7%, transparent);`;
  // Lightened in OKLCH, which keeps the color as vivid as it was; where that isn't
  // understood, the mix before it stands.
  const dark =
    `--color-accent: color-mix(in oklab, ${color} 55%, white); --color-accent: oklch(from ${color} max(l, 0.76) c h); ` +
    `--color-accent-hover: color-mix(in oklab, ${color} 35%, white); --color-accent-hover: oklch(from ${color} max(l, 0.84) c h); ` +
    `--color-accent-ink: #0a101c; --color-accent-soft: color-mix(in oklab, ${color} 28%, #0a101c); --color-grid: color-mix(in srgb, ${color} 8%, transparent);`;
  // As exact as a theme's own rules, which it follows on the page, so the site's
  // color wins over a theme's accent too.
  const any = ':is([data-palette], :not([data-palette]))';
  return `:root${any} { ${light} } @media (prefers-color-scheme: dark) { :root${any}:not([data-theme="light"]) { ${dark} } } :root${any}[data-theme="dark"] { ${dark} }`;
}

export function App({ name, icon, screens, ui, data, location, authentication = true, analytics, color, badges = [], build, footer, display = false }: AppProps) {
  const routes = (
    <>
      {color && <style>{accentOf(color)}</style>}
      {analytics && <Counting analytics={analytics} />}
      <Routes>
      {screens.map((s) => (
        <Route key={s.info.route} path={routerPath(s.info.route, s.info.shown)} element={<Page name={name} icon={icon} screen={s} screens={screens} />} />
      ))}
      <Route path="*" element={<NotFound name={name} icon={icon} screens={screens} />} />
      </Routes>
    </>
  );
  return (
    <UIContext.Provider value={ui}>
      <BadgeSpec.Provider value={badges}>
      <FooterSpec.Provider value={footer}>
      <DisplaySpec.Provider value={display}>
      <BuildSpec.Provider value={build}>
      <DataProvider source={data}>
        <SignInProvider offered={authentication}>
          {location === undefined ? (
            <BrowserRouter>{routes}</BrowserRouter>
          ) : (
            <MemoryRouter initialEntries={[location]}>{routes}</MemoryRouter>
          )}
        </SignInProvider>
      </DataProvider>
      </BuildSpec.Provider>
      </DisplaySpec.Provider>
      </FooterSpec.Provider>
      </BadgeSpec.Provider>
    </UIContext.Provider>
  );
}

function Shell({
  name,
  icon,
  title,
  screens,
  children,
}: {
  name: string;
  icon: string | undefined;
  title: string;
  screens: Screen[];
  children: ReactNode;
}) {
  const ui = useUI();
  const link = useLinks();
  const { pathname } = useLocation();
  const auth = useAuth();
  const signIn = useSignIn();
  const [signOutError, setSignOutError] = useState<string | undefined>();
  const badgeSpecs = useContext(BadgeSpec);
  const footer = useContext(FooterSpec);
  const display = useContext(DisplaySpec);
  const [counts, setCounts] = useState<readonly number[]>([]);
  const onRead = useCallback((at: number, n: number) => {
    setCounts((was) => {
      if (was[at] === n) return was;
      const next = [...was];
      next[at] = n;
      return next;
    });
  }, []);
  // Where a screen's Heading puts its buttons, on the title's row.
  const [slot, setSlot] = useState<HTMLElement | null>(null);
  // And where its Crumbs put the pages above it.
  const [trail, setTrail] = useState<HTMLElement | null>(null);
  // And where its Subtitle puts its words about itself.
  const [under, setUnder] = useState<HTMLElement | null>(null);
  useEffect(() => {
    document.title = !title || title === name ? name : `${title} · ${name}`;
  }, [title, name]);
  // The screens with a nav label, linked at their route without parameters, so
  // /docs/:page is listed as /docs. The current one is marked.
  const nav = screens
    .filter((s) => s.info.nav !== undefined)
    .map((s) => ({
      label: s.info.nav!,
      current: matchPath(s.info.route, pathname) !== null,
      ...link(s.info.route.split("/:")[0] || "/"),
    }));
  return (
    <ui.Page
      name={name}
      icon={icon}
      home={link("/")}
      nav={nav}
      title={title}
      heading={<div ref={setSlot} style={{ display: "contents" }} />}
      crumbs={<div ref={setTrail} style={{ display: "contents" }} />}
      subtitle={<div ref={setUnder} style={{ display: "contents" }} />}
      badges={auth?.person ? badgeSpecs.map((badge, at) => ({ count: counts[at] ?? 0, label: badge.value.replaceAll("_", " ") })) : undefined}
      footer={footer ? { layout: footer.layout, children: <footer.content /> } : undefined}
      display={display}
      account={
        signIn.offered &&
        auth && (
          <ui.Account
            name={auth.person?.name}
            ready={auth.person !== undefined}
            error={signIn.error ?? signOutError}
            onSignIn={() => {
              setSignOutError(undefined);
              signIn.begin();
            }}
            onSignOut={() => {
              setSignOutError(undefined);
              signIn.clear();
              auth.signOut().catch(() => setSignOutError("couldn't sign you out; try again"));
            }}
          />
        )
      }
    >
      {auth?.person && badgeSpecs.map((badge, at) => <ReadBadge key={at} badge={badge} at={at} onRead={onRead} />)}
      <HeadingSlot.Provider value={slot}>
        <CrumbsSlot.Provider value={trail}>
          <SubtitleSlot.Provider value={under}>
            <SiteName.Provider value={name}>
              <Confirmations>{children}</Confirmations>
            </SiteName.Provider>
          </SubtitleSlot.Provider>
        </CrumbsSlot.Provider>
      </HeadingSlot.Provider>
    </ui.Page>
  );
}

const HeadingSlot = createContext<HTMLElement | null>(null);

// A screen's own buttons on its title's row, at its end, like Edit project. They're
// drawn there from wherever the screen has them, keeping what's around them.
export function Heading({ children }: { children: ReactNode }) {
  const slot = useContext(HeadingSlot);
  return slot ? createPortal(children, slot) : null;
}

const CrumbsSlot = createContext<HTMLElement | null>(null);
// The site's name: a page titled with just that, as it's written rather than as it
// reads, has no title of its own, so no place among the pages above another. A
// project that happens to share the site's name keeps its place.
const SiteName = createContext("");
// Whether the page shown is titled with just the site's name.
const Untitled = createContext(false);
const SubtitleSlot = createContext<HTMLElement | null>(null);

// A screen's words about itself, like a project's summary, under its title.
export function Subtitle({ children }: { children: ReactNode }) {
  const slot = useContext(SubtitleSlot);
  return slot ? createPortal(children, slot) : null;
}

// The pages above a screen, each by its address and its title, which may be read
// from the page's views, like a project's name; the page itself comes last.
export function Crumbs({
  items,
}: {
  // fill: a parameter the page's address doesn't have, from a view, like an issue's
  // board, with how many parts the id is keyed by, so its own part fills it.
  items: readonly { to: string; title: readonly TitlePart[]; fill?: Record<string, readonly [ViewState, string, number]> }[];
}) {
  const slot = useContext(CrumbsSlot);
  const ui = useUI();
  const link = useLinks();
  const params = useParams();
  const current = usePageTitle();
  const name = useContext(SiteName);
  const untitled = useContext(Untitled);
  const filled = (name: string, fill: Record<string, readonly [ViewState, string, number]> = {}) => {
    if (params[name] !== undefined) return params[name];
    const from = fill[name];
    const id = from ? from[0].data?.[from[1]] : undefined;
    if (typeof id !== "string" || id === "") return undefined;
    return from[2] > 1 ? (partsOf(id, from[2])?.at(-1) ?? id) : id;
  };
  const own = items.filter(({ title }) => !(title.length === 1 && title[0] === name));
  const shown = own.map(({ to, title, fill }) => ({
    label: title.map((part) => (typeof part === "string" ? part : String(part[0].data?.[part[1]] ?? ""))).join("").trim() || "…",
    link: link(to.replace(/:([A-Za-z_]\w*)/g, (written, name: string) => {
      const value = filled(name, fill);
      return value === undefined ? written : encodeURIComponent(value);
    })),
  }));
  // A page with no title of its own shows no heading, and so no trail to it either.
  if (untitled) return null;
  return slot ? createPortal(<ui.Crumbs items={shown} current={current} />, slot) : null;
}

// What to tell someone whose sign-in didn't work. Closing the sign-in window is a
// choice, not a failure, so it says nothing.
function signInMessage(e: unknown): string | undefined {
  const code = (e as { code?: unknown } | null)?.code;
  if (code === "auth/popup-closed-by-user" || code === "auth/cancelled-popup-request") return undefined;
  if (code === "auth/popup-blocked") return "your browser blocked the sign-in window; allow pop-ups for this site and try again";
  if (code === "auth/account-exists-with-different-credential") {
    return "your email already signs in here another way; sign in that way once, and this way will work too";
  }
  if (code === "auth/unauthorized-domain") return "sign-in isn't allowed on this address yet";
  if (code === "auth/operation-not-allowed") return "this way of signing in isn't turned on for this site";
  // Anything else names Firebase's reason, so a problem can be found from it.
  return typeof code === "string" ? `couldn't sign you in (${code.replace(/^auth\//, "")}); try again` : "couldn't sign you in; try again";
}

// A route's last parameter may take the rest of the address, like :file* in
// /code/:file*, which the router writes as *.
const restOf = /\/:([A-Za-z_]\w*)\*$/;

function routerPath(route: string, shown = false): string {
  if (restOf.test(route)) return route.replace(restOf, "/*");
  return shown ? `${route === "/" ? "" : route}/*` : route;
}

// What a page's address says after its own path, about what it shows, like
// board/in_progress, and the path it's after, for the hooks that read and write it.
// A screen whose path doesn't take more, like a hand-written one, keeps what it
// shows in the page instead.
export const ShownPath = createContext<{ base: string; parts: string[]; open: boolean }>({ base: "", parts: [], open: false });

// Every route's element is a Page, so going from /books/a to /books/b keeps the
// same one mounted. The body is keyed by the address, so what was typed about one
// book, or why a command on it failed, is never shown or sent as another's. What a
// last parameter like :file* takes isn't part of the key: going from one file to
// another is the same page, as an editor keeps its place moving between files.
function Page({ name, icon, screen: Body, screens }: { name: string; icon: string | undefined; screen: Screen; screens: Screen[] }) {
  const { pathname } = useLocation();
  const [titled, setTitled] = useState<string | undefined>();
  const rest = restOf.exec(Body.info.route)?.[1];
  const shown = Boolean(Body.info.shown) && !rest;
  const taken = rest || shown ? matchPath(routerPath(Body.info.route, shown), pathname)?.params["*"] : undefined;
  const key = taken ? pathname.split("/").slice(0, -taken.split("/").length).join("/") : pathname.replace(/\/$/, "");
  // A tab or a board in the address is what the page shows, not another page, so
  // changing it keeps the page as it is.
  const parts = shown && taken ? taken.split("/").filter(Boolean).map(decodeURIComponent) : [];
  const base = shown ? key || "/" : pathname;
  return (
    <Shell name={name} icon={icon} title={titled ?? Body.info.title} screens={screens}>
      <Rest.Provider value={rest}>
        <ShownPath.Provider value={{ base, parts, open: shown }}>
        <Titling.Provider value={setTitled}>
          <Titled.Provider value={titled ?? Body.info.title}>
            <Untitled.Provider value={Body.info.title === name}>
              <Contained key={key}>
                <Body />
              </Contained>
            </Untitled.Provider>
          </Titled.Provider>
        </Titling.Provider>
        </ShownPath.Provider>
      </Rest.Provider>
    </Shell>
  );
}

// A title made from what the page shows, like an issue's number and title: its
// words, and each value as the view and field it's read from. Until every view it
// reads has arrived, the page has no title, rather than a wrong one, and leaving
// the page gives it back its own.
export type TitlePart = string | readonly [ViewState, string];

const Titling = createContext<(title: string | undefined) => void>(() => {});
const Titled = createContext("");

// The page's title as it's shown now, like an issue's number and title.
export function usePageTitle(): string {
  return useContext(Titled);
}

export function useTitle(parts: readonly TitlePart[]) {
  const set = useContext(Titling);
  const ready = parts.every((part) => typeof part === "string" || part[0].status === "live");
  const title = ready ? parts.map((part) => (typeof part === "string" ? part : String(part[0].data?.[part[1]] ?? ""))).join("").trim() : "";
  useEffect(() => {
    set(title);
    return () => set(undefined);
  }, [set, title]);
}

// A screen that throws while it's drawn takes down only itself, so the navigation
// is still there to go somewhere else.
class Contained extends Component<{ children: ReactNode }, { failed: boolean }> {
  state = { failed: false };

  static getDerivedStateFromError() {
    return { failed: true };
  }

  render() {
    return this.state.failed ? <Broken /> : this.props.children;
  }
}

function Broken() {
  const ui = useUI();
  return <ui.Text>Something went wrong showing this page. Try reloading it.</ui.Text>;
}

function NotFound({ name, icon, screens }: { name: string; icon: string | undefined; screens: Screen[] }) {
  const ui = useUI();
  return (
    <Shell name={name} icon={icon} title="Not found" screens={screens}>
      <ui.Text>There's nothing at this address.</ui.Text>
    </Shell>
  );
}

// Asking before a command runs. A screen's <Confirm> registers the question for a
// command, and anything that runs that command asks it first, with {field} in the
// question filled in from the row or form it's about.

interface ConfirmContextValue {
  questions: Map<string, string>;
  ask: (question: string) => Promise<boolean>;
}

const ConfirmContext = createContext<ConfirmContextValue | null>(null);

export function useConfirmContext(): ConfirmContextValue {
  const context = useContext(ConfirmContext);
  if (!context) throw new Error("uione: confirmations need to be inside a screen");
  return context;
}

export function fill(question: string, values: CommandInput): string {
  return question.replace(/\{(\w+)\}/g, (_, name: string) => String(values[name] ?? ""));
}

function Confirmations({ children }: { children: ReactNode }) {
  const ui = useUI();
  const questions = useRef(new Map<string, string>());
  type Pending = { question: string; answer: (yes: boolean) => void };
  const [pending, setPending] = useState<Pending | null>(null);
  const open = useRef<Pending | null>(null);
  // A new question replaces one still open, and the one it replaces is answered no,
  // so whatever was waiting on it doesn't wait forever.
  const ask = useCallback(
    (question: string) =>
      new Promise<boolean>((resolve) => {
        open.current?.answer(false);
        open.current = { question, answer: resolve };
        setPending(open.current);
      }),
    [],
  );
  const value = useMemo(() => ({ questions: questions.current, ask }), [ask]);
  const answer = (yes: boolean) => {
    open.current?.answer(yes);
    open.current = null;
    setPending(null);
  };
  return (
    <ConfirmContext.Provider value={value}>
      {children}
      <ui.Dialog open={pending !== null} title="Are you sure?" onClose={() => answer(false)}>
        <ui.Text>{pending?.question}</ui.Text>
        <ui.Button kind="secondary" onClick={() => answer(false)}>
          Cancel
        </ui.Button>
        <ui.Button kind="danger" onClick={() => answer(true)}>
          Yes
        </ui.Button>
      </ui.Dialog>
    </ConfirmContext.Provider>
  );
}
