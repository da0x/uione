// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// An app is a list of screens. Each screen knows its own title and route, so the
// app can build the routes and the navigation without anyone writing them twice.

import { Component, createContext, useCallback, useContext, useEffect, useMemo, useRef, useState } from "react";
import type { ComponentType, ReactNode } from "react";
import { BrowserRouter, MemoryRouter, Route, Routes, matchPath, useLocation } from "react-router";
import type { Analytics, ComponentSet } from "./contract.js";
import { DataProvider, useAuth } from "./data.js";
import type { CommandInput, DataSource, ViewState } from "./data.js";
import { Rest, UIContext, useLinks, useUI } from "./ui.js";

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
  authentication?: boolean; // whether people sign in here, so the page offers it; true unless the project says otherwise
  analytics?: Analytics; // counting visitors, once they agree; none counts no one, and asks no one
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

export function App({ name, icon, screens, ui, data, location, authentication = true, analytics }: AppProps) {
  const routes = (
    <>
      {analytics && <Counting analytics={analytics} />}
      <Routes>
      {screens.map((s) => (
        <Route key={s.info.route} path={routerPath(s.info.route)} element={<Page name={name} icon={icon} screen={s} screens={screens} />} />
      ))}
      <Route path="*" element={<NotFound name={name} icon={icon} screens={screens} />} />
      </Routes>
    </>
  );
  return (
    <UIContext.Provider value={ui}>
      <DataProvider source={data}>
        <SignInProvider offered={authentication}>
          {location === undefined ? (
            <BrowserRouter>{routes}</BrowserRouter>
          ) : (
            <MemoryRouter initialEntries={[location]}>{routes}</MemoryRouter>
          )}
        </SignInProvider>
      </DataProvider>
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

function routerPath(route: string): string {
  return route.replace(restOf, "/*");
}

// Every route's element is a Page, so going from /books/a to /books/b keeps the
// same one mounted. The body is keyed by the address, so what was typed about one
// book, or why a command on it failed, is never shown or sent as another's. What a
// last parameter like :file* takes isn't part of the key: going from one file to
// another is the same page, as an editor keeps its place moving between files.
function Page({ name, icon, screen: Body, screens }: { name: string; icon: string | undefined; screen: Screen; screens: Screen[] }) {
  const { pathname } = useLocation();
  const [titled, setTitled] = useState<string | undefined>();
  const rest = restOf.exec(Body.info.route)?.[1];
  const taken = rest ? matchPath(routerPath(Body.info.route), pathname)?.params["*"] : undefined;
  const key = taken ? pathname.split("/").slice(0, -taken.split("/").length).join("/") : pathname;
  return (
    <Shell name={name} icon={icon} title={titled ?? Body.info.title} screens={screens}>
      <Rest.Provider value={rest}>
        <Titling.Provider value={setTitled}>
          <Contained key={key}>
            <Body />
          </Contained>
        </Titling.Provider>
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
