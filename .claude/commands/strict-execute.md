---
description: Activate strict plan execution with TDD, reviews, worktrees
---

Execute the approved plan using **MAJOR Work Protocol** from CLAUDE.md.

# Protocol Reminder

## Setup
1. **Worktree**: Create `../helixscreen-<feature>` from `origin/main`
2. **Orchestration only**: Main session spawns agents, evaluates results, commits

## Per Phase
1. **Write failing tests FIRST** — real tests that fail if feature removed
2. **Run new tests only** during development: `./build/bin/helix-test "[tag]"`
3. **Delegate** implementation to agent
4. **Code review** after agent completes (spawn review agent)
5. **Commit**: `[phase-N] description`

## Completion Requirements
- [ ] Full test suite passes: `make test-run`
- [ ] Final comprehensive code review completed
- [ ] All phases marked complete in IMPLEMENTATION_PLAN.md
- [ ] Worktree cleanly mergeable to main

## Test Quality Gates

**Tests must be REAL:**
- ❌ `REQUIRE(true)`, unchecked `.has_value()`, mocking the thing under test
- ❌ Happy-path only, no edge cases
- ✅ Tests that FAIL if feature is removed
- ✅ Edge cases, error conditions, boundary values

## Stop Criteria

**STOP after 3 failed attempts at any step.** Before asking, provide:
- What you tried (with specific errors)
- Why each attempt failed
- Current hypothesis about root cause
- What information would unblock you

---

Begin execution now. Start by creating the worktree if not already created.
