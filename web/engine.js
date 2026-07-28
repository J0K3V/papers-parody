/**
 * Playback engine.
 *
 * Draws the same frames the desktop renderer produces, only in real time on a
 * canvas: the picture wipes in from the right, the report is typed out letter
 * by letter and the soundtrack is scheduled underneath through the Web Audio
 * API. The very same timeline drives the recorder, so what you watch is what
 * gets saved.
 */

export const FRAME_WIDTH = 1280;
export const FRAME_HEIGHT = 720;
export const FPS = 30;

export const PICTURE_WIDTH = 480;
export const PICTURE_HEIGHT = 320;
export const PICTURE_LEFT = (FRAME_WIDTH - PICTURE_WIDTH) / 2;
export const PICTURE_TOP = Math.round(FRAME_HEIGHT / 3 - PICTURE_HEIGHT / 2);

// The report sits in a column centred on the frame, laid out inside it, which
// is how the briefing screens in the game are arranged.
export const FONT_SIZE = 40;
export const TEXT_BLOCK_WIDTH = 800;
export const TEXT_LEFT = (FRAME_WIDTH - TEXT_BLOCK_WIDTH) / 2;
export const TEXT_TOP = Math.floor((FRAME_HEIGHT * 2) / 3);
export const TEXT_MAX_WIDTH = TEXT_BLOCK_WIDTH;
export const ALIGNMENTS = ['left', 'center', 'right'];
export const FONT_ASCENT = 43; // measured from pixelplay.ttf at 40px
export const LINE_HEIGHT = 60;
export const FONT_FAMILY = 'PixelPlay';

// Pacing, in frames at 30 fps, matching the desktop renderer.
const WIPE_FRAMES = 20;
const PAUSE_FRAMES = Math.floor(FPS * 0.2);
const HOLD_FRAMES = Math.floor(FPS * 2.0);
const BLANK_FRAMES = Math.floor(FPS * 0.1);
const FINAL_FRAMES = Math.floor(FPS * 5.0);

export const TYPING_SPEEDS = { slow: 3, normal: 2, fast: 1 };
const LETTER_GAIN = 0.45; // the typing clicks sit under everything else

// The game never rolls on by itself: each screen waits on a NEXT button. Once
// the line is written the button appears and a hand comes up to press it.
export const ART_SCALE = 2;
export const BUTTON_TOP = 636;
export const BUTTON_LABEL = 'NEXT';
export const BUTTON_LABEL_SIZE = 24;

// The sprite is two states stacked: the idle bar on top, the lit one below.
// The word itself is not in the artwork, the game draws it on.
const BUTTON_IDLE_INK = '#7a8078';
const BUTTON_LIT_INK = '#222622';
const CURSOR_TRAVEL = 0.55;
const CURSOR_PRESS_AT = 0.9;

/** Sprites the stage needs, filled in by the app once they are loaded. */
export const sprites = { button: null, cursor: null };

/** Split a line so it fits the text column, using the canvas metrics. */
export function wrapText(ctx, text, maxWidth = TEXT_MAX_WIDTH) {
  if (!text) return [];
  const lines = [];
  let current = '';

  for (const word of text.split(' ')) {
    const candidate = current ? `${current} ${word}` : word;
    if (current && ctx.measureText(candidate).width > maxWidth) {
      lines.push(current);
      current = word;
    } else {
      current = candidate;
    }
  }

  lines.push(current);
  return lines;
}

/** Redraw a picture into the 480x320 box the renderer uses. */
export function fitPicture(image, mode = 'contain') {
  const canvas = document.createElement('canvas');
  canvas.width = PICTURE_WIDTH;
  canvas.height = PICTURE_HEIGHT;
  const ctx = canvas.getContext('2d');

  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, PICTURE_WIDTH, PICTURE_HEIGHT);

  // The game's art is 240x160 and gets blown up to fill the box. Smoothing
  // turns those hard pixels to mush, so only smooth when shrinking.
  const growing = image.width < PICTURE_WIDTH || image.height < PICTURE_HEIGHT;
  ctx.imageSmoothingEnabled = !growing;

  if (mode === 'stretch') {
    ctx.drawImage(image, 0, 0, PICTURE_WIDTH, PICTURE_HEIGHT);
  } else {
    const scale = Math.min(PICTURE_WIDTH / image.width, PICTURE_HEIGHT / image.height);
    const width = Math.round(image.width * scale);
    const height = Math.round(image.height * scale);
    ctx.drawImage(
      image,
      Math.round((PICTURE_WIDTH - width) / 2),
      Math.round((PICTURE_HEIGHT - height) / 2),
      width,
      height
    );
  }

  return canvas;
}

