# Enable Amendments at Genesis — Full Assessment

## Analogy: The Restaurant Grand Opening

Think of xrpld like a restaurant. Amendments are features on the menu. Normally when the restaurant opens (genesis ledger), it starts with a **blank menu** and the staff has to **vote** on which dishes to add. Even after everyone agrees, there's a **2-week waiting period** before the dishes actually get served.

This change says: "If we're opening a brand new restaurant from scratch, just put **everything we know how to cook** on the menu from day one."

---

## Before vs After

```
                    BEFORE (master)
                    ═══════════════

  Fresh Genesis Start
        │
        ▼
  ┌─────────────────────────┐
  │  startGenesisLedger()   │
  │                         │
  │  getDesired() ──────────┼──► Returns only amendments where:
  │                         │    • supported = yes
  │                         │    • vote = DefaultYes
  │                         │    • NOT vetoed
  │                         │
  │  (leaves out DefaultNo  │    ~30 amendments
  │   and vetoed ones)      │
  └─────────┬───────────────┘
            │
            ▼
  ┌─────────────────────────┐
  │  Genesis Ledger created │    Partial amendments enabled
  │  with partial list      │
  └─────────────────────────┘
            │
            ▼
  ┌─────────────────────────┐
  │  Wait ~256 ledgers for  │
  │  flag ledger...         │
  │  Then wait 2 weeks (or  │    ◄── Even with config override,
  │  min 15 min) for the    │        minimum 15 minutes
  │  rest to get majority   │
  └─────────────────────────┘



                    AFTER (this branch)
                    ═══════════════════

  Fresh Genesis Start
        │
        ▼
  ┌─────────────────────────┐
  │  startGenesisLedger()   │
  │                         │
  │  getAllSupported() ──────┼──► Returns ALL amendments where:
  │                         │    • supported = yes
  │                         │    (ignores vote behavior,
  │                         │     ignores veto status)
  │                         │
  │                         │    ~50+ amendments
  └─────────┬───────────────┘
            │
            ▼
  ┌─────────────────────────┐
  │  Genesis Ledger created │    ALL supported amendments
  │  with full list         │    enabled immediately!
  └─────────────────────────┘
            │
            ▼
            ✓ Ready to go, no waiting
```

---

## The 4 Changed Files

### 1. `src/xrpld/app/misc/detail/AmendmentTable.cpp` — The new `getAllSupported()` method

```
  ┌──────────────────────────────────────┐
  │         amendmentMap_                 │
  │                                      │
  │  ┌──────────┬───────────┬─────────┐  │
  │  │ Amendment│ supported │ enabled │  │
  │  ├──────────┼───────────┼─────────┤  │
  │  │ FeatureA │    yes    │   no    │──┼──► included ✓
  │  │ FeatureB │    yes    │   no    │──┼──► included ✓
  │  │ FixC     │    yes    │  yes    │──┼──► EXCLUDED (already on)
  │  │ FeatureD │    no     │   no    │──┼──► EXCLUDED (unsupported)
  │  │ FixE     │    yes    │   no    │──┼──► included ✓ (even if vetoed!)
  │  └──────────┴───────────┴─────────┘  │
  └──────────────────────────────────────┘

  getDesired():       supported && vote==up && !vetoed  (selective)
  getAllSupported():   supported && !enabled             (everything)
```

