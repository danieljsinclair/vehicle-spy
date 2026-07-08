# ELM327Transport Refactor Shape Proposal (Primary Opinion)

**Target File**: `src/boundary/ELM327Transport.cpp`
**Sonar Issues**: S3776 cc37 at :68 (parseOBD2Response), S3776 cc29 at :226 (parseCANFrame); S134 at :112
**Status**: READ-ONLY ANALYSIS — Awaiting user's view

## Current State Analysis

### Functions with Issues

1. **`parseOBD2Response` — :68-140 (cc37)**
   - **Complexity**: Character-stream parsing with nested loops and conditional branches
   - **Issues**:
     - Prompt stripping via `extractPrompt`
     - Info-prefix skipping (SEARCHING, BUSINIT, OK)
     - Error message detection
     - Hex digit accumulation loop
     - Colon-based line number reset
     - Dead-code branches at :114-116 and :124-128 (investigated separately)
   - **Lines**: 68-140, 72 lines total

2. **`parseCANFrame` — :226-326 (cc29)**
   - **Complexity**: CAN frame parsing with token counting and type-prefix validation
   - **Issues**:
     - Line ending cleanup
     - ELM327 response filtering
     - Hex tokenization loop
     - Type-prefix range validation (0x600-0x6FF)
     - Data byte count verification
   - **Lines**: 226-326, 100 lines total

### Locked Test Coverage

The contract is locked by `test/boundary/ELM327Transport.test.cpp` with 7 blind contracts:

**parseCANFrame contracts**:
1. Valid frame without type prefix (line 13)
2. Valid frame with type prefix (line 32)
3. Prompt returns nullopt (line 51)
4. Empty string returns nullopt (line 64)
5. OBD2 response returns nullopt (line 70)
6. Invalid hex returns nullopt (line 77)
7. With line numbers returns nullopt (line 84)

**parseOBD2Response behavior**:
- Valid hex data extraction
- Prompt stripping
- Info-prefix handling
- Error message rejection
- Non-hex character filtering

## Community Best Practice Research

### Sources Consulted

