// SPDX-License-Identifier: Apache-2.0
// Runs in the frame, before the web interface. The interface is the same
// build the controller serves; it expects to be at the root of the
// controller's own address and to reach /api/v1 there. This script gives it
// that: the frame's address becomes "/", and fetch, XMLHttpRequest and
// EventSource for /api/v1 go to the firmware module in the page around it
// (window.parent.zoneflowDemo, see demo.js) instead of the network.
(() => {
  const host = window.parent !== window ? window.parent.zoneflowDemo : null;
  if (!host) {
    document.addEventListener('DOMContentLoaded', () => {
      document.body.innerHTML =
        '<p style="font:16px system-ui;padding:24px">This is the web interface of the ZoneFlow demo. ' +
        'It needs the simulator around it: <a href="./">open the demo</a>.</p>';
    });
    window.__zoneflowNoHost = true;
    return;
  }

  // Asset URLs in app.html are relative to this directory; keep them so.
  const base = document.createElement('base');
  base.href = new URL('.', location.href).href;
  document.head.append(base);
  history.replaceState(null, '', '/');

  const isApi = (url) => url.origin === location.origin && url.pathname.startsWith('/api/v1');
  const report = () => host.navigated(location.pathname);
  for (const name of ['pushState', 'replaceState']) {
    const original = history[name].bind(history);
    history[name] = (...args) => {
      original(...args);
      report();
    };
  }
  window.addEventListener('popstate', report);

  // Links are for the interface's own router. Opening one in a new tab would
  // ask the documentation server for the controller's page.
  const block = (e) => {
    const a = e.target instanceof Element ? e.target.closest('a[href^="/"]') : null;
    if (a && (e.button === 1 || e.ctrlKey || e.metaKey || e.shiftKey)) e.preventDefault();
  };
  window.addEventListener('auxclick', block, true);
  window.addEventListener('click', block, true);

  // ---- fetch ----
  const realFetch = window.fetch.bind(window);
  window.fetch = async (input, init = {}) => {
    const request = input instanceof Request ? input : null;
    const url = new URL(request ? request.url : String(input), location.href);
    if (!isApi(url)) return realFetch(input, init);
    const method = (init.method ?? request?.method ?? 'GET').toUpperCase();
    const headers = {};
    new Headers(init.headers ?? request?.headers ?? {}).forEach((value, name) => (headers[name.toLowerCase()] = value));
    const signal = init.signal ?? request?.signal;
    if (signal?.aborted) throw new DOMException('Aborted', 'AbortError');
    // The one streaming read: the interface asks why its stream failed.
    if (url.pathname === '/api/v1/events') {
      return new Promise((resolve, reject) => {
        const close = host.openStream({
          open: () => {
            close();
            resolve(new Response('', { status: 200, headers: { 'Content-Type': 'text/event-stream' } }));
          },
          data: () => {},
          fail: (permanent) => (permanent ? resolve(new Response('{}', { status: 503 })) : reject(new TypeError('Failed to fetch'))),
        });
      });
    }
    let body = init.body ?? '';
    const binary = typeof body !== 'string';
    if (binary) body = '';
    const res = await host.request({ method, url: url.pathname + url.search, headers, body, binary });
    if (signal?.aborted) throw new DOMException('Aborted', 'AbortError');
    const empty = res.status === 204 || res.status === 304;
    return new Response(empty ? null : res.body, { status: res.status, headers: res.headers });
  };

  // ---- XMLHttpRequest: only the two upload routes use it ----
  const RealXhr = window.XMLHttpRequest;
  window.XMLHttpRequest = class extends RealXhr {
    #api = null;
    #headers = {};
    #response = null;
    open(method, url, ...rest) {
      const target = new URL(String(url), location.href);
      this.#api = isApi(target) ? { method: String(method).toUpperCase(), url: target.pathname + target.search } : null;
      if (!this.#api) super.open(method, url, ...rest);
    }
    setRequestHeader(name, value) {
      if (this.#api) this.#headers[name.toLowerCase()] = value;
      else super.setRequestHeader(name, value);
    }
    get status() {
      return this.#api ? (this.#response?.status ?? 0) : super.status;
    }
    get responseText() {
      return this.#api ? (this.#response?.body ?? '') : super.responseText;
    }
    getResponseHeader(name) {
      if (!this.#api) return super.getResponseHeader(name);
      const hit = Object.entries(this.#response?.headers ?? {}).find(([k]) => k.toLowerCase() === name.toLowerCase());
      return hit ? hit[1] : null;
    }
    send(body) {
      if (!this.#api) return super.send(body);
      host
        .request({ ...this.#api, headers: this.#headers, body: '', binary: true })
        .then((res) => {
          this.#response = res;
          this.onload?.(new ProgressEvent('load'));
        })
        .catch(() => this.onerror?.(new ProgressEvent('error')));
    }
  };

  // ---- EventSource ----
  class DemoEventSource extends EventTarget {
    static CONNECTING = 0;
    static OPEN = 1;
    static CLOSED = 2;
    CONNECTING = 0;
    OPEN = 1;
    CLOSED = 2;
    readyState = 0;
    onopen = null;
    onmessage = null;
    onerror = null;
    #close = null;
    #retry = 3000;
    #timer = 0;
    #buffer = '';
    constructor(url) {
      super();
      this.url = new URL(String(url), location.href).href;
      this.withCredentials = false;
      this.#connect();
    }
    #connect() {
      this.readyState = 0;
      this.#buffer = '';
      this.#close = host.openStream({
        open: () => {
          this.readyState = 1;
          this.#emit(new Event('open'), this.onopen);
        },
        data: (text) => this.#feed(text),
        fail: (permanent) => {
          if (this.readyState === 2) return;
          // As the browser does: a refused connection ends the source, a
          // dropped one is retried.
          this.readyState = permanent ? 2 : 0;
          this.#emit(new Event('error'), this.onerror);
          if (!permanent) this.#timer = setTimeout(() => this.#connect(), this.#retry);
        },
      });
    }
    #emit(event, handler) {
      handler?.call(this, event);
      this.dispatchEvent(event);
    }
    #feed(text) {
      this.#buffer += text;
      let end;
      while ((end = this.#buffer.indexOf('\n\n')) >= 0) {
        const block = this.#buffer.slice(0, end);
        this.#buffer = this.#buffer.slice(end + 2);
        let type = 'message';
        let id = '';
        const data = [];
        for (const line of block.split('\n')) {
          if (line.startsWith(':')) continue;
          const colon = line.indexOf(':');
          const field = colon < 0 ? line : line.slice(0, colon);
          const value = colon < 0 ? '' : line.slice(colon + 1).replace(/^ /, '');
          if (field === 'event') type = value;
          else if (field === 'data') data.push(value);
          else if (field === 'id') id = value;
          else if (field === 'retry' && /^\d+$/.test(value)) this.#retry = Number(value);
        }
        if (data.length === 0) continue;
        const event = new MessageEvent(type, { data: data.join('\n'), lastEventId: id, origin: location.origin });
        this.#emit(event, type === 'message' ? this.onmessage : null);
      }
    }
    close() {
      this.readyState = 2;
      clearTimeout(this.#timer);
      this.#close?.();
    }
  }
  window.EventSource = DemoEventSource;
})();
