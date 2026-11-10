// Typed client for the controller's REST API. Request and response types
// come from schema.d.ts, generated from docs/firmware/openapi.json.
//
// Session handling follows docs/firmware/web-ui-contract.md: the session
// cookie is HttpOnly and handled by the browser; the CSRF token is kept in
// memory only and sent as X-CSRF-Token on every write.
import type { components, paths } from './schema';

export type Schemas = components['schemas'];
export type Status = Schemas['Status'];
export type Zone = Schemas['Zone'];
export type Program = Schemas['Program'];
export type ProgramInput = Schemas['ProgramInput'];
export type Config = Schemas['Config'];
export type Network = Schemas['Network'];
export type OtaStatus = Schemas['OtaStatus'];
export type AuthStatus = Schemas['AuthStatus'];
export type FaultCode = NonNullable<Schemas['Faults']['active']>[number];

type Method = 'get' | 'put' | 'post' | 'delete' | 'patch';
type Json<C> = C extends { content: { 'application/json': infer T } } ? T : unknown;
type Success<R> = R extends { 200: infer A }
  ? Json<A>
  : R extends { 201: infer B }
    ? Json<B>
    : R extends { 202: infer C }
      ? Json<C>
      : unknown;
type Op<P extends keyof paths, M extends Method> = NonNullable<paths[P][M]>;
export type Response<P extends keyof paths, M extends Method> =
  Op<P, M> extends { responses: infer R } ? Success<R> : never;
export type Body<P extends keyof paths, M extends Method> =
  Op<P, M> extends { requestBody?: { content: { 'application/json': infer B } } } ? B : never;
type PathParams<P extends keyof paths, M extends Method> =
  Op<P, M> extends { parameters: { path: infer X } } ? X : never;
type Query<P extends keyof paths, M extends Method> =
  Op<P, M> extends { parameters: { query?: infer Q } } ? Q : never;

export interface Options<P extends keyof paths, M extends Method> {
  params?: PathParams<P, M>;
  query?: Query<P, M>;
  body?: Body<P, M>;
  signal?: AbortSignal;
}

export const BASE = '/api/v1';

/** An error answer of the API (`{"error": {code, message, path}}`) or a network failure. */
export class ApiError extends Error {
  readonly status: number;
  readonly code: string;
  readonly path: string | undefined;
  readonly retryAfter: number | undefined;
  constructor(status: number, code: string, message: string, path?: string, retryAfter?: number) {
    super(message);
    this.status = status;
    this.code = code;
    this.path = path;
    this.retryAfter = retryAfter;
  }
}

let csrfToken: string | null = null;
let onUnauthorized: (() => void) | null = null;

export function setCsrf(token: string | null | undefined): void {
  csrfToken = token ?? null;
}
export function hasCsrf(): boolean {
  return csrfToken !== null;
}
/** Called on every 401, so the app can return to the auth check. */
export function setUnauthorizedHandler(fn: (() => void) | null): void {
  onUnauthorized = fn;
}

export function buildUrl(path: string, params?: object, query?: object): string {
  let url = path.replace(/\{(\w+)\}/g, (_, k: string) => {
    const v = (params as Record<string, unknown> | undefined)?.[k];
    if (v === undefined) throw new Error(`missing path parameter ${k}`);
    return encodeURIComponent(String(v));
  });
  const q = Object.entries(query ?? {}).filter(([, v]) => v !== undefined);
  if (q.length) url += '?' + q.map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(String(v))}`).join('&');
  return BASE + url;
}

function writeHeaders(method: string, contentType?: string): Record<string, string> {
  const h: Record<string, string> = { Accept: 'application/json' };
  if (contentType) h['Content-Type'] = contentType;
  if (method !== 'GET' && csrfToken) h['X-CSRF-Token'] = csrfToken;
  return h;
}

export async function toApiError(res: { status: number; headers: { get(n: string): string | null } }, text: string): Promise<ApiError> {
  let code = `http_${res.status}`;
  let message = text || `HTTP ${res.status}`;
  let path: string | undefined;
  try {
    const j = JSON.parse(text) as { error?: { code?: string; message?: string; path?: string } };
    if (j.error) {
      code = j.error.code ?? code;
      message = j.error.message ?? message;
      path = j.error.path;
    }
  } catch {
    // not JSON (for example the device's plain-text 413)
  }
  const ra = Number(res.headers.get('Retry-After'));
  return new ApiError(res.status, code, message, path, Number.isFinite(ra) && ra > 0 ? ra : undefined);
}

/** One typed API call. Throws ApiError on any non-2xx answer or network failure. */
export async function call<P extends keyof paths, M extends Method>(
  method: M,
  path: P,
  opts: Options<P, M> = {},
): Promise<Response<P, M>> {
  const m = method.toUpperCase();
  const hasBody = opts.body !== undefined || (m !== 'GET' && m !== 'DELETE');
  let res: globalThis.Response;
  try {
    res = await fetch(buildUrl(path as string, opts.params as object, opts.query as object), {
      method: m,
      headers: writeHeaders(m, hasBody ? 'application/json' : undefined),
      body: hasBody ? JSON.stringify(opts.body ?? {}) : undefined,
      credentials: 'same-origin',
      cache: 'no-store',
      signal: opts.signal ?? null,
    });
  } catch (e) {
    if ((e as Error).name === 'AbortError') throw e;
    throw new ApiError(0, 'network', (e as Error).message);
  }
  const text = await res.text();
  if (!res.ok) {
    const err = await toApiError(res, text);
    if (res.status === 401 && path !== '/auth/login' && path !== '/auth/setup' && path !== '/auth/password') {
      onUnauthorized?.();
    }
    throw err;
  }
  return (text ? JSON.parse(text) : {}) as Response<P, M>;
}

/**
 * Streams a binary file (firmware or web UI image) with upload progress.
 * fetch() has no upload progress, so this uses XMLHttpRequest.
 */
export function upload(
  path: '/firmware/upload' | '/firmware/assets',
  file: Blob,
  opts: { query?: Record<string, string>; onProgress?: (sent: number, total: number) => void } = {},
): Promise<Schemas['Accepted']> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', buildUrl(path, undefined, opts.query));
    for (const [k, v] of Object.entries(writeHeaders('POST', 'application/octet-stream'))) xhr.setRequestHeader(k, v);
    xhr.upload.onprogress = (e) => opts.onProgress?.(e.loaded, e.lengthComputable ? e.total : file.size);
    xhr.onerror = () => reject(new ApiError(0, 'network', 'upload failed'));
    xhr.onload = async () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve(xhr.responseText ? JSON.parse(xhr.responseText) : {});
      } else {
        if (xhr.status === 401) onUnauthorized?.();
        reject(await toApiError({ status: xhr.status, headers: { get: (n) => xhr.getResponseHeader(n) } }, xhr.responseText));
      }
    };
    xhr.send(file);
  });
}

/** POST /config/restore takes a whole backup document, which the schema leaves untyped. */
export function restoreBackup(doc: unknown) {
  return call('post', '/config/restore', { body: doc as Body<'/config/restore', 'post'> });
}
