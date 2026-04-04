# Release Milestones

## MVP 0.1 - Foundation Release
**Target Date**: 2 weeks
**Goal**: Working build system, test infrastructure, basic architecture

✅ Acceptance Gates:
- `make` builds cleanly on macOS
- `make test` runs and passes
- ARCH.md document complete
- TESTING.md document complete
- No cyclic dependencies
- All core interfaces defined

## MVP 0.2 - Telemetry Core
**Target Date**: 4 weeks
**Goal**: Working physics simulation, data model, public API

✅ Acceptance Gates:
- Speed, throttle, RPM values available
- Acceleration calculation implemented
- Event bus for value changes
- Full unit test coverage for core math
- Benchmarks show <1ms latency

## MVP 0.3 - BLE Integration
**Target Date**: 6 weeks
**Goal**: Real hardware connectivity

✅ Acceptance Gates:
- Tesla BLE scan working
- Authenticated connection established
- Live telemetry streaming at 10Hz
- Simulation / hardware mode switch

## MVP 1.0 - Production Ready
**Target Date**: 8 weeks
**Goal**: Stable release for end users

✅ Acceptance Gates:
- Full documentation
- CLI interface complete
- Error handling and recovery
- 24 hour stability test passed
- Memory leak check clean
