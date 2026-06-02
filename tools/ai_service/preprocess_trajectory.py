#!/usr/bin/env python3
"""Preprocess MAA-Simu trajectory JSONL for VLA training.

Usage:
    python preprocess_trajectory.py <session_dir> [--output train.jsonl]
"""

import json
import argparse
import sys
from pathlib import Path


def load_jsonl(path: Path) -> list[dict]:
    records = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    return records


def save_jsonl(records: list[dict], path: Path):
    with open(path, "w", encoding="utf-8") as f:
        for r in records:
            f.write(json.dumps(r, ensure_ascii=False) + "\n")


def filter_empty_observation(records: list[dict]) -> list[dict]:
    """Remove records with empty observation path."""
    return [
        r for r in records
        if r.get("observation", "").strip()
    ]


def filter_noise_actions(records: list[dict]) -> list[dict]:
    """Filter known mechanical noise patterns.

    - BattleCancelSelection: always follows a deploy, carries no decision info.
    """
    noise_tasks = {"BattleCancelSelection"}
    filtered = []
    for r in records:
        task = r.get("action", {}).get("task", "")
        if task in noise_tasks:
            continue
        filtered.append(r)
    return filtered


def deduplicate_consecutive_waits(records: list[dict]) -> list[dict]:
    """Remove consecutive idential wait actions."""
    result = []
    prev_text = None
    for r in records:
        if r["task_type"] == "wait":
            text = r.get("action", {}).get("text", "")
            if text == prev_text:
                continue
            prev_text = text
        else:
            prev_text = None
        result.append(r)
    return result


def remove_standalone_waits(records: list[dict]) -> list[dict]:
    """Remove wait records that are not preceded or followed by an action.
    These are typically loading screens without meaningful context."""
    result = []
    for i, r in enumerate(records):
        if r["task_type"] == "wait":
            prev_action = i > 0 and records[i - 1]["task_type"] != "wait"
            next_action = i + 1 < len(records) and records[i + 1]["task_type"] != "wait"
            if not prev_action and not next_action:
                continue
        result.append(r)
    return result


def normalize_action(record: dict) -> dict:
    """Ensure action field has consistent structure."""
    action = record.get("action", {})
    # Ensure 'text' field exists
    if "text" not in action:
        action["text"] = f"{record['task_type']}"

    # For ClickSelf/ClickRect, ensure coordinates exist
    if action.get("type") in ("ClickSelf", "ClickRect"):
        if "x" not in action:
            action["type"] = "click"  # mark as incomplete
    return record


def compute_reward_stats(records: list[dict]) -> dict:
    """Compute reward statistics for the episode."""
    rewards = [r.get("reward", 0) for r in records]
    return {
        "total_reward": sum(rewards),
        "mean_reward": sum(rewards) / len(rewards) if rewards else 0,
        "min_reward": min(rewards) if rewards else 0,
        "max_reward": max(rewards) if rewards else 0,
    }


def print_stats(records: list[dict], label: str = ""):
    """Print distribution statistics."""
    by_type = {}
    for r in records:
        t = r["task_type"]
        by_type[t] = by_type.get(t, 0) + 1

    print(f"\n{'='*60}")
    print(f"  {label} ({len(records)} records)")
    print(f"{'='*60}")
    for t, count in sorted(by_type.items(), key=lambda x: -x[1]):
        pct = count / len(records) * 100
        print(f"  {t:<15} {count:>5}  ({pct:5.1f}%)")

    reward_stats = compute_reward_stats(records)
    print(f"\n  Reward: total={reward_stats['total_reward']:.1f}, "
          f"mean={reward_stats['mean_reward']:.3f}, "
          f"range=[{reward_stats['min_reward']}, {reward_stats['max_reward']}]")


def main():
    parser = argparse.ArgumentParser(description="Preprocess MAA trajectory for VLA training")
    parser.add_argument("session_dir", help="Path to session directory (contains trajectory.jsonl)")
    parser.add_argument("--output", "-o", help="Output JSONL file path", default=None)
    parser.add_argument("--no-filter-noise", action="store_true", help="Don't filter noise actions")
    parser.add_argument("--no-dedup-wait", action="store_true", help="Don't dedup consecutive waits")
    parser.add_argument("--keep-empty-obs", action="store_true", help="Keep records with empty observation")
    parser.add_argument("--keep-standalone-waits", action="store_true", help="Keep standalone wait records")
    args = parser.parse_args()

    session = Path(args.session_dir)
    jsonl_path = session / "trajectory.jsonl"
    if not jsonl_path.exists():
        print(f"Error: {jsonl_path} not found")
        sys.exit(1)

    records = load_jsonl(jsonl_path)
    print_stats(records, "Original")

    # Step 1: Filter empty observations
    if not args.keep_empty_obs:
        records = filter_empty_observation(records)
        print_stats(records, "After removing empty observations")

    # Step 2: Filter noise actions (BattleCancelSelection etc.)
    if not args.no_filter_noise:
        records = filter_noise_actions(records)
        print_stats(records, "After noise filtering")

    # Step 3: Dedup consecutive waits
    if not args.no_dedup_wait:
        records = deduplicate_consecutive_waits(records)
        print_stats(records, "After wait dedup")

    # Step 4: Remove standalone waits
    if not args.keep_standalone_waits:
        records = remove_standalone_waits(records)
        print_stats(records, "After removing standalone waits")

    # Step 5: Normalize actions
    records = [normalize_action(r) for r in records]

    # Step 6: Remove records with truly invalid observations
    records = [r for r in records if r.get("observation")]

    # Write output
    out_path = Path(args.output) if args.output else session / "trajectory_clean.jsonl"
    save_jsonl(records, out_path)
    print(f"\n  Clean trajectory saved to: {out_path}")

    # Print action distribution
    action_types = {}
    for r in records:
        t = r.get("action", {}).get("type", "unknown")
        action_types[t] = action_types.get(t, 0) + 1
    print(f"\n  Action type distribution:")
    for t, c in sorted(action_types.items(), key=lambda x: -x[1]):
        print(f"    {t:<15} {c:>5}  ({c/len(records)*100:5.1f}%)")


if __name__ == "__main__":
    main()
