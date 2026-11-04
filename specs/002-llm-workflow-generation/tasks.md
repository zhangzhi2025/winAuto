---

description: "Implementation tasks for LLM-assisted workflow generation"
---

# Tasks: 大模型辅助创建自动化流程

**Input**: Design documents from `specs/002-llm-workflow-generation/`

**Prerequisites**: `plan.md`, `spec.md`, `research.md`, `data-model.md`, `contracts/workflow-generation-contract.md`, `quickstart.md`

**Organization**: Tasks are grouped by user story so each increment can be implemented and reviewed independently. Automated test-writing tasks are omitted because the feature specification does not explicitly request TDD or test implementation.

## Phase 1: Setup

**Purpose**: Enable the optional Qt desktop target to use asynchronous HTTP.

- [X] T001 Find Qt5 Network with the existing optional Qt5 Widgets configuration in `CMakeLists.txt` and link `Qt5::Network` to `winauto-ui` in `ui/CMakeLists.txt`; keep UI skipped when Qt is unavailable.

---

## Phase 2: Foundational

**Purpose**: Establish shared provider-neutral types, capabilities, secure credentials, and settings required by generation and configuration stories.

- [X] T002 [P] Define provider-neutral generation request, proposal statuses (`ready`, `needs_clarification`, `unsupported`), safe error categories, and the bounded supported-step capability catalog in `ui/WorkflowGeneration.h`, `ui/WorkflowCapabilities.h`, and `ui/WorkflowCapabilities.cpp`.
- [X] T003 [P] Implement current-user DPAPI credential protect/read/clear operations with explicit error results and no secret logging in `platform_win/ModelCredentialStore.h`, `platform_win/ModelCredentialStore.cpp`, and `platform_win/CMakeLists.txt`.
- [X] T004 Implement `ModelConnectionSettings` in `ui/ModelConnectionSettings.h` and `ui/ModelConnectionSettings.cpp`, persisting non-secret provider/model values with `QSettings` and storing credentials separately through `ModelCredentialStore`: provider is exactly `claude` or `openai`; model name is non-empty and belongs to the configured/current supported provider range; credentials are masked in UI and never written to workflow JSON, prompts, logs, or diagnostics.

**Checkpoint**: Shared contracts and safe configuration storage are available to all user stories.

---

## Phase 3: User Story 1 - 从自然语言生成流程草稿 (Priority: P1)

**Goal**: Send a user's task to Claude or OpenAI using structured tool calling and return a bounded, cancellable proposal without operating desktop applications.

**Independent Test**: With a configured provider, submit a task expressible using existing WinAuto steps; verify the response produces a proposal for a reviewable standard workflow, while the current editor workflow remains unchanged and no target application is operated.

- [X] T005 [P] [US1] Implement the Claude Messages API request and response adapter for the single `propose_workflow` tool in `ui/ClaudeWorkflowProvider.h` and `ui/ClaudeWorkflowProvider.cpp`; reject plain-text-only, multiple-tool, unexpected-tool, and incomplete responses.
- [X] T006 [P] [US1] Implement the OpenAI Responses API request and response adapter for the single `propose_workflow` tool in `ui/OpenAIWorkflowProvider.h` and `ui/OpenAIWorkflowProvider.cpp`; reject plain-text-only, multiple-tool, unexpected-tool, and incomplete responses.
- [X] T007 [US1] Implement the asynchronous generation coordinator in `ui/WorkflowGeneration.cpp` using Qt Network; enforce bounded task/response sizes, timeout, cancellation, bounded rate-limit retry, HTTPS/TLS, provider error mapping, and discard partial output on failure or cancellation.
- [X] T008 [US1] Create `ui/WorkflowGenerationDialog.h` and `ui/WorkflowGenerationDialog.cpp` for task entry, configured provider/model selection, generation progress/cancellation, and distinct clarification, unsupported-capability, and failure states; retain task text after failure.
- [X] T009 [US1] Add a workflow-generation action and dialog launch path in `ui/MainWindow.h` and `ui/MainWindow.cpp`; pass the current connection and capability catalog to generation without changing the open workflow until explicit acceptance.

**Checkpoint**: A successful request yields structured proposal data; it cannot execute requested desktop tools or alter the editor by itself.

---

## Phase 4: User Story 2 - 检查、修正并保存生成的流程 (Priority: P1)

**Goal**: Parse and validate proposals with the existing workflow authority, then let users explicitly accept or discard drafts while preserving editor safeguards.

**Independent Test**: Return malformed JSON, unknown step types, missing fields, and undefined window aliases; verify each is rejected with a step/field-located issue and cannot replace the editor. Accept a valid proposal only after review, save/reopen it, and verify normal run confirmation remains required.

