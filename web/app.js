/**
 * Parody, Please - web editor.
 *
 * Build a report scene by scene, watch it play in real time and record it to a
 * video file. Pictures and sounds you import stay in this browser through
 * IndexedDB, so closing the tab does not throw your work away.
 */

import { storage } from './storage.js';
import {
  FPS,
  Player,
  SoundBank,
  buildTimeline,
  fitPicture,
  sprites,
} from './engine.js';

const $ = (id) => document.getElementById(id);

// --------------------------------------------------------------------- state

const state = {
  manifest: null,
  scenes: [],
  selected: null,
  options: {
    soundtrack: 'bad ending', fit: 'contain', typing: 'normal',
    output: 'my-parody.mp4', movieMode: true,
  },
  projectName: null,   // what Save writes over; null until the first save/open
  timeline: null,

  // The three sliders beside the booth. They shape what the speakers do -
  // never what RECORD writes - and they are remembered between visits.
  volumes: { master: 1, music: 1, effects: 1 },
};

const VOLUME_STORE = 'parody-please-volume';

const bank = new SoundBank();
let player = null;

/** Cached object URLs for assets stored in the browser. */
const urlCache = new Map();
/** Cached 480x320 canvases, keyed by asset id and fit mode. */
const pictureCache = new Map();

// ------------------------------------------------------------------ helpers

let toastTimer = null;

/** Flash a short message at the bottom of the screen. */
function toast(message) {
  const node = $('toast');
  node.textContent = message;
  node.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => node.classList.remove('show'), 2600);
}

/** Format seconds as m:ss. */
function timecode(seconds) {
  const whole = Math.max(0, Math.floor(seconds));
  return `${Math.floor(whole / 60)}:${String(whole % 60).padStart(2, '0')}`;
}

/** Turn an asset id into something an <img> or fetch() can use. */
async function assetUrl(id) {
  if (!id) return null;
  if (urlCache.has(id)) return urlCache.get(id);

  if (id.startsWith('user/')) {
    const row = await storage.getAsset(id);
    if (!row) return null;
    const url = URL.createObjectURL(row.blob);
    urlCache.set(id, url);
    return url;
  }

  urlCache.set(id, id);
  return id;
}

/** Load an image element for an asset id. */
async function loadImage(id) {
  const url = await assetUrl(id);
  if (!url) return null;

  return new Promise((resolve) => {
    const image = new Image();
    // Decoded off the main thread before anyone draws it, so a big imported
    // photo does not freeze the page at its first drawImage.
    image.onload = () => image.decode().then(() => resolve(image), () => resolve(image));
    image.onerror = () => resolve(null);
    image.src = url;
  });
}

/** Build (and cache) the 480x320 picture a scene draws. */
async function pictureFor(id) {
  const key = `${id}|${state.options.fit}`;
  if (pictureCache.has(key)) return pictureCache.get(key);

  const image = await loadImage(id);
  if (!image) return null;

  const canvas = fitPicture(image, state.options.fit);
  pictureCache.set(key, canvas);
  return canvas;
}

/** The friendly name of an asset id. */
function assetName(id) {
  if (!id) return '-';
  return id.split('/').pop().replace(/\.[^.]+$/, '');
}

/** Sound entries from the manifest, indexed by bare file name. */
function manifestSoundByName(fileName) {
  const all = [...state.manifest.sounds.default, ...state.manifest.sounds.custom];
  return all.find((item) => item.id.endsWith(`/${fileName}`) || item.name === fileName.replace('.wav', ''));
}

/** Convert a soundtrack pack into asset ids the sound bank understands. */
function resolvePack(packName) {
  const pack = state.manifest.packs[packName] || state.manifest.packs['bad ending'];
  const map = (names) => names.map((name) => {
    const entry = manifestSoundByName(name);
    return entry ? entry.id : null;
  }).filter(Boolean);

  return { music: map(pack.music), next: map(pack.next), letter: map(pack.letter) };
}

// ------------------------------------------------------------- scene list UI

/**
 * Keep the scene list on whichever scene the report is showing.
 *
 * Only the highlight moves, not the whole list: this runs on every frame, and
 * rebuilding thirty cards a second would be felt.
 */
function followPlayingScene(frame) {
  // The player's own timeline, not the editor's: an edit made mid-play can
  // briefly leave the two apart, and the frame numbers belong to the stage.
  const timeline = (player && player.timeline) || state.timeline;
  if (!timeline) return;

  // While the report is being written into, the selection stays put - moving
  // it mid-keystroke would redirect the typing into whatever scene happens to
  // be playing.
  if (document.activeElement === $('scene-text')) return;

  const entry = timeline.entries.find((item) => frame < item.start + item.end)
    || timeline.entries[timeline.entries.length - 1];
  if (!entry || entry.index === state.selected) return;

  state.selected = entry.index;

  const cards = $('scene-list').children;
  for (let i = 0; i < cards.length; i += 1) {
    cards[i].classList.toggle('selected', i === entry.index);
  }
  if (cards[entry.index]) {
    cards[entry.index].scrollIntoView({ block: 'nearest' });
  }
  renderInspector();
}

/** Redraw the scene cards on the left. */
async function renderSceneList() {
  const list = $('scene-list');
  list.innerHTML = '';

  if (state.scenes.length === 0) {
    const hint = document.createElement('div');
    hint.className = 'empty-hint';
    hint.textContent = 'No scenes yet. Press ADD SCENE to start your report.';
    list.appendChild(hint);
  }

  for (const [index, scene] of state.scenes.entries()) {
    const card = document.createElement('div');
    card.className = 'scene-card' + (index === state.selected ? ' selected' : '');
    card.addEventListener('click', () => selectScene(index));

    const thumb = document.createElement('img');
    thumb.alt = '';
    assetUrl(scene.image).then((url) => {
      if (url) thumb.src = url;
    });

    const body = document.createElement('div');
    body.className = 'body';

    const number = document.createElement('div');
    number.className = 'index';
    number.textContent = `SCENE ${index + 1}`;

    const caption = document.createElement('div');
    caption.className = 'caption';
    caption.textContent = scene.text.trim() || '(no text)';

    body.append(number, caption);

    if (scene.sound) {
      const cue = document.createElement('div');
      cue.className = 'cue';
      cue.textContent = `♪ ${assetName(scene.sound)}`;
      body.appendChild(cue);
    }

    card.append(thumb, body);
    list.appendChild(card);
  }

  $('scene-count').textContent = String(state.scenes.length);
}

