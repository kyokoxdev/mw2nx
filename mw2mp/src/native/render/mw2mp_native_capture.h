// Derived from GPL-3.0 code; modified for bo2-zm-nx in 2026.
//
// PNG captures of the game image, for testing the renderer without looking at the screen.
//
// With mw2mp_capture_every_s > 0 a PNG is saved every N seconds in captures/ next to the executable. It works with the
// native renderer and with the Xenos emulation alike, so the two can be compared image by image.

#pragma once

#include <chrono>
#include <functional>
#include <string>

namespace rex::ui {
class Presenter;
}

namespace mw2mp::capture {

// Starts the capture thread if the cvar asks for it. The presenter is requested
// on every capture: it may not exist yet at startup.
void Boot(std::function<rex::ui::Presenter*()> get_presenter);

// One capture, saved as captures/<label>.png, `delay` from now (for test helpers; ignored before Boot). Captures are
// taken in the order they fall due.
void RequestCapture(std::string label, std::chrono::milliseconds delay = {});

// Stops the thread (idempotent).
void Stop();

// One capture right away, on the calling thread, saved as captures/<label>.png, only when test captures are on
// (mw2mp_capture_every_s > 0). For screens drawn before the capture thread starts, such as the shader index's
// progress screen.
void CaptureNowForTests(rex::ui::Presenter* presenter, const std::string& label);

}  // namespace mw2mp::capture
