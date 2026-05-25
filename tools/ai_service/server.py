"""MAA-Simu AI Inference Service - Phase 1 MVP

Random-policy recruitment agent for roguelike mode.
Receives screenshot + candidate list, returns a random selection.

Usage:
    pip install fastapi uvicorn pillow numpy pydantic
    python server.py
"""

import base64
import io
import random
from typing import Optional

from fastapi import FastAPI
from pydantic import BaseModel
import uvicorn

app = FastAPI(title="MAA-Simu AI Service")


# --- Request/Response Models ---

class Candidate(BaseModel):
    name: str
    elite: int
    level: int
    priority: int
    is_alternate: bool


class GameState(BaseModel):
    theme: str
    floor: int
    hope: int
    hp: int
    own_operators: list[str]


class ActRequest(BaseModel):
    screenshot_b64: str
    candidates: list[Candidate]
    state: GameState


class ActResponse(BaseModel):
    chosen_operator: str
    confidence: float = 1.0


# --- Policy ------------------------------------------------------------------

def random_policy(candidates: list[Candidate]) -> str:
    """Random recruitment policy - picks a random candidate."""
    if not candidates:
        return ""
    chosen = random.choice(candidates)
    print(f"[random_policy] {len(candidates)} candidates, chose: {chosen.name}")
    return chosen.name


# --- API Endpoints -----------------------------------------------------------

@app.get("/api/health")
async def health():
    print("[health] received health check from MAA-Simu")
    return {"status": "ok"}


@app.post("/api/act", response_model=ActResponse)
async def act(req: ActRequest):
    # Decode screenshot for debugging
    img_bytes = base64.b64decode(req.screenshot_b64)
    print(f"[act] received screenshot: {len(img_bytes)} bytes")
    print(f"[act] candidates: {len(req.candidates)}, state: theme={req.state.theme} floor={req.state.floor}")

    chosen = random_policy(req.candidates)

    return ActResponse(chosen_operator=chosen, confidence=1.0)


if __name__ == "__main__":
    uvicorn.run(app, host="127.0.0.1", port=8765)
