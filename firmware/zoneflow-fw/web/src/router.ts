// Minimal history router. The device serves index.html for any path without
// an extension, so /zones/3 survives a reload (web-ui-contract.md).
import { signal } from '@preact/signals';

export const path = signal(typeof location === 'undefined' ? '/' : location.pathname);

/** Hook for "unsaved changes" guards: return false to cancel navigation. */
let guard: (() => boolean) | null = null;
export function setNavigationGuard(fn: (() => boolean) | null): void {
  guard = fn;
}

export function navigate(to: string, opts: { replace?: boolean } = {}): void {
  if (to === path.value) return;
  if (guard && !guard()) return;
  guard = null;
  if (opts.replace) history.replaceState(null, '', to);
  else history.pushState(null, '', to);
  path.value = to;
  window.scrollTo(0, 0);
  // Move focus to the new page's heading for screen readers and keyboards.
  requestAnimationFrame(() => document.querySelector<HTMLElement>('main h1')?.focus({ preventScroll: true }));
}

if (typeof window !== 'undefined') {
  window.addEventListener('popstate', () => {
    if (guard && !guard()) {
      history.pushState(null, '', path.value);
      return;
    }
    guard = null;
    path.value = location.pathname;
  });
}

/** Matches "/zones/:id" against a path; returns params or null. */
export function match(pattern: string, p: string): Record<string, string> | null {
  const a = pattern.split('/').filter(Boolean);
  const b = p.split('/').filter(Boolean);
  if (a.length !== b.length) return null;
  const out: Record<string, string> = {};
  for (let i = 0; i < a.length; i++) {
    const x = a[i] as string;
    const y = b[i] as string;
    if (x.startsWith(':')) out[x.slice(1)] = decodeURIComponent(y);
    else if (x !== y) return null;
  }
  return out;
}

/** Click handler for in-app links: keeps modifier-clicks working. */
export function onLinkClick(e: MouseEvent): void {
  const a = (e.currentTarget as HTMLAnchorElement | null)?.closest('a');
  if (!a || e.defaultPrevented || e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;
  const href = a.getAttribute('href');
  if (!href || !href.startsWith('/') || a.target) return;
  e.preventDefault();
  navigate(href);
}
