import { en, type StringKey } from './en.ts';

type Vars = Record<string, string | number>;

const table: Record<string, string> = en;

/** Looks up a string and fills {name} placeholders. */
export function t(key: StringKey, vars?: Vars): string {
  const s = table[key] ?? key;
  if (!vars) return s;
  return s.replace(/\{(\w+)\}/g, (m, k: string) => (k in vars ? String(vars[k]) : m));
}

type PluralBase<K> = K extends `${infer B}_one` ? B : never;

/** Plural lookup: key_one / key_other, with {n} set to the count. */
export function tn(base: PluralBase<StringKey>, n: number, vars?: Vars): string {
  const lang = typeof document === 'undefined' ? 'en' : document.documentElement.lang || 'en';
  const rule = new Intl.PluralRules(lang).select(n);
  const key = (rule === 'one' ? `${base}_one` : `${base}_other`) as StringKey;
  return t(key, { n, ...vars });
}

/** For dynamic keys from the API (fault codes, reasons); falls back to `fallback`. */
export function tk(key: string, vars?: Vars, fallback?: string): string {
  if (key in table) return t(key as StringKey, vars);
  return fallback ?? key;
}

export type { StringKey };
