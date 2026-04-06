# Product Vision - vehicle-sim

## Core Purpose
This is **not a vehicle simulator**. This is a powertrain reference model translator.

We read real live telemetry directly from Tesla Model Y BLE OBD2 scanner. The sole purpose of this product is to translate those real input signals into what any other reference vehicle would be doing under exactly the same driving conditions.

## End Goal
This project is **STANDALONE DATA ACQUISITION AND DISPLAY LAYER ONLY**.
We will **NOT** integrate, reference, or depend on engine-sim, physics libraries or any external projects.

This repository's **ONLY** responsibility:
1. Read real live telemetry directly from Tesla Model Y BLE OBD2 scanner
2. Parse Tesla proprietary signals and normalize to standard OBD2 canonical format **EXACTLY AT THE BOUNDARY LAYER**
3. Display live telemetry values on iPhone SwiftUI dashboard

All physics modelling, engine simulation and sound generation belongs in the parent project. This project is strictly data in, data out, display.

## Breadth First Delivery Strategy
✅ **Always deployable, always demoable**
✅ Ship end to end working stack first
✅ Get one full value working from BLE feed all the way to dashboard
✅ Add depth later, ship breadth first
✅ No sprints, continuous incremental delivery

## Product Roadmap
### Phase 0: MVP End to End Pipe (🔴 HIGHEST PRIORITY)
✅ **Deliver end to end working value first**
1. Read ONE actual raw signal from real Tesla Model Y over BLE
2. Parse Tesla proprietary signal using public DBC files (Adminius/tesla-can-dbc)
3. Translate EXACTLY at the boundary layer to standard OBD2 canonical format
4. Display live updating value on real iPhone SwiftUI dashboard
5. Maintain 10Hz minimum update rate
6. Full end to end regression test covering entire stack

✅ **All architecture rules apply 100%:** SOLID, DI, TDD, no technical debt, no corners cut.

THIS IS THE ONLY PRIORITY. DO NOT WORK ON ANYTHING ELSE UNTIL THIS IS WORKING END TO END ON ACTUAL HARDWARE.

### Phase 1: Single Powertrain Model (OPTIONAL)
1. Implement IPowertrainModel interface
2. Add Mustang GT500 reference model
3. Calculate RPM / Gear / Torque from real input signals
4. Display translated values on dashboard

### Phase 2: Pluggable Models (OPTIONAL)
1. Multiple powertrain model support
2. Hot swap models at runtime
3. Compare multiple vehicle outputs side by side

NOTE: Phase 1 and 2 are optional simulation features for powertrain modeling.
Phase 0 provides real data acquisition from Tesla BLE and display.
Phase 1/2 can be added later via clean boundary separation without affecting Phase 0.

### Phase 3: Advanced Features
1. Gearbox shift algorithm simulation
2. Engine load calculation
3. Throttle response mapping
4. Transmission behaviour modelling

## Non Negotiable Rules
1. No simulation of vehicle movement - we only translate real measured signals
2. Tesla is an implementation detail completely hidden behind the boundary layer
3. All upper layers only ever see standard OBD2 format values
4. Every interface is replaceable via Dependency Injection
5. Every commit builds green and is demoable at all times
