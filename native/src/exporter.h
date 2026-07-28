// Writing the report out as a video.
//
// Frames are drawn into the same off-screen target the editor previews, read
// back and piped straight into ffmpeg as raw pixels, with the mixed soundtrack
// handed over as a wav alongside. Nothing is written to disk frame by frame.
//
// The work is done a slice at a time from the main loop rather than on a
// thread, so the window keeps drawing and the shutter keeps moving while it
// encodes, with no locking to get wrong.

#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "mixer.h"
#include "project.h"
#include "stage.h"
#include "timeline.h"

namespace parody {

class Exporter {
  public:
    // Returns true once the whole thing is finished, one way or the other.
    bool step(Stage& stage, Mixer& mixer, const Timeline& timeline, const Project& project,
              const Paths& paths, const std::string& target, float& progress);

    bool failed() const { return !error_.empty(); }
    const std::string& error() const { return error_; }

  private:
    void finish();

    bool running_ = false;
    int frame_ = 0;
    std::FILE* pipe_ = nullptr;
    std::string error_;
    std::string audio_file_;
    std::string script_;
};

}  // namespace parody
