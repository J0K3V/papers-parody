#include "timeline.h"

#include <algorithm>

namespace parody {

Timeline::Timeline(const std::vector<Scene>& scenes, const std::string& typing_speed,
                   bool show_button, bool interactive)
    : show_button_(show_button), interactive_(interactive) {
    step_ = layout::typing_step(typing_speed.c_str());
    click_step_ = step_ + 1;

    int frame = 0;
    for (size_t i = 0; i < scenes.size(); ++i) {
        const Scene& scene = scenes[i];
        const bool is_last = (i + 1 == scenes.size());
        const int start = frame;

        Entry entry;
        entry.index = int(i);
        entry.text = scene.text;
        entry.align = scene.align;
        entry.music = scene.music;
        entry.start = start;
        entry.is_last = is_last;

        entry.wipe_end = layout::WIPE_FRAMES + 1;
        entry.pause_end = entry.wipe_end + layout::PAUSE_FRAMES;

        const int typing_frames = int(scene.text.size()) * step_ + 1;
        entry.type_end = entry.pause_end + typing_frames;

        // Clicks land on a fixed frame grid rather than on every letter, which
        // is what the original does and why fast typing does not machine-gun.
        for (int elapsed = 0; elapsed < typing_frames; ++elapsed) {
            if (elapsed % click_step_ == 0) {
                cues_.push_back({Cue::Kind::Letter, start + entry.pause_end + elapsed, ""});
            }
        }

        entry.hold_end = entry.type_end + (is_last ? layout::FINAL_FRAMES : layout::HOLD_FRAMES);
        entry.end = is_last ? entry.hold_end : entry.hold_end + layout::BLANK_FRAMES;

        if (!is_last) {
            cues_.push_back({Cue::Kind::Next, start + entry.end, ""});
        }

        // The scene's own sound, placed once the scene's shape is known so it
        // can be hung off the typing or the end rather than only the start.
        if (!scene.sound.empty()) {
            int at = start;
            switch (scene.sound_at) {
                case SoundAt::Start: break;
                case SoundAt::Typed: at = start + entry.type_end; break;
                case SoundAt::End: at = start + entry.hold_end; break;
                case SoundAt::Delayed:
                    at = start + int(scene.sound_delay * layout::FPS);
                    break;
            }
            // A delay longer than the scene would otherwise land in the middle
            // of the next one, playing over a picture it was never meant for.
            at = std::min(at, start + entry.end - 1);
            cues_.push_back({Cue::Kind::SceneSound, at, scene.sound});
        }

        entries_.push_back(entry);
        frame = start + entry.end;
    }

    total_frames_ = frame;
}

const Entry* Timeline::entry_at(int frame) const {
    for (const Entry& entry : entries_) {
        if (frame < entry.start + entry.end) return &entry;
    }
    return entries_.empty() ? nullptr : &entries_.back();
}

FrameState Timeline::state_at(int frame) const {
    FrameState state;
    const Entry* entry = entry_at(frame);
    if (entry == nullptr) return state;

    state.entry = entry;
    state.align = entry->align;

    const int local = std::max(0, frame - entry->start);

    state.hidden = local < entry->wipe_end
                       ? std::max(0.0f, 1.0f - float(local) / layout::WIPE_FRAMES)
                       : 0.0f;

    if (local >= entry->pause_end && local < entry->type_end) {
        const size_t shown = size_t((local - entry->pause_end) / step_);
        state.text = entry->text.substr(0, std::min(shown, entry->text.size()));
    } else if (local >= entry->type_end && local < entry->hold_end) {
        state.text = entry->text;
    }

    // Once the line is written the screen waits on NEXT, and a hand comes up
    // to press it. The closing scene only gets one when somebody can click it.
    if (show_button_ && local >= entry->type_end && local < entry->hold_end &&
        (!entry->is_last || interactive_)) {
        const int span = std::max(1, entry->hold_end - entry->type_end);
        const float through = float(local - entry->type_end) / span;

        state.button = "idle";
        if (through >= layout::CURSOR_TRAVEL) {
            const float reach = (through - layout::CURSOR_TRAVEL) /
                                std::max(1e-6f, 1.0f - layout::CURSOR_TRAVEL);
            state.cursor = std::min(1.0f, reach);
            if (through >= layout::CURSOR_PRESS_AT) state.button = "press";
        }
    }

    return state;
}

std::vector<MusicRun> Timeline::music_runs(const std::vector<std::string>& fallback) const {
    std::vector<MusicRun> runs;

    for (const Entry& entry : entries_) {
        std::vector<std::string> tracks =
            entry.music.empty() ? fallback : std::vector<std::string>{entry.music};
        const int end = entry.start + entry.end;

        if (!runs.empty() && runs.back().tracks == tracks) {
            runs.back().end = end;
        } else {
            runs.push_back({tracks, entry.start, end});
        }
    }

    return runs;
}

}  // namespace parody
