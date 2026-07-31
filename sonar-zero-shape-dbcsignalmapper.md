# DBCSignalMapper Refactor Shape Proposal

**Target File**: `src/domain/DBCSignalMapper.cpp`
**Sonar Issues**: S3776 (cognitive complexity 31), S134 (multiple guard clauses)
**Status**: READ-ONLY ANALYSIS — Awaiting team-lead greenlight

## Current State Analysis

### Functions with Issues

1. **`mapSignal(frame, definition)` — :10-26**
   - **S3776 cc31**: Cognitive complexity 31 (threshold 15)
   - **Issues**: Nested if-else for signed/unsigned paths, duplicate clamp logic
   - **Lines 19-25**: Duplicate clamp branches for signed vs unsigned

2. **`mapGearSignal` — :46-106**
   - **S134 @ :65, :88, :123, :148**: Guard clause opportunities
   - **Complexity**: Nested loops (definitions → signals), nested if-else chain for value-table mapping
   - **Lines 87-99**: if-else chain for gear description matching

3. **`extractRawBits` — :108-160**
   - **Complexity**: Nested conditional (byteOrder), loop with complex bit arithmetic
   - **Lines 116-126**: Intel byte-order loop
   - **Lines 142-156**: Motorola byte-order loop with sawtooth bit numbering

### Locked Test Coverage

The contract is fully locked by `DBCSignalMapperContract.test.cpp`:
- Gear mapping: 6 tests (PARK, REVERSE, NEUTRAL, AUTO_1, INVALID=0, SNA=7)
- Signed Motorola spanning bytes: 1 test
- Clamp behavior: 2 tests (above max, below min)
- Lookup failures: 3 tests (unknown CAN ID, unknown signal, empty definitions)
- Empty value table fallback: 1 test
- Unknown raw value fallback: 1 test

## Community Best Practice Research

### Sources Consulted

