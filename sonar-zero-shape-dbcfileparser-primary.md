# DBCFileParser Refactor Shape Proposal (Primary Opinion)

**Target File**: `src/domain/DBCFileParser.cpp`
**Sonar Issues**: S3776 cc56 at :36 (parseSignalDefinition), S3776 cc52 at :246 (parseString); S134 nesting throughout
**Status**: READ-ONLY ANALYSIS — Awaiting blind tests + user's S3776 view

## Current State Analysis

### Functions with Issues

1. **`parseSignalDefinition` — :36-142 (cc56)**
   - **Complexity**: Manual cursor walk through SG_ line with nested conditionals
   - **Issues**: 
     - Manual position tracking with repeated bounds checks
     - Multiple parsing stages (name, multiplexor, startBit|bitLength@, scale/offset, min/max, unit)
     - Deeply nested error returns (14 `return false` statements)
   - **Lines**: 41-142, 106 lines total

2. **`parseString` — :246-315 (cc52)**
   - **Complexity**: Top-level loop with BO_/SG_/VAL_ arm dispatch
   - **Issues**:
     - Nested if-else chain for line type detection
     - VAL_ parsing inlined (lines 281-308)
     - Manual cursor walks for BO_ and VAL_ parsing
   - **Lines**: 246-315, 69 lines total

3. **`parseOneValueEntry` — :147-173**
   - **Complexity**: Value table entry parsing with multiple validation points
   - **Issues**: Repeated whitespace skipping, bounds checking, nested error returns
   - **Lines**: 147-173, 26 lines total

4. **`buildResult` — :191-227**
   - **Complexity**: Nested loops for value table application and signal grouping
   - **Issues**: Two-pass algorithm (value tables, then grouping), nested iterations
   - **Lines**: 191-227, 36 lines total

### Locked Test Coverage

The contract is fully locked by `DBCFileParser.test.cpp` and `DBCSignalMapperContract.test.cpp`:

**Malformed-input contracts (lines 369-489)**:
- Multiplexor marker consumption (line 369)
- Orphan signals before BO_ (line 389)
- Missing colon after name (line 405)
- Non-numeric startBit (line 417)
- Missing scale/offset group (line 428)
- Well-formed entries before malformed (line 439)
- Non-numeric message ID (line 461)
- Non-numeric VAL_ ID (line 473)

**Valid parsing contracts (lines 17-358)**:
- Empty/whitespace/garbage input
- Single/multiple signals, multiple messages
- Byte order (Intel/Motorola), signedness
- Scale/offset, min/max, value tables
- Real Tesla DBC fragment integration

## Community Best Practice Research

### Sources Consulted

