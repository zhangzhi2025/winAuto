"""
Copilot SDK WebSocket bridge.
Requires GitHub Copilot CLI installed and authenticated (`copilot` command).
"""
from __future__ import annotations

import asyncio
import json
import logging
import os
import signal
import shutil
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import websockets
from copilot import CopilotClient

LOG = logging.getLogger("copilot-qt.backend")
logging.basicConfig(
    level=os.getenv("LOG_LEVEL", "INFO"),
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)

HOST = os.getenv("WS_HOST", "127.0.0.1")
PORT = int(os.getenv("WS_PORT", "8765"))
WORKSPACE = Path(os.getenv("WORKSPACE", Path.cwd())).expanduser().resolve()
CLI_PATH = os.getenv("COPILOT_CLI_PATH") or shutil.which("copilot")
AUTO_APPROVE = os.getenv("COPILOT_AUTO_APPROVE", "false").lower() == "true"

def jsonable(value: Any) -> Any:
    """Convert SDK event objects to JSON-safe dictionaries."""
    if value is None or isinstance(value, (str, int, float, bool)):
        return value
    if isinstance(value, dict):
        return {str(k): jsonable(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [jsonable(v) for v in value]
    if hasattr(value, "model_dump"):
        return jsonable(value.model_dump())
    if hasattr(value, "__dict__"):
        return {k: jsonable(v) for k, v in vars(value).items() if not k.startswith("_")}
    return str(value)


@dataclass
class ClientState:
    ws: Any
    session_id: str | None = None
    session: Any = None
    send_lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    task: asyncio.Task | None = None

    async def send(self, payload: dict[str, Any]) -> None:
        async with self.send_lock:
            await self.ws.send(json.dumps(payload, ensure_ascii=False))


class CopilotBridge:
    def __init__(self) -> None:
        self.client = CopilotClient()
        self._connected = False

    async def start(self) -> None:
        await self.client.start()
        self._connected = True
        LOG.info("Copilot SDK client started")

    async def stop(self) -> None:
        if self._connected:
            await self.client.stop()
            self._connected = False

    async def handle(self, ws) -> None:
        state = ClientState(ws=ws)
        LOG.info("WebSocket connected")
        try:
            async for raw in ws:
                try:
                    msg = json.loads(raw)
                    await self.dispatch(state, msg)
                except json.JSONDecodeError:
                    await state.send({"type": "error", "error": "无效 JSON"})
                except Exception as exc:
                    LOG.exception("Request failed")
                    await state.send({"type": "error", "error": str(exc)})
        finally:
            if state.task and not state.task.done():
                state.task.cancel()
                await asyncio.gather(state.task, return_exceptions=True)
            if state.session:
                try:
                    await state.session.disconnect()
                except Exception:
                    LOG.exception("Session disconnect failed")
            LOG.info("WebSocket disconnected")

    async def dispatch(self, st: ClientState, msg: dict[str, Any]) -> None:
        kind = msg.get("type")
        if kind == "start":
            await self.start_session(st, msg)
        elif kind == "resume":
            await self.resume_session(st, msg)
        elif kind == "chat":
            if not st.session:
                raise RuntimeError("请先 start 或 resume 会话")
            if st.task and not st.task.done():
                raise RuntimeError("当前会话仍在处理请求，请稍后再发送")
            prompt = str(msg.get("prompt", "")).strip()
            if not prompt:
                raise ValueError("prompt 不能为空")
            st.task = asyncio.create_task(self.run_prompt(st, prompt))
        elif kind == "abort":
            if st.session:
                await st.session.abort()
                await st.send({"type": "status", "status": "aborted"})
        elif kind == "disconnect":
            if st.session:
                await st.session.disconnect()
                st.session = None
                st.session_id = None
            await st.send({"type": "status", "status": "disconnected"})
        elif kind == "ping":
            await st.send({"type": "pong"})
        else:
            raise ValueError(f"未知消息类型: {kind}")

    async def start_session(self, st: ClientState, msg: dict[str, Any]) -> None:
        if st.session:
            await st.session.disconnect()
        cwd = Path(msg.get("working_directory") or WORKSPACE).expanduser().resolve()
        if not cwd.exists() or not cwd.is_dir():
            raise ValueError(f"工作目录不存在: {cwd}")
        kwargs: dict[str, Any] = {
            "working_directory": str(cwd),
            "streaming": True,
            "model": msg.get("model") or "auto",
        }
        if AUTO_APPROVE:
            from copilot.session import PermissionHandler
            kwargs["on_permission_request"] = PermissionHandler.approve_all
            LOG.warning("COPILOT_AUTO_APPROVE enabled: tool/file permissions are auto-approved")
        st.session = await self.client.create_session(**kwargs)
        st.session_id = st.session.session_id
        await st.send({
            "type": "session", "action": "started",
            "session_id": st.session_id, "working_directory": str(cwd),
        })

    async def resume_session(self, st: ClientState, msg: dict[str, Any]) -> None:
        session_id = str(msg.get("session_id") or "").strip()
        if not session_id:
            raise ValueError("session_id 不能为空")
        if st.session:
            await st.session.disconnect()
        kwargs: dict[str, Any] = {"streaming": True}
        if AUTO_APPROVE:
            from copilot.session import PermissionHandler
            kwargs["on_permission_request"] = PermissionHandler.approve_all
        st.session = await self.client.resume_session(session_id, **kwargs)
        st.session_id = session_id
        await st.send({"type": "session", "action": "resumed", "session_id": session_id})

    async def run_prompt(self, st: ClientState, prompt: str) -> None:
        await st.send({"type": "turn_start", "session_id": st.session_id})
        loop = asyncio.get_running_loop()
        done = asyncio.Event()
        last_error: list[str] = []

        def on_event(event: Any) -> None:
            # SDK callbacks are synchronous; marshal websocket writes to this loop.
            event_type = getattr(event.type, "value", str(event.type))
            data = jsonable(event.data)
            if event_type == "assistant.message_delta":
                delta = data.get("delta_content", "") if isinstance(data, dict) else ""
                if delta:
                    asyncio.run_coroutine_threadsafe(
                        st.send({"type": "delta", "text": delta}), loop
                    )
            elif event_type == "assistant.message":
                content = data.get("content", "") if isinstance(data, dict) else ""
                if content:
                    asyncio.run_coroutine_threadsafe(
                        st.send({"type": "message", "text": content}), loop
                    )
            elif event_type == "tool.execution_start":
                asyncio.run_coroutine_threadsafe(
                    st.send({"type": "tool_event", "event": jsonable(event)}), loop
                )
            elif event_type == "session.error":
                last_error.append(str(data))
            elif event_type == "session.idle":
                loop.call_soon_threadsafe(done.set)

        unsubscribe = st.session.on(on_event)
        try:
            # send_and_wait waits for the current turn and avoids a polling race.
            await st.session.send_and_wait(prompt)
            if last_error:
                await st.send({"type": "error", "error": last_error[-1]})
            await st.send({"type": "turn_complete"})
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            LOG.exception("Copilot turn failed")
            await st.send({"type": "error", "error": str(exc)})
        finally:
            unsubscribe()
            await st.send({"type": "turn_end"})


async def main() -> None:
    bridge = CopilotBridge()
    await bridge.start()
    try:
        async with websockets.serve(
            bridge.handle, HOST, PORT, max_size=8 * 1024 * 1024,
            ping_interval=20, ping_timeout=20,
        ):
            LOG.info("WebSocket listening on ws://%s:%s", HOST, PORT)
            await asyncio.Future()
    finally:
        await bridge.stop()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
