# Feature Specification: Python and Lua Scripting

**Feature Branch**: `Not created`

**Created**: 2026-10-27

**Status**: Draft

**Input**: User description: "支持python或lua脚本"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Extend a Workflow with a Script (Priority: P1)

As an automation author, I want a Python or Lua script to read and update workflow data and invoke automation actions so that I can express workflow behavior that is not covered by built-in steps.

**Why this priority**: Adding and running a script is the core value of this feature.

**Independent Test**: Create a workflow containing a script step in each supported language; verify each script can use workflow data and perform an automation action within the workflow.

**Acceptance Scenarios**:

1. **Given** a workflow being edited, **When** the user adds a script step and selects Python or Lua, **Then** the workflow records the selected language and script source.
2. **Given** a valid script step and its required language runtime is available, **When** the workflow runs, **Then** the selected script is executed at that step and workflow execution continues on success.
3. **Given** a running workflow with available workflow data, **When** a script reads or updates that data and invokes an automation action, **Then** those operations apply to the active workflow and respect its safety confirmations.
4. **Given** workflow data initialized before a run, **When** a script updates a value, **Then** subsequent script steps in that run can read the updated value without changing the saved workflow file.

### User Story 2 - Diagnose Script Failures (Priority: P2)

As an automation author, I want script errors to identify the failing step and explain the failure so that I can correct the script or its environment.

**Why this priority**: Clear failure information is necessary to use scripts reliably in workflows.

**Independent Test**: Run a workflow with invalid script source and verify the run reports the failed step and a useful error.

**Acceptance Scenarios**:

1. **Given** a script with a syntax or runtime error, **When** its step executes, **Then** the run reports the script step as failed and provides an actionable error detail.
2. **Given** a script step whose required runtime is unavailable, **When** its step executes, **Then** the run fails clearly rather than silently skipping the script.
3. **Given** a script that exceeds the workflow's configured step timeout, **When** the timeout is reached, **Then** the step fails with a timeout diagnostic and the workflow's failure behavior is applied.

### Edge Cases

- Script source is empty or contains only whitespace.
- The selected language runtime is missing, incompatible, or cannot start.
- A script runs for an unusually long time or does not terminate.
- The workflow's failure policy requests continuing after a script step fails.
- Script output contains sensitive text or a large amount of data.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Users MUST be able to add a script step to a workflow and choose Python or Lua as its language.
- **FR-002**: Users MUST be able to enter, save, reopen, and edit the source of a script step without changing its language or source unexpectedly.
- **FR-003**: The workflow MUST execute a script step using the language selected for that step, and MUST NOT silently skip it.
- **FR-004**: A successful script step MUST allow the workflow to proceed to its next step.
- **FR-005**: A script step failure MUST identify the failed step and provide useful error details; the workflow's configured failure behavior MUST be respected.
- **FR-006**: Empty script source and unavailable runtimes MUST be reported as failures with clear explanations.
- **FR-007**: Script steps MUST remain distinguishable from built-in steps when a workflow is viewed, edited, saved, or loaded.
- **FR-008**: Python and Lua scripts MUST be able to read and update workflow data and invoke automation actions in the active workflow.
- **FR-009**: Script execution MUST honor the workflow's existing authorization and safety confirmations and MUST NOT bypass them.
- **FR-010**: Script execution MUST respect the workflow's configured step timeout and report when that limit is reached.
- **FR-011**: Users MUST be able to provide initial JSON-compatible workflow data that scripts can read and update for the duration of a run; script updates MUST NOT silently rewrite the saved workflow file.

### Key Entities

- **Script step**: A workflow action containing a selected language and the source text to execute.
- **Language runtime**: The execution environment required to run a script in its selected language.
- **Script result**: The success or failure of a script step, including diagnostic information used in the workflow run report.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Users can add, save, reopen, and identify a Python or Lua script step in a workflow without losing its selected language or source in 100% of verification cases.
- **SC-002**: A valid script in either supported language executes as its workflow step, and subsequent workflow steps run after script success in 100% of verification cases.
- **SC-003**: Every tested script syntax, runtime, or missing-runtime failure identifies the script step and includes a non-empty diagnostic.
- **SC-004**: Users can determine whether a script step succeeded or failed from the workflow run result without inspecting implementation logs.

## Assumptions

- Both Python and Lua are in scope; users select one language per script step.
- A script is part of the workflow and runs only when workflow execution reaches its step.
- A language runtime must be available in the user's environment; missing runtimes are reported rather than silently substituted.
- Scripts follow the workflow's existing step failure policy.
- Scripts can read and update workflow data and invoke automation actions; they operate within the active workflow rather than as standalone programs.
- Existing authorization and safety confirmations continue to apply to script-triggered automation actions.
- The workflow's existing step timeout also limits script execution.
- Initial script data is explicitly stored with the workflow; each run uses a mutable copy whose changes are shared by later script steps but discarded when the run ends.
