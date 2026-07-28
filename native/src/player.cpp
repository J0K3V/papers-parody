#include "player.h"

#include <algorithm>
#include <cstring>

namespace parody {
namespace {

// raylib's stream callback carries no pointer of its own, so the player that
// owns the running stream is kept here. There is only ever one. Atomic because
// the audio thread reads it while the main thread is clearing it.
std::atomic<Player*> live_player{nullptr};

void feed_stream(void* buffer, unsigned int frames) {
    Player* player = live_player.load(std::memory_order_acquire);
    if (player != nullptr) {
        player->fill(buffer, frames);
    } else {
        std::memset(buffer, 0, size_t(frames) * CHANNELS * sizeof(float));
    }
}

}  // namespace

Player::~Player() {
    stop_stream();
    forget_effects();
}

bool Player::prepare(Mixer& mixer, const Timeline& timeline, const Project& project) {
    stop();
    forget_effects();

    timeline_ = &timeline;
    sounds_dir_ = mixer.sounds_dir();
    movie_mode_ = project.movie_mode;
    if (timeline.total_frames() == 0) return false;

    const auto& packs = sound_packs();
    auto found = packs.find(project.soundtrack);
    pack_ = found != packs.end() ? found->second : packs.at(default_pack());

    build_runs(mixer, timeline);
    current_run_ = -1;
    music_ = nullptr;
    return true;
}

void Player::build_runs(Mixer& mixer, const Timeline& timeline) {
    // Each stretch of music at its own full length. The effects are not mixed
    // in: they are fired one at a time as the picture reaches them, so that the
    // music can run on its own clock and never has to restart.
    run_music_.clear();
    run_start_.clear();
    run_tracks_.clear();

    for (const MusicRun& run : timeline.music_runs(pack_.music)) {
        auto whole = std::make_shared<Clip>();
        for (const std::string& name : run.tracks) {
            const Clip* piece = mixer.load(name);
            if (piece == nullptr || piece->empty()) continue;
            whole->samples.insert(whole->samples.end(), piece->samples.begin(),
                                  piece->samples.end());
        }
        // A track that will not load falls back to the pack's own, so a report
        // is never left in silence.
        if (whole->empty()) {
            for (const std::string& name : pack_.music) {
                const Clip* piece = mixer.load(name);
                if (piece != nullptr && !piece->empty()) {
                    whole->samples.insert(whole->samples.end(), piece->samples.begin(),
                                          piece->samples.end());
                }
            }
        }
        run_music_.push_back(std::move(whole));
        run_start_.push_back(run.start);
        run_tracks_.push_back(run.tracks);
    }
}

void Player::refresh(Mixer& mixer, const Timeline& timeline, const Project& project) {
    if (!playing_) {
        prepare(mixer, timeline, project);
        return;
    }

    // What is playing right now, so it can be left alone if the edit did not
    // touch it. Typing a line should not restart the music.
    const std::vector<std::string> was =
        current_run_ >= 0 && current_run_ < int(run_tracks_.size())
            ? run_tracks_[size_t(current_run_)]
            : std::vector<std::string>{};

    const auto& packs = sound_packs();
    auto found = packs.find(project.soundtrack);
    pack_ = found != packs.end() ? found->second : packs.at(default_pack());

    timeline_ = &timeline;
    build_runs(mixer, timeline);

    // The picture keeps its place; the timing around it may have moved.
    frame_ = std::clamp(frame_, 0, std::max(0, timeline.total_frames() - 1));
    fired_to_ = frame_;
    started_at_frame_ = frame_;
    started_at_time_ = GetTime();

    const int run = run_at(frame_);
    const std::vector<std::string> now =
        run >= 0 && run < int(run_tracks_.size()) ? run_tracks_[size_t(run)]
                                                  : std::vector<std::string>{};

    if (run != current_run_ || now != was) {
        switch_music(run, frame_);   // genuinely different music, so start it
    } else {
        current_run_ = run;   // same piece: the stream and its clip are left alone
    }
}

int Player::run_at(int frame) const {
    int found = run_start_.empty() ? -1 : 0;
    for (size_t i = 0; i < run_start_.size(); ++i) {
        if (frame >= run_start_[i]) found = int(i);
    }
    return found;
}

void Player::switch_music(int run, int from_frame) {
    if (run < 0 || run >= int(run_music_.size())) return;

    // Swapped only here, and start_stream stops the callback first, so the
    // audio thread is never inside a clip that is being replaced.
    stop_stream();
    current_run_ = run;
    music_ = run_music_[size_t(run)];

    // Where in that piece the picture has reached, so starting a report partway
    // through does not begin the music again from its first bar.
    const int into = std::max(0, from_frame - run_start_[size_t(run)]);
    start_stream(into);
}

void Player::start_stream(int from_frame) {
    stop_stream();

    if (!IsAudioDeviceReady()) return;

    // Starting partway in picks the score up at the same point, wrapped in case
    // the report outlasts the track.
    const int total = music_ ? music_->frames() : 0;
    cursor_ = total > 0 ? int(double(SAMPLE_RATE) * from_frame / layout::FPS) % total : 0;
    fed_ = 0;

    stream_ = LoadAudioStream(SAMPLE_RATE, 32, CHANNELS);
    stream_ready_ = true;
    live_player.store(this, std::memory_order_release);
    SetAudioStreamCallback(stream_, feed_stream);
    PlayAudioStream(stream_);
}

void Player::stop_stream() {
    if (!stream_ready_) return;

    // Order matters. Stopping first takes the stream out of the mixer, so the
    // callback is no longer being called; only then is it safe to drop the
    // pointer it reads and free the stream itself. Doing it the other way round
    // can leave the audio thread inside a callback whose stream is being freed.
    StopAudioStream(stream_);
    SetAudioStreamCallback(stream_, nullptr);
    live_player.store(nullptr, std::memory_order_release);
    UnloadAudioStream(stream_);
    stream_ready_ = false;
}

void Player::release() {
    // Everything raylib owns has to go while the audio device is still open.
    stop_stream();
    forget_effects();
    music_.reset();
    run_music_.clear();
}

void Player::fill(void* buffer, unsigned int frames) {
    float* out = static_cast<float*>(buffer);
    const std::shared_ptr<Clip> clip = music_;   // a copy, so it cannot be freed mid-read

    if (!clip || clip->empty()) {
        std::memset(out, 0, size_t(frames) * CHANNELS * sizeof(float));
        return;
    }

    const int total = clip->frames();
    const float gain = music_gain_.load(std::memory_order_relaxed);
    int at = cursor_.load();

    for (unsigned int frame = 0; frame < frames; ++frame) {
        // The score never runs out: reaching the end takes it back to the
        // beginning, for as long as the report is being watched.
        if (at >= total) at = 0;
        for (int channel = 0; channel < CHANNELS; ++channel) {
            out[size_t(frame) * CHANNELS + channel] =
                clip->samples[size_t(at) * CHANNELS + channel] * gain;
        }
        ++at;
    }

    cursor_.store(at % total);
    fed_.fetch_add(frames);
}

void Player::play(int from_frame) {
    if (timeline_ == nullptr) return;

    // Scenes before the starting point count as already seen, so it does not
    // stop on buttons that were skipped past.
    cleared_.clear();
    for (const Entry& entry : timeline_->entries()) {
        if (entry.start + entry.end <= from_frame) cleared_.insert(entry.index);
    }

    playing_ = true;
    paused_ = false;
    waiting_ = false;
    finished_ = false;
    frame_ = from_frame;
    fired_to_ = from_frame;
    started_at_frame_ = from_frame;
    started_at_time_ = GetTime();

    // The score starts here and is not touched again unless the report itself
    // calls for a different piece.
    current_run_ = -1;
    switch_music(run_at(from_frame), from_frame);
}

void Player::stop() {
    stop_stream();
    playing_ = false;
    paused_ = false;
    waiting_ = false;
    hover_ = false;
}

void Player::toggle_pause() {
    // Waiting on the closing NEXT is still a place someone may want to hold:
    // the score is running under it. The browser editor pauses there; so
    // does this one now.
    if (!playing_) return;

    paused_ = !paused_;
    if (paused_) {
        if (stream_ready_) PauseAudioStream(stream_);
    } else {
        if (stream_ready_) ResumeAudioStream(stream_);
        // The picture takes up where it left off; the score simply carries on
        // from the sample it was paused at.
        started_at_frame_ = frame_;
        started_at_time_ = GetTime();
    }
}

bool Player::press_next() {
    if (!waiting_ || timeline_ == nullptr) return false;

    const Entry* entry = timeline_->entry_at(frame_);
    if (entry == nullptr) return false;

    cleared_.insert(entry->index);
    waiting_ = false;
    hover_ = false;

    // The page turn itself, which the mixed-down video gets from its own cue.
    if (!pack_.next.empty()) play_effect(pack_.next[0], 1.0f);

    // Pressing NEXT on the closing scene winds the whole thing back.
    if (entry->is_last) {
        stop();
        finished_ = true;
        return true;
    }

    const int resume_at = entry->start + entry->end;
    if (resume_at >= timeline_->total_frames()) {
        stop();
        finished_ = true;
        return true;
    }

    // Only the picture moves on. The score is left exactly where it is, which
    // is the whole point of keeping it on a stream of its own.
    frame_ = resume_at;
    fired_to_ = resume_at;
    started_at_frame_ = resume_at;
    started_at_time_ = GetTime();
    return false;
}

void Player::seek(int frame) {
    if (!playing_ || timeline_ == nullptr) return;

    frame = std::clamp(frame, 0, std::max(0, timeline_->total_frames() - 1));

    // Everything before the new spot counts as already seen, so the report does
    // not immediately stop on a button that was skipped over.
    cleared_.clear();
    for (const Entry& entry : timeline_->entries()) {
        if (entry.start + entry.end <= frame) cleared_.insert(entry.index);
    }

    frame_ = frame;
    fired_to_ = frame;      // no backlog of clicks to catch up on
    waiting_ = false;
    hover_ = false;
    finished_ = false;
    started_at_frame_ = frame;
    started_at_time_ = GetTime();

    // The music only moves if the destination belongs to a different piece.
    const int run = run_at(frame);
    if (run != current_run_) switch_music(run, frame);
}

void Player::update() {
    if (!playing_ || timeline_ == nullptr) return;
    if (paused_ || waiting_) return;

    const double elapsed = GetTime() - started_at_time_;
    frame_ = started_at_frame_ + int(elapsed * layout::FPS);

    if (frame_ >= timeline_->total_frames()) {
        frame_ = timeline_->total_frames() - 1;
        fire_cues(fired_to_, frame_ + 1);
        stop();
        finished_ = true;
        return;
    }

    // Outside movie mode the report waits on every button, like the game does.
    // In movie mode it rolls on by itself but still stops on the closing one,
    // which is what takes you back to the start.
    const Entry* entry = timeline_->entry_at(frame_);
    const bool gated = entry != nullptr && (!movie_mode_ || entry->is_last);

    if (gated && timeline_->show_button() && cleared_.count(entry->index) == 0 &&
        frame_ >= entry->start + entry->type_end) {
        // The picture holds here until somebody clicks. The score is not told,
        // and carries on underneath.
        frame_ = entry->start + entry->type_end;
        waiting_ = true;
    }

    fire_cues(fired_to_, frame_ + 1);
    fired_to_ = frame_ + 1;

    // The only thing that ever changes the music mid-report: reaching a scene
    // that asks for a different piece.
    const int run = run_at(frame_);
    if (run != current_run_) switch_music(run, frame_);
}

void Player::fire_cues(int from_frame, int to_frame) {
    if (timeline_ == nullptr || to_frame <= from_frame) return;

    for (const Cue& cue : timeline_->cues()) {
        if (cue.frame < from_frame || cue.frame >= to_frame) continue;

        switch (cue.kind) {
            case Cue::Kind::Letter:
                if (!pack_.letter.empty()) {
                    play_effect(pack_.letter[size_t(GetRandomValue(0, int(pack_.letter.size()) - 1))],
                                layout::LETTER_GAIN);
                }
                break;
            case Cue::Kind::SceneSound:
                play_effect(cue.file, 1.0f);
                break;
            case Cue::Kind::Next:
                // In a film nobody presses anything, so the page turn is heard
                // as the picture reaches it. Outside movie mode the click makes
                // the sound instead, in press_next, so that it lands when the
                // button is actually pressed rather than at a frame the report
                // has been sitting still on.
                if (movie_mode_ && !pack_.next.empty()) play_effect(pack_.next[0], 1.0f);
                break;
        }
    }
}

void Player::play_effect(const std::string& file, float volume) {
    if (file.empty() || !IsAudioDeviceReady()) return;

    auto found = effects_.find(file);
    if (found == effects_.end()) {
        // Scene sounds carry a whole path; the pack's own effects are bare
        // names living in the sounds folder.
        const std::string path = FileExists(file.c_str()) ? file : sounds_dir_ + "/" + file;
        if (!FileExists(path.c_str())) {
            effects_[file] = Effect{};
            return;
        }
        effects_[file] = Effect{LoadSound(path.c_str()), 1.0f};
        found = effects_.find(file);
    }

    if (found->second.sound.frameCount == 0) return;
    found->second.level = volume;
    SetSoundVolume(found->second.sound, volume * effects_gain_);
    PlaySound(found->second.sound);
}

void Player::set_effects_volume(float gain) {
    effects_gain_ = gain;
    // Landed on anything still ringing, not only on the next one fired.
    for (auto& pair : effects_) {
        if (pair.second.sound.frameCount != 0) {
            SetSoundVolume(pair.second.sound, pair.second.level * gain);
        }
    }
}

void Player::forget_effects() {
    for (auto& pair : effects_) {
        if (pair.second.sound.frameCount != 0) UnloadSound(pair.second.sound);
    }
    effects_.clear();
}

}  // namespace parody
