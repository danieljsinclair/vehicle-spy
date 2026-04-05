# Product Vision - vehicle-sim

## Core Purpose
This is **not a vehicle simulator**. This is a powertrain reference model translator.

We read real live telemetry directly from Tesla Model Y BLE OBD2 scanner. The sole purpose of this product is to translate those real input signals into what any other reference vehicle would be doing under exactly the same driving conditions.

## End Goal
Given real throttle position, speed, and acceleration from a Tesla, we act as an adapter that feeds these canonical signals into the existing parent engine-sim physics library. We will NOT reimplement engine physics. We will:
1. Normalize real Tesla signals into standard OBD2 format
2. Feed normalized signals into existing engine-sim physics library
3. Receive resulting gear, RPM and torque values from the parent physics engine
4. Forward final values to the engine sound generator

This project is the adapter layer between real vehicle telemetry and the existing physics engine.

## Breadth First Delivery Strategy
✅ **Always deployable, always demoable**
✅ Ship end to end working stack first
✅ Get one full value working from BLE feed all the way to dashboard
✅ Add depth later, ship breadth first
✅ No sprints, continuous incremental delivery

## Product Roadmap
### Phase 0: End to End Pipe
1. Read raw Tesla BLE signal
2. Translate to standard OBD2 canonical format at the boundary
3. Normalize and validate
4. Display value on dashboard UI

### Phase 1: Single Powertrain Model
1. Implement IPowertrainModel interface
2. Add Mustang GT500 reference model
3. Calculate RPM / Gear / Torque from real input signals
4. Display translated values on dashboard

### Phase 2: Pluggable Models
1. Multiple powertrain model support
2. Hot swap models at runtime
3. Compare multiple vehicle outputs side by side

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