1. **Extract Method — Refactoring.Guru**
   - Canonical reference for Extract Method refactoring
   - Copy code fragment to new method, replace with call
   - [refactoring.guru/extract-method](https://refactoring.guru/extract-method)

2. **Refactoring Long Methods with Extract Method — CodeSignal**
   - Focused on refactoring long parsing methods
   - Extract when reducing duplication and increasing readability
   - [codesignal.com/learn/refactoring-by-example](https://codesignal.com/learn/courses/refactoring-by-leveraging-your-tests-with-java-junit/lessons/refactoring-long-methods-with-extract-method-pattern-in-java)

3. **Stack Overflow — Tokenizer, Parser, and Lexer Definition**
   - Clarifies tokenizer/lexer vs parser separation
   - Tokenizer breaks text into tokens, parser builds structure
   - [stackoverflow.com/questions/380455](https://stackoverflow.com/questions/380455/looking-for-a-clear-definition-of-what-a-tokenizer-parser-and-lexers-are)

4. **Qt QCanDbcFileParser Class Documentation**
   - Production-quality C++ DBC parser reference
   - Qt 6's official implementation for DBC file parsing
   - [doc.qt.io/qt-6/qcandbcfileparser.html](https://doc.qt.io/qt-6/qcandbcfileparser.html)

5. **Cantools (GitHub) — Issue #52**
   - Python DBC parser performance characteristics
   - A few-thousand-line DBC file takes ~1 second to parse
   - [github.com/eerimoq/cantools/issues/52](https://github.com/eerimoq/cantools/issues/52)

### Key Insights

- **Extract Method** is particularly effective for breaking down large parsing loops with high cognitive complexity into named, single-purpose helpers
- **Tokenization vs Manual Parsing**: For simple grammars like DBC (keyword-prefixed sections), manual cursor parsing is simpler than full tokenizer/parser separation
- **Over-extraction risk**: Too many tiny methods can make parser flow harder to follow (the "50 lines" argument from LinkedIn discussion)
- **Line-oriented parsers**: Benefit from extracting line-type handlers rather than full tokenization

## Refactor Alternatives

### Alternative A: Extract Method (Recommended)

**Changes**:
1. Extract `parseBoLine(trimmed, currentCanId)` helper
   - Handles BO_ message header parsing
   - Returns std::optional<std::uint16_t> for canId
   - Clears lines 261-273 in parseString

2. Extract `parseValLine(trimmed)` helper
   - Handles VAL_ value table parsing
   - Returns tuple of (canId, signalName, entries) or nullopt
   - Clears lines 281-308 in parseString

3. Extract `parseSignalMultiplicity(pos, line)` helper
   - Handles multiplexor marker parsing ('M' or 'm' + digits)
   - Advances position past multiplexor text
   - Clears lines 52-60 in parseSignalDefinition

4. Extract `parseBitLengthAndByteOrder(pos, line, out)` helper
   - Handles startBit|bitLength@byteOrder+sign parsing
   - Returns true on success, advances position
   - Clears lines 66-90 in parseSignalDefinition

5. Extract `parseScaleOffset(pos, line, out)` helper
   - Handles (scale,offset) parsing
   - Returns true on success, advances position
   - Clears lines 94-109 in parseSignalDefinition

6. Extract `parseMinMax(pos, line, out)` helper
   - Handles [min|max] parsing
   - Returns true on success, advances position
   - Clears lines 113-128 in parseSignalDefinition

7. Extract `parseUnit(pos, line, out)` helper
   - Handles optional "unit" parsing
   - Returns true on success, advances position
   - Clears lines 132-138 in parseSignalDefinition

**SRP/DRY Win**:
- **Single Responsibility**: Each helper has one clear parsing purpose
- **DRY**: No duplicate parsing logic
- **Testability**: Each helper can be unit tested independently

**Risk**: Low
- Minimal signature changes (internal helpers only)
- Locked tests guard all behavior
- No changes to public API

**Test Guard**:
- All malformed-input tests (lines 369-489) guard error-handling behavior
- All valid parsing tests (lines 17-358) guard successful parsing
- Value table entry tests (lines 439-459) guard partial parsing

**Estimated Complexity Reduction**:
- parseSignalDefinition: cc56 → ~cc15 (extract 5 helpers)
- parseString: cc52 → ~cc20 (extract 2 helpers)

### Alternative B: Line-Type Strategy Pattern

**Changes**:
1. Create `LineParser` interface with `canParse(line)` and `parse(line, state)` methods
2. Implement `BoLineParser`, `SgLineParser`, `ValLineParser` classes
3. Use `std::vector<std::unique_ptr<LineParser>>` in parseString for dispatch

**SRP/DRY Win**:
- **Open/Closed**: New line types can be added without modifying existing code
- **Strategy**: Line-type logic encapsulated in separate classes
- **SRP**: Each parser class handles one line type

**Risk**: Medium
- Adds abstraction complexity (4 new classes)
- May be overkill for 3 line types (BO_, SG_, VAL_)
- Requires additional test coverage for strategy classes

**Test Guard**:
- Same as Alternative A, plus new tests for strategy classes

**Estimated Complexity Reduction**:
- parseString: cc52 → ~cc10 (dispatch logic simplified)
- parseSignalDefinition: cc56 (unchanged)

### Alternative C: Cursor/Scanner Helper Object

**Changes**:
1. Create `DbcCursor` class that wraps `std::string_view` and position
2. Provide methods: `skipWhitespace()`, `expect(char)`, `parseNumber()`, `parseQuotedString()`
3. Use cursor in all parsing functions instead of raw position tracking

**SRP/DRY Win**:
- **DRY**: Common parsing operations (whitespace skipping, bounds checking) centralized
- **Readability**: Parsing logic becomes more declarative
- **Error Handling**: Cursor can track position for better error messages

**Risk**: Medium-High
- Adds new abstraction layer
- Changes error-handling semantics (cursor throws vs returns bool)
- Requires extensive testing of cursor operations

**Test Guard**:
- Same as Alternative A, plus new tests for cursor operations
- Must verify error behavior is identical

**Estimated Complexity Reduction**:
- parseSignalDefinition: cc56 → ~cc20 (cursor simplifies bounds checks)
- parseOneValueEntry: cc26 → ~cc10 (cursor simplifies whitespace skipping)
- parseString: cc52 (unchanged)

## Recommendation

**Proceed with Alternative A (Extract Method)** with the following structure:

### Phase 1: parseString Decomposition (S3776 cc52 → ~cc20)
1. Extract `parseBoLine(trimmed, currentCanId)` — BO_ header parsing
2. Extract `parseValLine(trimmed)` — VAL_ value table parsing
3. Flatten guard clauses for early returns

### Phase 2: parseSignalDefinition Decomposition (S3776 cc56 → ~cc15)
1. Extract `parseSignalMultiplicity(pos, line)` — multiplexor marker
2. Extract `parseBitLengthAndByteOrder(pos, line, out)` — bit spec
3. Extract `parseScaleOffset(pos, line, out)` — scale/offset group
4. Extract `parseMinMax(pos, line, out)` — min/max range
5. Extract `parseUnit(pos, line, out)` — optional unit

### Phase 3: parseOneValueEntry Cleanup (S134)
1. Flatten guard clauses for early returns
2. Extract whitespace skipping logic if beneficial

**Order of Operations**:
1. Extract BO_ and VAL_ helpers (parseString) — highest impact on cc52
2. Extract signal parsing helpers (parseSignalDefinition) — highest impact on cc56
3. Apply guard clauses for early returns — addresses remaining S134

**Estimated Complexity Reduction**:
- parseString: cc52 → ~cc20
- parseSignalDefinition: cc56 → ~cc15
- Overall: Both functions below S3776 threshold (15)

**Deferred**:
- Strategy pattern (Alternative B) — over-engineering for 3 line types
- Cursor object (Alternative C) — high risk, changes error semantics
- buildResult refactoring — not flagged by Sonar, separate concern

## Blind Test Requirements

The following behaviors MUST be guarded by test-arch2's blind tests (8 locked contracts):

1. **Multiplexor marker consumption** — 'M' or 'm' + digits must be consumed without corrupting signal parse
2. **Orphan signals** — SG_ before BO_ must be dropped (no owning message)
3. **Missing colon** — SG_ without ':' must be dropped, not partially parsed
4. **Non-numeric startBit** — startBit|bitLength with non-numeric must yield no signals
5. **Missing scale/offset** — SG_ without (...) must be dropped
6. **Partial value table** — VAL_ with malformed final entry must keep preceding well-formed entries
7. **Non-numeric message ID** — BO_ with non-numeric ID must yield no signals
8. **Non-numeric VAL_ ID** — VAL_ with non-numeric ID must be ignored

**All 8 contracts are already locked in DBCFileParser.test.cpp (lines 369-489)**

## Next Steps

1. **Await blind tests confirmation** — test-arch2 is writing 8 contracts
2. **Await user's S3776 view** — user must approve refactor approach
3. **Apply refactor one phase at a time** — each extract must be tested
4. **Run tests after each extraction** — verify no behavior changes
5. **Run Sonar scan** — confirm S3776 and S134 are resolved
6. **Commit one rule per commit** — cpp:S3776 prefix for complexity reductions

## Input Validation vs ASSERT Guidance

**Keep as Runtime Guards** (external DBC input):
- Missing delimiters (colon, pipe, brackets, parentheses)
- Non-numeric values where numbers expected
- Malformed line structures
- Missing line prefixes (BO_, SG_, VAL_)
- Out-of-bounds position accesses

**Convert to ASSERT** (impossible-by-grammar states):
- After successful delimiter confirmation (e.g., after `expect(':')` succeeds)
- After successful numeric parsing (e.g., after `from_chars` succeeds)
- Grammar-invariant states (e.g., DBC grammar guarantees certain characters)

## Questions for Team Lead

1. Is Alternative A (Extract Method) acceptable, or should we consider Strategy Pattern (Alternative B)?
2. Should Cursor object (Alternative C) be considered for a follow-up refactor?
3. Are there any other concerns about the proposed shape?

---

**Sources**:
- [Extract Method — Refactoring.Guru](https://refactoring.guru/extract-method)
- [Refactoring Long Methods with Extract Method — CodeSignal](https://codesignal.com/learn/courses/refactoring-by-leveraging-your-tests-with-java-junit/lessons/refactoring-long-methods-with-extract-method-pattern-in-java)
- [Stack Overflow — Tokenizer, Parser, and Lexer Definition](https://stackoverflow.com/questions/380455/looking-for-a-clear-definition-of-what-a-tokenizer-parser-and-lexers-are)
- [Qt QCanDbcFileParser Class Documentation](https://doc.qt.io/qt-6/qcandbcfileparser.html)
- [Cantools (GitHub) — Issue #52](https://github.com/eerimoq/cantools/issues/52)
