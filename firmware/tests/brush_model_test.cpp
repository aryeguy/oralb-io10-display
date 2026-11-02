#include "brush_model.hpp"
#include <cassert>

int main() {
    BrushModel display(true);
    BrushSnapshot remote;
    remote.valid = true;
    remote.serverState = true;
    remote.brushing = true;
    remote.elapsedSeconds = 42;
    remote.receivedAtMs = 1000;
    remote.pressure = Pressure::Normal;
    remote.coverage[3] = 80;
    remote.activeSurface = 3;
    display.apply(remote);

    // Replay many raw packets while HTTP is skipped or fails. These contain
    // cached idle state and no classification, and must never reach the UI.
    for (uint32_t ms = 1500; ms <= 17000; ms += 500) {
        BrushSnapshot raw;
        raw.valid = true;
        raw.receivedAtMs = ms;
        raw.motionPayloadSize = 20;
        display.apply(raw);
        display.tick(ms, false);
        assert(display.state().brushing);
        assert(display.state().pressure == Pressure::Normal);
        assert(display.state().coverage[3] == 80);
        assert(display.state().activeSurface == 3);
        assert(display.state().elapsedSeconds == 42 + (ms - 1000) / 1000);
    }

    // An authoritative stop still takes effect, and coverage survives.
    remote.brushing = false;
    remote.elapsedSeconds = 58;
    remote.receivedAtMs = 17500;
    display.apply(remote);
    display.tick(20000, false);
    assert(!display.state().brushing);
    assert(display.state().elapsedSeconds == 58);
    assert(display.state().coverage[3] == 80);

    // Standalone BLE and mock mode still accept local snapshots.
    BrushModel standalone;
    remote.serverState = false;
    remote.brushing = true;
    standalone.apply(remote);
    assert(standalone.state().brushing);

    display.reset();
    assert(!display.state().brushing);
    assert(display.state().coverage[3] == 0);
}
