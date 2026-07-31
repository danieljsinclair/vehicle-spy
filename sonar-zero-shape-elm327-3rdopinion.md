# ELM327Transport S3776 Third-Opinion Analysis

**Independent Third Opinion** for ELM327Transport cognitive complexity reduction (S3776)  
**Research Date:** 2025-01-06  
**Analyst:** Researcher Agent  
**Target:** `ELM327Transport::parseOBD2Response` + `parseCANFrame` (`src/boundary/ELM327Transport.cpp` — S3776 cc37 at :68, cc29 at :226; S134 at :112)  
**Callers:** `OBD2Protocol.cpp` (line 16), `BLEManagerBase.cpp` (line 81, 357) — **TWO live production callers, behavior preservation critical**

---

## Executive Summary

The ELM327Transport exhibits **moderate cognitive complexity** concentrated in two parsing functions with **shared responsibility**: both parse ASCII hex responses from an ELM327 OBD2 adapter, but with different semantics (OBD2 query responses vs. CAN monitor frames). 

The **obvious extract-method approach would be counterproductive** — it would fragment the **stateful hex-accumulation pipeline** that is central to both parsers. Community best practices for ELM327 response parsing suggest **two better shapes**:

1. **ELM327 Response Tokenizer** — Extract a tokenizer abstraction that provides hex-consumption primitives (prompt strip, prefix strip, hex accumulate, colon reset), reducing repeated manual cursor-walking code.
2. **State Machine Parser** — Model the ELM327 response format as explicit states (PROMPT, PREFIX, HEX_DATA, LINE_NUMBER), making control flow data-driven rather than nested if-else.

**RECOMMENDATION:** **ELM327 Response Tokenizer** (Shape A). It aligns with the ELM327 protocol's **character-stream nature**, respects SOLID/SRP/DRY/KISS, and yields testable components without introducing a full state-machine framework.

**CRITICAL CONTEXT:** This parser has **TWO live production callers** (`OBD2Protocol` and `BLEManagerBase`). Any refactor must preserve **7 specific behaviors** that the pending blind tests must guard.

---

## Complexity Hotspot Analysis

### `parseOBD2Response` (lines 68-140) — cc37

**What it does:** Parses ELM327 OBD2 query responses (e.g., `010C` → `0: 41 0C 1A F8`) into binary byte vectors.

**Complexity sources:**
- **Pipeline of transformations:** extractPrompt → strip INFO prefixes → check errors → hex accumulate
- **Manual hex accumulation:** Loop over characters, build `hexStr` buffer, process pairs via `parseHexByte()`
- **Colon reset logic:** If `':'` encountered, clear `hexStr` (prevents line numbers like "014:" from being parsed as data)
- **Trailing hex check:** After loop, if odd hex digits remain, return `nullopt`
- **Multiple early returns:** Empty response, prompt-only, error messages, invalid hex, no data

**Why extraction-method alone fails:**
The hex accumulation loop is a **stateful pipeline** — characters are inspected, transformed, accumulated, and consumed. Extracting pieces (e.g., `stripInfoPrefixes()`, `accumulateHex()`, `checkTrailingHex()`) would create **artificial boundaries** in what is fundamentally a **single-pass character stream process**. The reader would need to jump between functions to understand one character's journey.

### `parseCANFrame` (lines 226-326) — cc29

**What it does:** Parses ELM327 CAN monitor mode responses (e.g., `123 01 02 03 04 05 06 07 08`) into `CANFrame` structs.

**Complexity sources:**
- **Repeated validation from `parseOBD2Response`:** Check for prompt-only, error messages, line numbers
- **Tokenization loop:** Split on non-hex characters (similar to hex accumulation in `parseOBD2Response`)
- **Type-prefix detection:** If 10 tokens, check if first is type byte (0x600-0x6FF range)
- **Exact byte count validation:** Must have exactly 8 data bytes after CAN ID
- **Multiple early returns:** Empty line, prompt-only, error messages, colon present, wrong token count, invalid type prefix, wrong data byte count

**Why extraction-method alone fails:**
This function **duplicates `parseOBD2Response`'s validation logic** (prompt check, error check, colon check). Extracting validation helpers would help, but the **root complexity is the repeated tokenization pattern**. Both functions manually iterate characters, checking `isxdigit()`, accumulating, and splitting on delimiters.

