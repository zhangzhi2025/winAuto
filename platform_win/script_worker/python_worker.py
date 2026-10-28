import json
import sys
import traceback

MAX_LINE = 1 << 20
MAX_OUTPUT = 16 << 10
output_size = 0
request_id = 0


class OutputSink:
    def write(self, text):
        global output_size
        output_size = min(MAX_OUTPUT, output_size + len(text))
        return len(text)

    def flush(self):
        pass


def send(message):
    line = json.dumps(message, ensure_ascii=False, separators=(",", ":"))
    if len(line.encode("utf-8")) > MAX_LINE:
        raise RuntimeError("protocol message too large")
    sys.__stdout__.write(line + "\n")
    sys.__stdout__.flush()


def receive():
    line = sys.stdin.buffer.readline(MAX_LINE + 1)
    if not line or len(line) > MAX_LINE or not line.endswith(b"\n"):
        raise RuntimeError("invalid or oversized protocol input")
    value = json.loads(line.decode("utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError("invalid protocol message")
    return value


def call(operation, arguments):
    global request_id
    request_id += 1
    send({"type": "request", "request_id": request_id, "operation": operation,
          "arguments": arguments})
    response = receive()
    if response.get("type") == "cancel":
        raise InterruptedError("cancelled")
    if (response.get("type") != "response" or
            response.get("request_id") != request_id or
            not isinstance(response.get("ok"), bool)):
        raise RuntimeError("invalid host response")
    if not response["ok"]:
        raise RuntimeError("host operation failed: " +
                           str(response.get("error", {}).get("code", "error")))
    return response.get("result")


class WorkflowData:
    def __getitem__(self, key):
        result = call("data.get", {"key": key})
        if not result.get("found"):
            raise KeyError(key)
        return result.get("value")

    def __setitem__(self, key, value):
        call("data.set", {"key": key, "value": value})

    def get(self, key, default=None):
        result = call("data.get", {"key": key})
        return result.get("value") if result.get("found") else default


class Automation:
    @staticmethod
    def activate_window(window):
        return call("window.activate", {"window": window})

    @staticmethod
    def read(window, locator):
        return call("element.read", {"window": window, "locator": locator})

    @staticmethod
    def click(window, locator):
        return call("element.click", {"window": window, "locator": locator})

    @staticmethod
    def move(window, locator):
        return call("element.move", {"window": window, "locator": locator})

    @staticmethod
    def set_value(window, locator, text):
        return call("element.set_value", {"window": window, "locator": locator,
                                          "text": text})

    @staticmethod
    def input_text(window, text):
        return call("input.text", {"window": window, "text": text})

    @staticmethod
    def hotkey(window, keys):
        return call("input.hotkey", {"window": window, "keys": keys})


def main():
    start = receive()
    if (start.get("type") != "start" or start.get("version") != 1 or
            start.get("language") != "python" or
            not isinstance(start.get("code"), str) or
            not isinstance(start.get("data"), dict)):
        send({"type": "error", "diagnostic": "invalid Python worker start message"})
        return
    sys.stdout = OutputSink()
    sys.stderr = OutputSink()
    namespace = {"__name__": "__main__", "data": WorkflowData(),
                 "automation": Automation()}
    try:
        exec(compile(start["code"], "<workflow-script>", "exec"), namespace, namespace)
        send({"type": "complete"})
    except BaseException as error:
        if isinstance(error, SyntaxError):
            line = error.lineno or 0
        else:
            frames = traceback.extract_tb(error.__traceback__)
            line = next((frame.lineno for frame in reversed(frames)
                         if frame.filename == "<workflow-script>"), 0)
        diagnostic = type(error).__name__ + (" at line " + str(line) if line else "")
        try:
            send({"type": "error", "diagnostic": diagnostic[:512]})
        except BaseException:
            pass


if __name__ == "__main__":
    main()
