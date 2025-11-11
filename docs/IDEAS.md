* More test coverage
* have libhv use spdlog for logging if possible
* have tip of the day fade out and in over 1 second with animation
* fix scrollbar on main region of home screen (scrollable="false")
* make it clear you can click the tip of the day by putting a (?) icon at the end (that is also clickable)
* don't do network scanning background task/timer when we aren't actually on that screen of the wizard.  start/stop it as necessary.
* WiFi wizard password modal for encrypted networks
* WiFi connection flow with visual feedback (connecting state, success/error messages)
* Fix wizard Step 3 printer type roller collapsed/invisible issue
* easy calibration workflow
* AFC control
* belt tension: The printer uses controlled belt excitation combined with stroboscopic feedback from the LED to visualize belt resonance

---

## Agent Documentation Compression (Future Performance Optimization)

**Status:** Deferred - Not currently needed
**Last Reviewed:** 2025-11-10

### Current State
- 7 custom agents in `.claude/agents/`: 3,740 lines total
- Largest agents:
  - `ui-reviewer.md`: 1,072 lines
  - `widget-maker.md`: 790 lines
  - `cross-platform-build-agent.md`: ~400 lines
- Agent prompts currently load fine, no performance issues observed

### Proposed Optimization
Compress agent documentation from 3,740 → ~1,500 lines (~60% reduction) using:
1. **Table compression** - Convert verbose rule lists to scannable tables (same technique used in CLAUDE.md)
2. **Extract examples** - Move code examples to separate `examples/` directory (load on-demand)
3. **Reference instead of duplicate** - Link to LVGL9_XML_GUIDE.md instead of repeating XML syntax
4. **Remove historical context** - Agents don't need development history, only current patterns

### Risks & Mitigations

**🔴 HIGH RISK: Agent effectiveness may decrease**
- Verbose examples help agents understand patterns
- Too much compression could make agents less helpful
- Hard to predict impact without testing

**Mitigation Strategy:**
1. **A/B test one agent at a time** - Start with least-used agent (e.g., `test-harness-agent`)
2. **Keep backups** - Save original as `.backup` before compressing
3. **Incremental compression** - Remove 10-20% at a time, test, repeat
4. **Monitor quality** - If agents start making more mistakes, roll back immediately
5. **Move, don't delete** - Extract examples to separate files instead of deleting

### When to Revisit

**Trigger conditions (any of these):**
- Agent startup times become noticeable (>2-3 seconds)
- Token usage from agent prompts becomes problematic
- Running into context limits during agent execution
- Need to add more agents and total docs >5,000 lines

**Current verdict:** Agents work well at current size. Don't fix what isn't broken.

### Implementation Plan (if needed in future)

**Phase 1: Test Subject**
- Compress `test-harness-agent.md` (~300 lines → ~150 lines)
- Create `test-harness-examples.md` with extracted code samples
- Test with real tasks, compare quality to original
- If successful, proceed. If not, roll back and abandon.

**Phase 2: Incremental Rollout**
- Compress `gcode-preview-agent.md` (smaller, less critical)
- Monitor for regression
- Continue only if no quality degradation

**Phase 3: Major Agents** (only if Phase 1-2 successful)
- Compress `widget-maker.md` (most-used, be very careful)
- Compress `ui-reviewer.md`
- Final validation with real-world usage

**Estimated effort:** 4-6 hours for full implementation + 1 week testing period

### Notes
- This optimization was considered during 2025-11-10 token reduction work
- Decision: Skip for now, current agent docs are fine
- Main session CLAUDE.md compression achieved 74% reduction successfully
- Agent compression is riskier due to potential quality impact