---

## Production Caller Context

### Caller 1: `OBD2Protocol::processIncomingData` (line 16)

```cpp
auto binaryData = ELM327Transport::parseOBD2Response(std::string(asciiData));
if (!binaryData) {
    return;
}
```

**Usage:** Converts raw ASCII from ELM327 to binary, then feeds to VIN/fuel type detector. Expects `nullopt` for non-OBD2 responses (prompts, errors, CAN frames).

### Caller 2: `BLEManagerBase::parseASCIIResponseToBinary` (line 81)

```cpp
auto binaryData = boundary::ELM327Transport::parseOBD2Response(response);
return binaryData.value_or(std::vector<uint8_t>{});
```

**Usage:** Converts BLE ASCII data to binary. Empty vector on parse failure (used as "no valid OBD2 data" signal).

### Caller 3: `BLEManagerBase::invokeDataCallback` (line 357)

```cpp
auto frame = boundary::ELM327Transport::parseCANFrame(asciiStr);
if (frame && frame->data.size() == 8) {
    // Process CAN frame
}
```

**Usage:** Parses CAN monitor mode frames. Expects `nullopt` for OBD2 responses, prompts, errors.

**Risk Assessment:**
- **High caller sensitivity:** Both callers rely on specific `nullopt` behaviors for non-matching responses.
- **Behavior preservation critical:** Any refactor must maintain the exact `nullopt` conditions (odd trailing hex, invalid hex mid-stream, BUSINIT prefix, colon reset, etc.).

---

## Community Best Practices for ELM327 Response Parsing

### Source 1: [ELM327 Datasheet — SparkFun](https://cdn.sparkfun.com/assets/learn_tutorials/8/3/ELM327DS.pdf)

**Key insights:**
- ELM327 sends **ASCII character streams** with specific prompts (`>`), prefixes (`BUSINIT:`, `SEARCHING`), and formats.
- Responses can be **multi-line** with line numbers (e.g., `014: 41 0C 1A F8`) where the colon indicates continuation.
- The adapter uses **state-based response handling** — it knows when to expect headers, data, or prompts based on the command sent.

**Relevance:** The parser should model ELM327's **character-stream protocol**. The current code's manual hex accumulation is correct in spirit but could be abstracted.

### Source 2: [python-OBD Library — GitHub](https://github.com/brendan-w/python-OBD)

**Key insights:**
- python-OBD uses a **connection-based approach** where the response parser is aware of the command sent (query vs. monitor mode).
- The implementation separates **response cleaning** (strip prompt, headers) from **hex decoding**.
- Error messages are detected via regex/string matching early in the pipeline.

**Relevance:** Separating concerns (cleaning vs. parsing) is valid, but the **hex decoding loop remains character-by-character** — no way around that for streaming ASCII.

### Source 3: [Generalizing ELM327 Payloads — Reddit r/CarHacking](https://www.reddit.com/r/CarHacking/comments/19a0kd4/generalizing_elm327_payloads/)

**Key insights:**
- ELM327 payloads vary by protocol (ISO-TP, CAN, J1850), but the **ASCII hex format is universal**.
- Community discussion emphasizes **robust prefix handling** — `BUSINIT:`, `SEARCHING...`, `OK` must be stripped before hex parsing.
- Line numbers with colons are a **common gotcha** — naive parsers treat the line number as data.

**Relevance:** The current code correctly handles colons (reset `hexStr`), but this logic is **embedded in the hex loop**. A tokenizer could make this explicit.

---

## Alternative Decomposition Shapes

### SHAPE A: ELM327 Response Tokenizer (RECOMMENDED)

**Concept:** Extract a **tokenizer abstraction** that encapsulates the ELM327-specific response processing pipeline:

```cpp
class ELM327Tokenizer {
    std::string_view input_;
    std::size_t pos_ = 0;
    
public:
    explicit ELM327Tokenizer(std::string_view response);
    
    // Pipeline primitives
    bool stripPrompt();                    // Remove trailing '>'
    bool stripInfoPrefixes();              // Remove BUSINIT:, SEARCHING..., OK
    bool checkErrorMessages();             // Return true if error found
    std::optional<std::vector<uint8_t>> accumulateHex();  // Parse hex bytes, handle ':' reset
    bool isPromptOnly() const;             // Check if only '>' remains
};
```

