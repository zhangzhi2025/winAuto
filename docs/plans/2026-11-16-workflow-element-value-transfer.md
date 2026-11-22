# Development Plan: Transfer Values Between Workflow Steps

## Goal

Allow a workflow to read the current value of a UI Automation element in one step and use that value in a later `set_value` or `input_text` step. Keep the value in memory for the current workflow run, and never write the captured value back into the workflow file.

## Current State

- `set_value` and `input_text` accept literal text or `textFrom: "prompt"`; they cannot reference a prior step's output.
- `IInputController` and `UiaSession` can set a UIA `ValuePattern` value but cannot read it.
- The engine already maintains run-local `workflowData`, initialized from workflow JSON `data`, shared across script steps, and discarded when a run ends.
- Script `automation.read(window, locator)` returns element metadata, not its `ValuePattern` value. Scripts can access and update run-local data through `data.get` / `data.set`.
- The Qt step editor represents these fields in `StepPanel`; workflow JSON parsing, semantic validation, schema, examples, and tests are maintained separately.

## Proposed User-Facing Workflow

Add a `get_value` step that reads a UIA `ValuePattern` string into a named run-local variable:

```json
{
  "id": "READ_ACCOUNT",
  "type": "get_value",
  "window": "source",
  "locator": { "strategy": "uia", "automationId": "account-number" },
  "variable": "accountNumber",
  "sensitive": true
}
```

Allow `set_value` and `input_text` to consume that variable using a new `valueFrom` field:

```json
{
  "id": "WRITE_ACCOUNT",
  "type": "set_value",
  "window": "target",
  "locator": { "strategy": "uia", "automationId": "account-input" },
  "valueFrom": { "variable": "accountNumber" }
}
```

Existing literal `text` and `textFrom: "prompt"` workflows remain valid. A step must specify exactly one text source. The UI presents literal, prompt, and variable sources without making users edit JSON manually.

## Behavioral Decisions

- `get_value` reads the control's current UIA `ValuePattern` value. It does not infer a value from the element name, displayed text, or OCR.
- Missing elements, stale UIA references, and unsupported `ValuePattern` are explicit step failures; no empty-string fallback is allowed.
- A `get_value` step overwrites its named variable. This supports repeated reads in loops.
- Variables use the existing workflow run data object. They are shared with later workflow and script steps, including nested blocks and loop iterations, and are discarded when the run finishes.
- A variable reference that is absent at runtime fails explicitly with the variable name. Do not silently substitute an empty string.
- `sensitive: true` on `get_value` registers the captured value for normal log/report redaction. Its value must not appear in UI logs, CLI output, summaries, screenshots, or persisted workflow JSON.
- For the first version, support variable input for both `set_value` and `input_text`. Preserve the existing prohibition on sending sensitive values through clipboard input; ensure variable-derived sensitive values cannot select the clipboard method.
- `automation.get_value(window, locator)` should expose the same read capability to Python and Lua scripts. Its result can be assigned to `data` and then used by later script or workflow steps.

## Implementation Tasks

### 1. Extend the platform-neutral value interface

Files: `core/Interfaces.h`, `platform_win/UiaSession.h`, `platform_win/UiaSession.cpp`, `platform_win/Win32InputController.h`, `platform_win/Win32InputController.cpp`.

- Add a typed `getValue` operation alongside `setValue`.
- Implement `UiaSession::getValue` using `IUIAutomationValuePattern::get_CurrentValue`.
- Convert the returned BSTR to UTF-8, release COM resources on every path, and map unsupported pattern / COM failures to existing `Status` and `ErrCode` results.
- Keep Win32 and COM types out of public core interfaces.

**Tests:** Add platform-adapter coverage where practical; manually verify standard edit, read-only ValuePattern, and a control without ValuePattern.

### 2. Add workflow data-source model and serialization

Files: `core/Model.h`, `core/Model.cpp`, `core/Json.cpp`, `core/WorkflowJson.cpp`, `core/Validator.cpp`, `schemas/workflow.schema.json`.

