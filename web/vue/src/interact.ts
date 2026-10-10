// The panel's interaction state: which action is waiting for the printer, and
// the one transient message row (docs/design/UNIFIED_UI.md 7; PRINCIPLES.md 8,
// 9). One per OpenamsPanel and provided to every widget, so a click anywhere
// resolves the same way and an action is never sent twice.
//
// A renderer decides nothing here: the runner it is given is the panel's, which
// asks the logic and emits the result.
import Vue from 'vue';
import type { ActionResult, Tone, View, ViewAction, ViewForm } from '@logic';

/** The form values `logic.action(line, form)` takes, by field id. */
export type FormValues = Record<string, string | number | boolean>;

/** How long an action may wait before the panel says the printer did not
 *  answer (principle 9: a stalled progress must say why). */
export const PENDING_MS = 8000;

/** A transient message's own lifetime: long enough to read, then gone. */
const FLASH_MS = 6000;

/** Touch has no hover, so a tap on a dimmed action says why instead
 *  (principle 10). */
export const isCoarsePointer = (): boolean =>
    typeof window !== 'undefined' && typeof window.matchMedia === 'function'
        && window.matchMedia('(pointer: coarse)').matches;

interface Pending {
    /** The timeout that ends the wait. */
    timer: number;
    /** The view the action was pressed in, as JSON: the wait ends when the
     *  printer's next view differs from it. */
    view: string;
}

/** The one dialog the panel may have open (principle 10: never nested). */
export interface DialogState {
    action: ViewAction;
    /** "form" for a form, "confirm" for the action's verbatim confirmation. */
    mode: 'form' | 'confirm';
    values: FormValues;
}

/** Which lead word a refusal is said with: the spool editor's own, or the
 *  general one. resolve() knows which call it handed the host. */
export type Refusal = 'edit' | 'action';

/** A form's opening values, taken from the core's own defaults. A value keeps
 *  the kind its field declares, so a select's value is still a number when it
 *  goes back to the logic. */
export function defaults(form: ViewForm): FormValues {
    const values: FormValues = {};
    for (const field of form.fields) values[field.id] = field.kind === 'toggle' ? !!field.value : field.value;
    return values;
}

/** What a form's own submit button says, and whether it stays dead until a
 *  field differs from the one the form opened with. Both are the core's, and
 *  both are read through this shape so a panel built against a core that has
 *  not got them yet still compiles and picks them up unchanged when it does. */
export interface FormExtras {
    submit_label?: string;
    require_change?: boolean;
}

/** The two fields, or nothing at all: a core that writes neither leaves the
 *  button as it was, on the action's own label and always live. */
export function formExtras(form: ViewForm | null | undefined): FormExtras {
    return (form ?? {}) as ViewForm & FormExtras;
}

/** True once a field holds something other than what it opened with. */
export function changed(form: ViewForm, values: FormValues): boolean {
    const start = defaults(form);
    return Object.keys(start).some((id) => values[id] !== start[id]);
}

/** Resolves an action line; the panel's own, so the logic stays the only
 *  thing that knows how a line becomes a request. */
export type Runner = (line: string, form: FormValues | null) => ActionResult | null;

/** The provide/inject key: one Interactivity per OpenamsPanel, so every widget
 *  below it resolves an action the same way. */
export const INTERACT = 'oamsInteract';

export class Interactivity {
    /** The last view the panel drew. Held outside the reactive state: a view is
     *  a large tree, and Vue must not walk it. */
    private current: View | null = null;
    private flashTimer = 0;
    /** What the host's answer to a call releases, by the line that asked for
     *  it: `load <slot>` after a spool edit that ends in "then load". Held
     *  outside the reactive state for the same reason as the view: nothing ever
     *  draws it. */
    private followups: Record<string, string> = {};

    private readonly state = new Vue({
        data: {
            pending: {} as Record<string, Pending>,
            message: '',
            tone: 'neutral' as Tone,
            dialog: null as DialogState | null,
        },
    });

    constructor(private readonly runner: Runner) {}

    get pending(): Record<string, Pending> {
        return this.state.pending;
    }

    /** The one open dialog, or null. */
    get dialog(): DialogState | null {
        return this.state.dialog;
    }

    /** The transient message, or "" when there is none. */
    get message(): string {
        return this.state.message;
    }

    /** How to draw it: red for an error, plain for a reason. */
    get tone(): Tone {
        return this.state.tone;
    }

    get anythingPending(): boolean {
        return Object.keys(this.state.pending).length > 0;
    }

    /** True while `action` waits: a pending action cannot be sent again, so a
     *  double click is one load and not two (principle 9). */
    isPending(action: ViewAction): boolean {
        return Object.prototype.hasOwnProperty.call(this.state.pending, action.line);
    }

    /** Every view the panel draws goes through here, so a wait ends as soon as
     *  the printer's state has moved on. */
    setView(view: View | null): void {
        this.current = view;
        if (!this.anythingPending) return;
        const json = view ? JSON.stringify(view) : '';
        for (const line of Object.keys(this.state.pending)) {
            if (this.state.pending[line].view !== json) this.release(line);
        }
    }

