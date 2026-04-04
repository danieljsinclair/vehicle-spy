# Product Backlog

## Priority Legend
🔴 CRITICAL | 🟡 HIGH | 🟢 MEDIUM | ⚪ LOW

## Sprint 0 - Foundation
| ID | Story | Points | Priority | Acceptance Criteria |
|----|-------|--------|----------|---------------------|
| 0.1 | As a developer I can run `make` and get a working executable | 2 | 🔴 | `make` completes without errors; binary exists in build/ |
| 0.2 | As a developer I can run `make test` and all unit tests pass | 3 | 🔴 | All tests run; summary output with pass/fail counts |
| 0.3 | As an architect I can see clear separation between layers | 5 | 🟡 | No cyclic dependencies; every class has single responsibility |

## Sprint 1 - Core Telemetry
| ID | Story | Points | Priority | Acceptance Criteria |
|----|-------|--------|----------|---------------------|
| 1.1 | As a user I can read current vehicle speed | 3 | 🔴 | Speed value returned in km/h; valid range 0-250 |
| 1.2 | As a user I can read throttle position | 2 | 🔴 | Throttle value 0.0 - 1.0 |
| 1.3 | As a user I can read RPM | 2 | 🔴 | RPM value 0 - 8000 |
| 1.4 | As a user I can read calculated acceleration | 3 | 🟡 | m/s² value; derived from speed delta over time |

## Sprint 2 - BLE OBD2
| ID | Story | Points | Priority | Acceptance Criteria |
|----|-------|--------|----------|---------------------|
| 2.1 | As a user I can scan for Tesla BLE devices | 5 | 🟡 | List nearby Tesla vehicles with VIN |
| 2.2 | As a user I can connect to vehicle BLE interface | 8 | 🔴 | Secure authenticated connection |
| 2.3 | As a user I can receive real-time telemetry updates | 5 | 🔴 | 10Hz update rate; no dropped frames |

## Sprint 3 - Simulation Engine
| ID | Story | Points | Priority | Acceptance Criteria |
|----|-------|--------|----------|---------------------|
| 3.1 | As a tester I can run physics simulation without hardware | 5 | 🔴 | Standalone mode; no BLE required |
| 3.2 | As a tester I can inject arbitrary sensor values | 3 | 🟡 | Inject known values for test validation |

## Non Functional Requirements
1. All public API methods must be async/non-blocking
2. No global state anywhere in the codebase
3. Every concrete class must implement an abstract interface
4. All dependencies must be injected via constructor
5. Minimum 90% test coverage for core logic