/** Update the inspector to show one scene. */
function renderInspector() {
  const scene = state.selected === null ? null : state.scenes[state.selected];

  $('scene-text').value = scene ? scene.text : '';
  $('picture-name').textContent = scene ? assetName(scene.image) : '-';
  $('sound-name').textContent = scene && scene.sound ? assetName(scene.sound) : 'none';
  $('music-name').textContent = scene && scene.music ? assetName(scene.music) : 'carries on';
  $('btn-preview-sound').disabled = !(scene && scene.sound);

  // When that sound fires. Only worth showing once there is one to place.
  const when = scene ? scene.soundAt || 'start' : 'start';
  $('sound-when-group').hidden = !(scene && scene.sound);
  $('opt-sound-when').value = when;
  $('sound-delay-group').hidden = when !== 'delayed';
  $('sound-delay-value').textContent = `${(scene && scene.soundDelay || 0).toFixed(1)}s`;

  const align = scene ? scene.align || 'left' : 'left';
  ['left', 'center', 'right'].forEach((key) => {
    $(`align-${key}`).classList.toggle('active', key === align);
  });

  const hasScene = Boolean(scene);
  ['btn-change-picture', 'btn-change-sound', 'btn-change-music', 'btn-up', 'btn-down',
   'btn-duplicate', 'btn-delete', 'align-left', 'align-center', 'align-right']
    .forEach((id) => { $(id).disabled = !hasScene; });
  $('scene-text').disabled = !hasScene;
}

/** Select a scene and show its final frame on the stage. */
async function selectScene(index) {
  state.selected = index;

  // Picking a scene while the report is running jumps to it rather than only
  // changing what the inspector edits. The music carries on through.
  if (player.playing && state.timeline && state.timeline.entries[index]) {
    player.seek(state.timeline.entries[index].start);
    await renderSceneList();
    renderInspector();
    return;
  }

  await renderSceneList();
  renderInspector();
  await drawSelectedFrame();
}

// ---------------------------------------------------------------- the stage

/** Rebuild the timeline and refresh the duration readout. */
function refreshTimeline() {
  // The button is always there: it is how a report is read, and the control
  // for turning it off has gone from both editors.
  state.timeline = buildTimeline(state.scenes, state.options.typing, true);
  $('timecode').textContent = `0:00 / ${timecode(state.timeline.durationSeconds)}`;
  $('progress').value = 0;
  return state.timeline;
}

/** Collect the fitted pictures for every scene, in order. */
async function loadPictures() {
  return Promise.all(state.scenes.map((scene) => pictureFor(scene.image)));
}

/** Show the selected scene with its text fully typed out. */
async function drawSelectedFrame() {
  // The stage belongs to the player while it runs; painting a still over the
  // moving picture flashed - and during a recording, straight into the file.
  if (player && player.playing) return;

  const timeline = refreshTimeline();
  const canvas = $('stage');
  const ctx = canvas.getContext('2d');

  if (state.scenes.length === 0 || state.selected === null) {
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    return;
  }

  const pictures = await loadPictures();
  const entry = timeline.entries[state.selected];
  player.showFrame(timeline, entry.start + entry.typeEnd, pictures);
}

/** Make sure every sound the video needs is decoded. */
async function preloadSounds() {
  const pack = resolvePack(state.options.soundtrack);
  const ids = new Set([...pack.music, ...pack.next, ...pack.letter]);
  state.scenes.forEach((scene) => {
    if (scene.sound) ids.add(scene.sound);
    if (scene.music) ids.add(scene.music);
  });

  // All of them at once: decoding is off the main thread, and one at a time
  // simply multiplied the wait before the first note.
  await Promise.all([...ids].map(async (id) => {
    try {
      if (id.startsWith('user/')) {
        const row = await storage.getAsset(id);
        if (row) await bank.load(id, row.blob);
      } else {
        const entry = [...state.manifest.sounds.default, ...state.manifest.sounds.custom]
          .find((item) => item.id === id);
        await bank.load(id, entry ? entry.url : id);
      }
    } catch (error) {
      console.warn('Could not decode', id, error);
    }
  }));

  return pack;
}

