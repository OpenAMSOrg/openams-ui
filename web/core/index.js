/**
 * @file web/core/index.js
 * The JS side of the openams-core WebAssembly module (platform/core-wasm,
 * docs/design/UNIFIED_UI.md 8): a singleton with apply(), reset(), view(),
 * action() and subscribe().
 *
 *   const core = await loadCore();
 *   core.apply('openams_ui', 'oams1', statusObject);   // a whole object or a diff
 *   const view = core.view();                          // the panel tree
 *   core.action('unload fps1');                        // { kind: 'gcode', script }
 *   const stop = core.subscribe(view => render(view)); // at most once per frame
 *
 * The glue and the .wasm come from `platform/core-wasm/build.sh`
 * (./dist/openams-core.{js,wasm}). They are imported lazily, on the first
 * loadCore(), so a host can put this whole module in a lazy chunk.
 */

let corePromise = null;

/**
 * Load the module once per page. Every call returns the same promise, so the
 * resolved core is one instance with one model, however many components ask.
 * A failed load is not cached: the next call tries again.
 *
 * @param {{ locateFile?: (file: string, dir: string) => string,
 *           wasmBinary?: ArrayBuffer | Uint8Array }} [options]
 *   passed to the Emscripten module (where to fetch the .wasm from, or its
 *   bytes); ignored on every call but the first.
 * @returns {Promise<object>} the singleton core (see index.d.ts)
 */
export function loadCore(options) {
  if (!corePromise) {
    corePromise = instantiate(options).catch((err) => {
      corePromise = null;
      throw err;
    });
  }
  return corePromise;
}

async function instantiate(options) {
  const { default: createModule } = await import('./dist/openams-core.js');
  const mod = await createModule(options ? { ...options } : {});
  mod._core_wasm_init();
  return makeCore(mod);
}

/** The next frame in a browser; a microtask anywhere else (node, workers
 *  without requestAnimationFrame). */
function scheduleFlush(fn) {
  if (typeof requestAnimationFrame === 'function') requestAnimationFrame(fn);
  else queueMicrotask(fn);
}

function makeCore(mod) {
  const listeners = new Set();
  let flushPending = false;

  /** Copy @p text into the wasm heap (never the small C stack), run @p fn on
   *  the pointer, free it. A null/undefined text is the C null pointer. */
  function withString(text, fn) {
    if (text === null || text === undefined) return fn(0);
    const size = mod.lengthBytesUTF8(text) + 1;
    const ptr = mod._malloc(size);
    if (!ptr) throw new RangeError('openams-core: out of memory (input of ' + size + ' bytes)');
    try {
      mod.stringToUTF8(text, ptr, size);
      return fn(ptr);
    } finally {
      mod._free(ptr);
    }
  }

  /** Read and free a string the core returned. */
  function takeString(ptr) {
    if (!ptr) throw new RangeError('openams-core: out of memory');
    try {
      return mod.UTF8ToString(ptr);
    } finally {
      mod._core_wasm_free(ptr);
    }
  }

  function flush() {
    flushPending = false;
    if (listeners.size === 0) return;
    const v = view();
    for (const listener of Array.from(listeners)) {
      try {
        listener(v);
      } catch (err) {
        console.error('openams-core: a subscriber threw', err);
      }
    }
  }

  function requestFlush() {
    if (flushPending || listeners.size === 0) return;
    flushPending = true;
    scheduleFlush(flush);
  }

  function apply(kind, name, obj) {
    const json = JSON.stringify(obj);
    if (json === undefined) throw new TypeError('openams-core: apply() needs a JSON value');
    stamp();
    const rc = withString(kind, (k) =>
      withString(name, (n) => withString(json, (j) => mod._core_wasm_apply(k, n, j))));
    if (rc < 0) {
      throw new Error('openams-core: apply(' + JSON.stringify(kind) + ') failed (bad arguments, invalid JSON or out of memory)');
    }
    requestFlush();
    return rc === 1;
  }

  function reset(keepAlerts) {
    mod._core_wasm_reset(keepAlerts ? 1 : 0);
    requestFlush();
  }

  function view() {
    stamp();
    return JSON.parse(takeString(mod._core_wasm_view()));
  }

  function action(line, form) {
    const formJson = form === null || form === undefined ? null : JSON.stringify(form);
    return JSON.parse(takeString(
      withString(String(line), (l) => withString(formJson, (f) => mod._core_wasm_action(l, f)))));
  }

  /** Show the view's text in `code` ("en", "qps"): every later view() uses
   *  it. False when no such language is built in (the current one is kept).
   *  Nothing is cached, so read view() again afterwards. */
  function setLanguage(code) {
    if (typeof code !== 'string') throw new TypeError('openams-core: setLanguage() needs a language code');
    const rc = withString(code, (c) => mod._core_wasm_set_language(c));
    if (rc < 0) throw new RangeError('openams-core: setLanguage() failed (out of memory)');
    if (rc === 1) requestFlush();
    return rc === 1;
  }

  let fixedClock = null;
  const stamp = () => mod._core_wasm_set_clock((fixedClock === null ? Date.now() : fixedClock) >>> 0);

  /** Set the clock alerts are stamped and aged by (ms, a uint32), or null to follow Date.now(). */
  function setClock(ms) {
    fixedClock = ms === null ? null : Number(ms) >>> 0;
    stamp();
  }

  function subscribe(listener) {
    if (typeof listener !== 'function') throw new TypeError('openams-core: subscribe() needs a function');
    listeners.add(listener);
    return () => {
      listeners.delete(listener);
    };
  }

  return Object.freeze({ apply, reset, view, action, setLanguage, setClock, subscribe });
}