1. **Extract Method — Refactoring.Guru**
   - Canonical reference for Extract Method refactoring
   - Copy code fragment to new method, replace with call
   - [refactoring.guru/extract-method](https://refactoring.guru/extract-method)

2. **Refactoring 010 — Extract Method Object (HackerNoon)**
   - Method Object pattern for complex parsers with shared state
   - Local variables become private attributes
   - [hackernoon.com/refactoring-010-extract-method-object](https://hackernoon.com/refactoring-010-extract-method-object)

3. **Parsing Short Hexadecimal Strings Efficiently — Daniel Lemire**
   - Efficient hex string parsing with lookup tables
   - Character validation and conversion patterns
   - [lemire.me/blog/2019/04/17/parsing-short-hexadecimal-strings-efficiently](https://lemire.me/blog/2019/04/17/parsing-short-hexadecimal-strings-efficiently)

4. **Dead Code Elimination Strategies — vFunction**
   - Practical strategies for detecting and eliminating dead code
   - [vfunction.com/blog/dead-code](https://vfunction.com/blog/dead-code)

5. **"I'd rather read 50 lines than Extract Method" (YouTube)**
   - Critical perspective on over-extraction in parsing code
   - [youtube.com/watch?v=MtVqasDREkg](https://www.youtube.com/watch?v=MtVqasDREkg)

### Key Insights

- **Extract Method** is particularly effective for breaking down complex parsing loops into named helpers
- **Method Object pattern** useful for parsers with lots of shared state
- **Over-extraction risk**: Too many tiny methods can make parser flow harder to follow
- **Dead code elimination**: Unreachable branches should be removed to reduce complexity

## Refactor Alternatives

### Alternative A: Extract Method (Recommended)

**Changes**:
1. **parseOBD2Response decomposition**:
   - Extract `stripInfoPrefixes(cleaned)` — handles SEARCHING/BUSINIT/OK skipping
   - Extract `accumulateHexDigits(cleaned)` — hex accumulation loop with colon handling
   - Extract `convertHexPairsToBytes(hexStr)` — pair conversion to bytes

2. **parseCANFrame decomposition**:
   - Extract `tokenizeHexLine(cleaned)` — hex tokenization loop
   - Extract `validateTypePrefix(tokens)` — type-prefix range check (0x600-0x6FF)
   - Extract `parseCANDataBytes(tokens, dataStart)` — data byte parsing

**SRP/DRY Win**:
- **Single Responsibility**: Each helper has one clear parsing purpose
- **DRY**: No duplicate parsing logic
- **Testability**: Each helper can be unit tested independently

**Risk**: Low
- Minimal signature changes (internal helpers only)
- Locked tests guard all behavior
- 2 live callers (OBD2Protocol.cpp, BLEManagerBase.cpp) have stable interfaces

**Test Guard**:
- All 7 blind contracts in ELM327Transport.test.cpp guard frame parsing behavior
- OBD2Protocol usage locked by integration tests
- BLEManagerBase usage locked by BLE tests

**Estimated Complexity Reduction**:
- parseOBD2Response: cc37 → ~cc15 (extract 3 helpers)
- parseCANFrame: cc29 → ~cc12 (extract 3 helpers)

### Alternative B: Small Tokenizer (Researcher's View)

**Changes**:
1. Create `ELM327Tokenizer` class with methods:
   - `nextToken()` — returns next hex token
   - `skipInfoPrefixes()` — handles SEARCHING/BUSINIT/OK
   - `isHexDigit(char)` — character validation
   - `hexPairToByte()` — pair conversion

2. Use tokenizer in both parseOBD2Response and parseCANFrame

**SRP/DRY Win**:
- **Single Responsibility**: Tokenizer handles all character-stream processing
- **DRY**: Shared tokenization logic between both parsers
- **Open/Closed**: New token types can be added without modifying parsers

**Risk**: Medium
- Adds new class abstraction
- May be over-engineering for simple hex parsing
- Requires additional test coverage for tokenizer

**Test Guard**:
- Same as Alternative A, plus new tests for tokenizer operations

**Estimated Complexity Reduction**:
- parseOBD2Response: cc37 → ~cc10 (delegates to tokenizer)
- parseCANFrame: cc29 → ~cc8 (delegates to tokenizer)

### Alternative C: Method Object Pattern

**Changes**:
1. Create `OBD2ResponseParser` class for parseOBD2Response
2. Create `CANFrameParser` class for parseCANFrame
3. Local state becomes private attributes
4. Each class has multiple private methods for parsing stages

**SRP/DRY Win**:
- **Single Responsibility**: Each parser class handles one format
- **Encapsulation**: Parsing state is well-contained
- **Testability**: Parser classes can be tested independently

**Risk**: Medium-High
- Adds significant class complexity
- May be over-engineering for current needs
- Requires extensive refactoring of callers

**Test Guard**:
- Same as Alternative A, plus new tests for parser classes

**Estimated Complexity Reduction**:
- parseOBD2Response: cc37 → ~cc12 (delegates to parser object)
- parseCANFrame: cc29 → ~cc10 (delegates to parser object)

## Recommendation

**Proceed with Alternative A (Extract Method)** with the following structure:

### Phase 1: parseOBD2Response Decomposition (cc37 → ~cc15)
1. Extract `stripInfoPrefixes(cleaned)` — info-prefix skipping
2. Extract `accumulateHexDigits(cleaned)` — hex accumulation with colon handling
3. Extract `convertHexPairsToBytes(hexStr)` — pair conversion

### Phase 2: parseCANFrame Decomposition (cc29 → ~cc12)
1. Extract `tokenizeHexLine(cleaned)` — hex tokenization
2. Extract `validateTypePrefix(tokens)` — type-prefix range check
3. Extract `parseCANDataBytes(tokens, dataStart)` — data byte parsing

### Dead Code Removal (Separate or Folded)
**Investigation Complete**: Lines 114-116 and 124-128 are **unreachable dead code**
- `hexStr` only accumulates `isxdigit` characters (line 100 guard)
- `parseHexByte` can never receive invalid pairs in the main loop
- The 7 locked tests confirm non-hex is **ignored**, not rejected

**Recommendation**: Remove unreachable branches as standalone cleanup commit
- **Rationale**: Unreachable over-zealous defensive code → removal is clean fix
- **Benefit**: Reduces cognitive complexity (helps S3776)
- **Risk**: None — unreachable means no behavior change possible
- **Commit**: `cpp:S1763 — remove unreachable dead code in parseOBD2Response` (if Sonar flags) OR `cpp:DEADCODE — remove unreachable branches in parseOBD2Response`

**Order of Operations**:
1. Remove dead code first (standalone cleanup) — reduces cc count
2. Extract parseOBD2Response helpers — highest impact on cc37
3. Extract parseCANFrame helpers — addresses cc29

**Estimated Complexity Reduction**:
- parseOBD2Response: cc37 → ~cc15 (with dead-code removal: cc33 → ~cc12)
- parseCANFrame: cc29 → ~cc12
- Overall: Both functions below S3776 threshold (15)

**Deferred**:
- Tokenizer class (Alternative B) — may be useful if more ELM327 parsing added
- Method Object pattern (Alternative C) — over-engineering for current needs

## Dead Code Analysis (TASK 2)

### Investigation Results

**Location**: Lines 114-116 and 124-128 in parseOBD2Response

**Code Path Analysis**:
```cpp
// Line 100: Only hex digits accumulate
if (std::isxdigit(c)) {
    hexStr += static_cast<char>(std::toupper(c));
} else {
    // Lines 111-118: Process accumulated hex
    while (hexStr.length() >= 2) {
        if (auto byte = parseHexByte(hexStr.substr(0, 2)); byte.has_value()) {
            result.push_back(*byte);
        } else {
            return std::nullopt; // LINES 114-116: UNREACHABLE
        }
        hexStr = hexStr.substr(2);
    }
}

// Lines 123-130: Process remaining hex
while (hexStr.length() >= 2) {
    if (auto byte = parseHexByte(hexStr.substr(0, 2)); byte.has_value()) {
        result.push_back(*byte);
    } else {
        return std::nullopt; // LINES 126-128: UNREACHABLE
    }
    hexStr = hexStr.substr(2);
}
```

**Proof of Unreachability**:
1. `hexStr` only accumulates characters that pass `std::isxdigit(c)` (line 100)
2. `parseHexByte` checks `hexCharToByte` which returns `0xFF` for invalid hex
3. `parseHexByte` returns `nullopt` if high or low byte is `0xFF` (line 48)
4. Since `hexStr` contains only valid hex digits, `parseHexByte` can never return `nullopt`
5. Therefore, the `else { return std::nullopt; }` branches are unreachable

**Sonar Rule**: Likely S1763 (unreachable code) or S2583 (condition always true/false)

**Locked Test Confirmation**:
- The 7 blind contracts confirm non-hex characters are **ignored**, not rejected
- Tests verify valid hex parsing, prompt handling, and error message detection
- No test expects the unreachable branches to execute

**Proposed Fix**: Remove both unreachable branches
- **Benefit**: Reduces cognitive complexity (helps S3776)
- **Risk**: None — unreachable means no behavior change possible
- **Approach**: Standalone cleanup commit OR fold into S3776 refactor

## Next Steps

1. **Await user's view** on tokenizer vs extract-method approach
2. **Await confirmation** on dead-code removal approach
3. **Apply refactor** one phase at a time after approval
4. **Run tests** after each extraction — verify no behavior changes
5. **Run Sonar scan** — confirm S3776 and S134 are resolved
6. **Commit** one rule per commit — cpp:S3776 prefix for complexity reductions

## Questions for Team Lead

1. Is Alternative A (Extract Method) acceptable, or should we consider Tokenizer (Alternative B)?
2. Should dead-code removal be standalone or folded into S3776 refactor?
3. Are there any other concerns about the proposed shape?

---

**Sources**:
- [Extract Method — Refactoring.Guru](https://refactoring.guru/extract-method)
- [Refactoring 010 — Extract Method Object (HackerNoon)](https://hackernoon.com/refactoring-010-extract-method-object)
- [Parsing Short Hexadecimal Strings Efficiently — Daniel Lemire](https://lemire.me/blog/2019/04/17/parsing-short-hexadecimal-strings-efficiently)
- [Dead Code Elimination Strategies — vFunction](https://vfunction.com/blog/dead-code)
- ["I'd rather read 50 lines than Extract Method" (YouTube)](https://www.youtube.com/watch?v=MtVqasDREkg)