/** Play the whole report, optionally recording it to a file. */
async function play({ record = false } = {}) {
  if (state.scenes.length === 0) {
    toast('Add at least one scene first.');
    return;
  }

  // One run at a time, checked before anything is awaited: two clicks in the
  // moment the sounds were still decoding could otherwise start twice - and
  // claim to be recording while nothing was being captured.
  if (player.playing || $('btn-play').disabled) return;
  $('btn-play').disabled = true;
  $('btn-record').disabled = true;

  bank.ensureContext();
  let timeline;
  let pictures;
  let pack;
  try {
    timeline = refreshTimeline();
    pictures = await loadPictures();
    pack = await preloadSounds();
  } catch (error) {
    console.warn('Could not get the report ready', error);
    $('btn-play').disabled = false;
    $('btn-record').disabled = false;
    return;
  }

  $('btn-stop').disabled = false;

  // A film can be paused. A simulation cannot: it is already stopping on every
  // button, and a recording has to run start to finish in real time.
  $('btn-pause').disabled = record || !state.options.movieMode;
  $('btn-pause').textContent = 'PAUSE';

  if (record) toast('Recording in real time - let it play to the end.');

  player.onFrame = (frame, total) => {
    $('progress').value = frame / total;
    $('timecode').textContent = `${timecode(frame / FPS)} / ${timecode(total / FPS)}`;
    followPlayingScene(frame);
  };

  player.onFinish = ({ blob }) => {
    $('btn-play').disabled = false;
    $('btn-record').disabled = false;
    $('btn-stop').disabled = true;
    $('btn-pause').disabled = true;
    $('btn-pause').textContent = 'PAUSE';

    if (blob && blob.size > 0) {
      const url = URL.createObjectURL(blob);
      const link = document.createElement('a');
      link.href = url;
      // The name comes from the footer, with the browser's own format on the
      // end whatever extension was typed there.
      const typed = ($('opt-output').value || 'my-parody').trim();
      link.download = typed.replace(/\.(webm|mp4)$/i, '') + '.webm';
      link.click();
      setTimeout(() => URL.revokeObjectURL(url), 10000);
      toast(`Video saved as ${link.download}`);
    }
  };

  player.onWait = (entry) => {
    $('stage').classList.add('waiting');
    $('timecode').textContent = entry && entry.isLast
      ? 'the end - press NEXT to go back to the start'
      : 'waiting on NEXT - click it';
  };

  player.onRewind = async () => {
    if (document.fullscreenElement) {
      try {
        await document.exitFullscreen();
      } catch (error) {
        /* the browser said no, carry on */
      }
    }
    $('stage').classList.remove('waiting');
    state.selected = 0;
    await selectScene(0);
    toast('Back to the start.');
  };

  // Playing the game rather than watching the film: start on the scene you
  // have selected. A recording always runs from the top.
  const simulation = !state.options.movieMode && !record;
  const fromFrame = simulation && state.selected !== null
    ? timeline.entries[state.selected].start
    : 0;

  await player.start(timeline, pictures, { packs: { [state.options.soundtrack]: pack } }, {
    record,
    packName: state.options.soundtrack,
    movieMode: state.options.movieMode,
    fromFrame,
  });
}

// ------------------------------------------------------------ picture picker

let pictureResolve = null;
// The game's own frames come first: that is what a new report is built from,
// and it is where the desktop editor opens too.
let pictureTab = 'game';

/** Open the picture picker and resolve with the chosen asset id. */
function choosePicture() {
  return new Promise((resolve) => {
    pictureResolve = resolve;
    fillPictureGrid();
    $('picture-dialog').showModal();
  });
}

/** Fill the picker grid for the active tab. */
async function fillPictureGrid() {
  const generation = ++pictureGeneration;
  const grid = $('picture-grid');
  grid.innerHTML = '';

  ['mine', 'game', 'imported'].forEach((tab) => {
    $(`tab-${tab}`).classList.toggle('active', tab === pictureTab);
  });

  let entries = [];
  if (pictureTab === 'mine') {
    entries = state.manifest.images.custom.map((item) => ({ id: item.id, name: item.name, url: item.url }));
  } else if (pictureTab === 'game') {
    entries = state.manifest.images.default.map((item) => ({ id: item.id, name: item.name, url: item.url }));
  } else {
    const rows = await storage.listAssets('image');
    entries = await Promise.all(rows.map(async (row) => ({
      id: row.id,
      name: row.name.replace(/\.[^.]+$/, ''),
      url: await assetUrl(row.id),
      removable: true,
    })));

    // A faster tab switch has refilled the grid; these rows belong to a grid
    // that has already been cleared away.
    if (generation !== pictureGeneration) return;
  }

  if (entries.length === 0) {
    const hint = document.createElement('div');
    hint.className = 'empty-hint';
    hint.textContent = pictureTab === 'imported'
      ? 'Nothing imported yet. Use the button below - your files stay in this browser.'
      : 'Nothing here.';
    grid.appendChild(hint);
    return;
  }

  for (const entry of entries) {
    const tile = document.createElement('div');
    tile.className = 'tile';

    const image = document.createElement('img');
    image.src = entry.url;
    image.alt = entry.name;

    const name = document.createElement('div');
    name.className = 'name';
    name.textContent = entry.name;

    tile.append(image, name);
    tile.addEventListener('click', () => {
      // Taken before close(), or the dialog's close handler settles the
      // promise as "cancelled" first and the choice is lost.
      const resolve = pictureResolve;
      pictureResolve = null;
      $('picture-dialog').close();
      if (resolve) resolve(entry.id);
    });

    if (entry.removable) {
      const remove = document.createElement('div');
      remove.className = 'remove';
      remove.textContent = 'remove';
      remove.addEventListener('click', async (event) => {
        event.stopPropagation();
        await storage.deleteAsset(entry.id);
        const url = urlCache.get(entry.id);
        if (url && url !== entry.id) URL.revokeObjectURL(url);
        urlCache.delete(entry.id);
        for (const key of [...pictureCache.keys()]) {
          if (key.startsWith(`${entry.id}|`)) pictureCache.delete(key);
        }
        fillPictureGrid();
        toast('Removed from this browser.');
      });
      tile.appendChild(remove);
    }

    grid.appendChild(tile);
  }
}

// -------------------------------------------------------------- sound picker

let soundResolve = null;
let soundSelection = null;
let soundMode = 'effect';

// One preview at a time, owned here so closing the picker can silence it.
// A bare `new Audio(url).play()` kept playing long after the dialog had gone -
// the only way to stop a two minute track was to sit through it.
let previewAudio = null;

function previewPlay(url) {
  previewStop();
  previewAudio = new Audio(url);
  // An <audio> element does not pass through the player's buses, so the master
  // slider is applied by hand - the same slider the desktop preview sits under.
  previewAudio.volume = state.volumes.master;
  previewAudio.addEventListener('ended', () => previewStop());
  previewAudio.play().catch(() => {});
  $('preview-transport').hidden = false;
  $('preview-pause').textContent = 'PAUSE';
}

function previewStop() {
  if (previewAudio) {
    previewAudio.pause();
    previewAudio.src = '';
    previewAudio = null;
  }
  const transport = $('preview-transport');
  if (transport) transport.hidden = true;
}

function previewTogglePause() {
  if (!previewAudio) return;
  if (previewAudio.paused) {
    previewAudio.play().catch(() => {});
    $('preview-pause').textContent = 'PAUSE';
  } else {
    previewAudio.pause();
    $('preview-pause').textContent = 'RESUME';
  }
}