/**
 * Work out when everything happens.
 *
 * Returns the per scene frame offsets plus the list of sound cues, so both the
 * player and the recorder can share one description of the video.
 */
export function buildTimeline(scenes, typingSpeed = 'normal', showButton = true) {
  const step = TYPING_SPEEDS[typingSpeed] || 2;
  const clickStep = step + 1;

  const entries = [];
  const cues = [];
  let frame = 0;

  scenes.forEach((scene, index) => {
    const isLast = index === scenes.length - 1;
    const text = String(scene.text || '').replace(/\n/g, ' ').trim();
    const start = frame;

    const wipeEnd = WIPE_FRAMES + 1;
    const pauseEnd = wipeEnd + PAUSE_FRAMES;
    const typingFrames = text.length * step + 1;
    const typeEnd = pauseEnd + typingFrames;

    for (let elapsed = 0; elapsed < typingFrames; elapsed += 1) {
      if (elapsed % clickStep === 0) {
        cues.push({ kind: 'letter', frame: start + pauseEnd + elapsed });
      }
    }

    const holdEnd = typeEnd + (isLast ? FINAL_FRAMES : HOLD_FRAMES);
    const end = isLast ? holdEnd : holdEnd + BLANK_FRAMES;

    if (!isLast) {
      cues.push({ kind: 'next', frame: start + end });
    }

    // The scene's own sound, placed once the scene's shape is known so it can
    // be hung off the typing or the end rather than only the start.
    if (scene.sound) {
      let at = start;
      if (scene.soundAt === 'typed') at = start + typeEnd;
      else if (scene.soundAt === 'end') at = start + holdEnd;
      else if (scene.soundAt === 'delayed') at = start + Math.round((scene.soundDelay || 0) * FPS);

      // A delay longer than the scene would otherwise land in the middle of the
      // next one, over a picture it was never meant for.
      cues.push({ kind: 'scene', frame: Math.min(at, start + end - 1), sound: scene.sound });
    }

    const align = ALIGNMENTS.includes(scene.align) ? scene.align : 'left';
    entries.push({
      index, scene, text, align, music: scene.music || null,
      start, wipeEnd, pauseEnd, typeEnd, holdEnd, end, step, isLast,
    });
    frame = start + end;
  });

  return { entries, cues, showButton, totalFrames: frame, durationSeconds: frame / FPS };
}

/**
 * Stretches of the video that share one piece of music.
 *
 * Scenes in a row asking for the same track are merged, so the score plays
 * straight through instead of restarting every time a scene changes.
 */
export function musicRuns(timeline, fallbackTracks) {
  const runs = [];

  for (const entry of timeline.entries) {
    const tracks = entry.music ? [entry.music] : [...fallbackTracks];
    const end = entry.start + entry.end;
    const previous = runs[runs.length - 1];

    if (previous && previous.tracks.join('|') === tracks.join('|')) {
      previous.end = end;
    } else {
      runs.push({ tracks, start: entry.start, end });
    }
  }

  return runs;
}

