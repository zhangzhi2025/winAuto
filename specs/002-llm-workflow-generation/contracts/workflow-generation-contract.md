# Workflow Generation Contract

This contract describes the boundary between the model provider and WinAuto. Tool calls propose data only; the client never executes a model-requested desktop action.

## User-visible request

The user submits:

- A natural-language task description.
- A configured provider and model.
- The current supported-capability summary.

The request is sent only to the selected HTTPS provider endpoint. Credentials are sent in the provider-required authorization header and are never included in the prompt or URL.

## Offered tool: `propose_workflow`

The provider-specific adapter exposes one function/tool with this provider-neutral argument shape:

```json
{
  "status": "ready",
  "workflow": {
    "schemaVersion": "1.0",
    "id": "WF-GENERATED",
    "name": "Generated workflow",
    "windows": {
      "main": { "processName": "example.exe" }
    },
    "steps": [
      { "id": "S1", "type": "activate_window", "window": "main" }
    ]
  },
  "questions": [],
  "unsupported": []
}
```

`status` is exactly one of:

- `ready`: `workflow` is required; `questions` and `unsupported` are empty or omitted.
- `needs_clarification`: `questions` is required and non-empty; `workflow` is omitted.
- `unsupported`: `unsupported` is required and non-empty; `workflow` is omitted.

The adapter rejects conflicting status fields, multiple proposal calls, plain-text-only responses, incomplete/truncated responses, and unexpected tool requests. It does not send tool results to the model to continue an action loop.

## Workflow payload

The `workflow` value uses the current project workflow JSON contract in `schemas/workflow.schema.json`, including the existing step types, window aliases, locators, execution options, and version `1.0`. Vendor-specific `action`, `params`, `trigger`, cron, Excel, and arbitrary code fields are not part of this contract.

On `ready`, the client:

1. Converts the workflow object to JSON text and parses it with the existing workflow reader.
2. Runs the existing semantic validator.
3. Rejects any parse error, validation error, unknown field, unsupported action, or configured response-size violation.
4. Presents the valid result as a reviewable editor draft. Only an explicit user acceptance can replace the current editor state.

Warnings and high-risk issues remain subject to the existing review and run confirmation paths. Generation never starts the workflow.

## Provider adaptation

Claude and OpenAI use provider-specific request envelopes, authorization headers, tool definitions, and response parsers. Each adapter MUST normalize the accepted tool arguments into the shape above and map service failures to a safe user-visible category:

- `authentication_failed`
- `rate_limited`
- `network_error`
- `timeout`
- `cancelled`
- `provider_error`
- `invalid_response`
- `invalid_workflow`

Do not include API credentials, full request bodies, full model output, or user task text in diagnostics by default.
