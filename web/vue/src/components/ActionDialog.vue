<template>
    <dialog
        ref="dlg"
        class="oams-dialog"
        :aria-labelledby="headId"
        data-dialog="action"
        @cancel="dismiss"
        @close="dismiss">
        <div v-if="dialog" class="dlg-body">
            <h4 :id="headId" class="dlg-head">{{ heading }}</h4>
            <p v-if="dialog.mode === 'confirm'" class="dlg-text">{{ text }}</p>
            <template v-else>
                <div v-for="field in shownFields" :key="field.id" class="dlg-entry" :data-entry="field.id">
                <div class="dlg-field">
                <label :id="labelId(field)" :for="fieldId(field)">{{ field.label }}<span v-if="field.unit" class="dlg-unit">{{ field.unit }}</span></label>
                <input
                    v-if="field.kind === 'toggle'"
                    :id="fieldId(field)"
                    type="checkbox"
                    :data-field="field.id"
                    :checked="checked(field)"
                    @change="onToggle(field, $event)" />
                <select-field
                    v-else-if="field.kind === 'select'"
                    :id="fieldId(field)"
                    :labelled-by="labelId(field)"
                    :field="field.id"
                    :options="field.options ?? []"
                    :value="value(field)"
                    :swatches="field.id === 'color'"
                    @input="onSelect(field, $event)" />
                <input
                    v-else-if="field.kind === 'text'"
                    :id="fieldId(field)"
                    type="text"
                    class="dlg-text-input"
                    :data-field="field.id"
                    :maxlength="field.max_len"
                    :aria-describedby="field.note ? noteId(field) : undefined"
                    autocomplete="off"
                    autocapitalize="characters"
                    spellcheck="false"
                    :value="value(field)"
                    @input="onText(field, $event)" />
                <input
                    v-else
                    :id="fieldId(field)"
                    type="number"
                    :data-field="field.id"
                    :min="field.min"
                    :max="field.max"
                    :step="field.step"
                    :placeholder="field.optional ? '\u2014' : undefined"
                    :aria-describedby="field.note ? noteId(field) : undefined"
                    :value="numberValue(field)"
                    @input="onNumber(field, $event)" />
                </div>
                <p v-if="field.note" :id="noteId(field)" class="dlg-note" :data-note="field.id">{{ field.note }}</p>
                </div>
            </template>
            <div class="dlg-actions">
                <button type="button" class="dlg-btn" data-role="cancel" @click="dismiss">{{ cancelLabel }}</button>
                <button type="button" class="dlg-btn primary" :data-action="dialog.action.id" :disabled="submitOff"
                    @click="ok">{{ submitLabel }}</button>
            </div>
        </div>
    </dialog>
</template>

<script lang="ts">
import { Component, Inject, Prop, Vue, Watch } from 'vue-property-decorator';
import type { ViewField, ViewLabels } from '@logic';
import { INTERACT, changed, formExtras, shown, type Interactivity } from '../interact';
import SelectField from './SelectField.vue';

let seq = 0;

/** The panel's one dialog (docs/design/UNIFIED_UI.md 7: a native <dialog> with
 *  showModal(), in the top layer, with focus trapping and Escape for free). A
 *  form is where the task happens (principle 11); a confirm is shown verbatim
 *  before the action resolves, and only for a drastic one (principle 7). */
@Component({ components: { SelectField } })
export default class ActionDialog extends Vue {
    @Prop({ default: null }) readonly labels!: ViewLabels | null;
    @Inject(INTERACT) readonly ctrl!: Interactivity;

    headId = `oams-dialog-head-${++seq}`;

    get dialog() {
        return this.ctrl.dialog;
    }

    get fields(): ViewField[] {
        return this.dialog?.action.form?.fields ?? [];
    }

    /** The fields that show with the values as they stand (show_when): the
     *  spool editor's typed material on "Other…", its temperatures on "Other…"
     *  or a material the host gives no temperature. */
    get shownFields(): ViewField[] {
        const d = this.dialog;
        const form = d?.action.form;
        if (!d || !form) return [];
        return form.fields.filter((f) => shown(form, f, d.values));
    }

    /** A confirm is shown exactly as the core wrote it. */
    get heading(): string {
        const d = this.dialog;
        if (!d) return '';
        return d.mode === 'confirm' ? d.action.confirm?.title ?? d.action.label : d.action.label;
    }

    get text(): string {
        return this.dialog?.action.confirm?.text ?? '';
    }

    /** Buttons are verbs: the action's own label, or the confirmation's, or the
     *  form's own when the core names one ("Save changes", not "Edit spool"
     *  again). */
    get submitLabel(): string {
        const d = this.dialog;
        if (!d) return '';
        if (d.mode === 'confirm') return d.action.confirm?.ok_label ?? d.action.label;
        return formExtras(d.action.form).submit_label || d.action.label;
    }

    /** A form that asks for a change is not worth sending without one: the
     *  button stays dead until a field differs from the value it opened with,
     *  and goes dead again when it is put back. A form the core does not ask
     *  that of is always live, as it was.
     *
     *  A field the core marks required has no answer at all yet - "Choose a
     *  material" on an unidentified bay - and there is nothing to send until it
     *  holds one, whatever `require_change` says. */
    get submitOff(): boolean {
        const d = this.dialog;
        const form = d?.mode === 'form' ? d.action.form : null;
        if (!form) return false;
        if (formExtras(form).require_change && !changed(form, d!.values)) return true;
        return form.fields.some((f) => f.required && shown(form, f, d!.values)
            && (f.kind === 'text' ? String(d!.values[f.id] ?? '').length === 0 : Number(d!.values[f.id]) < 0));
    }