/** Draw one frame of the timeline onto a 2d context. */
export function drawFrame(ctx, timeline, frameIndex, pictures, ui = {}) {
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, FRAME_WIDTH, FRAME_HEIGHT);

  const entry = timeline.entries.find((item) => frameIndex < item.start + item.end)
    || timeline.entries[timeline.entries.length - 1];
  if (!entry) return;

  const local = Math.max(0, frameIndex - entry.start);
  const picture = pictures[entry.index];

  // The picture wipes in from the right.
  let hiddenFraction = 0;
  if (local < entry.wipeEnd) {
    hiddenFraction = 1 - local / WIPE_FRAMES;
  }
  const hidden = Math.min(PICTURE_WIDTH, Math.max(0, Math.round(hiddenFraction * PICTURE_WIDTH)));

  if (picture && hidden < PICTURE_WIDTH) {
    const visible = PICTURE_WIDTH - hidden;
    ctx.drawImage(
      picture,
      hidden, 0, visible, PICTURE_HEIGHT,
      PICTURE_LEFT + hidden, PICTURE_TOP, visible, PICTURE_HEIGHT
    );
  }

  // Then the report is typed out.
  let visibleText = '';
  if (local >= entry.pauseEnd && local < entry.typeEnd) {
    const elapsed = local - entry.pauseEnd;
    visibleText = entry.text.slice(0, Math.floor(elapsed / entry.step));
  } else if (local >= entry.typeEnd && local < entry.holdEnd) {
    visibleText = entry.text;
  }

  drawNextButton(ctx, timeline, entry, local, ui);

  if (visibleText) {
    ctx.font = `${FONT_SIZE}px "${FONT_FAMILY}", monospace`;
    ctx.textBaseline = 'alphabetic';
    ctx.fillStyle = '#fff';

    const align = entry.align || 'left';
    wrapText(ctx, visibleText).forEach((line, row) => {
      let x = TEXT_LEFT;
      if (align !== 'left') {
        const slack = TEXT_BLOCK_WIDTH - ctx.measureText(line).width;
        x += align === 'center' ? slack / 2 : slack;
      }
      ctx.fillText(line, x, TEXT_TOP + FONT_ASCENT + row * LINE_HEIGHT);
    });
  }
}

/** Where the NEXT button sits on the frame, or null if its sprite is missing. */
export function buttonBox() {
  const bar = sprites.button;
  if (!bar) return null;

  const width = bar.width * ART_SCALE;
  const height = (bar.height / 2) * ART_SCALE;
  return { x: Math.round((FRAME_WIDTH - width) / 2), y: BUTTON_TOP, width, height };
}

/**
 * Paint the NEXT button, and the hand reaching for it.
 *
 * In movie mode a hand comes up and presses it on its own. When the viewer is
 * doing the clicking there is no hand: the button just sits there and lights
 * up under the real cursor, the way it does in the game.
 */
export function drawNextButton(ctx, timeline, entry, local, ui = {}) {
  const bar = sprites.button;
  if (!bar || !timeline.showButton) return;

  if (local < entry.typeEnd || local >= entry.holdEnd) return;

  const span = Math.max(1, entry.holdEnd - entry.typeEnd);
  const through = (local - entry.typeEnd) / span;
  const waiting = Boolean(ui.gated);
  const pressed = waiting ? Boolean(ui.hover) : through >= CURSOR_PRESS_AT;

  // Two states stacked in one sheet: idle on top, lit underneath.
  const half = bar.height / 2;
  const width = bar.width * ART_SCALE;
  const height = half * ART_SCALE;
  const left = Math.round((FRAME_WIDTH - width) / 2);

  ctx.imageSmoothingEnabled = false;
  ctx.drawImage(bar, 0, pressed ? half : 0, bar.width, half,
                left, BUTTON_TOP, width, height);

  ctx.font = `${BUTTON_LABEL_SIZE}px "${FONT_FAMILY}", monospace`;
  ctx.textBaseline = 'middle';
  ctx.fillStyle = pressed ? BUTTON_LIT_INK : BUTTON_IDLE_INK;
  const label = ctx.measureText(BUTTON_LABEL).width;
  ctx.fillText(BUTTON_LABEL, left + (width - label) / 2, BUTTON_TOP + height / 2);
  ctx.textBaseline = 'alphabetic';

  const hand = sprites.cursor;
  if (hand && !waiting && through >= CURSOR_TRAVEL) {
    const reach = Math.min(1, (through - CURSOR_TRAVEL) / Math.max(1e-6, 1 - CURSOR_TRAVEL));
    const eased = 1 - (1 - reach) ** 2;

    const handWidth = hand.width * ART_SCALE;
    const handHeight = hand.height * ART_SCALE;
    const restX = left + width / 2 - handWidth / 4;
    const restY = BUTTON_TOP + height - handHeight / 4;

    ctx.drawImage(hand, restX, FRAME_HEIGHT + (restY - FRAME_HEIGHT) * eased,
                  handWidth, handHeight);
  }
}

