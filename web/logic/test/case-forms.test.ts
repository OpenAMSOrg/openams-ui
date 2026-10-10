// A template's {spool@acc} rebuilds the spool label in that case (Russian
// declines it); text no case form matches goes in as it is; English, which has
// no forms, is unchanged. The same cases as tests/test_languages.c.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { format, setLanguage } from '../strings.ts';

test('a {spool@case} placeholder is rebuilt in the case the template asks for', () => {
  assert.equal(setLanguage('ru'), true);
  try {
    assert.equal(format('EDIT_RESET_TITLE', { spool: 'Катушка 3' }, 96), 'Сбросить Катушку 3?');
    assert.equal(format('GP_FOR', { spool: 'Катушка 2 в AMS 2 Pro' }, 96), 'Группа для Катушки 2 в AMS 2 Pro');
    assert.equal(format('EDIT_RESET_TITLE', { spool: 'Мой PLA' }, 96), 'Сбросить Мой PLA?');
  } finally {
    setLanguage('en');
  }
  assert.equal(format('EDIT_RESET_TITLE', { spool: 'Spool 3' }, 96), 'Reset Spool 3?');
});