    get cancelLabel(): string {
        return this.labels?.cancel ?? '';
    }

    fieldId(field: ViewField): string {
        return `${this.headId}-${field.id}`;
    }

    /** The label names the control, and the control's popup names itself from
     *  the same label, so a reader hears "Color" on both. */
    labelId(field: ViewField): string {
        return `${this.fieldId(field)}-label`;
    }

    noteId(field: ViewField): string {
        return `${this.fieldId(field)}-note`;
    }

    /** A number as the input wants it; an optional one left at 0 is empty. */
    numberValue(field: ViewField): string | number {
        const v = this.value(field);
        return field.optional && Number(v) === 0 ? '' : v;
    }

    /** A number or a select's option value, as the input wants it. */
    value(field: ViewField): string | number {
        const v = this.dialog?.values[field.id];
        return typeof v === 'boolean' ? Number(v) : v ?? field.value;
    }

    checked(field: ViewField): boolean {
        return !!this.dialog?.values[field.id];
    }

    set(field: ViewField, value: string | number | boolean): void {
        this.ctrl.setValue(field.id, value);
    }

    onToggle(field: ViewField, event: Event): void {
        this.set(field, (event.target as HTMLInputElement).checked);
    }

    /** A list's own choice: the option's value, as the field declares it. */
    onSelect(field: ViewField, option: number): void {
        this.set(field, Number(option));
    }

    onNumber(field: ViewField, event: Event): void {
        const text = (event.target as HTMLInputElement).value;
        // an optional number emptied again is "not given"
        this.set(field, field.optional && text.trim() === '' ? 0 : Number(text));
    }

    /** A typed material as the edit takes it: capitals, digits and "-+._/"
     *  only, at most max_len; anything else is dropped as it is typed, so what
     *  the field shows is what is sent. */
    onText(field: ViewField, event: Event): void {
        const input = event.target as HTMLInputElement;
        const clean = input.value.toUpperCase().replace(/[^A-Z0-9\-+._/]/g, '').slice(0, field.max_len ?? 64);
        if (clean !== input.value) input.value = clean;
        this.set(field, clean);
    }

    @Watch('dialog')
    onDialog(): void {
        this.$nextTick(() => {
            const el = this.el;
            if (!el) return;
            if (this.dialog && !el.open) el.showModal();
            else if (!this.dialog && el.open) el.close();
        });
    }

    ok(): void {
        this.ctrl.submit();
    }

    /** Escape, the Cancel button, or the backdrop: nothing is sent. */
    dismiss(): void {
        this.ctrl.cancel();
    }

    private get el(): HTMLDialogElement | null {
        return (this.$refs.dlg as HTMLDialogElement | undefined) ?? null;
    }
}
</script>

<style lang="scss" scoped>
@use '../tokens' as *;

.oams-dialog {
    width: min(400px, calc(100vw - 32px));
    margin: auto;
    padding: 0;
    border: 1px solid color-mix(in srgb, var(--oams-text, currentColor) 14%, var(--oams-pop-base, #14161a));
    border-radius: 16px;
    background: var(--oams-pop-base, #14161a);
    color: var(--oams-pop-ink, inherit);
    text-shadow: none;
    font-family: var(--oams-font-ui, inherit);
    font-size: var(--oams-fs-body);
    box-shadow: 0 18px 48px rgba(0, 0, 0, 0.4);

    &::backdrop {
        background: rgba(0, 0, 0, 0.45);
    }
}

.dlg-body {
    padding: 18px 18px 14px;
    display: flex;
    flex-direction: column;
    gap: 12px;
}

.dlg-head {
    font-size: var(--oams-fs-section);
    font-weight: var(--oams-fw-section);
    line-height: 1.3;
}

.dlg-text {
    line-height: 1.45;
}

.dlg-entry {
    display: flex;
    flex-direction: column;
    gap: 4px;
}

.dlg-note {
    margin: 0;
    font-size: var(--oams-fs-meta);
    line-height: 1.35;
    color: var(--oams-text-muted);
}

.dlg-field {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    min-height: 32px;

    label {
        display: flex;
        align-items: baseline;
        gap: 6px;
    }

    input[type='number'],
    input[type='text'] {
        width: 128px;
        min-height: 32px;
        padding: 4px 8px;
        border: 1px solid var(--oams-line);
        border-radius: 8px;
        background: var(--oams-surface-3);
        color: inherit;
        font: inherit;
    }

    input[type='checkbox'] {
        width: 20px;
        height: 20px;
        accent-color: var(--oams-accent);
    }
}

.dlg-unit {
    font-size: var(--oams-fs-meta);
    color: var(--oams-text-muted);
}

.dlg-actions {
    display: flex;
    justify-content: flex-end;
    gap: 8px;
    margin-top: 4px;
}

.dlg-btn {
    @include oams-focus;
    min-height: 34px;
    min-width: 24px;
    margin: 0;
    padding: 7px 14px;
    border: 1px solid var(--oams-line);
    border-radius: 9px;
    background: var(--oams-surface-2);
    color: inherit;
    font: inherit;
    font-size: 13px;
    cursor: pointer;

    &:hover {
        background: var(--oams-surface-3);
    }

    &.primary {
        background: var(--oams-accent);
        border-color: transparent;
        color: #fff;
    }

    /* A button with nothing to send is dimmed rather than hidden, so the form
       keeps its shape (principles 1, 8). */
    &:disabled {
        opacity: 0.45;
        cursor: not-allowed;
    }
}
</style>
