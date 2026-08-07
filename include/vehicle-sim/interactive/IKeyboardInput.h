#pragma once

// Keyboard input contract — CONSUMED from the engine-sim-bridge submodule,
// not copy-pasted. The single definition lives at
//   external/engine-sim-bridge/include/input/IKeyboardInput.h
// and the bridge is treated as read-only upstream (the source of truth).
//
// This header exists only to expose that contract under vehicle-sim's own
// namespace, so call sites read naturally and stay decoupled from the
// submodule's include layout. If the bridge ever changes the contract,
// vehicle-sim fails to compile right here — which is the point: the
// duplicate can no longer drift silently, as the previous pasted copy did.
#include "input/IKeyboardInput.h"

namespace vehicle_sim::interactive {

/// Injected keyboard source: getKey() returns a key code, or -1 if none.
using IKeyboardInput = ::IKeyboardInput;

} // namespace vehicle_sim::interactive
