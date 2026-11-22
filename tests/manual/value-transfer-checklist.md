# Value Transfer Manual Checklist

Use a disposable application and non-sensitive test data. `get_value` reads the control's UI Automation `ValuePattern` value; it does not read OCR text or infer a value from the element name.

- [ ] Create a workflow with a `get_value` step targeting an editable control with `ValuePattern`, then use `valueFrom.variable` in a `set_value` step targeting a second control. Confirm the second control receives the exact value.
- [ ] Repeat with `input_text` using Unicode input. Confirm the value remains available across nested blocks and script steps for this run, and the saved workflow JSON contains no captured value.
- [ ] Run a Python and a Lua script calling `automation.get_value(window, locator)`. Assign the result to `data` and confirm a later script/workflow step can retrieve it.
- [ ] Mark `get_value` as sensitive and verify logs, summaries, CLI/UI output, and later platform error details do not expose the captured value. Confirm failure screenshots stop after the sensitive read.
- [ ] Try a control without `ValuePattern`. Confirm the step fails with an unsupported-pattern error rather than succeeding with an empty value.
- [ ] Reference a missing variable and a variable whose value is not a string. Confirm both fail explicitly.
- [ ] Try a clipboard `input_text` step referencing a sensitive value. Confirm validation rejects the workflow; do not use clipboard transport for sensitive data.
- [ ] Confirm existing literal-text and `textFrom: "prompt"` workflows still validate and execute.
