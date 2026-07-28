// Parody, Please - native editor.
//
//   ParodyPlease.exe                  open the editor
//   ParodyPlease.exe report.json      open a particular report
//   ParodyPlease.exe --render r.json [out.mp4]   render without a window

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "console.h"
#include "editor.h"
#include "exporter.h"
#include "mixer.h"
#include "project.h"
#include "raylib.h"
#include "stage.h"
#include "timeline.h"

namespace {

// Render a saved report straight to a video, no editor. Handy for batches and
// for checking that a packaged build still works.
int render_only(const std::string& file, const std::string& output) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(320, 200, "Parody, Please");
    InitAudioDevice();

    const parody::Paths paths = parody::Paths::discover(nullptr);

    parody::Project project;
    std::string error;
    if (!parody::load_project(paths, file, project, error)) {
        std::printf("Could not read the project: %s\n", error.c_str());
        CloseWindow();
        return 1;
    }
    if (project.scenes.empty()) {
        std::printf("That project has no scenes.\n");
        CloseWindow();
        return 1;
    }

    parody::Stage stage;
    stage.load(paths.assets);

    parody::Mixer mixer;
    mixer.set_sounds_dir(paths.sounds);

    const parody::Timeline timeline(project.scenes, project.typing,
                                    project.next_button == "on", false);

    std::string target = output.empty() ? project.output : output;
    if (target.find(':') == std::string::npos && !target.empty() && target[0] != '/') {
        target = paths.root + "/" + target;
    }

    parody::Exporter exporter;
    float progress = 0.0f;
    int shown = -1;

    while (!exporter.step(stage, mixer, timeline, project, paths, target, progress)) {
        const int step = int(progress * 20);
        if (step != shown) {
            shown = step;
            std::printf("\r[%5.1f%%] encoding", progress * 100.0f);
            std::fflush(stdout);
        }
    }
    std::printf("\r                        \r");

    if (exporter.failed()) {
        std::printf("Render failed: %s\n", exporter.error().c_str());
        stage.unload();
        CloseAudioDevice();
        CloseWindow();
        return 1;
    }

    std::printf("Written: %s\n", target.c_str());
    stage.unload();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 2 && (std::strcmp(argv[1], "--render") == 0 || std::strcmp(argv[1], "-r") == 0)) {
        // Rendering from a terminal has something to say, so it borrows the
        // terminal it was launched from. Opening the editor does not.
        parody::use_parent_console();
        return render_only(argv[2], argc > 3 ? argv[3] : "");
    }

    SetTraceLogLevel(LOG_WARNING);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "Parody, Please - scene editor");
    SetWindowMinSize(1000, 680);

    // Opens filling the screen: there is a lot to lay out and the stage is the
    // point of it. Asked for after the window exists rather than as a config
    // flag, which does not take. The 1280x800 above is what it goes back to.
    MaximizeWindow();

    // Buffers big enough for Bluetooth output - see STREAM_FRAMES. This has to
    // be set before the device is opened.
    SetAudioStreamBufferSizeDefault(parody::STREAM_FRAMES);
    InitAudioDevice();
    SetExitKey(KEY_NULL);

    // On the heap: the editor holds the whole stage, mixer and player, which is
    // more than the default thread stack on Windows is willing to give.
    auto editor = std::make_unique<parody::Editor>();
    editor->start(argc > 1 ? argv[1] : "");
    editor->run();
    editor->shutdown();

    CloseAudioDevice();
    CloseWindow();
    return 0;
}