The key difference: `getDesired()` respects vote behavior and vetoes (it's meant for the normal consensus voting process). `getAllSupported()` grabs **everything the binary knows how to run**.

### 2. `include/xrpl/ledger/AmendmentTable.h` — Two new public APIs

```
  AmendmentTable (abstract interface)
  ┌──────────────────────────────────────────┐
  │  existing:                               │
  │    getDesired()          ← voting only   │
  │    doValidatedLedger()   ← flag-gated    │
  │                                          │
  │  NEW:                                    │
  │    getAllSupported()      ← everything    │
  │    syncWithLedger()      ← bypass gate   │
  └──────────────────────────────────────────┘
```

`syncWithLedger()` is a convenience wrapper that calls `doValidatedLedger()` directly, bypassing the normal "is this a flag ledger?" check. This is used when loading a ledger from a file.

### 3. `src/xrpld/app/main/Application.cpp` — Two call sites changed

**Site A: `startGenesisLedger()` (line ~1623)**

```
BEFORE:  getDesired()       →  partial amendments
AFTER:   getAllSupported()   →  all amendments (only when START_UP == FRESH)
```

**Site B: `loadOldLedger()` (line ~1972)**

```
NEW:  m_amendmentTable->syncWithLedger(loadLedger)
```

This ensures that when loading a previously-saved ledger (e.g., from `--load` or `--ledger`), the AmendmentTable immediately knows which amendments are active — without waiting for a flag ledger boundary.

### 4. `src/test/app/AmendmentTable_test.cpp` — Unit test

Tests that:

- `getAllSupported()` returns more results than `getDesired()`
- All yes/enabled/vetoed/obsolete amendments are included
- Unsupported amendments are excluded
- Once an amendment is enabled, it drops out of the list

---

## Complete Startup Flow

```
main() [Main.cpp]
  │
  ├─ parse CLI flags:
  │    --start        → StartUpType::FRESH
  │    --ledger       → StartUpType::LOAD (or REPLAY with --replay)
  │    --ledgerfile   → StartUpType::LOAD_FILE
  │    --net          → StartUpType::NETWORK
  │    (default)      → StartUpType::NORMAL
  │
  ├─ make_Application()
  │
  ├─ app->setup() [Application.cpp ~1110-1250]
  │    │
  │    ├─ initRelationalDatabase() + initNodeStore()
  │    │
  │    ├─ CREATE AmendmentTable [line ~1191-1197]
  │    │    ├─ reads supportedAmendments() from features.macro
  │    │    ├─ reads config [veto_amendments] and [amendments] sections
  │    │    └─ make_AmendmentTable(registry, majorityTime, supported, up, down, journal)
  │    │
  │    └─ STARTUP PATH DECISION [line ~1202-1246]
  │         │
  │         ├─ FRESH ──────────► startGenesisLedger()
  │         │                      │
  │         │                      ├─ getAllSupported() ← NEW: returns ALL supported
  │         │                      ├─ Ledger(create_genesis, amendments)
  │         │                      │    └─ writes amendments into SLE at keylet::amendments()
  │         │                      ├─ store genesis, create next ledger
  │         │                      └─ create OpenLedger
  │         │
  │         ├─ LOAD/LOAD_FILE ─► loadOldLedger()
  │         │  REPLAY              │
  │         │                      ├─ load ledger from file/db/hash
  │         │                      ├─ validate structure
  │         │                      ├─ switch LCL, set validated, create OpenLedger
  │         │                      ├─ syncWithLedger() ← NEW: immediate amendment sync
  │         │                      └─ replay if requested
  │         │
  │         ├─ NETWORK ────────► startGenesisLedger() (empty amendments)
  │         │                    + setNeedNetworkLedger()
  │         │
  │         └─ NORMAL ─────────► startGenesisLedger() (empty amendments)
  │
  ├─ app->start(true)  — start timers and job queue
  │
  └─ app->run()  — blocks until shutdown
       │
       └─ consensus loop
            ├─ OperatingMode: DISCONNECTED → CONNECTED → SYNCING → TRACKING → FULL
            └─ ConsensusMode: observing → proposing (when FULL + validator keys)
```

---

## Safety Analysis: Impact on Non-Genesis Nodes

### Critical Question: Can this break existing nodes?

**Answer: No.** Here's the exhaustive analysis:

### Path-by-path verification

| Startup Type                 | What happens                                    | Changed?    | Safe?           |
| ---------------------------- | ----------------------------------------------- | ----------- | --------------- |
| `NORMAL` (default)           | `startGenesisLedger()` with empty `{}`          | No change   | Yes             |
| `NETWORK`                    | `startGenesisLedger()` with empty `{}`          | No change   | Yes             |
| `FRESH` (`--start`)          | `startGenesisLedger()` with `getAllSupported()` | **Changed** | Yes (intended)  |
| `LOAD` (`--ledger`)          | `loadOldLedger()` + `syncWithLedger()`          | **Changed** | Yes (see below) |
| `LOAD_FILE` (`--ledgerfile`) | `loadOldLedger()` + `syncWithLedger()`          | **Changed** | Yes (see below) |
| `REPLAY` (`--replay`)        | `loadOldLedger()` + `syncWithLedger()`          | **Changed** | Yes (see below) |

### Why `getAllSupported()` is safe for non-FRESH paths

The conditional on line ~1623 is:

```cpp
std::vector<uint256> const initialAmendments = (config_->START_UP == StartUpType::FRESH)
    ? m_amendmentTable->getAllSupported()
    : std::vector<uint256>{};
```

Only `FRESH` triggers `getAllSupported()`. All other startup types get an empty vector, which is identical to master behavior.

### Why `syncWithLedger()` in `loadOldLedger()` is safe

`syncWithLedger()` calls `doValidatedLedger(seq, enabled, majority)` directly. On master, this would have been called via the gated path the next time a flag ledger boundary was crossed. Since `lastUpdateSeq_` starts at 0, the `needValidatedLedger` check uses unsigned underflow: `(0-1)/256 = 16777215`, which means it would return `true` for ANY ledger sequence. So the first call to the gated path would have done the exact same thing.

**The change just makes it happen sooner** (before replay processing, instead of during normal ledger processing). If anything, this is an improvement — the AmendmentTable is now consistent before replay begins.

### Race conditions: None

`m_amendmentTable` is fully constructed at lines 1191-1197 (inside `setup()`), well before `startGenesisLedger()` is called at line 1208. The table is properly initialized. `enable()` and `doValidatedLedger()` each independently acquire and release `mutex_`. No recursive/deadlock risk.

### Levelization: No violations

`AmendmentTable.h` is in `include/xrpl/ledger/` (level 6). The new methods only call existing methods from the same header and `xrpl/ledger/View.h` (same level).

---

## Test Coverage Analysis

### What `testGetAllSupported()` DOES test

| Assertion                                         | Covered |
| ------------------------------------------------- | ------- |
| `getAllSupported().size() >= getDesired().size()` | Yes     |
| DefaultYes amendments included                    | Yes     |
| Config-enabled amendments included                | Yes     |
| Vetoed amendments included (unlike `getDesired`)  | Yes     |
| Obsolete amendments included                      | Yes     |
| Unsupported amendments excluded                   | Yes     |
| Already-enabled amendments drop out               | Yes     |

### What is NOT tested (gaps)

| Gap                           | Severity | Description                                                                                                 |
| ----------------------------- | -------- | ----------------------------------------------------------------------------------------------------------- |
| End-to-end FRESH genesis      | **High** | No test creates a `FRESH` Env and verifies the genesis ledger's Amendments SLE contains all expected hashes |
| `syncWithLedger()`            | **High** | Zero test coverage. Not called in any test                                                                  |
| `loadOldLedger()` integration | Medium   | `LedgerLoad_test.cpp` exercises the path but never checks amendment state                                   |
| Non-FRESH negative case       | Medium   | No test explicitly asserts that NORMAL startup produces no Amendments SLE                                   |
| Real production amendments    | Low      | Test uses synthetic amendments ("a", "b", "g"), not real ones from `features.macro`                         |
| Genesis SLE hash verification | Low      | No test reads back the SLE after genesis to verify hashes match                                             |

### What tests SHOULD exist for production readiness

1. **End-to-end FRESH genesis test** (most important): Create an `Env` with `START_UP = FRESH`, query `getEnabledAmendments()` on the genesis ledger, verify all supported amendments are present.

2. **Non-FRESH negative test**: Create an `Env` with `START_UP = NORMAL`, verify the genesis ledger does NOT contain an Amendments SLE.

3. **`syncWithLedger()` unit test**: Call `syncWithLedger()` on a table and verify it updates internal state even for non-flag ledger sequences.

4. **Amendment count validation**: After FRESH genesis, assert the count matches `supportedAmendments()` (accounting for `Supported::yes` only).

---

## Code Quality Assessment (DRY Score: 7/10)

### DRY Violation: `getAllSupported()` duplicates `doValidation()` skeleton

Both methods share this identical pattern:

```cpp
std::vector<uint256> amendments;
{
    std::lock_guard lock(mutex_);
    amendments.reserve(amendmentMap_.size());
    for (auto const& e : amendmentMap_)
    {
        if (<predicate>)
            amendments.push_back(e.first);
    }
}
if (!amendments.empty())
    std::sort(amendments.begin(), amendments.end());
return amendments;
```

**Recommendation**: Extract a private `collectAmendments(predicate)` template helper. Nice-to-have, not blocking.

### Style issues

| Issue                                                                                         | Severity | Fix                                   |
| --------------------------------------------------------------------------------------------- | -------- | ------------------------------------- |
| `getAllSupported()` uses `/** */` Doxygen style; surrounding methods use `//`                 | Low      | Match surrounding style               |
| `getAllSupported()` doc doesn't mention it excludes already-enabled amendments                | Medium   | State full contract                   |
| `syncWithLedger()` doc leaks implementation ("bypasses flag ledger boundary check")           | Low      | State the contract, not the mechanism |
| Test variable `enabled_` is confusing — it means "voted up", not "actually enabled on ledger" | Low      | Add a clarifying comment in the test  |

### Design question: Should `syncWithLedger()` exist?

It's only called in one place. An alternative with zero new API surface:

```cpp
// In loadOldLedger(), instead of:
m_amendmentTable->syncWithLedger(loadLedger);

// Call directly:
m_amendmentTable->doValidatedLedger(
    loadLedger->seq(),
    getEnabledAmendments(*loadLedger),
    getMajorityAmendments(*loadLedger));
```

This avoids adding to the abstract interface. However, the existing `doValidatedLedger(shared_ptr)` inline in the header sets precedent for this pattern, so `syncWithLedger()` is defensible.

### Things that ARE correct

- Locking is correct — no deadlock or re-entrancy risk
- `m_amendmentTable` fully constructed before use (no race)
- Return by value is fine (NRVO applies)
- Obsolete amendments correctly included (their pre-amendment code is removed, so they MUST be enabled)
- `std::sort` matches `doValidation()` style
- No levelization violations
- Only one implementation of the virtual method exists

---

## Key Constants

| Constant                         | Location                                   | Value      | Purpose                                      |
| -------------------------------- | ------------------------------------------ | ---------- | -------------------------------------------- |
| `defaultAmendmentMajorityTime`   | `include/xrpl/protocol/SystemParameters.h` | 2 weeks    | Normal waiting period after majority         |
| `[amendment_majority_time]`      | `xrpld.cfg`                                | min 15 min | Config override (still too slow for testing) |
| `FLAG_LEDGER_INTERVAL`           | `include/xrpl/protocol/Protocol.h:257`     | 256        | How often amendment voting is evaluated      |
| `amendmentMajorityCalcThreshold` | `include/xrpl/protocol/SystemParameters.h` | 80%        | Validator support needed                     |

---

## Server State Machine

```
  OperatingMode (server-level):         ConsensusMode (per-round):

  DISCONNECTED                          observing
       │                                     │
       ▼                                     ▼
  CONNECTED                             proposing
       │                                (when FULL + validator keys)
       ▼
  SYNCING
       │
       ▼
  TRACKING
       │
       ▼
  FULL ──────────────────────────────► can participate in consensus
```

"Proposing" is not an `OperatingMode` — it's a `ConsensusMode` within the consensus algorithm. A node reaches "proposing" when it's in `OperatingMode::FULL` and has validator keys configured.

---

## Recommendations for the PR

### Must-do before submitting

1. **Add at least one integration test** — create a `FRESH` Env, verify `getEnabledAmendments()` on the genesis ledger returns all supported amendments
2. **Fix doc comments** — state the contract (`getAllSupported` returns supported-but-not-yet-enabled; `syncWithLedger` unconditionally syncs regardless of flag ledger timing)

### Nice-to-have (reviewer may request)

3. Consider inlining `syncWithLedger()` — call `doValidatedLedger(seq, enabled, majority)` directly at the call site to avoid new API surface
4. Extract `collectAmendments(predicate)` private helper to DRY up the iteration
5. Add negative test for non-FRESH startup
6. Add `syncWithLedger()` test coverage

### Do NOT do (over-engineering)

- Don't guard `syncWithLedger()` to only run for certain LOAD subtypes — it's functionally equivalent to what master does, just earlier
- Don't rename `getAllSupported()` — the name is clear enough for a first PR
- Don't add feature flags or config options — this is the right default behavior

---

## Verification

### Automated

```bash
./rippled --unittest=AmendmentTable     # Unit tests
./rippled --unittest=LedgerEntry        # FRESH startup tests
```

### Manual (fresh genesis)

```bash
./rippled --start --conf cfg/standalone/xrpld.cfg
# Then query:
./rippled ledger_data
# Look for LedgerEntryType: "Amendments" — should have ~56 hashes
```

---

## Diagram

See `docs/startup-flow.excalidraw` for the visual diagram (open in VS Code Excalidraw extension or excalidraw.com).