/** Loads and decodes audio once, then hands out buffers by url. */
export class SoundBank {
  constructor() {
    this.context = null;
    this.buffers = new Map();
    this.pending = new Map();
  }

  /** Create the audio context on the first user gesture. */
  ensureContext() {
    if (!this.context) {
      const Ctor = window.AudioContext || window.webkitAudioContext;
      this.context = new Ctor();
    }
    if (this.context.state === 'suspended') {
      // Refused until the page has been touched; decoding works regardless.
      this.context.resume().catch(() => {});
    }
    return this.context;
  }

  /** Decode a sound from a url or a blob, caching the result. */
  async load(key, source) {
    if (this.buffers.has(key)) return this.buffers.get(key);

    // Two callers asking for the same sound at once - the idle preloader and
    // an eager PLAY, say - share one decode instead of racing through two.
    if (this.pending.has(key)) return this.pending.get(key);

    const job = (async () => {
      const context = this.ensureContext();
      const data = source instanceof Blob
        ? await source.arrayBuffer()
        : await (await fetch(source)).arrayBuffer();

      const buffer = await context.decodeAudioData(data);
      this.buffers.set(key, buffer);
      return buffer;
    })();

    this.pending.set(key, job);
    try {
      return await job;
    } finally {
      this.pending.delete(key);
    }
  }

  get(key) {
    return this.buffers.get(key);
  }
}

/**
 * Plays a timeline in real time, and optionally records it.
 *
 * Recording happens while the video plays, so it takes exactly as long as the
 * finished clip: there is no way to render faster than real time in a browser
 * without dropping the live audio.
 */
export class Player {
  constructor(canvas, bank) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.bank = bank;

    this.playing = false;
    this.paused = false;
    this.recording = false;
    this.waiting = false;   //: halted on a NEXT button, expecting a click
    this.hover = false;     //: the real cursor is over that button

    this.onFrame = null;
    this.onFinish = null;
    this.onWait = null;
    this.onRewind = null;

    this._sources = [];
    this._recorder = null;
    this._chunks = [];
    this._master = null;
    this._musicBus = null;
    this._fxBus = null;
    this._tapBus = null;
    this._origin = 0;
    this._frame = 0;
    this._cleared = new Set();

    // The three sliders beside the booth. They sit on the monitoring path
    // only: a recording is mixed at full level whatever they say, the same as
    // the desktop's RENDER.
    this.volumes = { master: 1, music: 1, effects: 1 };

