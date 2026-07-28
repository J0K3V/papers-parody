#include "exporter.h"

#include <algorithm>
#include <cstring>

namespace parody {
namespace {

// How many frames to encode per pass through the main loop. Enough to keep
// ffmpeg fed, small enough that the window never stutters.
constexpr int FRAMES_PER_STEP = 12;

std::string quote(const std::string& text) { return "\"" + text + "\""; }

}  // namespace

bool Exporter::step(Stage& stage, Mixer& mixer, const Timeline& timeline, const Project& project,
                    const Paths& paths, const std::string& target, float& progress) {
    if (!running_) {
        error_.clear();
        frame_ = 0;

        if (timeline.total_frames() == 0) {
            error_ = "there are no scenes to render";
            return true;
        }

        // The soundtrack goes to a temporary wav beside the target, which
        // ffmpeg reads as its second input.
        audio_file_ = target + ".soundtrack.wav";
        const Clip track = mixer.mix(timeline, project.soundtrack, false);
        if (!Mixer::write_wav(track, audio_file_)) {
            error_ = "could not write the soundtrack";
            return true;
        }

        // Raw frames in, H.264 out. -y overwrites, and the pixel format is what
        // players expect rather than what the GPU hands back.
        //
        // The command goes through a small batch file rather than straight to
        // popen: on Windows popen hands the string to cmd.exe, which mangles
        // the nested quotes that paths with spaces need.
        script_ = target + ".ffmpeg.bat";
        std::FILE* script = std::fopen(script_.c_str(), "w");
        if (script == nullptr) {
            error_ = "could not write the encoder script";
            return true;
        }
        std::fprintf(script,
                     "@echo off\r\n"
                     "%s -hide_banner -loglevel error -y "
                     "-f rawvideo -pixel_format rgba -video_size %dx%d -framerate %d -i - "
                     "-i %s "
                     "-c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac -b:a 192k "
                     "-map 0:v:0 -map 1:a:0 -shortest %s\r\n",
                     quote(paths.ffmpeg).c_str(), layout::FRAME_WIDTH, layout::FRAME_HEIGHT,
                     layout::FPS, quote(audio_file_).c_str(), quote(target).c_str());
        std::fclose(script);

        pipe_ = popen(quote(script_).c_str(), "wb");
        if (pipe_ == nullptr) {
            error_ = "could not start ffmpeg";
            return true;
        }

        running_ = true;
        progress = 0.0f;
        return false;
    }

    // Draw and push a slice of frames.
    const int until = std::min(timeline.total_frames(), frame_ + FRAMES_PER_STEP);
    for (; frame_ < until; ++frame_) {
        const FrameState state = timeline.state_at(frame_);
        const Picture* art = nullptr;
        if (state.entry != nullptr && state.entry->index < int(project.scenes.size())) {
            art = stage.picture(project.scenes[state.entry->index].image, project.fit);
        }

        stage.draw(state, art, false, false, {0, 0}, false);

        Image frame = LoadImageFromTexture(stage.target().texture);
        ImageFlipVertical(&frame);  // render textures come out upside down
        ImageFormat(&frame, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

        const size_t bytes = size_t(frame.width) * frame.height * 4;
        if (std::fwrite(frame.data, 1, bytes, pipe_) != bytes) {
            error_ = "ffmpeg stopped accepting frames";
            UnloadImage(frame);
            finish();
            return true;
        }
        UnloadImage(frame);
    }

    progress = float(frame_) / timeline.total_frames();

    if (frame_ >= timeline.total_frames()) {
        finish();
        return true;
    }
    return false;
}

void Exporter::finish() {
    if (pipe_ != nullptr) {
        const int status = pclose(pipe_);
        pipe_ = nullptr;
        if (status != 0 && error_.empty()) {
            error_ = "ffmpeg exited with " + std::to_string(status);
        }
    }

    if (!audio_file_.empty()) {
        std::remove(audio_file_.c_str());
        audio_file_.clear();
    }
    if (!script_.empty()) {
        std::remove(script_.c_str());
        script_.clear();
    }

    running_ = false;
}

}  // namespace parody
