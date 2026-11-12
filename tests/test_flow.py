"""sm86/translate/flow.py: shapes ptxas leaves that the structured form must keep, checked with flow_sim against the
branch graph."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sm86/translate"))
import flow  # noqa: E402
import flow_sim  # noqa: E402


def program(n: int, branches: dict) -> list[str]:
    """n instructions, each a statement except the branches."""
    return ["" if k in branches else f"x{k};" for k in range(n)]


def test_jump_into_a_shared_tail():
    # FS 63a69b46 (12/11/2026), its block at 806-832 cut down. The then-part (1-5) jumps from 2 into the tail [9, 10)
    # of the else-part (6-10); without the copy no nesting expresses it and the translator kept the switch.
    #  0 @!P0 BRA 6 | 1 a | 2 @P1 BRA 9 | 3 b | 4 c | 5 BRA 11
    #  6 d | 7 @!P1 BRA 10 | 8 e | 9 f | 10 g | 11 join
    branches = {0: (6, "!p0"), 2: (9, "p1"), 5: (11, None), 7: (10, "!p1")}
    branches[10] = (11, None)
    stmts = program(12, branches)
    stmts[9], stmts[10] = "f;", ""
    with pytest.raises(flow.Fail):  # the plain structuring runs other instructions
        mk, nb = flow_sim.markers(stmts), flow_sim.named(branches)
        if flow_sim.diff(mk, nb, flow._structure(mk, nb), 16):
            raise flow.Fail
    lines = flow.structure(stmts, branches)
    assert not any("switch" in x for x in lines)
    mk, nb = flow_sim.markers(stmts), flow_sim.named(branches)
    d = flow._dup_tails(stmts, branches, mk, nb, True)
    assert flow_sim.diff(mk, nb, flow._structure(d[2], d[3]), 64) is None


def test_no_tail_to_copy():
    # every jump goes straight on to a branch: nothing is copied, and the program keeps its plain structuring
    branches = {0: (3, "p0"), 2: (4, None)}
    stmts = program(5, branches)
    with pytest.raises(flow.Fail):
        flow._dup_tails(stmts, branches, flow_sim.markers(stmts), flow_sim.named(branches), False)
    assert flow.structure(stmts, branches)
