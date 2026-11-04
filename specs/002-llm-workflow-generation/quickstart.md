# Quickstart: Validate LLM Workflow Draft Generation

## Prerequisites

- Windows, Visual Studio 2019/2022, CMake 3.16+, and the Qt 5.15 MSVC kit matching the selected architecture. The documented `D:\Qt\qt5.15\msvc2019` kit is 32-bit, so use `-A Win32`.
- A user-supplied API key and a currently supported Claude or OpenAI model name with tool/function calling available.
- Network access to the selected HTTPS provider endpoint.
- Automated adapter, proposal, and workflow-validation tests use local JSON fixtures and do not require credentials or network access.

## Configure and build

From the repository root, configure and build with the installed Qt kit:

```bat
cmake -S . -B out\llm-build -G "Visual Studio 16 2019" -A Win32 -DCMAKE_PREFIX_PATH=D:\Qt\qt5.15\msvc2019 -DWINAUTO_WERROR=ON
cmake --build out\llm-build --config Debug
```

Qt Widgets and Network are required together to build the optional desktop UI. The workflow model, reader, and validator remain in the standard C++ `core/` library; provider adapters and credential storage are outside core.

## Automated validation

```bat
ctest --test-dir out\llm-build -C Debug -R "test_workflow_generation|test_editor|test_json|test_validator|core_purity" --output-on-failure
```

Expected: Claude/OpenAI request and response parsing, proposal-state checks, valid and invalid workflow decoding, editor draft dirty-state handling, existing workflow parsing and validation, and core-purity checks pass without external requests. `test_workflow_generation` covers HTTPS endpoints and confirms credentials are sent only in authorization headers, not request bodies. The generation coordinator enforces a 10,000-character task limit, a 2 MiB response limit, a 120-second timeout, cancellation, and at most two bounded rate-limit retries.

## Manual end-to-end scenarios

1. Open **流程 → 模型连接设置**, choose Claude or OpenAI, enter a supported model name and API key, and save. Verify the key is masked, never displayed again, and the UI explains that DPAPI protection is scoped to the current Windows user.
2. Open **流程 → AI 生成流程草稿**, describe a simple supported task, and generate. Verify no target application is operated, the current workflow remains untouched, and the proposed JSON appears for review.
3. Generate a task with missing target details. Verify the dialog shows clarification questions and disables draft acceptance.
4. Generate a task requiring Excel export or scheduled execution. Verify the capability boundary is shown and no unsupported workflow step is fabricated.
5. In the proposal editor, verify malformed JSON, unknown step types, missing required fields, unknown workflow fields, and undefined window aliases fail local parsing/semantic validation with step or field paths. Correct an invalid field, validate again, and verify acceptance becomes available only after validation succeeds.
6. With unsaved editor changes, accept a valid generated proposal. Verify the existing save/discard/cancel prompt appears; choosing cancel preserves the original workflow. Accepting the replacement loads a dirty, unsaved draft and does not run it.
7. Cancel a request, then separately exercise provider timeout, authentication failure, rate limiting and malformed/truncated responses. Verify the user task and existing workflow remain intact, partial output cannot be accepted, and errors do not include credentials, full requests, or full responses.
8. Save and reopen an accepted draft, then use the normal run action. Verify the existing run summary, risk confirmation, permission checks, stop control, and result reporting still apply.

See [workflow-generation-contract.md](contracts/workflow-generation-contract.md) and [data-model.md](data-model.md) for proposal shape and lifecycle.
