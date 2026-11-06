"""Local mock Agent WebSocket server for trying the WinAuto Agent dialog."""
from __future__ import annotations

import argparse
import asyncio
import json
import uuid
from pathlib import Path

import websockets


async def send(socket, message: dict) -> None:
    await socket.send(json.dumps(message, ensure_ascii=False))


async def run_prompt(socket, session_id: str, prompt: str) -> None:
    await send(socket, {"type": "turn_start", "session_id": session_id})
    reply = f"这是本地演示回复。我收到的任务是：{prompt}"
    try:
        for offset in range(0, len(reply), 8):
            await send(socket, {"type": "delta", "text": reply[offset : offset + 8]})
            await asyncio.sleep(0.15)
        await send(
            socket,
            {
                "type": "tool_event",
                "event": {"name": "demo.echo", "status": "simulated"},
            },
        )
        await send(socket, {"type": "turn_complete"})
        await send(socket, {"type": "turn_end"})
    except asyncio.CancelledError:
        raise


async def handle(socket) -> None:
    session_id = ""
    turn: asyncio.Task | None = None
    async for raw in socket:
        try:
            request = json.loads(raw)
        except json.JSONDecodeError:
            await send(socket, {"type": "error", "error": "无效 JSON"})
            continue
        if not isinstance(request, dict):
            await send(socket, {"type": "error", "error": "消息必须是 JSON 对象"})
            continue

        kind = request.get("type")
        if kind == "start":
            directory = Path(request.get("working_directory", "")).expanduser()
            if not directory.is_dir():
                await send(socket, {"type": "error", "error": "工作目录不存在或不是目录"})
                continue
            session_id = str(uuid.uuid4())
            await send(
                socket,
                {
                    "type": "session",
                    "action": "started",
                    "session_id": session_id,
                    "working_directory": str(directory.resolve()),
                },
            )
        elif kind == "resume":
            session_id = str(request.get("session_id", "")).strip()
            if not session_id:
                await send(socket, {"type": "error", "error": "session_id 不能为空"})
                continue
            await send(
                socket,
                {"type": "session", "action": "resumed", "session_id": session_id},
            )
        elif kind == "chat":
            prompt = str(request.get("prompt", "")).strip()
            if not session_id:
                await send(socket, {"type": "error", "error": "请先创建或恢复会话"})
            elif not prompt:
                await send(socket, {"type": "error", "error": "prompt 不能为空"})
            elif turn and not turn.done():
                await send(socket, {"type": "error", "error": "当前请求尚未结束"})
            else:
                turn = asyncio.create_task(run_prompt(socket, session_id, prompt))
        elif kind == "abort":
            if turn and not turn.done():
                turn.cancel()
                await asyncio.gather(turn, return_exceptions=True)
            await send(socket, {"type": "status", "status": "aborted"})
        elif kind == "disconnect":
            if turn and not turn.done():
                turn.cancel()
                await asyncio.gather(turn, return_exceptions=True)
            await send(socket, {"type": "status", "status": "disconnected"})
            return
        elif kind == "ping":
            await send(socket, {"type": "pong"})
        else:
            await send(socket, {"type": "error", "error": "未知消息类型"})


async def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    async with websockets.serve(handle, args.host, args.port, max_size=2 * 1024 * 1024):
        print(f"Demo Agent listening on ws://{args.host}:{args.port} (Ctrl+C to stop)")
        await asyncio.Future()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