/**
 * Anything this big counts as a score rather than an effect - the same rule
 * the desktop mixer uses, so a sound never files under MUSIC on one editor
 * and EFFECTS on the other. (A bitrate guess put BorderCallguards, a long
 * uncompressed announcement, in with the scores.)
 */
const MUSIC_MIN_BYTES = 1024 * 1024;

function looksLikeMusic(entry) {
  const bytes = entry.bytes || 0;
  if (bytes) return bytes >= MUSIC_MIN_BYTES;
  // No size recorded: fall back to the names the long tracks use.
  return /theme|ending|death/i.test(entry.name);
}

/** Open the sound picker and resolve with an asset id, or null. */
function chooseSound(current, mode = 'effect') {
  return new Promise((resolve) => {
    soundResolve = resolve;
    soundSelection = current || null;
    soundMode = mode;
    fillSoundList();
    $('sound-dialog').showModal();
  });
}

// Bumped whenever a picker starts filling, so a slower, older fill that is
// still awaiting its reads cannot append rows under a newer one.
let soundGeneration = 0;
let pictureGeneration = 0;

/** Fill the sound picker with imported sounds first, then the game ones. */
async function fillSoundList() {
  const generation = ++soundGeneration;
  const list = $('sound-list');
  list.innerHTML = '';

  const addRow = (id, title, hint, url) => {
    const row = document.createElement('div');
    row.className = 'sound-row' + (soundSelection === id ? ' selected' : '');

    const name = document.createElement('div');
    name.className = 'name';
    name.textContent = title;

    row.appendChild(name);

    if (hint) {
      const note = document.createElement('span');
      note.className = 'dim';
      note.textContent = hint;
      row.appendChild(note);
    }

    if (url) {
      const play = document.createElement('button');
      play.textContent = 'listen';
      play.addEventListener('click', (event) => {
        event.stopPropagation();
        previewPlay(url);
      });
      row.appendChild(play);
    }

    // Clicking a row is choosing it, the way the desktop picker works. The
    // promise is taken before close() so the dialog's own close handler -
    // which settles it as "cancelled" - finds nothing left to settle.
    row.addEventListener('click', () => {
      previewStop();
      const resolve = soundResolve;
      soundResolve = null;
      $('sound-dialog').close();
      if (resolve) resolve(id);
    });

    list.appendChild(row);
  };

  const heading = (text) => {
    const node = document.createElement('div');
    node.className = 'label';
    node.style.margin = '12px 0 6px';
    node.textContent = text;
    list.appendChild(node);
  };

  const music = soundMode === 'music';
  const first = state.selected === 0;

  // Clearing the choice, said plainly rather than as a name with a dash.
  addRow(
    null,
    !music ? 'Play no sound in this scene'
      : first ? 'Use the project soundtrack, chosen at the bottom of the window'
              : 'Keep playing whatever the scene before was playing',
    '',
    null
  );

  const rows = await storage.listAssets('sound');

  // Imports are sorted the same way the game's own files are, so a two
  // minute track someone brought in does not sit in the effects list. The
  // urls come from the shared cache rather than being minted per open.
  const imported = await Promise.all(
    rows
      .filter((row) => looksLikeMusic({ bytes: row.blob.size, name: row.name }) === music)
      .map(async (row) => ({
        id: row.id,
        name: row.name.replace(/\.[^.]+$/, ''),
        url: await assetUrl(row.id),
      }))
  );

  // A newer fill has taken the list over while this one was reading.
  if (generation !== soundGeneration) return;

  heading('IMPORTED');

  if (imported.length === 0) {
    const hint = document.createElement('div');
    hint.className = 'dim';
    hint.style.margin = '0 0 8px';
    hint.textContent = 'None yet - import a paper shuffle, a stamp, a track, whatever fits.';
    list.appendChild(hint);
  }

  for (const row of imported) {
    addRow(row.id, row.name, '', row.url);
  }

  // A picker for effects lists effects; a picker for music lists music. The
  // two used to share one list with the other kind underneath, which made both
  // of them twice as long to read through.
  const belongs = state.manifest.sounds.default.filter(
    (entry) => looksLikeMusic(entry) === music
  );

  heading(music ? 'MUSIC' : 'EFFECTS');
  for (const entry of belongs) {
    addRow(entry.id, entry.name, '', entry.url);
  }
}

// ------------------------------------------------------------------ projects

/** The plain object that gets saved, exported and autosaved. */
function serialise() {
  // The field names are the desktop editor's, so a report written here opens
  // there and the other way round. They used to be camelCase on this side,
  // which meant every setting was silently dropped crossing over.
  return {
    version: 2,
    scenes: state.scenes.map((scene) => {
      const node = {
        image: scene.image,
        text: scene.text,
        sound: scene.sound || null,
        music: scene.music || null,
        align: scene.align || 'left',
      };
      // Only written when it is not the default, so files stay tidy.
      if (scene.soundAt && scene.soundAt !== 'start') node.sound_at = scene.soundAt;
      if (scene.soundAt === 'delayed') node.sound_delay = scene.soundDelay || 0;
      return node;
    }),
    soundtrack: state.options.soundtrack,
    fit: state.options.fit,
    typing: state.options.typing,
    next_button: 'on',
    output: state.options.output || 'my-parody.mp4',
    movie_mode: state.options.movieMode,
  };
}