1. **Cantools Documentation** — Python DBC parser implementation
   - Normalizes Motorola (big_endian) start bits to little_endian internally
   - Separates bit-extraction by byte order into distinct code paths
   - [cantools.readthedocs.io](https://cantools.readthedocs.io/en/stable/)

2. **Refactoring with Cognitive Complexity — SonarSource**
   - Extract Method refactoring pattern for reducing complexity
   - Guard clauses for early returns
   - [YouTube: el9OKGrqU6o](https://www.youtube.com/watch?v=el9OKGrqU6o)

3. **Extract Method — Refactoring.Guru**
   - Canonical guide for function decomposition
   - [refactoring.guru/extract-method](https://refactoring.guru/extract-method)

4. **Refactoring Using Cognitive Complexity — Dev Genius**
   - Breaking complex logic into smaller functions
   - [blog.devgenius.io/refactoring-using-cognitive-complexity-7e55197335b6](https://blog.devgenius.io/refactoring-using-cognitive-complexity-7e55197335b6)

5. **DBC Signal Encoding/Decoding — DBCUtility**
   - Scale, offset, and clamp are standard DBC operations
   - [dbcutility.com/blog/dbc-signal-encoding-decoding](https://dbcutility.com/blog/dbc-signal-encoding-decoding)

### Key Insights

- **Bit extraction is inherently complex** — DBC Motorola sawtooth bit numbering is a known pain point
- **Extract Method** is the canonical refactor for cognitive complexity
- **SRP**: Clamp/scale/offset, bit-extraction, and value-table lookup are separate concerns
- **Guard clauses**: Early returns for error paths reduce nesting

## Refactor Alternatives

### Alternative A: Extract Method (Recommended)

**Changes**:
1. Extract `applyScaleOffsetClamp(rawValue, definition)` helper
   - Takes raw uint64, returns clamped double
   - Handles signed/unsigned, scale, offset, clamp in one place
   - Eliminates duplicate clamp code in mapSignal

2. Extract `mapValueTableToGear(rawValue, valueTable)` helper
   - Encapsulates the if-else chain for gear description matching
   - Returns std::optional<std::int32_t>
   - Clears S134 at :88 (value table lookup)

3. Extract `extractIntelBits(frame, definition)` and `extractMotorolaBits(frame, definition)` helpers
   - Separates byte-order concerns in extractRawBits
   - Each helper focuses on one bit-numbering scheme
   - Reduces cognitive complexity of the parent function

**SRP/DRY Win**:
- **Single Responsibility**: Each helper has one clear purpose
- **DRY**: Clamp logic no longer duplicated
- **Testability**: Helpers can be unit tested independently

**Risk**: Low
- Minimal signature changes (internal helpers only)
- Locked tests guard all behavior

**Test Guard**:
- `DBCSignalMapperClampTest` (lines 180-214) guards clamp behavior
- `DBCGearMappingTest` (lines 67-117) guards gear mapping
- `DBCSignalMapperSignedMotorolaTest` (lines 154-171) guards signed extraction

### Alternative B: Strategy Pattern for Byte Order

**Changes**:
1. Create `BitExtractor` interface with `extractIntel` and `extractMotorola` methods
2. Implement `IntelBitExtractor` and `MotorolaBitExtractor` classes
3. Pass extractor to `mapSignal` based on `definition.byteOrder`

**SRP/DRY Win**:
- **Open/Closed**: New byte orders can be added without modifying existing code
- **Strategy**: Byte-order logic encapsulated in separate classes

**Risk**: Medium
- Adds abstraction complexity
- May be overkill for just two byte orders
- Requires additional test coverage for strategy classes

**Test Guard**:
- Same as Alternative A, plus new tests for strategy classes

### Alternative C: Lookup Table for Gear Mapping

**Changes**:
1. Replace if-else chain in `mapGearSignal` with a static lookup table
2. Table maps description strings to Gear constants
3. Use `std::unordered_map<std::string, std::int32_t>` for O(1) lookup

**SRP/DRY Win**:
- **Performance**: O(1) vs O(n) lookup
- **Maintainability**: Adding new gears is a single table entry

**Risk**: Low-Medium
- Requires static initialization
- Must handle unknown descriptions gracefully

**Test Guard**:
- `DBCGearMappingTest` (lines 67-117) guards all gear mappings
- `DBCGearMappingTest.ValueTableMissingEntry_UnknownRaw_ReturnsNullopt` (lines 242-260) guards unknown values

## Recommendation

**Proceed with Alternative A (Extract Method)** with the following structure:

1. `applyScaleOffsetClamp(std::uint64_t rawBits, const DBCSignalDefinition& definition)` 
   - Handles signed/unsigned, scale, offset, clamp
   - Returns `std::optional<double>` (nullopt if clamp fails, though current code always clamps)

2. `mapValueTableToGear(std::int64_t rawValue, const std::vector<DBCValueEntry>& valueTable)`
   - Encapsulates the if-else chain for gear description matching
   - Returns `std::optional<std::int32_t>`

3. Keep `extractRawBits` as-is for now
   - The Motorola sawtooth logic is well-documented and verified
   - Extracting byte-order helpers can be a follow-up refactor

4. Apply guard clauses for early returns
   - At :65 (empty value table)
   - At :88 (value table lookup start)
   - At :123 (raw value validation)

**Order of Operations**:
1. Extract `applyScaleOffsetClamp` — simplest, highest impact on S3776
2. Extract `mapValueTableToGear` — clears S134 in gear mapping
3. Apply guard clauses — addresses remaining S134 issues
4. Consider byte-order extraction as a separate follow-up task

**Estimated Complexity Reduction**:
- `mapSignal`: cc31 → ~cc12 (eliminate nested if-else)
- `mapGearSignal`: cc26 → ~cc15 (extract value-table lookup)
- Overall: Below S3776 threshold (15) for all functions

## Next Steps

1. **Await team-lead greenlight** on this refactor shape
2. **Wait for test-arch2 to commit** — currently holds the build slot
3. **Verify build passes** after test-arch2 commits
4. **Apply refactor** one helper at a time, running tests after each change
5. **Run Sonar scan** to confirm S3776 and S134 are resolved
6. **Commit** with rule prefix: `cpp:S3776 — extract applyScaleOffsetClamp helper`

## Questions for Team Lead

1. Is Alternative A (Extract Method) the preferred approach, or should we consider Alternative B (Strategy Pattern) for byte order?
2. Should byte-order extraction be refactored in this pass, or deferred to a separate task?
3. Are there any other concerns about the proposed refactor shape?

---

**Sources**:
- [cantools Documentation](https://cantools.readthedocs.io/en/stable/)
- [Refactoring with Cognitive Complexity — SonarSource](https://www.youtube.com/watch?v=el9OKGrqU6o)
- [Extract Method — Refactoring.Guru](https://refactoring.guru/extract-method)
- [Refactoring Using Cognitive Complexity — Dev Genius](https://blog.devgenius.io/refactoring-using-cognitive-complexity-7e55197335b6)
- [DBC Signal Encoding/Decoding — DBCUtility](https://dbcutility.com/blog/dbc-signal-encoding-decoding)
