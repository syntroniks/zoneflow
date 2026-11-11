// Shared building blocks. Native elements first (button, input, dialog,
// radio groups), so keyboard use and screen readers work without extra code.
import type { ComponentChildren, JSX } from 'preact';
import { useEffect, useId, useRef, useState } from 'preact/hooks';
import { signal } from '@preact/signals';
import { t } from '../i18n/index.ts';
import { onLinkClick } from '../router.ts';
import { Icon, type IconName } from './icons.tsx';

type BtnVariant = 'primary' | 'secondary' | 'danger' | 'ghost' | 'tonal';

export function Button({
  variant = 'secondary',
  icon,
  loading,
  block,
  size,
  children,
  class: cls,
  ...rest
}: {
  variant?: BtnVariant;
  icon?: IconName;
  loading?: boolean;
  block?: boolean;
  size?: 'sm' | 'lg';
} & JSX.ButtonHTMLAttributes<HTMLButtonElement>) {
  const c = ['btn', `btn-${variant}`, block && 'btn-block', size && `btn-${size}`, cls].filter(Boolean).join(' ');
  return (
    <button type="button" class={c} aria-busy={loading || undefined} {...rest} disabled={rest.disabled || loading}>
      {icon && <Icon name={icon} size={size === 'sm' ? 18 : 20} />}
      {children && <span>{children}</span>}
    </button>
  );
}

export function LinkButton({ href, variant = 'secondary', icon, children, block }: { href: string; variant?: BtnVariant; icon?: IconName; children: ComponentChildren; block?: boolean }) {
  return (
    <a href={href} onClick={onLinkClick} class={['btn', `btn-${variant}`, block && 'btn-block'].filter(Boolean).join(' ')}>
      {icon && <Icon name={icon} size={20} />}
      <span>{children}</span>
    </a>
  );
}

export function Card({ title, action, children, class: cls, id }: { title?: ComponentChildren; action?: ComponentChildren; children: ComponentChildren; class?: string; id?: string }) {
  const hid = useId();
  return (
    <section class={cls ? `card ${cls}` : 'card'} aria-labelledby={title ? hid : undefined} id={id}>
      {(title || action) && (
        <header class="card-head">
          {title && <h2 id={hid}>{title}</h2>}
          {action}
        </header>
      )}
      {children}
    </section>
  );
}

export function PageTitle({ title, back, children }: { title: string; back?: string; children?: ComponentChildren }) {
  return (
    <div class="page-title">
      {back && (
        <a href={back} onClick={onLinkClick} class="icon-btn" aria-label={t('back')}>
          <Icon name="back" />
        </a>
      )}
      <h1 tabIndex={-1}>{title}</h1>
      {children && <div class="page-title-actions">{children}</div>}
    </div>
  );
}

export function Field({ label, help, error, children, id, class: cls }: { label: ComponentChildren; help?: ComponentChildren; error?: string | null; children: (ids: { id: string; describedBy: string | undefined; invalid: boolean }) => ComponentChildren; id?: string; class?: string }) {
  const gen = useId();
  const fid = id ?? gen;
  const helpId = help ? `${fid}-help` : '';
  const errId = error ? `${fid}-err` : '';
  const describedBy = [errId, helpId].filter(Boolean).join(' ') || undefined;
  return (
    <div class={['field', error && 'field-invalid', cls].filter(Boolean).join(' ')}>
      <label for={fid}>{label}</label>
      {children({ id: fid, describedBy, invalid: !!error })}
      {error && (
        <p class="field-error" id={errId}>
          <Icon name="alert" size={16} /> {error}
        </p>
      )}
      {help && (
        <p class="field-help" id={helpId}>
          {help}
        </p>
      )}
    </div>
  );
}

export function TextField({ label, help, error, value, onInput, ...rest }: { label: string; help?: string; error?: string | null; value: string; onInput: (v: string) => void } & Omit<JSX.InputHTMLAttributes<HTMLInputElement>, 'value' | 'onInput' | 'label'>) {
  return (
    <Field label={label} help={help} error={error} id={rest.id as string | undefined}>
      {({ id, describedBy, invalid }) => (
        <input
          class="input"
          {...rest}
          id={id}
          value={value}
          aria-describedby={describedBy}
          aria-invalid={invalid || undefined}
          onInput={(e) => onInput((e.currentTarget as HTMLInputElement).value)}
        />
      )}
    </Field>
  );
}

