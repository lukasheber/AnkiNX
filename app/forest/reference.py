"""Check the Rust port of the forest (switch/anki_switch_core/src/forest.rs) against the add-on.

  forest_dump <collection.anki2> [nature] > dump.json
  python3 app/forest/reference.py <collection.anki2> dump.json [nature]

Works the forest out with Memory Forest's own Python (app/library/memory-forest), for the same
day and clock as the dump, as its payload.py does with its default settings on Peaceful, and
lists every difference from the port's. Exits 1 if there is any.
"""

from __future__ import annotations

import datetime as dt
import json
import os
import sqlite3
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "library", "memory-forest"))

import events  # noqa: E402
import forest_data  # noqa: E402
import milestones  # noqa: E402
import scene  # noqa: E402
import study_log  # noqa: E402

PAGE_LEAVES_OUT = ("mature", "day")


class DB:
    """The add-on's DBProxy, as its dev scripts stand it in (.all / .scalar)."""

    def __init__(self, path: str):
        self.con = sqlite3.connect(f"file:{path}?mode=ro", uri=True)

    def all(self, sql, *args):
        return self.con.execute(sql, args).fetchall()

    def scalar(self, sql, *args):
        row = self.con.execute(sql, args).fetchone()
        return row[0] if row else None


def expected(db: DB, cutoff: int, today: int, now: float, nature: str) -> dict:
    """events_state.apply and payload.payload on the add-on's first look (nothing remembered yet),
    with the review log all there."""
    rows = study_log.load_rows(db, cutoff, suspended=True)
    forest = forest_data.build_forest(rows, cutoff, today, now)
    days = forest["review_days"]
    date = lambda d: study_log.day_date(d, cutoff).isoformat()  # noqa: E731
    extras = {"nature": nature, "craters": [], "strike": None, "doom": None, "fire": None}
    if nature == "merciless":
        out = events.merciless(forest["trees"], days)
        if out["latest"]:
            latest = out["latest"]
            extras["craters"] = [dict(c, date=date(c["ago"])) for c in out["craters"]]
            extras["strike"] = {"date": date(latest["ago"]), "lost": latest["lost"], "seen": date(latest["ago"]),
                                "fresh": False, "news": latest["ago"] <= events.STRIKE_NEWS_DAYS, "told": False}
        if out["hits"]:
            hit = out["hits"][-1]
            reviews = sum(n for d, n in forest["day_reviews"].items() if d < hit)
            forest = forest_data.rebuild(forest, out["trees"], {d for d in days if d < hit}, reviews)
        if out["doom"]:
            extras["doom"] = out["doom"]
    elif nature == "wild":
        trees, extras["fire"] = events.set_fire(forest["trees"], days, forest_data.MAX_INDIVIDUAL_TREES)
        forest = dict(forest, trees=trees)
    visitors = milestones.visitors(forest["stats"], milestones.arrivals(forest["stats"], {}, None), today)
    trees = events.mark_big_days(forest["trees"])
    stagnation = events.stagnation(trees, days)
    if stagnation and not study_log.has_new_cards(db):
        stagnation = 0.0
    overdue, counts = study_log.load_backlog(db, today, cutoff, events.REVIEW_HELL_USUAL_DAYS)
    usual = events.usual_reviews(counts)
    backlog = {"overdue": overdue, "usual": round(usual), "hell": round(events.review_hell(overdue, usual), 3),
               "cleared": False}
    cured = study_log.load_cured(db, cutoff, events.CURED_DAYS)
    trees = [dict(t, cured=cured[t["ago"]]) if cured.get(t["ago"]) else t for t in trees]
    if events.calm(nature):
        trees, stagnation, backlog = events.calm_trees(trees), 0.0, dict(backlog, hell=0.0)
    anki_today = study_log.day_date(0, cutoff)
    ann_all = milestones.anniversaries(trees, anki_today)
    merged = forest_data.merge_old(dict(forest, trees=trees))
    merged_count = len(trees) - len(merged["trees"])
    free = set()
    for older, newer in study_log.load_free_days(db, cutoff, None):
        free |= set(range(older, newer + 1))
    streak, longest = forest_data._streaks(set(rows.day_reviews), frozenset(free))
    return dict(extras, **{
        "trees": [{k: v for k, v in t.items() if k not in PAGE_LEAVES_OUT} for t in merged["trees"]],
        "stats": dict(forest["stats"], streak=streak, longest_streak=longest, streak_week=7),
        "visitors": visitors,
        "anniversaries": [i - merged_count for i in ann_all if i >= merged_count],
        "events": scene.events(forest["stats"], anki_today),
        "merged": merged.get("merged"),
        "forestSeed": forest["forest_seed"],
        "stagnation": stagnation,
        "backlog": backlog,
    })


def differences(want, got, path="") -> list:
    if isinstance(want, dict) and isinstance(got, dict):
        out = []
        for k in sorted(set(want) | set(got)):
            if k not in got:
                out.append(f"{path}.{k}: missing (add-on: {want[k]!r})")
            elif k not in want:
                out.append(f"{path}.{k}: extra ({got[k]!r})")
            else:
                out += differences(want[k], got[k], f"{path}.{k}")
        return out
    if isinstance(want, list) and isinstance(got, list):
        if len(want) != len(got):
            return [f"{path}: {len(got)} items, add-on {len(want)}"]
        return [d for i, (a, b) in enumerate(zip(want, got)) for d in differences(a, b, f"{path}[{i}]")]
    if isinstance(want, float) or isinstance(got, float):
        return [] if abs(float(want) - float(got)) < 1e-9 else [f"{path}: {got!r}, add-on {want!r}"]
    return [] if want == got else [f"{path}: {got!r}, add-on {want!r}"]


def main() -> None:
    db = DB(sys.argv[1])
    dump = json.load(open(sys.argv[2], encoding="utf-8"))
    at, page = dump["at"], dump["page"]
    nature = sys.argv[3] if len(sys.argv) > 3 else "peaceful"
    want = expected(db, at["cutoff"], at["today"], at["now"], nature)
    got = {k: page.get(k) for k in want}
    diffs = differences(want, got)
    s = want["stats"]
    print(f"{s['trees']} trees ({len(want['trees'])} drawn), {s['cards']} cards, streak {s['streak']} "
          f"(longest {s['longest_streak']}), {s['ponds']} ponds, visitors {[v['key'] for v in want['visitors']]}, "
          f"{sum(1 for t in want['trees'] if t.get('big'))} big days, {sum(1 for t in want['trees'] if t.get('cured'))} with songbirds")
    print(f"{nature}: fire {want['fire']}, {len(want['craters'])} craters, strike {want['strike']}, doom {want['doom']}, "
          f"{sum(1 for t in want['trees'] if t.get('leeches'))} trees with crows, grass {want['stagnation']}, backlog {want['backlog']}")
    for d in diffs[:40]:
        print("  ", d)
    print("same as the add-on" if not diffs else f"{len(diffs)} differences")
    sys.exit(1 if diffs else 0)


if __name__ == "__main__":
    main()