**What it extracts:**
- **ELM327 protocol knowledge:** Prompt stripping, prefix handling, error detection
- **Hex accumulation loop:** Character-by-character parsing with colon reset
- **Repeated validation logic:** Shared between `parseOBD2Response` and `parseCANFrame`

**SRP/DRY win:**
- **SRP:** Tokenizer has one job: transform ELM327 ASCII responses into clean hex streams. `parseOBD2Response` focuses on OBD2 semantics (mode byte validation).
- **DRY:** Eliminates duplicated prompt-check, error-check, and hex-accumulation code across both parsers.

**Risk:**
- **Medium:** Tokenizer is a new abstraction with specific ELM327 semantics. Must be thoroughly tested.
- **Caller behavior preservation:** Must maintain exact `nullopt` conditions — tokenizer API must expose "invalid input" state clearly.

**Behavior blind tests must guard:**
1. **Odd trailing hex digits** → `nullopt` (e.g., `"41 0C 1"` is invalid)
2. **Invalid hex mid-stream** → `nullopt` (e.g., `"41 0C XX 1A"` returns `nullopt` at `XX`)
3. **BUSINIT prefix strip** → data after `BUSINIT:` is parsed (prefix removed)
4. **OK prefix strip** → `"OK 41 0C 1A F8"` parses to `[0x41, 0x0C, 0x1A, 0xF8]`
5. **Colon reset** → `"014: 41 0C 1A F8"` ignores `014`, parses remaining hex
6. **Prompt-only** → `">"` returns `nullopt`
7. **8-data-byte exactness** (CAN frames) → frames with ≠ 8 bytes return `nullopt`
8. **Type-prefix range boundary** → CAN frames with type byte outside 0x600-0x6FF return `nullopt`

**Why tech-arch's "extract-method + guard clauses" might miss this:**
Extract-method would create functions like `stripPrompt()`, `stripInfoPrefixes()`, `accumulateHex()`. But these functions **still contain character-loop code** — the complexity is moved, not eliminated. A tokenizer centralizes the **character-stream processing model**, making it testable and reusable. The tokenizer also **encapsulates ELM327 protocol knowledge** (what counts as an error message, what prefixes exist) rather than scattering it across multiple functions.

---

### SHAPE B: State Machine Parser

**Concept:** Model the ELM327 response parsing as explicit states:

```cpp
enum class ParseState {
    PROMPT_STRIP,
    PREFIX_CHECK,
    HEX_DATA,
    LINE_NUMBER,
    ERROR_DETECTED
};

class ELM327ResponseParser {
    ParseState state_ = ParseState::PROMPT_STRIP;
    std::string hexBuffer_;
    
public:
    std::optional<std::vector<uint8_t>> parse(std::string_view response);
    
private:
    void processPrompt(char c);
    void processPrefix(char c);
    void processHexData(char c);
    void processLineNumber(char c);
};
```

**What it extracts:**
- **Control flow structure:** The nested if-else chain becomes a state transition table
- **Hex accumulation state:** The `hexBuffer_` is explicitly managed as part of state

**SRP/DRY win:**
- **SRP:** Each state handler focuses on one context (prompt, prefix, hex, line number).
- **DRY:** State transitions are data-driven; no repeated `if-else if` chains.

**Risk:**
- **High:** State machine is a **paradigm shift**, not a refactor. Changes the mental model from "process character loop" to "state transitions."
- **Over-engineering:** ELM327 responses are simple enough that a full state machine is disproportionate.

**Behavior blind tests must guard:**
- Same as Shape A, plus: **state transition correctness** (e.g., colon in HEX_DATA state → LINE_NUMBER state → back to HEX_DATA).

**Why tech-arch's "extract-method + guard clauses" might miss this:**
Extract-method doesn't address the **stateful nature** of ELM327 parsing. The current code implicitly tracks state (are we in hex accumulation? are we skipping a line number?). Making this explicit via a state machine could reduce nesting, but the **complexity cost is high** for a simple protocol.

---

### SHAPE C: Unified Response Parser (NOT RECOMMENDED)

**Concept:** Create a single `parseELM327Response()` that detects response type (OBD2 vs. CAN frame) and dispatches accordingly.

**SRP/DRY win:**
- **High:** Eliminates duplicated validation code between `parseOBD2Response` and `parseCANFrame`.