/** Load a project object into the editor. */
async function deserialise(payload, announce = false) {
  // Whatever was running was built from the report being replaced. The
  // desktop editor stops on Open and New; so does this one.
  if (player) player.stop(false);

  // A sound named some other way - a bare "GoodEnding.mp3" from an older
  // desktop save, a path with backslashes in it - is matched to the manifest
  // by its file name, so the same report sounds the same on both editors.
  const knownSound = (value) => {
    if (!value || value.startsWith('user/')) return value || null;
    const all = [...state.manifest.sounds.default, ...state.manifest.sounds.custom];
    if (all.some((item) => item.id === value)) return value;
    const entry = manifestSoundByName(value.split(/[\\/]/).pop());
    return entry ? entry.id : value;
  };

  state.scenes = (payload.scenes || []).map((scene) => ({
    image: scene.image,
    text: scene.text || '',
    sound: knownSound(scene.sound),
    music: knownSound(scene.music),
    align: scene.align || 'left',
    soundAt: scene.sound_at || 'start',
    soundDelay: Number(scene.sound_delay) || 0,
  }));

  // camelCase is still read so reports saved by older versions of this page
  // still open; everything written from now on uses the desktop's names.
  state.options.soundtrack = payload.soundtrack || 'bad ending';
  state.options.fit = payload.fit || 'contain';
  state.options.typing = payload.typing || 'normal';
  state.options.output = payload.output || 'my-parody.mp4';
  state.options.movieMode = (payload.movie_mode ?? payload.movieMode) !== false;

  $('opt-soundtrack').value = state.options.soundtrack;
  $('opt-fit').value = state.options.fit;
  $('opt-typing').value = state.options.typing;
  $('opt-movie-mode').checked = state.options.movieMode;
  $('opt-output').value = state.options.output.replace(/\.(webm|mp4)$/i, '') + '.webm';

  state.selected = state.scenes.length ? 0 : null;
  pictureCache.clear();
  await renderSceneList();
  renderInspector();
  await drawSelectedFrame();

  // Whatever was just loaded is now the work in progress, so a reload brings
  // this back rather than whatever was open before it.
  scheduleAutosave();

  if (announce) revealBehindShutter();
}

let draftTimer = null;

/** Remember the work in progress, a moment after the last change. */
function scheduleAutosave() {
  clearTimeout(draftTimer);
  draftTimer = setTimeout(() => {
    storage.saveDraft(serialise()).catch(() => {});
  }, 600);
}

/** Anything that changes the video: redraw, re-time and autosave. */
async function changed({ redraw = true } = {}) {
  await renderSceneList();
  renderInspector();

  if (player.playing && !player.recording) {
    // An edit made while the report plays shows up in it straight away, the
    // desktop editor's rule. The player only restarts what actually changed.
    player.refresh(refreshTimeline(), await loadPictures());
  } else if (redraw) {
    await drawSelectedFrame();
  }
  scheduleAutosave();
}

/** Show how much room the imported files take. */
async function updateStorageNote() {
  if (!storage.available) {
    $('storage-note').textContent = 'this browser cannot store imports';
    return;
  }
  const bytes = await storage.usedBytes();
  const megabytes = (bytes / (1024 * 1024)).toFixed(1);
  $('storage-note').textContent = bytes
    ? `${megabytes} MB imported, kept in this browser`
    : 'imports are kept in this browser';
}

// -------------------------------------------------------------------- wiring

