// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// Everything that opens over the page opens in this, so it behaves the same
// everywhere: a dialog in the middle of a wide screen, and the whole screen on a
// phone, its title and × held at the top and only what's under them scrolling. It
// sits above the page's header, Escape or the × closes it, and focus goes back to
// whatever opened it.

import * as Dialog from "@radix-ui/react-dialog";
import { useRef } from "react";
import type { ReactNode } from "react";

export function Sheet({ open, title, onClose, children }: { open: boolean; title: string; onClose: () => void; children: ReactNode }) {
  // What had focus when it opened, like the button that opened it, which gets it
  // back when it closes; it's opened by any button, not only one of its own.
  const opener = useRef<HTMLElement | null>(null);
  return (
    <Dialog.Root open={open} onOpenChange={(next) => !next && onClose()}>
      <Dialog.Portal>
        <Dialog.Overlay className="fixed inset-0 z-50 bg-ink/30 backdrop-blur-[2px]" />
        <Dialog.Content
          aria-describedby={undefined}
          onOpenAutoFocus={() => {
            opener.current = document.activeElement instanceof HTMLElement ? document.activeElement : null;
          }}
          onCloseAutoFocus={(e) => {
            if (!opener.current?.isConnected) return;
            e.preventDefault();
            opener.current.focus();
          }}
          className="fixed inset-0 z-50 flex h-[100dvh] w-full flex-col overflow-hidden bg-surface text-ink sm:inset-auto sm:top-1/2 sm:left-1/2 sm:h-auto sm:max-h-[calc(100dvh-2rem)] sm:w-[min(28rem,calc(100vw-2rem))] sm:-translate-x-1/2 sm:-translate-y-1/2 sm:rounded-box sm:border sm:border-line sm:shadow-raised"
        >
          <div className="flex shrink-0 items-center justify-between gap-4 border-b border-line px-4 pt-[calc(0.75rem+env(safe-area-inset-top))] pb-3 sm:items-start sm:border-0 sm:px-6 sm:pt-6 sm:pb-4">
            <Dialog.Title className="text-lg font-semibold">{title}</Dialog.Title>
            <Dialog.Close aria-label="Close" className="-mr-2 flex h-10 w-10 items-center justify-center rounded-box text-xl text-muted hover:text-ink sm:-mt-1 sm:h-8 sm:w-8">
              <span aria-hidden="true">×</span>
            </Dialog.Close>
          </div>
          <div className="flex min-h-0 flex-1 flex-col gap-4 overflow-y-auto px-4 pt-4 pb-[calc(1.5rem+env(safe-area-inset-bottom))] sm:px-6 sm:pt-0 sm:pb-6 [&>button]:self-auto">{children}</div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  );
}