    /** What a click on an action does: nothing for a dimmed one, a dialog for
     *  one that asks first, and the action itself otherwise. A dialog is where
     *  the task happens (principle 11), so it opens before anything is sent. */
    ask(action: ViewAction): void {
        if (!action.enabled) {
            this.run(action);
            return;
        }
        if (this.isPending(action) || this.state.dialog) return;
        if (action.confirm) {
            this.state.dialog = { action, mode: 'confirm', values: {} };
            return;
        }
        if (action.form) {
            this.state.dialog = { action, mode: 'form', values: defaults(action.form) };
            return;
        }
        this.run(action);
    }

    /** A form field's new value. */
    setValue(id: string, value: string | number | boolean): void {
        const dialog = this.state.dialog;
        if (!dialog) return;
        this.state.$set(dialog.values, id, value);
    }

    /** The dialog's own button: resolve the action with the form's values. */
    submit(): void {
        const dialog = this.state.dialog;
        if (!dialog) return;
        this.state.dialog = null;
        this.run(dialog.action, dialog.mode === 'form' ? dialog.values : null);
    }

    /** Cancel, or Escape: nothing is sent. */
    cancel(): void {
        this.state.dialog = null;
    }

    /** Resolve an action, once the user has committed to it. A dimmed action
     *  does nothing; on a touchscreen it says why. */
    run(action: ViewAction, form: FormValues | null = null): void {
        if (!action.enabled) {
            if (isCoarsePointer()) this.flash(action.reason);
            return;
        }
        if (this.isPending(action)) return;
        const result = this.runner(action.line, form);
        // "local" is a display-only line: nothing was sent, so there is nothing
        // to press and nothing to wait for.
        if (!result || result.kind === 'local') return;
        if (result.kind === 'error') {
            this.flash(result.reason, 'error');
            return;
        }
        // A call that names what follows it: nothing is sent until the host
        // says it took the call.
        if (result.kind === 'rpc' && result.then) this.followups[action.line] = result.then;
        this.press(action);
    }

    /** A call the host waited on is over: the action stops waiting, and a
     *  refusal (the printer's own words) is said in the message row, after
     *  the core's lead - "Could not save the spool:" for the spool editor,
     *  "Could not do that:" for anything else (H3).
     *
     *  A refusal that arrives after the wait has already ended is the outcome
     *  of a run that had already started - a load's script is answered only
     *  once it has run, which can be minutes. The printer's own status says
     *  it then, so the panel says nothing; the ESP32 follows the same rule. The
     *  follow-up is not one of those: the host did answer, in words, so what it
     *  asked to follow goes out either way. */
    settle(line: string, message?: string, kind: Refusal = 'edit'): void {
        // A host that answers at once does so before the press is recorded (run()
        // calls the runner first), so the settling waits for the end of the click.
        queueMicrotask(() => {
            const waited = this.pending[line] !== undefined;
            this.release(line);
            const then = this.followups[line];
            delete this.followups[line];
            // The host took the call, so the load it asked for follows now,
            // through the same path any action line takes: the job the user came
            // for happens instead of being handed back to them (principle 13).
            if (then && !message) this.send(then);
            if (!message || !waited) return;
            const labels = this.current?.labels;
            const lead = kind === 'action' ? labels?.action_refused : labels?.edit_failed;
            this.flash(lead ? `${lead} ${message}` : message, 'error');
        });
    }

    /** One line on its own, with no action of its own behind it: a follow-up the
     *  host's answer released. The panel's own runner, so it resolves and is
     *  emitted exactly as any other line is. */
    private send(line: string): void {
        const result = this.runner(line, null);

        if (!result || result.kind === 'local') return;
        if (result.kind === 'error') this.flash(result.reason, 'error');
    }

    /** Say something for a while: a core's reason, a dimmed action's, or the
     *  panel's own. It goes in the panel's one message row, so nothing moves. */
    flash(text: string, tone: Tone = 'neutral'): void {
        window.clearTimeout(this.flashTimer);
        this.state.message = text;
        this.state.tone = tone;
        if (!text) return;
        this.flashTimer = window.setTimeout(() => {
            this.state.message = '';
        }, FLASH_MS);
    }

    /** The pressed state, at once, and the wait behind it. */
    private press(action: ViewAction): void {
        const record: Pending = {
            timer: 0,
            view: this.current ? JSON.stringify(this.current) : '',
        };
        record.timer = window.setTimeout(() => this.timeOut(action), PENDING_MS);
        this.state.$set(this.state.pending, action.line, record);
    }

    /** The wait ended: the printer's view moved on, so stop waiting. */
    private release(line: string): void {
        const record = this.state.pending[line];
        if (!record) return;
        window.clearTimeout(record.timer);
        this.state.$delete(this.state.pending, line);
    }

    /** Nothing answered in time: say so rather than leaving a spinner of
     *  patience on screen. */
    private timeOut(action: ViewAction): void {
        this.release(action.line);
        // The text is the core's, like every other string on the screen (labels.no_response).
        this.flash(this.current?.labels.no_response ?? '', 'error');
    }

    /** The panel is going away: nothing may fire after it. */
    dispose(): void {
        for (const line of Object.keys(this.state.pending)) this.release(line);
        this.followups = {};
        window.clearTimeout(this.flashTimer);
        this.state.message = '';
        this.state.dialog = null;
    }
}