    // Music is held apart from the cues. It is never scheduled to stop, so
    // waiting on a button does not cut it off: it simply keeps looping until
    // a scene asks for something else.
    this._musicNodes = [];
    this._musicKey = null;
  }

  /** Render a still frame, used to preview a scene while editing. */
  showFrame(timeline, frameIndex, pictures) {
    drawFrame(this.ctx, timeline, frameIndex, pictures);
  }

  /** Start playback from the top. Pass `record: true` to capture a webm. */
  async start(timeline, pictures, sounds, options = {}) {
    if (this.playing) return;

    const { record = false, packName = 'bad ending', movieMode = true, fromFrame = 0 } = options;
    const context = this.bank.ensureContext();

    this.timeline = timeline;
    this.pictures = pictures;
    this.pack = sounds.packs[packName] || sounds.packs.standard;
    this.movieMode = record ? true : movieMode;  // a file cannot wait for clicks
    this.playing = true;
    this.recording = record;
    this.waiting = false;
    this._cleared = new Set();
    this._chunks = [];

    // What the speakers get: everything through its own bus, each with its
    // slider, into the master. The recording tap hangs off the sources before
    // any of these gains, so the file comes out at full level regardless.
    this._master = context.createGain();
    this._master.gain.value = this.volumes.master;
    this._master.connect(context.destination);

    this._musicBus = context.createGain();
    this._musicBus.gain.value = this.volumes.music;
    this._musicBus.connect(this._master);

    this._fxBus = context.createGain();
    this._fxBus.gain.value = this.volumes.effects;
    this._fxBus.connect(this._master);

    const tap = record ? context.createMediaStreamDestination() : null;
    this._tapBus = null;
    if (tap) {
      this._tapBus = context.createGain();
      this._tapBus.connect(tap);
    }

    if (record) {
      const stream = new MediaStream([
        ...this.canvas.captureStream(FPS).getVideoTracks(),
        ...tap.stream.getAudioTracks(),
      ]);
      const mimeType = [
        'video/webm;codecs=vp9,opus',
        'video/webm;codecs=vp8,opus',
        'video/webm',
      ].find((type) => MediaRecorder.isTypeSupported(type));

      this._recorder = new MediaRecorder(stream, mimeType ? { mimeType } : undefined);
      this._recorder.ondataavailable = (event) => {
        if (event.data.size > 0) this._chunks.push(event.data);
      };
      this._recorder.start();
    }

    // Every scene before the starting point counts as already seen, so the
    // report does not stop on buttons you have skipped past.
    this.timeline.entries.forEach((entry) => {
      if (entry.start + entry.end <= fromFrame) this._cleared.add(entry.index);
    });

    this._schedule(fromFrame);
    requestAnimationFrame(() => this._tick());
  }

  /**
   * Lay out the audio from a given frame onwards.
   *
   * Everything is scheduled against a virtual origin, so resuming after a
   * halt is the same operation as starting: only the frame differs.
   */
  _schedule(fromFrame) {
    const context = this.bank.ensureContext();
    this._silence();

    this._origin = context.currentTime + 0.08 - fromFrame / FPS;
    const at = (frame) => this._origin + frame / FPS;

    for (const cue of this.timeline.cues) {
      if (cue.frame < fromFrame) continue;

      let key = null;
      let level = 1;

      if (cue.kind === 'letter') {
        key = this.pack.letter[Math.floor(Math.random() * this.pack.letter.length)];
        level = LETTER_GAIN;
      } else if (cue.kind === 'next') {
        // Outside movie mode nobody turns the page but the viewer: the click
        // itself makes the sound, in clickNext. Scheduling it here as well
        // played it twice - an 80ms flam on every button press.
        if (!this.movieMode) continue;
        key = this.pack.next[0];
      } else if (cue.kind === 'scene') {
        key = cue.sound;
      }

      const buffer = key ? this.bank.get(key) : null;
      if (!buffer) continue;

      const source = context.createBufferSource();
      source.buffer = buffer;

      const gain = context.createGain();
      gain.gain.value = level;
      source.connect(gain);
      gain.connect(this._fxBus);
      if (this._tapBus) gain.connect(this._tapBus);
      source.start(at(cue.frame));
      this._sources.push({ source, frame: cue.frame });
    }
  }

  /**
   * Stop the cues, leaving the music running underneath.
   *
   * Given a frame, only cues beyond it are stopped: parking on a NEXT button
   * must not cut off a sound that has just rightly begun - the typing-done
   * sound the desktop editor lets ring through the wait.
   */
  _silence(afterFrame = -Infinity) {
    const keep = [];
    for (const entry of this._sources) {
      if (entry.frame <= afterFrame) {
        keep.push(entry);
        continue;
      }
      try {
        entry.source.stop();
      } catch (error) {
        /* already stopped */
      }
    }
    this._sources = keep;
  }

  /**
   * Keep the right music playing for a given frame.
   *
   * A run that is already playing is left completely alone, which is what
   * makes it carry across scenes and across a wait on the NEXT button. Only a
   * genuine change of track starts anything, and then the old one fades out.
   */
  _music(frame) {
    const runs = musicRuns(this.timeline, this.pack.music);
    const run = runs.find((item) => frame < item.end) || runs[runs.length - 1];
    if (!run) return;

    const key = run.tracks.join('|');
    if (key === this._musicKey) return;  // already playing, do not touch it

    const context = this.bank.ensureContext();
    const now = context.currentTime;
    const fade = this._musicKey === null ? 0 : 0.4;

    for (const node of this._musicNodes) {
      try {
        node.gain.gain.cancelScheduledValues(now);
        node.gain.gain.setValueAtTime(node.gain.gain.value, now);
        node.gain.gain.linearRampToValueAtTime(0, now + fade);
        node.source.stop(now + fade + 0.05);
      } catch (error) {
        /* already gone */
      }
    }
    this._musicNodes = [];
    this._musicKey = key;

    // Pick the track up where this run has already got to, so a resume does
    // not rewind it.
    const into = Math.max(0, (frame - run.start) / FPS);

    for (const name of run.tracks) {
      const buffer = this.bank.get(name);
      if (!buffer) continue;

      const source = context.createBufferSource();
      source.buffer = buffer;
      source.loop = true;   // if it runs out it simply goes round again

      const gain = context.createGain();
      gain.gain.setValueAtTime(fade ? 0 : 1, now);
      if (fade) gain.gain.linearRampToValueAtTime(1, now + fade);

      source.connect(gain);
      gain.connect(this._musicBus);
      if (this._tapBus) gain.connect(this._tapBus);
      source.start(now, into % buffer.duration);
      this._musicNodes.push({ source, gain });
    }
  }

  /** Stop the music as well, for good. */
  _stopMusic() {
    for (const node of this._musicNodes) {
      try {
        node.source.stop();
      } catch (error) {
        /* already stopped */
      }
    }
    this._musicNodes = [];
    this._musicKey = null;
  }

  /**
   * Take up an edited report without restarting anything that has not
   * actually changed - the desktop editor's rule for typing while it plays.
   * The picture follows the new timing at once; the music is compared on the
   * next tick and only a genuine change of track touches it. A recording is
   * left entirely alone: it captures the report as it stood at RECORD.
   */
  refresh(timeline, pictures) {
    if (!this.playing || this.recording) return;

    this.timeline = timeline;
    this.pictures = pictures;
    this._frame = Math.max(0, Math.min(this._frame, timeline.totalFrames - 1));

    // Parked on a button the cues are already silent; clickNext lays them out
    // again against whatever the report says by then.
    if (this.waiting) return;

    this._schedule(this._frame);
    if (this.paused) this.bank.ensureContext().suspend();
  }

  /**
   * Move one of the three sliders. Lands mid-note when something is playing;
   * remembered for the next run when nothing is.
   */
  setVolume(kind, value) {
    this.volumes[kind] = value;
    if (kind === 'master' && this._master) this._master.gain.value = value;
    if (kind === 'music' && this._musicBus) this._musicBus.gain.value = value;
    if (kind === 'effects' && this._fxBus) this._fxBus.gain.value = value;
  }

  /**
   * Hold everything where it is.
   *
   * Suspending the audio context freezes its clock, and the picture is driven
   * off that same clock - so sound and image stop together and start together,
   * with nothing to put back in step afterwards.
   */
  togglePause() {
    if (!this.playing || this.recording) return this.paused;

    const context = this.bank.ensureContext();
    this.paused = !this.paused;
    if (this.paused) {
      context.suspend();
    } else {
      context.resume();
      requestAnimationFrame(() => this._tick());
    }
    return this.paused;
  }

  /** Draw whichever frame is due, halting on a NEXT button when asked to. */
  _tick() {
    if (!this.playing) return;
    if (this.paused) return;   // the clock is frozen; nothing to draw

    if (this.waiting) {
      drawFrame(this.ctx, this.timeline, this._frame, this.pictures,
                { gated: true, hover: this.hover });
      requestAnimationFrame(() => this._tick());
      return;
    }

    const context = this.bank.ensureContext();
    const frame = Math.floor(Math.max(0, context.currentTime - this._origin) * FPS);

    if (frame >= this.timeline.totalFrames) {
      drawFrame(this.ctx, this.timeline, this.timeline.totalFrames - 1, this.pictures);
      this.stop(true);
      return;
    }

    // Outside movie mode the report waits on every button, like the game does.
    // In movie mode it rolls on by itself but still stops on the closing one,
    // which is what takes you back to the start. A recording stops for nobody:
    // like the desktop's RENDER it runs to the end and saves itself - parked
    // on the closing button it would sit there for ever with the recorder on.
    const entry = this.timeline.entries.find((item) => frame < item.start + item.end);
    const gated = entry && !this.recording && (!this.movieMode || entry.isLast);
    if (
      gated && this.timeline.showButton && !this._cleared.has(entry.index)
      && frame >= entry.start + entry.typeEnd
    ) {
      this._frame = entry.start + entry.typeEnd;
      this.waiting = true;
      this._silence(this._frame);
      if (this.onWait) this.onWait(entry);
      requestAnimationFrame(() => this._tick());
      return;
    }

    this._frame = frame;
    this._music(frame);
    drawFrame(this.ctx, this.timeline, frame, this.pictures);
    if (this.onFrame) this.onFrame(frame, this.timeline.totalFrames);
    requestAnimationFrame(() => this._tick());
  }

  /** True while the report is parked on a button the viewer has to press. */
  get gateBox() {
    return this.waiting ? buttonBox() : null;
  }

  /** Press the button the report is waiting on and carry on to the next scene. */
  clickNext() {
    if (!this.waiting) return false;

    const entry = this.timeline.entries.find((item) => this._frame < item.start + item.end);
    if (!entry) return false;

    this._cleared.add(entry.index);
    this.waiting = false;
    this.hover = false;

    // The page turn belongs to the click, so it fires here rather than on a
    // clock - and before the closing scene winds things back, so the very
    // last press is heard too, the way the desktop editor plays it.
    const context = this.bank.ensureContext();
    const buffer = this.bank.get(this.pack.next[0]);
    if (buffer) {
      const source = context.createBufferSource();
      source.buffer = buffer;
      source.connect(this._fxBus);
      if (this._tapBus) source.connect(this._tapBus);
      source.start(context.currentTime);
    }

    // Pressing NEXT on the closing scene winds the whole thing back to the
    // start and leaves it there, so you can watch it again from the top.
    if (entry.isLast) {
      this.stop(true);
      if (this.onRewind) this.onRewind();
      return true;
    }

    const resumeAt = entry.start + entry.end;
    if (resumeAt >= this.timeline.totalFrames) {
      this.stop(true);
      return true;
    }

    this._schedule(resumeAt);
    return true;
  }

  /**
   * Jump the picture to a frame while it is playing.
   *
   * The music is left alone unless the new place belongs to a different piece,
   * so picking through the scenes does not keep chopping it up. _schedule lays
   * the effects out again from the new spot; _music only acts on a real change
   * of track.
   */
  seek(frame) {
    if (!this.playing || this.recording) return;

    const at = Math.max(0, Math.min(frame, this.timeline.totalFrames - 1));
    this.waiting = false;
    this.hover = false;

    // Anything before the new spot counts as already seen, so it does not
    // immediately halt on a button that was skipped over.
    this._cleared = new Set(
      this.timeline.entries.filter((e) => e.start + e.end <= at).map((e) => e.index)
    );

    this._schedule(at);

    // Paused, the clock must stay frozen: _schedule's ensureContext just
    // resumed it, which had audio running under a still picture. Freeze it
    // again and show the new frame; RESUME picks up exactly here. No new
    // animation frame is queued either way - the running loop is already
    // ticking, and starting another one per seek stacked them up until every
    // frame was drawn several times over.
    if (this.paused) {
      this.bank.ensureContext().suspend();
      this._frame = at;
      drawFrame(this.ctx, this.timeline, at, this.pictures);
    }
  }

  /** Stop playback, returning any recording through onFinish. */
  stop(finished = false) {
    if (!this.playing) return;

    this.playing = false;
    this.waiting = false;

    // A context left suspended would stay that way and the next PLAY would be
    // silent with a frozen picture.
    if (this.paused) {
      this.paused = false;
      this.bank.ensureContext().resume();
    }

    this._silence();
    this._stopMusic();

    if (this._recorder && this._recorder.state !== 'inactive') {
      this._recorder.onstop = () => {
        const blob = new Blob(this._chunks, { type: this._recorder.mimeType || 'video/webm' });
        this._recorder = null;
        if (this.onFinish) this.onFinish({ finished, blob });
      };
      this._recorder.stop();
      this.recording = false;
      return;
    }

    this.recording = false;
    if (this.onFinish) this.onFinish({ finished, blob: null });
  }
}