/** Hook up every control. */
function wire() {
  $('btn-add-scene').addEventListener('click', async () => {
    const id = await choosePicture();
    if (!id) return;
    state.scenes.push({
      image: id, text: '', sound: null, music: null, align: 'left',
      soundAt: 'start', soundDelay: 0,
    });
    state.selected = state.scenes.length - 1;
    await changed();
    $('scene-text').focus();
  });

  $('btn-change-picture').addEventListener('click', async () => {
    if (state.selected === null) return;
    const id = await choosePicture();
    if (!id) return;
    state.scenes[state.selected].image = id;
    await changed();
  });

  $('btn-change-sound').addEventListener('click', async () => {
    if (state.selected === null) return;
    const current = state.scenes[state.selected].sound;
    const chosen = await chooseSound(current);
    if (chosen === undefined) return;
    state.scenes[state.selected].sound = chosen;
    await changed({ redraw: false });
  });

  $('opt-sound-when').addEventListener('change', async (event) => {
    if (state.selected === null) return;
    state.scenes[state.selected].soundAt = event.target.value;
    await changed({ redraw: false });
  });

  const nudgeDelay = async (by) => {
    if (state.selected === null) return;
    const scene = state.scenes[state.selected];
    scene.soundDelay = Math.min(30, Math.max(0, (scene.soundDelay || 0) + by));
    await changed({ redraw: false });
  };
  $('sound-delay-less').addEventListener('click', () => nudgeDelay(-0.5));
  $('sound-delay-more').addEventListener('click', () => nudgeDelay(0.5));

  $('btn-change-music').addEventListener('click', async () => {
    if (state.selected === null) return;
    const chosen = await chooseSound(state.scenes[state.selected].music, 'music');
    if (chosen === undefined) return;
    state.scenes[state.selected].music = chosen;
    await changed({ redraw: false });
  });

  $('btn-preview-sound').addEventListener('click', async () => {
    if (state.selected === null) return;
    const sound = state.scenes[state.selected].sound;
    const url = await assetUrl(sound);
    // Through the shared preview, so a second listen replaces the first
    // instead of stacking on top of it, and closing a picker silences it.
    if (url) previewPlay(url);
  });

  ['left', 'center', 'right'].forEach((key) => {
    $(`align-${key}`).addEventListener('click', async () => {
      if (state.selected === null) return;
      state.scenes[state.selected].align = key;
      await changed();
    });
  });

  $('scene-text').addEventListener('input', (event) => {
    if (state.selected === null) return;
    state.scenes[state.selected].text = event.target.value.replace(/\n/g, ' ');
    // Only this scene's caption changes, so only it is touched. Rebuilding
    // every card on every keystroke is what made typing feel sticky.
    const card = $('scene-list').children[state.selected];
    const caption = card ? card.querySelector('.caption') : null;
    if (caption) caption.textContent = state.scenes[state.selected].text.trim() || '(no text)';

    if (player.playing && !player.recording) {
      // Typed into a playing report, the line lands in it as it runs.
      const timeline = refreshTimeline();
      loadPictures().then((pictures) => player.refresh(timeline, pictures));
    } else {
      drawSelectedFrame();
    }
    scheduleAutosave();
  });

  $('btn-up').addEventListener('click', async () => {
    const index = state.selected;
    if (index === null || index === 0) return;
    [state.scenes[index - 1], state.scenes[index]] = [state.scenes[index], state.scenes[index - 1]];
    state.selected = index - 1;
    await changed();
  });

  $('btn-down').addEventListener('click', async () => {
    const index = state.selected;
    if (index === null || index >= state.scenes.length - 1) return;
    [state.scenes[index + 1], state.scenes[index]] = [state.scenes[index], state.scenes[index + 1]];
    state.selected = index + 1;
    await changed();
  });

  $('btn-duplicate').addEventListener('click', async () => {
    if (state.selected === null) return;
    state.scenes.splice(state.selected + 1, 0, { ...state.scenes[state.selected] });
    state.selected += 1;
    await changed();
  });

  $('btn-delete').addEventListener('click', async () => {
    if (state.selected === null) return;
    state.scenes.splice(state.selected, 1);
    state.selected = state.scenes.length ? Math.min(state.selected, state.scenes.length - 1) : null;
    await changed();
  });

  // Pressing NEXT yourself, when movie mode is off.
  const stage = $('stage');

  const stagePoint = (event) => {
    const rect = stage.getBoundingClientRect();
    return {
      x: (event.clientX - rect.left) * (stage.width / rect.width),
      y: (event.clientY - rect.top) * (stage.height / rect.height),
    };
  };

  const overButton = (event) => {
    const box = player.gateBox;
    if (!box) return false;
    const point = stagePoint(event);
    return point.x >= box.x && point.x <= box.x + box.width
        && point.y >= box.y && point.y <= box.y + box.height;
  };

  stage.addEventListener('mousemove', (event) => {
    const over = player.waiting && overButton(event);
    player.hover = over;
    stage.classList.toggle('over-next', over);
  });

  stage.addEventListener('mouseleave', () => {
    player.hover = false;
    stage.classList.remove('over-next');
  });

  stage.addEventListener('click', (event) => {
    if (!player.waiting || !overButton(event)) return;
    player.clickNext();
    stage.classList.remove('waiting');
  });

  // Transport
  $('btn-play').addEventListener('click', () => play({ record: false }));
  $('btn-record').addEventListener('click', () => play({ record: true }));
  $('btn-stop').addEventListener('click', () => player.stop(false));

  $('lever').addEventListener('click', pullLever);

  // The volume sliders. Each lands at once on whatever is already playing,
  // and the setting is kept for the next visit.
  for (const kind of ['master', 'music', 'effects']) {
    $(`vol-${kind}`).addEventListener('input', (event) => {
      const value = Math.min(1, Math.max(0, Number(event.target.value) / 100));
      state.volumes[kind] = value;
      $(`vol-${kind}-pct`).textContent = `${Math.round(value * 100)}%`;
      player.setVolume(kind, value);
      if (previewAudio && kind === 'master') previewAudio.volume = value;
      try {
        localStorage.setItem(VOLUME_STORE, JSON.stringify(state.volumes));
      } catch (error) {
        /* private mode: the sliders still work, they are just not remembered */
      }
    });
  }

  $('btn-pause').addEventListener('click', () => {
    const held = player.togglePause();
    $('btn-pause').textContent = held ? 'RESUME' : 'PAUSE';
  });
  $('btn-fullscreen').addEventListener('click', () => {
    if (document.fullscreenElement) document.exitFullscreen();
    else $('booth').requestFullscreen().catch(() => toast('Fullscreen was refused.'));
  });
  $('fs-close').addEventListener('click', () => {
    if (document.fullscreenElement) document.exitFullscreen();
  });
  document.addEventListener('fullscreenchange', () => {
    $('btn-fullscreen').textContent = document.fullscreenElement ? 'WINDOWED' : 'FULLSCREEN';
  });

  $('opt-output').addEventListener('change', (event) => {
    state.options.output = event.target.value;
    scheduleAutosave();
  });

  // Options
  $('opt-soundtrack').addEventListener('change', (event) => {
    state.options.soundtrack = event.target.value;
    scheduleAutosave();
  });
  $('opt-fit').addEventListener('change', async (event) => {
    // The cache keys carry the fit mode, so flipping between the two modes
    // reuses what was already fitted instead of re-decoding every picture.
    state.options.fit = event.target.value;
    await changed();
  });
  $('opt-typing').addEventListener('change', async (event) => {
    state.options.typing = event.target.value;
    await changed();
  });
  $('opt-movie-mode').addEventListener('change', (event) => {
    state.options.movieMode = event.target.checked;
    scheduleAutosave();
  });

  // Picture dialog
  $('tab-mine').addEventListener('click', () => { pictureTab = 'mine'; fillPictureGrid(); });
  $('tab-game').addEventListener('click', () => { pictureTab = 'game'; fillPictureGrid(); });
  $('tab-imported').addEventListener('click', () => { pictureTab = 'imported'; fillPictureGrid(); });
  $('picture-cancel').addEventListener('click', () => $('picture-dialog').close());
  $('picture-dialog').addEventListener('close', () => {
    // Escape closes a dialog without touching any button; the promise must
    // still settle or the next click on Change does nothing.
    if (pictureResolve) pictureResolve(null);
    pictureResolve = null;
  });
  $('btn-import-picture').addEventListener('click', () => $('picture-file').click());
  $('picture-file').addEventListener('change', async (event) => {
    const files = [...event.target.files];
    event.target.value = '';
    for (const file of files) {
      const id = await storage.putAsset('image', file.name, file);
      // Re-importing under the same name replaces the stored blob, so any
      // url or fitted picture cached for the old one is stale.
      const url = urlCache.get(id);
      if (url && url !== id) URL.revokeObjectURL(url);
      urlCache.delete(id);
      for (const key of [...pictureCache.keys()]) {
        if (key.startsWith(`${id}|`)) pictureCache.delete(key);
      }
    }
    pictureTab = 'imported';
    await fillPictureGrid();
    await updateStorageNote();
    await changed({ redraw: true });
    toast(`${files.length} picture(s) saved in this browser.`);
  });

  // Sound dialog. However it closes, the preview stops with it.
  $('sound-cancel').addEventListener('click', () => {
    $('sound-dialog').close();
    if (soundResolve) soundResolve(undefined);
    soundResolve = null;
  });
  $('sound-dialog').addEventListener('close', () => {
    previewStop();
    if (soundResolve) soundResolve(undefined);
    soundResolve = null;
  });
  $('preview-pause').addEventListener('click', previewTogglePause);
  $('preview-stop').addEventListener('click', previewStop);
  $('btn-import-sound').addEventListener('click', () => $('sound-file').click());
  $('sound-file').addEventListener('change', async (event) => {
    const files = [...event.target.files];
    event.target.value = '';
    let last = null;
    for (const file of files) {
      last = await storage.putAsset('sound', file.name, file);
      // A re-import under the same name replaces the blob: the old url and
      // any buffer already decoded from it would keep playing the old sound.
      const url = urlCache.get(last);
      if (url && url !== last) URL.revokeObjectURL(url);
      urlCache.delete(last);
      bank.buffers.delete(last);
    }
    soundSelection = last;
    await fillSoundList();
    await updateStorageNote();
    toast('Sound saved in this browser.');
  });

  // Project menu. The same four words as the desktop: New, Open, Save, Save as.
  $('btn-new').addEventListener('click', async () => {
    if (state.scenes.length && !confirm('Discard the current scenes?')) return;
    state.projectName = null;
    await loadSampleProject(true);
  });

  $('btn-save').addEventListener('click', async () => {
    // Save just saves once the report has a name; only the first press asks.
    const name = state.projectName || prompt('Name for this report:', 'my-report');
    if (!name) return;
    state.projectName = name;
    await storage.putProject(name, serialise());
    toast(`Saved "${name}" in this browser.`);
  });

  $('btn-saveas').addEventListener('click', async () => {
    const name = prompt('Save a copy as:', state.projectName || 'my-report');
    if (!name) return;
    state.projectName = name;
    await storage.putProject(name, serialise());

    // And a file too, since "save as" in a browser means a download.
    const blob = new Blob([JSON.stringify(serialise(), null, 2)], { type: 'application/json' });
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = `${name}.json`;
    link.click();
    setTimeout(() => URL.revokeObjectURL(link.href), 5000);
    toast(`Saved "${name}" and downloaded a copy.`);
  });

  $('btn-open').addEventListener('click', async () => {
    await fillProjectList();
    $('projects-dialog').showModal();
  });
  $('projects-close').addEventListener('click', () => $('projects-dialog').close());

  $('btn-import-project').addEventListener('click', () => $('project-file').click());
  $('project-file').addEventListener('change', async (event) => {
    const file = event.target.files[0];
    event.target.value = '';
    if (!file) return;
    try {
      await deserialise(JSON.parse(await file.text()), true);
      // The opened file is what Save now writes over - under its own name.
      // Keeping the previous name here made Save quietly overwrite whatever
      // report had been open before.
      state.projectName = file.name.replace(/\.json$/i, '');
      $('projects-dialog').close();
      toast(`Loaded "${state.projectName}".`);
    } catch (error) {
      toast('That file is not a valid project.');
    }
  });
}

