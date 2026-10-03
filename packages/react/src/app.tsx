// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// An app is a list of screens. Each screen knows its own title and route, so the
// app can build the routes and the navigation without anyone writing them twice.

import { Component, createContext, useCallback, useContext, useEffect, useMemo, useRef, useState } from "react";
import type { ComponentType, ReactNode } from "react";
import { BrowserRouter, MemoryRouter, Route, Routes, matchPath, useLocation } from "react-router";
import type { ComponentSet } from "./contract.js";
import { DataProvider, useAuth } from "./data.js";
import type { CommandInput, DataSource } from "./data.js";
import { UIContext, useLinks, useUI } from "./ui.js";

export interface ScreenInfo {
  title: string;
  route: string;
  nav?: string; // the label in the navigation; a screen without one isn't listed
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
  signin?: boolean; // whether people sign in here, so the page offers it; true unless the project says otherwise
}

// Whether the page offers signing in. An app whose project names no way of signing
// in doesn't, so it never shows a button that can't work.
const SignInOffered = createContext(true);

export function App({ name, icon, screens, ui, data, location, signin = true }: AppProps) {
  const routes = (
    <Routes>
      {screens.map((s) => (
        <Route key={s.info.route} path={s.info.route} element={<Page name={name} icon={icon} screen={s} screens={screens} />} />
      ))}
      <Route path="*" element={<NotFound name={name} icon={icon} screens={screens} />} />
    </Routes>
  );
  return (
    <UIContext.Provider value={ui}>
      <SignInOffered.Provider value={signin}>
        <DataProvider source={data}>
          {location === undefined ? (
            <BrowserRouter>{routes}</BrowserRouter>
          ) : (
            <MemoryRouter initialEntries={[location]}>{routes}</MemoryRouter>
          )}
        </DataProvider>
      </SignInOffered.Provider>
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
  const offered = useContext(SignInOffered);
  const [signInError, setSignInError] = useState<string | undefined>();
  useEffect(() => {
    document.title = title === name ? name : `${title} · ${name}`;
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
      account={
        offered &&
        auth && (
          <ui.Account
            name={auth.person?.name}
            ready={auth.person !== undefined}
            error={signInError}
            onSignIn={() => {
              setSignInError(undefined);
              auth.signIn().catch((e: unknown) => setSignInError(signInMessage(e)));
            }}
            onSignOut={() => {
              setSignInError(undefined);
              auth.signOut().catch(() => setSignInError("couldn't sign you out; try again"));
            }}
          />
        )
      }
    >
      <Confirmations>{children}</Confirmations>
    </ui.Page>
  );
}

// What to tell someone whose sign-in didn't work. Closing the sign-in window is a
// choice, not a failure, so it says nothing.
function signInMessage(e: unknown): string | undefined {
  const code = (e as { code?: unknown } | null)?.code;
  if (code === "auth/popup-closed-by-user" || code === "auth/cancelled-popup-request") return undefined;
  if (code === "auth/popup-blocked") return "your browser blocked the sign-in window; allow pop-ups for this site and try again";
  if (code === "auth/account-exists-with-different-credential") {
    return "that email already signs in another way here; sign in that way";
  }
  if (code === "auth/unauthorized-domain") return "sign-in isn't allowed on this address yet";
  if (code === "auth/operation-not-allowed") return "this way of signing in isn't turned on for this site";
  // Anything else names Firebase's reason, so a problem can be found from it.
  return typeof code === "string" ? `couldn't sign you in (${code.replace(/^auth\//, "")}); try again` : "couldn't sign you in; try again";
}

// Every route's element is a Page, so going from /books/a to /books/b keeps the
// same one mounted. The body is keyed by the address, so what was typed about one
// book, or why a command on it failed, is never shown or sent as another's.
function Page({ name, icon, screen: Body, screens }: { name: string; icon: string | undefined; screen: Screen; screens: Screen[] }) {
  const { pathname } = useLocation();
  return (
    <Shell name={name} icon={icon} title={Body.info.title} screens={screens}>
      <Contained key={pathname}>
        <Body />
      </Contained>
    </Shell>
  );
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