**Risk:**
- **Very High:** Merges two distinct semantics (OBD2 query responses vs. CAN monitor frames) into one function. Violates SRP.
- **Caller disruption:** Both callers expect specific return types (`vector<uint8_t>` vs. `CANFrame`). Changing the API would require changes at both call sites.

**Behavior blind tests must guard:**
- Same as Shape A, plus: **response type detection correctness** (OBD2 vs. CAN frame).

**Why NOT recommended:**
This violates SRP by making one function do two things. The current separation (OBD2 parser vs. CAN frame parser) is **semantically correct**. Merging them would reduce cognitive complexity locally but increase it globally (one function to understand for two different use cases).

---

## Recommendation: SHAPE A (ELM327 Response Tokenizer)

### Why SHAPE A over SHAPE B?

1. **Aligns with ELM327's character-stream nature:** ELM327 sends ASCII characters sequentially. A tokenizer that provides "consume primitives" matches this naturally.
2. **Respects SOLID/SRP/DRY/KISS:** Tokenizer is a **small, focused class** with a single responsibility. It eliminates repeated character-loop code without over-abstracting.
3. **Testable:** Tokenizer methods are pure functions (given a string, return transformed string or `nullopt`). Easy to unit-test.
4. **Incremental:** Can be introduced gradually. Start with `parseOBD2Response`, then apply to `parseCANFrame`.
5. **No paradigm shift:** Unlike a state machine, tokenizer is still fundamentally a character-loop abstraction — just with cleaner boundaries.

### Why NOT SHAPE B?

State machines are excellent for **complex protocols with many states** (e.g., ISO-TP multi-frame responses). ELM327's ASCII responses are simple enough that a full state machine adds more complexity (state enum, transition table) than it removes.

### What Shape A Looks Like in Practice

**Step 1:** Define `ELM327Tokenizer` class (internal to .cpp):

```cpp
namespace {
class ELM327Tokenizer {
    std::string_view input_;
    std::size_t pos_ = 0;
    
public:
    explicit ELM327Tokenizer(std::string_view response) : input_(response) {}
    
    bool stripPrompt() {
        if (auto promptPos = input_.find('>'); promptPos != std::string::npos) {
            input_ = input_.substr(0, promptPos);
            return true;
        }
        return false;
    }
    
    bool stripInfoPrefixes() {
        // Remove BUSINIT:, SEARCHING..., OK prefixes
        // (Implementation from current parseOBD2Response lines 76-90)
        return true;
    }
    
    bool isErrorMessage() const {
        // Check for ERROR, NO DATA, etc.
        // (Implementation from current isErrorMessage())
        return false;
    }
    
    std::optional<std::vector<uint8_t>> accumulateHex() {
        std::vector<uint8_t> result;
        std::string hexStr;
        
        for (std::size_t i = 0; i < input_.size(); ++i) {
            char c = input_[i];
            if (std::isxdigit(c)) {
                hexStr += static_cast<char>(std::toupper(c));
            } else if (c == ':') {
                hexStr.clear();  // Reset on line number
            } else {
                while (hexStr.length() >= 2) {
                    if (auto byte = parseHexByte(hexStr.substr(0, 2)); byte.has_value()) {
                        result.push_back(*byte);
                    } else {
                        return std::nullopt;  // Invalid hex
                    }
                    hexStr = hexStr.substr(2);
                }
            }
        }
        
        // Process remaining
        while (hexStr.length() >= 2) {
            if (auto byte = parseHexByte(hexStr.substr(0, 2)); byte.has_value()) {
                result.push_back(*byte);
            } else {
                return std::nullopt;
            }
            hexStr = hexStr.substr(2);
        }
        
        if (!hexStr.empty()) {
            return std::nullopt;  // Odd trailing hex
        }
        
        return result;
    }
    
    bool isPromptOnly() const {
        return input_.empty() || input_ == ">";
    }
};
}
```

**Step 2:** Refactor `parseOBD2Response` to use tokenizer:

```cpp
std::optional<std::vector<uint8_t>> ELM327Transport::parseOBD2Response(const std::string& response) {
    ELM327Tokenizer tokenizer(response);
    
    tokenizer.stripPrompt();
    if (tokenizer.isPromptOnly()) return std::nullopt;
    
    tokenizer.stripInfoPrefixes();
    if (tokenizer.isErrorMessage()) return std::nullopt;
    
    auto result = tokenizer.accumulateHex();
    if (!result || result->empty()) return std::nullopt;
    
    return result;
}
```