/** Fill the saved projects dialog. */
async function fillProjectList() {
  const list = $('project-list');
  list.innerHTML = '';

  const projects = await storage.listProjects();
  if (projects.length === 0) {
    const hint = document.createElement('div');
    hint.className = 'empty-hint';
    hint.textContent = 'Nothing saved yet.';
    list.appendChild(hint);
    return;
  }

  for (const project of projects) {
    const row = document.createElement('div');
    row.className = 'sound-row';

    // The whole row opens it, the way the desktop's recent list works.
    const name = document.createElement('div');
    name.className = 'name';
    name.textContent = `${project.name} - ${project.data.scenes.length} scene(s)`;
    row.addEventListener('click', async () => {
      state.projectName = project.name;
      await deserialise(project.data, true);
      $('projects-dialog').close();
      toast(`Loaded "${project.name}".`);
    });

    const remove = document.createElement('button');
    remove.className = 'danger';
    remove.textContent = 'x';
    remove.title = 'Forget this saved report';
    remove.addEventListener('click', async (event) => {
      event.stopPropagation();
      await storage.deleteProject(project.name);
      if (state.projectName === project.name) state.projectName = null;
      fillProjectList();
    });

    row.append(name, remove);
    list.appendChild(row);
  }
}

// --------------------------------------------------------------------- start

// ------------------------------------------------------------------- shutter

const SHUTTER_SETTLE_MS = 900;

// The booth's own sounds, fetched once at startup and held ready. Creating a
// fresh <audio> at the moment of the pull meant the very first rattle arrived
// only after a round trip to the network - or not at all.
const boothSounds = new Map();

function preloadBoothSounds() {
  for (const name of ['ShutterRise', 'ShutterDrop']) {
    const entry = state.manifest.sounds.default.find((item) => item.name === name);
    if (!entry) continue;
    const audio = new Audio(entry.url);
    audio.preload = 'auto';
    boothSounds.set(name, audio);
  }
}

/** Play one of the booth sounds outside the timeline, like the shutter. */
function playEffect(fileName) {
  const audio = boothSounds.get(fileName);
  if (!audio) return;
  audio.currentTime = 0;
  // The shutter counts as an effect, so its sliders reach it too.
  audio.volume = Math.min(1, 0.9 * state.volumes.master * state.volumes.effects);
  audio.play().catch(() => {});  // a browser may refuse before any gesture
}

/** Drop the shutter over the booth. */
function closeShutter(withSound = true) {
  const booth = $('booth');
  booth.classList.remove('opening');
  booth.classList.add('closed');
  if (withSound) playEffect('ShutterDrop');
}

/** Lift it again. */
function openShutter(withSound = true) {
  const booth = $('booth');
  booth.classList.add('opening');
  booth.classList.remove('closed');
  if (withSound) playEffect('ShutterRise');
}

