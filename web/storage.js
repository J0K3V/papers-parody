/**
 * Browser side persistence.
 *
 * Pictures and sounds the visitor imports are kept as blobs in IndexedDB, so
 * reloading the page - or coming back tomorrow - does not lose them. The
 * project being edited is autosaved to the same database.
 */

const DB_NAME = 'papers-parody';
const DB_VERSION = 1;

const STORE_ASSETS = 'assets';
const STORE_PROJECTS = 'projects';
const STORE_STATE = 'state';

let dbPromise = null;

/** Open (and if needed create) the database. */
function openDatabase() {
  if (dbPromise) return dbPromise;

  dbPromise = new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, DB_VERSION);

    request.onupgradeneeded = () => {
      const db = request.result;
      if (!db.objectStoreNames.contains(STORE_ASSETS)) {
        db.createObjectStore(STORE_ASSETS, { keyPath: 'id' });
      }
      if (!db.objectStoreNames.contains(STORE_PROJECTS)) {
        db.createObjectStore(STORE_PROJECTS, { keyPath: 'name' });
      }
      if (!db.objectStoreNames.contains(STORE_STATE)) {
        db.createObjectStore(STORE_STATE, { keyPath: 'key' });
      }
    };

    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });

  return dbPromise;
}

/** Run one transaction and resolve with the request result. */
async function run(storeName, mode, action) {
  const db = await openDatabase();
  return new Promise((resolve, reject) => {
    const transaction = db.transaction(storeName, mode);
    const store = transaction.objectStore(storeName);
    const request = action(store);

    transaction.oncomplete = () => resolve(request ? request.result : undefined);
    transaction.onerror = () => reject(transaction.error);
    transaction.onabort = () => reject(transaction.error);
  });
}

export const storage = {
  /** True when the browser lets us keep data around. */
  get available() {
    return typeof indexedDB !== 'undefined';
  },

  /** Save an imported file. `kind` is 'image' or 'sound'. */
  async putAsset(kind, name, blob) {
    const id = `user/${kind}s/${name}`;
    await run(STORE_ASSETS, 'readwrite', (store) =>
      store.put({ id, kind, name, blob, addedAt: Date.now() })
    );
    return id;
  },

  /** Every asset of one kind, oldest first. */
  async listAssets(kind) {
    const all = await run(STORE_ASSETS, 'readonly', (store) => store.getAll());
    return (all || []).filter((item) => item.kind === kind).sort((a, b) => a.addedAt - b.addedAt);
  },

  /** Fetch a single asset by its id. */
  async getAsset(id) {
    return run(STORE_ASSETS, 'readonly', (store) => store.get(id));
  },

  /** Forget an imported asset. */
  async deleteAsset(id) {
    return run(STORE_ASSETS, 'readwrite', (store) => store.delete(id));
  },

  /** Store a named project. */
  async putProject(name, data) {
    return run(STORE_PROJECTS, 'readwrite', (store) =>
      store.put({ name, data, savedAt: Date.now() })
    );
  },

  /** All saved projects, most recent first. */
  async listProjects() {
    const all = await run(STORE_PROJECTS, 'readonly', (store) => store.getAll());
    return (all || []).sort((a, b) => b.savedAt - a.savedAt);
  },

  /** Load one project by name. */
  async getProject(name) {
    return run(STORE_PROJECTS, 'readonly', (store) => store.get(name));
  },

  /** Delete a saved project. */
  async deleteProject(name) {
    return run(STORE_PROJECTS, 'readwrite', (store) => store.delete(name));
  },

  /** Remember the work in progress so a reload picks it back up. */
  async saveDraft(data) {
    return run(STORE_STATE, 'readwrite', (store) => store.put({ key: 'draft', value: data }));
  },

  /** The work in progress from the previous visit, if any. */
  async loadDraft() {
    const row = await run(STORE_STATE, 'readonly', (store) => store.get('draft'));
    return row ? row.value : null;
  },

  /** How much space the imported files take, in bytes. */
  async usedBytes() {
    const all = await run(STORE_ASSETS, 'readonly', (store) => store.getAll());
    return (all || []).reduce((total, item) => total + (item.blob ? item.blob.size : 0), 0);
  },
};