**Step 3:** Apply tokenizer to `parseCANFrame` (similar pattern).

**Step 4:** Unit-test tokenizer independently (test prompt strip, prefix strip, hex accumulation, colon reset, odd trailing hex).

### Complexity Reduction Estimate

- **`parseOBD2Response`:** cc37 → ~cc15 (tokenizer primitives eliminate nesting, character-loop complexity hidden in tokenizer)
- **`parseCANFrame`:** cc29 → ~cc18 (tokenizer handles validation, leaving only CAN-specific logic)
- **Overall:** Cognitive complexity halved while **preserving behavior** and **improving testability**.

---

## Risks and Mitigations

### Risk 1: Behavior Regression

**Concern:** Tokenizer changes the exact `nullopt` conditions, breaking callers.

**Mitigation:**
- **Blind tests first:** Write the 7 behavior tests BEFORE refactoring (TDD gate task).
- Tests must cover:
  - Odd trailing hex → `nullopt`
  - Invalid hex mid-stream → `nullopt`
  - BUSINIT/OK prefix strip
  - Colon reset (`:` clears hex buffer)
  - Prompt-only → `nullopt`
  - 8-data-byte exactness (CAN frames)
  - Type-prefix range boundary (0x600-0x6FF)

### Risk 2: Over-Abstraction

**Concern:** Tokenizer becomes a "do-everything string utility" class.

**Mitigation:**
- Keep tokenizer **ELM327-specific**: only methods needed for ELM327 response parsing.
- No regex, no Unicode, no "smart" features.
- Tokenizer is **internal to the .cpp** (not in header), so usage is contained.

### Risk 3: Performance Regression

**Concern:** Tokenizer adds overhead vs. raw character loops.

**Mitigation:**
- Tokenizer uses `std::string_view` (no copies).
- Methods are simple enough to be inlined.
- ELM327 responses are small (typically < 100 bytes), so performance is not critical.

---

## Behaviors Blind Tests Must Guard

Per the task description, 7 specific behaviors must be tested:

1. **Odd trailing hex digits** → `nullopt`
   - Input: `"41 0C 1"`
   - Expected: `std::nullopt` (odd hex remaining)

2. **Invalid hex mid-stream** → `nullopt`
   - Input: `"41 0C XX 1A F8"`
   - Expected: `std::nullopt` (fails at `XX`)

3. **BUSINIT prefix strip** → data parsed
   - Input: `"BUSINIT: 41 0C 1A F8 >"`
   - Expected: `[0x41, 0x0C, 0x1A, 0xF8]` (prefix removed)

4. **OK prefix strip** → data parsed
   - Input: `"OK 41 0C 1A F8 >"`
   - Expected: `[0x41, 0x0C, 0x1A, 0xF8]` (prefix removed)

5. **Colon reset (`:`)** → line number ignored
   - Input: `"014: 41 0C 1A F8 >"`
   - Expected: `[0x41, 0x0C, 0x1A, 0xF8]` (line number `014` ignored)

6. **Prompt-only** → `nullopt`
   - Input: `">"`
   - Expected: `std::nullopt` (prompt only, no data)

7. **8-data-byte exactness** (CAN frames)
   - Input: `"123 01 02 03 04 05 06 07 08"` (9 tokens)
   - Input: `"123 01 02 03 04 05 06 07"` (8 tokens)
   - Input: `"123 01 02 03 04 05 06 07 08 09"` (10 tokens without type prefix)
   - Expected: Only 9-token input (CAN ID + 8 data bytes) returns valid `CANFrame`

8. **Type-prefix range boundary** (CAN frames)
   - Input: `"600 123 01 02 03 04 05 06 07 08"` (type 0x600)
   - Input: `"6FF 123 01 02 03 04 05 06 07 08"` (type 0x6FF)
   - Input: `"5FF 123 01 02 03 04 05 06 07 08"` (type 0x5FF, invalid)
   - Expected: 0x600-0x6FF range accepted, others return `nullopt`

---

## What Tech-Arch's "Extract Method + Guard Clauses" Might Miss

The primary decomposition pattern ("extract-method + guard clauses") is excellent for **business logic complexity**. However, for **ELM327 response parsing**, it has limitations:

1. **Fragments the character-stream pipeline:** When each transformation (prompt strip, prefix strip, hex accumulate) is a separate function, the reader loses sight of the **sequential nature** of ELM327 responses. The mental model becomes "call these functions in this order" rather than "this is an ELM327 response flowing through a processing pipeline."

2. **Duplicates character-loop code:** Each extracted method would still contain the `for (char c : input)` loop pattern. The complexity is **moved, not eliminated**.

3. **Misses the tokenizer abstraction:** A tokenizer is a **third concept** — neither business logic nor control flow. It's a **primitive for character-stream processing**. Extract-method doesn't create primitives; it only names existing logic.

4. **Doesn't address shared validation:** Both `parseOBD2Response` and `parseCANFrame` perform identical validation (prompt check, error check, colon check). Extract-method would duplicate this duplication.

5. **Violates DRY:** Character-loop code is repeated across both functions. Extract-method would create separate functions for each, but the **fundamental pattern** (iterate characters, check `isxdigit`, accumulate) is still repeated.

**ELM327 Response Tokenizer (Shape A)** addresses all these gaps by:
- **Centralizing character-loop processing** in a reusable class
- **Naming primitives** (`stripPrompt`, `stripInfoPrefixes`, `accumulateHex`) rather than business concepts
- **Flattening control flow** via tokenizer's fail-fast API (return `false`/`nullopt` on error)
- **Preserving pipeline structure** — `parseOBD2Response` reads like "strip prompt → strip prefixes → check errors → accumulate hex"

---

## Conclusion

The ELM327Transport's cognitive complexity is **moderate and concentrated** in two parsing functions with **shared character-stream processing logic**. The obvious extraction-method approach would **fragment the pipeline** and duplicate character-loop code.

**Two better shapes** emerge from community best practices:

1. **ELM327 Response Tokenizer (Shape A)** — Extract a tokenizer class that provides character-stream processing primitives. Aligns with ELM327's ASCII nature, respects SOLID/SRP/DRY/KISS, and yields testable components.

2. **State Machine Parser (Shape B)** — Model response parsing as explicit states (PROMPT, PREFIX, HEX_DATA, LINE_NUMBER). Makes control flow data-driven but adds state-transition complexity.

**RECOMMENDATION: SHAPE A (ELM327 Response Tokenizer).**

This shape:
- Reduces cognitive complexity by **eliminating repeated character-loop code**
- Preserves the **character-stream pipeline structure** in the code
- Introduces a **testable, reusable primitive** (tokenizer)
- Requires **no new dependencies** and can be introduced incrementally
- Aligns with community best practices for ELM327 response handling (python-OBD, ELM327 datasheet)

**Risks:** Medium — tokenizer is a new abstraction with ELM327-specific semantics. Blind tests MUST guard the 7 specific behaviors (odd trailing hex, invalid hex mid-stream, prefix strips, colon reset, prompt-only, 8-byte exactness, type-prefix range).

**Implementation priority:** Write blind tests first (7 behaviors), then introduce tokenizer, refactor `parseOBD2Response` to use it, then apply to `parseCANFrame`.

---

## Sources

1. **[ELM327 Datasheet — SparkFun](https://cdn.sparkfun.com/assets/learn_tutorials/8/3/ELM327DS.pdf)** — Official ELM327 specification; character-stream format, prompts, prefixes, line numbers.
2. **[python-OBD Library — GitHub](https://github.com/brendan-w/python-OBD)** — Reference implementation for ELM327 response parsing; separation of cleaning vs. hex decoding.
3. **[Generalizing ELM327 Payloads — Reddit r/CarHacking](https://www.reddit.com/r/CarHacking/comments/19a0kd4/generalizing_elm327_payloads/)** — Community discussion on prefix handling and hex parsing best practices.
4. **[Initialization of OBD adapter — Stack Overflow](https://stackoverflow.com/questions/13764442/initialization-of-obd-adapter)** — BUSINIT prefix explanation.
5. **[How can I improve ELM327 response stability on Arduino — Arduino Stack Exchange](https://arduino.stackexchange.com/questions/98729/how-can-i-improve-elm327-response-stability-on-arduino-after-setup-verified-with)** — Response parsing robustness considerations.

---

**Independent Third Opinion provided by Researcher Agent.**