- Add the `get_value` step type, output variable name, and a text-source representation for `set_value` / `input_text`.
- Read and write the new `variable`, `valueFrom`, and `sensitive` fields while retaining backward compatibility with literal `text` and `textFrom: "prompt"`.
- Reject conflicting sources, empty/invalid variable names, source fields on unsupported step types, and clipboard use where the source is sensitive.
- Document that schema validation is editor assistance; keep actual semantics in `Validator`.

**Tests:** Round-trip JSON tests, backward compatibility tests, and validator tests for valid and invalid source combinations.

### 3. Implement engine capture and substitution

Files: `core/Engine.cpp`, `core/Interfaces.h`, `tests/test_engine.cpp`.

- Route `get_value` through existing locator polling and `IInputController::getValue`.
- Store successful reads in run-local `workflowData`; preserve shared visibility for subsequent steps and scripts.
- Resolve variable-backed text immediately before `set_value` / `input_text` execution. Fail clearly if the key does not exist or is not a string.
- Register sensitive captured values with the existing logger redactor and preserve existing screenshot suppression behavior for sensitive steps.
- Keep the data in memory only; do not mutate the workflow's serialized initial `data`.

**Tests:** Fake-based engine tests for read-then-set, read-then-type, values spanning nested blocks/loops, script-visible variables, variable overwrite, missing/non-string variables, provider failures, and sensitive log redaction.

### 4. Add script-worker API parity

Files: `core/Engine.cpp`, `platform_win/script_worker/python_worker.py`, `platform_win/script_worker/lua_worker.lua`, `tests/test_script_runtime.cpp`.

- Add `automation.get_value(window, locator)` to Python and Lua using a brokered `element.get_value` operation.
- Reuse existing locator validation, window checks, cancellation/deadline handling, and script protocol size limits.
- Keep `automation.read` behavior unchanged so scripts relying on its metadata result are not broken.

**Tests:** Verify Python and Lua can read a value, put it in `data`, and read the same value later; verify unsupported controls and provider failures are surfaced.

### 5. Integrate the Qt workflow editor

Files: `ui/StepPanel.h`, `ui/StepPanel.cpp`, `ui/MainWindow.cpp`, related editor/model code.

- Add `get_value` to the step-type menu and expose its locator, output variable, and sensitive toggle.
- For `set_value` and `input_text`, add a source selector for literal, prompt, or variable; show only the appropriate editor.
- Preserve the current literal/prompt behavior and avoid submitting partially edited or conflicting step data.
- Ensure summaries and validation issue display do not reveal captured runtime values.

**Tests:** Add or extend UI/model tests for form-to-model serialization, selection/visibility, and legacy literal/prompt steps. Manually check the full editor workflow.

### 6. Document and validate end to end

Files: `README.md`, `examples/` (new value-transfer example), `tests/manual/` (new Windows checklist).

- Document `get_value`, `valueFrom`, run-local lifetime, ValuePattern requirements, sensitive handling, and failure cases.
- Add an example that reads a value from one control and sets another.
- Add a Windows checklist for a real UIA ValuePattern control, including missing variable and unsupported control cases.
- Run JSON, validator, engine, script-runtime, core-purity, schema-example, and full CTest suites; build Win32 CLI/UI and perform the manual checklist.

## Acceptance Criteria

1. A workflow can read a string-valued UIA control into a variable and set a different control from that variable in the same run.
2. The same run-local data is visible to Python/Lua steps through `data.get` / `data.set`; scripts can also use the new `automation.get_value`.
3. Existing workflows using `text` or `textFrom: "prompt"` continue to parse, validate, and run unchanged.
4. An absent variable, missing element, unsupported ValuePattern, or failed provider call reports a real error; it never becomes a successful empty value.
5. Captured values are not written into the workflow file or execution summaries. Sensitive values are redacted from logs and reports.
6. Core remains independent of Win32, COM, and Qt, and all relevant tests/builds pass.

## Risks and Boundaries

- Not every UIA control exposes `ValuePattern`; controls exposing only another pattern are unsupported in this increment.
- Some providers return a value whose visible content is not the same as the displayed text. The feature contract is specifically the UIA `ValuePattern` value.
- Workflow data is shared with scripts. Since scripts are executable workflow content, `sensitive` redaction must also cover values returned to a script and echoed through the existing logger.
- Do not add cross-run persistence, arbitrary variable expressions, or OCR-derived text capture in this implementation.