export function PasswordField(props: { label: string; help?: string; error?: string | null; value: string; onInput: (v: string) => void; autocomplete?: string; autoFocus?: boolean; name?: string }) {
  const [show, setShow] = useState(false);
  return (
    <Field label={props.label} help={props.help} error={props.error}>
      {({ id, describedBy, invalid }) => (
        <div class="input-group">
          <input
            class="input"
            id={id}
            name={props.name}
            type={show ? 'text' : 'password'}
            value={props.value}
            autocomplete={props.autocomplete}
            autoFocus={props.autoFocus}
            spellcheck={false}
            autocapitalize="off"
            aria-describedby={describedBy}
            aria-invalid={invalid || undefined}
            onInput={(e) => props.onInput((e.currentTarget as HTMLInputElement).value)}
          />
          <button type="button" class="icon-btn" aria-label={show ? t('hide_password') : t('show_password')} aria-pressed={show} onClick={() => setShow(!show)}>
            <Icon name={show ? 'eyeOff' : 'eye'} size={20} />
          </button>
        </div>
      )}
    </Field>
  );
}

/** On/off setting. A labelled switch row; the whole row is the hit target. */
export function Switch({ label, help, checked, onChange, disabled }: { label: ComponentChildren; help?: ComponentChildren; checked: boolean; onChange: (v: boolean) => void; disabled?: boolean }) {
  const id = useId();
  return (
    <div class="switch-row">
      <label for={id} class="switch-label">
        <span>{label}</span>
        {help && (
          <small class="field-help" id={`${id}-h`}>
            {help}
          </small>
        )}
      </label>
      <input
        id={id}
        type="checkbox"
        role="switch"
        class="switch"
        checked={checked}
        disabled={disabled}
        aria-describedby={help ? `${id}-h` : undefined}
        onChange={(e) => onChange((e.currentTarget as HTMLInputElement).checked)}
      />
    </div>
  );
}

/** A choice of a few options as radio buttons styled as segments or chips. */
export function Choice<T extends string | number>({ label, help, options, value, onChange, kind = 'segmented', hideLabel }: { label: string; help?: string; options: { value: T; label: ComponentChildren; disabled?: boolean }[]; value: T | null; onChange: (v: T) => void; kind?: 'segmented' | 'chips'; hideLabel?: boolean }) {
  const name = useId();
  return (
    <fieldset class={`choice choice-${kind}`} aria-describedby={help ? `${name}-h` : undefined}>
      <legend class={hideLabel ? 'sr-only' : undefined}>{label}</legend>
      {help && (
        <small class="field-help" id={`${name}-h`}>
          {help}
        </small>
      )}
      <div class="choice-items">
        {options.map((o) => (
          <label key={String(o.value)} class={o.value === value ? 'is-checked' : undefined}>
            <input type="radio" name={name} checked={o.value === value} disabled={o.disabled} onChange={() => onChange(o.value)} />
            <span>{o.label}</span>
          </label>
        ))}
      </div>
    </fieldset>
  );
}

/** Number entry with large − / + buttons, for gloved or wet fingers. */
export function Stepper({ label, value, onChange, min, max, step = 1, unit, help, error, hideLabel, id: idIn }: { label: string; value: number; onChange: (v: number) => void; min: number; max: number; step?: number; unit?: string; help?: string; error?: string | null; hideLabel?: boolean; id?: string }) {
  const clamp = (v: number) => Math.min(max, Math.max(min, v));
  const [text, setText] = useState(String(value));
  useEffect(() => setText(String(value)), [value]);
  return (
    <Field label={<span class={hideLabel ? 'sr-only' : undefined}>{label}</span>} help={help} error={error} class="stepper-field" id={idIn}>
      {({ id, describedBy, invalid }) => (
        <div class="stepper">
          <button type="button" class="icon-btn stepper-btn" aria-label={t('decrease', { what: label })} disabled={value <= min} onClick={() => onChange(clamp(value - step))}>
            <Icon name="minus" />
          </button>
          <span class="stepper-value">
            <input
              id={id}
              class="input"
              inputMode="numeric"
              pattern="[0-9]*"
              value={text}
              aria-describedby={describedBy}
              aria-invalid={invalid || undefined}
              onInput={(e) => {
                const v = (e.currentTarget as HTMLInputElement).value;
                setText(v);
                const n = Number(v);
                if (v !== '' && Number.isFinite(n)) onChange(clamp(Math.round(n)));
              }}
              onBlur={() => setText(String(value))}
            />
            {unit && <span class="stepper-unit" aria-hidden="true">{unit}</span>}
          </span>
          <button type="button" class="icon-btn stepper-btn" aria-label={t('increase', { what: label })} disabled={value >= max} onClick={() => onChange(clamp(value + step))}>
            <Icon name="plus" />
          </button>
        </div>
      )}
    </Field>
  );
}

export function Banner({ severity, title, children, action, icon }: { severity: 'error' | 'warning' | 'info' | 'success'; title: ComponentChildren; children?: ComponentChildren; action?: ComponentChildren; icon?: IconName }) {
  const ic: IconName = icon ?? (severity === 'info' ? 'info' : severity === 'success' ? 'check' : 'alert');
  return (
    <div class={`banner banner-${severity}`} role={severity === 'error' ? 'alert' : undefined}>
      <Icon name={ic} class="banner-icon" />
      <div class="banner-body">
        <p class="banner-title">{title}</p>
        {children && <div class="banner-detail">{children}</div>}
        {action && <div class="banner-action">{action}</div>}
      </div>
    </div>
  );
}

