import { afterEach, describe, expect, expectTypeOf, it, vi } from 'vitest';
import { ApiError, buildUrl, call, setCsrf, setUnauthorizedHandler, type Response } from '../../src/api/client.ts';
import { errorText, reasonText } from '../../src/state/errors.ts';

function stubFetch(status: number, body: unknown, headers: Record<string, string> = {}) {
  const fn = vi.fn(async (_url: string, _init: RequestInit) => new globalThis.Response(typeof body === 'string' ? body : JSON.stringify(body), { status, headers }));
  vi.stubGlobal('fetch', fn);
  return fn;
}

afterEach(() => {
  vi.unstubAllGlobals();
  setCsrf(null);
  setUnauthorizedHandler(null);
});

describe('api client', () => {
  it('types responses from the OpenAPI document', () => {
    expectTypeOf<Response<'/status', 'get'>>().toHaveProperty('zones');
    expectTypeOf<NonNullable<NonNullable<Response<'/status', 'get'>['zones']>[number]['state']>>().toEqualTypeOf<'off' | 'waiting' | 'open'>();
    expectTypeOf<Response<'/zones/{zone}/run', 'post'>>().toHaveProperty('confirmed');
  });

  it('fills path parameters and query', () => {
    expect(buildUrl('/zones/{zone}/run', { zone: 3 })).toBe('/api/v1/zones/3/run');
    expect(buildUrl('/firmware/upload', undefined, { force: 'true' })).toBe('/api/v1/firmware/upload?force=true');
    expect(() => buildUrl('/zones/{zone}', {})).toThrow();
  });

  it('sends JSON and the CSRF token on writes only', async () => {
    const f = stubFetch(202, { accepted: true, confirmed: true });
    setCsrf('tok');
    await call('post', '/zones/{zone}/run', { params: { zone: 2 }, body: { duration_s: 60 } });
    const [url, init] = f.mock.calls[0]!;
    expect(url).toBe('/api/v1/zones/2/run');
    const h = init.headers as Record<string, string>;
    expect(h['X-CSRF-Token']).toBe('tok');
    expect(h['Content-Type']).toBe('application/json');
    expect(init.body).toBe('{"duration_s":60}');
    f.mockClear();
    await call('get', '/status');
    expect((f.mock.calls[0]![1].headers as Record<string, string>)['X-CSRF-Token']).toBeUndefined();
  });

  it('sends {} as the body of empty commands (the device wants application/json)', async () => {
    const f = stubFetch(202, { accepted: true });
    await call('post', '/queue/stop');
    expect(f.mock.calls[0]![1].body).toBe('{}');
  });

  it('turns error answers into ApiError with code, path and Retry-After', async () => {
    stubFetch(409, { error: { code: 'zone_resting', message: 'resting' } }, { 'Retry-After': '600' });
    const e = await call('post', '/zones/{zone}/run', { params: { zone: 1 }, body: { duration_s: 5 } }).catch((x) => x);
    expect(e).toBeInstanceOf(ApiError);
    expect(e).toMatchObject({ status: 409, code: 'zone_resting', retryAfter: 600 });
    expect(errorText(e, { zone: 'Back lawn' })).toBe('Back lawn is resting after its maximum run time. Try again in 10 min.');
  });

  it('reports 401 to the app, except for the login itself', async () => {
    const h = vi.fn();
    setUnauthorizedHandler(h);
    stubFetch(401, { error: { code: 'unauthorized', message: 'x' } });
    await call('post', '/queue/stop').catch(() => {});
    expect(h).toHaveBeenCalledTimes(1);
    await call('post', '/auth/login', { body: { password: 'x' } }).catch(() => {});
    expect(h).toHaveBeenCalledTimes(1);
  });

  it('describes network failures and rate limits plainly', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => Promise.reject(new TypeError('Failed to fetch'))));
    const e = await call('get', '/status').catch((x) => x);
    expect(errorText(e)).toMatch(/did not answer/);
    expect(errorText(new ApiError(429, 'rate_limited', 'x', undefined, 4))).toBe('Too many wrong attempts. Try again in 4 s.');
    expect(reasonText('master_locked_out')).toMatch(/master valve/);
    expect(reasonText('something_new')).toBe('something new');
  });
});
