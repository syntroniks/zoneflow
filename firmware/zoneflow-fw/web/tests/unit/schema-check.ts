// The JSON Schema subset docs/firmware/openapi.json uses ($ref, allOf,
// oneOf, type, enum, const, properties, required, items); the same rules as
// the firmware's host test web_openapi_schema_test.cpp.
import { readFileSync } from 'node:fs';

type Schema = Record<string, unknown>;
export const openapi = JSON.parse(readFileSync(new URL('../../../../../docs/firmware/openapi.json', import.meta.url), 'utf8')) as {
  paths: Record<string, Record<string, { responses: Record<string, { content?: Record<string, { schema: Schema }> }> }>>;
  components: { schemas: Record<string, Schema> };
};

function resolve(s: Schema): Schema {
  while (typeof s['$ref'] === 'string') s = openapi.components.schemas[(s['$ref'] as string).split('/').pop()!]!;
  return s;
}

function typeOk(t: string, v: unknown): boolean {
  switch (t) {
    case 'object':
      return typeof v === 'object' && v !== null && !Array.isArray(v);
    case 'array':
      return Array.isArray(v);
    case 'integer':
      return Number.isInteger(v);
    case 'null':
      return v === null;
    default:
      return typeof v === t;
  }
}

export function check(schema: Schema, v: unknown, path = '$'): string[] {
  const s = resolve(schema);
  const errors: string[] = [];
  for (const sub of (s['allOf'] as Schema[] | undefined) ?? []) errors.push(...check(sub, v, path));
  if (s['oneOf']) {
    const results = (s['oneOf'] as Schema[]).map((sub) => check(sub, v, path));
    const ok = results.filter((r) => r.length === 0).length;
    if (ok !== 1) errors.push(`${path}: matches ${ok} of oneOf (${JSON.stringify(v).slice(0, 80)})`);
  }
  if (s['type']) {
    const types = Array.isArray(s['type']) ? (s['type'] as string[]) : [s['type'] as string];
    if (!types.some((t) => typeOk(t, v))) return [...errors, `${path}: wrong type ${JSON.stringify(v)}`];
  }
  if ('const' in s && s['const'] !== v) errors.push(`${path}: not ${JSON.stringify(s['const'])}`);
  if (s['enum'] && v !== null && !(s['enum'] as unknown[]).includes(v)) errors.push(`${path}: ${JSON.stringify(v)} not in enum`);
  if (typeof v === 'object' && v !== null && !Array.isArray(v)) {
    const o = v as Record<string, unknown>;
    for (const k of (s['required'] as string[] | undefined) ?? []) if (!(k in o)) errors.push(`${path}.${k}: required but missing`);
    for (const [k, sub] of Object.entries((s['properties'] as Record<string, Schema> | undefined) ?? {}))
      if (k in o) errors.push(...check(sub, o[k], `${path}.${k}`));
  }
  if (Array.isArray(v) && s['items']) v.forEach((x, i) => errors.push(...check(s['items'] as Schema, x, `${path}[${i}]`)));
  return errors;
}

export function responseSchema(route: string, method = 'get', status = '200'): Schema {
  const s = openapi.paths[route]?.[method]?.responses[status]?.content?.['application/json']?.schema;
  if (!s) throw new Error(`no schema for ${method} ${route} ${status}`);
  return s;
}