export function Progress({ value, max, label }: { value: number; max: number; label: string }) {
  const pct = max > 0 ? Math.min(100, Math.max(0, (value / max) * 100)) : 0;
  return (
    <div class="progress" role="progressbar" aria-label={label} aria-valuemin={0} aria-valuemax={100} aria-valuenow={Math.round(pct)}>
      <div class="progress-bar" style={{ width: `${pct}%` }} />
    </div>
  );
}

export function Row({ href, children, trailing, onClick }: { href?: string; children: ComponentChildren; trailing?: ComponentChildren; onClick?: () => void }) {
  const inner = (
    <>
      <span class="row-main">{children}</span>
      {trailing}
      {(href || onClick) && <Icon name="chevron" class="row-chevron" size={20} />}
    </>
  );
  if (href)
    return (
      <a class="row" href={href} onClick={onLinkClick}>
        {inner}
      </a>
    );
  if (onClick)
    return (
      <button type="button" class="row" onClick={onClick}>
        {inner}
      </button>
    );
  return <div class="row">{inner}</div>;
}

export function KeyValue({ items }: { items: [ComponentChildren, ComponentChildren][] }) {
  return (
    <dl class="kv">
      {items.map(([k, v], i) => (
        <div key={i}>
          <dt>{k}</dt>
          <dd>{v}</dd>
        </div>
      ))}
    </dl>
  );
}

/** Modal sheet on a native <dialog> (focus trap, Escape and inert page for free). */
export function Sheet({ open, onClose, title, children, footer }: { open: boolean; onClose: () => void; title: string; children: ComponentChildren; footer?: ComponentChildren }) {
  const ref = useRef<HTMLDialogElement>(null);
  const hid = useId();
  useEffect(() => {
    const d = ref.current;
    if (!d) return;
    if (open && !d.open) d.showModal();
    if (!open && d.open) d.close();
  }, [open]);
  return (
    <dialog
      ref={ref}
      class="sheet"
      aria-labelledby={hid}
      onClose={onClose}
      onCancel={(e) => {
        e.preventDefault();
        onClose();
      }}
      onClick={(e) => {
        if (e.target === ref.current) onClose(); // backdrop
      }}
    >
      {open && (
        <div class="sheet-inner">
          <header class="sheet-head">
            <h2 id={hid}>{title}</h2>
            <button type="button" class="icon-btn" aria-label={t('close')} onClick={onClose}>
              <Icon name="close" />
            </button>
          </header>
          <div class="sheet-body">{children}</div>
          {footer && <footer class="sheet-foot">{footer}</footer>}
        </div>
      )}
    </dialog>
  );
}

// ---- confirmation dialog -----------------------------------------------------

interface ConfirmRequest {
  title: string;
  body?: ComponentChildren;
  confirmLabel: string;
  danger?: boolean;
  /** The user must type this word to enable the button. */
  typed?: string;
  resolve: (ok: boolean) => void;
}
const confirmReq = signal<ConfirmRequest | null>(null);

/** Asks before a destructive action. Resolves true on confirm. */
export function confirmAction(o: Omit<ConfirmRequest, 'resolve'>): Promise<boolean> {
  return new Promise((resolve) => {
    confirmReq.value = { ...o, resolve };
  });
}

export function ConfirmHost() {
  const r = confirmReq.value;
  const [typed, setTyped] = useState('');
  useEffect(() => setTyped(''), [r]);
  const done = (ok: boolean) => {
    r?.resolve(ok);
    confirmReq.value = null;
  };
  const blocked = !!r?.typed && typed.trim().toUpperCase() !== r.typed;
  return (
    <Sheet
      open={!!r}
      onClose={() => done(false)}
      title={r?.title ?? ''}
      footer={
        <>
          <Button onClick={() => done(false)}>{t('cancel')}</Button>
          <Button variant={r?.danger ? 'danger' : 'primary'} disabled={blocked} onClick={() => done(true)}>
            {r?.confirmLabel}
          </Button>
        </>
      }
    >
      {typeof r?.body === 'string' ? <p>{r.body}</p> : r?.body}
      {r?.typed && <TextField label={t('type_to_confirm', { word: r.typed })} value={typed} onInput={setTyped} autoComplete="off" autocapitalize="characters" spellcheck={false} autoFocus />}
    </Sheet>
  );
}

export function Empty({ children, icon = 'info' }: { children: ComponentChildren; icon?: IconName }) {
  return (
    <div class="empty">
      <Icon name={icon} size={28} />
      <p>{children}</p>
    </div>
  );
}

export function Spinner({ label }: { label: string }) {
  return (
    <div class="spinner-wrap" role="status">
      <span class="spinner" aria-hidden="true" />
      <span>{label}</span>
    </div>
  );
}