let revealTimer = null;

/** Drop it and lift it, to show something new behind. */
function revealBehindShutter() {
  closeShutter();
  clearTimeout(revealTimer);
  revealTimer = setTimeout(() => {
    // Left alone if somebody has pulled the lever meanwhile: their word on
    // the shutter beats the flourish's.
    if ($('booth').classList.contains('closed')) openShutter();
  }, 620);
}

/** The handle in the corner: whichever way the shutter is, send it the other. */
function pullLever() {
  if ($('booth').classList.contains('closed')) {
    openShutter();
  } else {
    closeShutter();
  }
}

/**
 * The booth's first lift of the visit.
 *
 * It opens by itself, the way the desktop editor does - the shutter going up
 * is how the editor says hello, and holding it shut until the visitor clicked
 * something made the page look stuck. The rattle comes with it whenever the
 * browser allows sound: it is asked for by playing the very sound the lift
 * makes, which a return visit usually permits. Refused - a browser will not
 * make noise before it has been touched - the shutter still goes up, only
 * quietly.
 */
async function firstOpen() {
  // The visitor got to the lever before the timer did; the booth is theirs.
  if (!$('booth').classList.contains('closed')) return;

  const rise = boothSounds.get('ShutterRise');
  if (!rise) {
    openShutter();
    return;
  }

  try {
    rise.volume = Math.min(1, 0.9 * state.volumes.master * state.volumes.effects);
    rise.currentTime = 0;
    await rise.play();
    if (!$('booth').classList.contains('closed')) {
      // Opened by hand while the probe was in flight; one rattle is plenty.
      rise.pause();
      rise.currentTime = 0;
      return;
    }
    openShutter(false);   // its sound is already playing
  } catch (error) {
    // Sound is not allowed yet. The booth opens anyway - silence is better
    // than a shutter that will not budge.
    if ($('booth').classList.contains('closed')) openShutter(false);
  }
}

/** Load the booth sprites the stage draws on top of a scene. */
async function loadSprites() {
  const wanted = {
    button: 'assets/images/sprites/NextButton.png',
    cursor: 'assets/images/sprites/CursorHand.png',
  };

  await Promise.all(Object.entries(wanted).map(([key, url]) => new Promise((resolve) => {
    const image = new Image();
    image.onload = () => { sprites[key] = image; resolve(); };
    image.onerror = () => resolve();  // the stage works without them
    image.src = url;
  })));

}

/** Load the manifest and font, then restore the previous session. */
async function boot() {
  // Nothing here depends on anything else, so it all goes to the network at
  // once. Fetched one after another, first paint waited on the whole
  // waterfall - the "lags a moment on load" the live site had.
  const manifestJob = fetch('assets/manifest.json').then((response) => response.json());
  const spritesJob = loadSprites();
  const fontsJob = document.fonts.load('40px PixelPlay')
    .then(() => document.fonts.ready)
    .catch((error) => console.warn('The pixel font did not load', error));
  const draftJob = storage.available
    ? storage.loadDraft().catch(() => null)
    : Promise.resolve(null);

  state.manifest = await manifestJob;

  // Personal artwork is never published, so this file only exists when you are
  // running your own copy - and it is only looked for there. Asking the public
  // site for it just put a red 404 in every visitor's console.
  const athome = ['localhost', '127.0.0.1', ''].includes(location.hostname);
  if (athome) {
    try {
      const local = await fetch('assets/manifest.local.json');
      if (local.ok) {
        const extra = await local.json();
        state.manifest.images.custom = extra.images?.custom || [];
        state.manifest.sounds.custom = extra.sounds?.custom || [];
      }
    } catch (error) {
      /* running from disk: nothing to merge */
    }
  }

  preloadBoothSounds();
  await Promise.all([spritesJob, fontsJob]);

  player = new Player($('stage'), bank);
  window.__player = player;  // handy from the console, and for the tests
  wire();

  // The sliders as they were left last visit.
  try {
    const kept = JSON.parse(localStorage.getItem(VOLUME_STORE) || '{}');
    for (const kind of ['master', 'music', 'effects']) {
      const value = Number(kept[kind]);
      if (Number.isFinite(value)) state.volumes[kind] = Math.min(1, Math.max(0, value));
    }
  } catch (error) {
    /* nothing remembered */
  }
  for (const kind of ['master', 'music', 'effects']) {
    $(`vol-${kind}`).value = String(Math.round(state.volumes[kind] * 100));
    $(`vol-${kind}-pct`).textContent = `${Math.round(state.volumes[kind] * 100)}%`;
    player.setVolume(kind, state.volumes[kind]);
  }

  // The booth starts shut and opens once everything has finished loading, so
  // the shutter is not competing with the slowest part of startup.
  closeShutter(false);

  const draft = await draftJob;
  if (draft && draft.scenes && draft.scenes.length) {
    await deserialise(draft);
    toast('Picked up where you left off.');
  } else {
    await loadSampleProject(false);
  }

  await updateStorageNote();

  // Everything is loaded and drawn; now the shutter has the screen to itself.
  setTimeout(() => { firstOpen().catch(() => openShutter()); }, SHUTTER_SETTLE_MS);

  // Decode the soundtrack while nothing else is happening, so the first PLAY
  // starts on the beat instead of after a second of silent decoding.
  const idle = window.requestIdleCallback || ((task) => setTimeout(task, 1500));
  idle(() => { preloadSounds().catch(() => {}); });
}

/**
 * The game's own ending, which is what New starts from and what a first visit
 * opens - so there is always something to press PLAY on. Never plays itself.
 */
async function loadSampleProject(announce) {
  try {
    const sample = await (await fetch('projects/default.json')).json();
    await deserialise(sample, announce);
  } catch (error) {
    await deserialise({ scenes: [] }, announce);
  }
  scheduleAutosave();
}

boot().catch((error) => {
  console.error(error);
  document.body.innerHTML = `<pre style="padding:24px;color:#d8d3c2">Could not start:\n${error}</pre>`;
});
