# Windows Script Runtime Manual Checklist

Run these checks with a disposable workflow and a non-critical target application. Script workers run with the current user's privileges; they are not a security sandbox.

- [ ] Run a Python step that reads `data["count"]`, updates it with `data["count"] = data["count"] + 1`, then run a second step that reads the updated value. Confirm the saved workflow file still contains its original `data`.
- [ ] Repeat the data check in Lua with `data.get("count")` and `data.set("count", value)`.
- [ ] Use the pre-run summary to inspect the script step's language and the explicit arbitrary-code/trust warning. Decline risk confirmation and confirm that no script starts; confirm with `RISK` and verify the script starts.
- [ ] Remove the selected runtime from `PATH` and unset its `WINAUTO_PYTHON` or `WINAUTO_LUA` override. Confirm the step fails with a runtime-not-found diagnostic and the workflow failure policy is honored.
- [ ] Run syntax-error and runtime-error scripts in both languages. Confirm the failing step ID is reported and the diagnostic is bounded and does not include script source or exception contents.
- [ ] Run a long-running script, verify the configured timeout reports `Timeout`, then repeat and use the emergency stop control to verify cancellation reports `Cancelled`.
- [ ] Exercise each documented broker operation against a disposable window. Confirm unknown aliases/operations, malformed locators, disabled controls, background keyboard input, and unsupported hotkeys fail without being treated as success.
- [ ] Replace the worker with a test worker that exits early or emits malformed/oversized JSON Lines. Confirm the host reports a protocol/worker failure, terminates the worker process tree, and does not log the worker's raw output.
- [ ] Print a known test secret and trigger an exception containing another test secret. Confirm neither value appears in the run log or report.
- [ ] Confirm the worker uses no persistent script/data files and that the original workflow JSON is unchanged after success, timeout, cancellation, and failure.