- [X] T010 [P] [US2] Implement proposal-to-workflow decoding in `ui/WorkflowProposalParser.h` and `ui/WorkflowProposalParser.cpp`; convert only `ready` proposals, reject unknown fields/types and incomplete payloads, and call `readWorkflow` plus `validateWorkflow` before exposing a valid draft with existing issue paths.
- [X] T011 [US2] Extend `ui/WorkflowGenerationDialog.h` and `ui/WorkflowGenerationDialog.cpp` to show the proposal preview and validation issues, and emit explicit accept/discard results; clarification, unsupported, invalid, failed, and cancelled results must not be presented as runnable drafts.
- [X] T012 [US2] Integrate accepted-draft replacement in `ui/MainWindow.h` and `ui/MainWindow.cpp` through the existing editor and unsaved-change protection; do not replace editor contents before acceptance, auto-save, or auto-run, and retain existing run summaries, risk confirmation, permissions, stop control, and result reporting.

**Checkpoint**: Only a locally valid proposal explicitly accepted by the user can enter the editor; execution remains governed by existing safeguards.

---

## Phase 5: User Story 3 - 管理模型连接并了解能力边界 (Priority: P2)

**Goal**: Let users configure supported providers and credentials, understand the available workflow operations, and receive safe, actionable configuration feedback.

**Independent Test**: Save, update, and clear each provider configuration; verify a missing credential blocks generation, the secret is masked and absent from logs/workflows, and unsupported Excel or scheduled-trigger requests are identified without fabricated steps.

- [X] T013 [P] [US3] Create `ui/ModelSettingsDialog.h` and `ui/ModelSettingsDialog.cpp` for selecting `claude` or `openai`, entering a non-empty supported model name, saving or clearing the separately protected API credential, and presenting the supported-step catalog plus the Excel/scheduling exclusions.
- [X] T014 [US3] Add the model-settings action and dialog wiring in `ui/MainWindow.h` and `ui/MainWindow.cpp`; show configuration-required feedback before any request is sent and ensure subsequent requests use updated settings without displaying the full credential.

**Checkpoint**: Users can safely configure model access and understand which tasks the current workflow engine can represent.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Keep the implementation guide aligned with the delivered application and verify the configured desktop build.

- [X] T015 [P] Update `specs/002-llm-workflow-generation/quickstart.md` with the final supported build configuration and the implemented generation, review, credential-protection, cancellation, and unsupported-capability acceptance steps.
- [X] T016 Build the Qt-enabled UI and run the focused CTest commands documented in `specs/002-llm-workflow-generation/quickstart.md`; resolve regressions without weakening `core_purity` or existing workflow validation and execution gates.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: T001 enables Qt Network and must complete first.
- **Foundational (Phase 2)**: T002 and T003 can run in parallel after T001; T004 depends on T003. Complete T002-T004 before story implementation.
- **User Stories (Phase 3+)**: All depend on the foundational phase. US1 and US3 can be developed in parallel after foundation; US2 depends on the proposal contract from T002 and integrates with the generated proposal flow from US1.
- **Polish (Phase 6)**: T015 can run alongside implementation; T016 follows all implementation and documentation work.

### User Story Dependencies

- **US1 (P1)**: Depends on Phase 2; generation can be implemented with configured settings, while end-to-end use requires the US3 settings UI.
- **US2 (P1)**: Proposal decoding can begin after Phase 2; dialog and editor acceptance integration depend on the US1 proposal flow. Does not depend on US3's settings UI.
- **US3 (P2)**: Depends on Phase 2; can proceed in parallel with provider adapters and generation dialog. MainWindow menu wiring must be coordinated with US1/US2 MainWindow edits.

### Parallel Opportunities

- **Foundational**: T002 and T003 are independent files after T001; T004 follows T003.
- **US1**: T005 and T006 are independent provider adapters and can be implemented in parallel. T007 follows both; T008 and T009 then follow in order.
- **US2**: T010 can proceed in parallel with US1 provider adapter work after T002. T011 depends on both T010 and T008; T012 follows T011 and coordinates edits to MainWindow.
- **US3**: T013 can proceed in parallel with US1 adapter/coordinator work after T004; T014 coordinates MainWindow changes with T009/T012.
- **Polish**: T015 is documentation-only and can proceed independently; T016 runs after feature integration.

### Parallel Execution Examples

```text
US1: after T002, assign T005 (Claude adapter) and T006 (OpenAI adapter) concurrently.
US2: after T002, T010 (proposal parser) can proceed while T005/T006 are being implemented; integrate with the dialog after T008.
US3: after T004, T013 (settings dialog) can proceed while provider adapters and generation coordination are implemented; wire MainWindow after the other MainWindow tasks.
```

## Implementation Strategy

### Incremental Delivery

1. Complete T001-T004 to provide Qt networking and shared safe configuration primitives.
2. Implement US1 provider adapters and generation interaction; use configured test settings to verify proposal-only behavior.
3. Implement US2 local validation and explicit review/acceptance so valid proposals can safely enter the editor.
4. Implement US3 settings management and capability disclosure for complete user-configured operation.
5. Update the quickstart and run the Qt-enabled build and focused CTest validation.

### MVP Scope

US1 is the central value, but it is not independently usable without configured credentials or explicit draft review. The smallest safe end-to-end MVP is **Phase 2 plus US1, US2, and the provider-configuration portions of US3**; capability disclosure and the full settings UX complete US3.
