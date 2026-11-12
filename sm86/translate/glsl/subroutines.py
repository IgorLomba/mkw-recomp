"""Subroutines called from several places, copied into each call for the structured form (flow.py).

ptxas keeps a short helper once after the last EXIT and calls it from a few places: the caller sets the return
address, `MOV R18, 0xe90; CALL.REL.NOINC 0x3fd0`, and the helper ends `MOV R12, R18; MOV R13, 0x0; RET.REL.NODEC R12`
(the slow path of a square root, with its own forward branches to that end). One such RET left the whole program as a
`switch (pc)`: 18 of the 21 programs that took AMD's compiler 2-6 s in that form (12/11/2026). The calls are plain
jumps and the RET jumps back through a register, so with the return address proven to be the instruction after each
CALL, a copy of the body in place of each call runs the same instructions.
"""

import re

from program import Insn

from glsl.common import PT

# what a body may not hold: its only branches are forward jumps inside it
STOP = {"BRX", "BRXU", "CALL", "RET", "EXIT", "BSSY", "BSYNC", "BREAK", "WARPSYNC", "BPT", "KILL"}


def _plain(i: Insn) -> bool:
    return i.v("Pg", PT) == PT and not i.v("Pg@not", 0) and i.v("UPg", PT) == PT and not i.v("UPg@not", 0)


def _branch_target(i: Insn) -> int:
    return i.pc + 16 + 4 * i.sv("sImm")


def _writes(stmt: str, r: int) -> bool:
    """Whether a translated statement assigns register r (registers are only written as `rN = `)."""
    return re.search(rf"\br{r} = ", stmt) is not None


def calls(insns: list[Insn]) -> dict[int, tuple[int, int]] | None:
    """{CALL index: (body start, RET index)} by the shape of the code, or None unless every CALL and RET fits: an
    unpredicated CALL right after `MOV Rx, imm`; a body after an unpredicated EXIT, BRA or RET (nothing falls into it)
    whose branches are forward jumps inside it and whose first RET is unpredicated; no other jump into a body. Where
    the RET goes is checked on the translation (returns)."""
    call_ks = [k for k, i in enumerate(insns) if i.op == "CALL"]
    rets = {k for k, i in enumerate(insns) if i.op == "RET"}
    if not call_ks or not rets:
        return None
    index = {i.pc: k for k, i in enumerate(insns)}
    out: dict[int, tuple[int, int]] = {}
    for c in call_ks:
        call = insns[c]
        t = index.get(call.pc + 16 + 4 * call.sv("Sa"))
        if t is None or t == 0 or c == 0 or not _plain(call):
            return None
        before = insns[t - 1]
        if before.op not in ("EXIT", "BRA", "RET") or not _plain(before):
            return None
        r = next((k for k in range(t, len(insns)) if insns[k].op in STOP), None)
        if r is None or r not in rets or not _plain(insns[r]) or insns[r].sym("addr") != "REL":
            return None
        for k in range(t, r):
            if insns[k].op == "BRA":
                dest = index.get(_branch_target(insns[k]))
                if dest is None or not k < dest <= r:
                    return None
        if insns[c - 1].cls != "mov__RI" or not _plain(insns[c - 1]):
            return None
        out[c] = (t, r)
    bodies = {(t, r) for t, r in out.values()}
    if rets - {r for _, r in bodies}:
        return None
    for k, i in enumerate(insns):
        if i.op == "BRA" and not any(t <= k <= r for t, r in bodies):
            dest = index.get(_branch_target(i), -1)
            if any(t <= dest <= r for t, r in bodies):
                return None
    return out


def returns(insns: list[Insn], stmts: list[str], inline: dict[int, tuple[int, int]]) -> bool:
    """Whether each RET of `inline` goes back to the instruction after its CALL: its register Ra, or the Rx that an
    unpredicated `MOV Ra, Rx` copies on every path (after the body's last join, no branch until the RET) with Rx
    unchanged before it, holds what `MOV Rx, imm` just before the CALL put there, and imm plus the RET's offset is the
    CALL's pc + 16. stmts: the translation of each instruction, whose `rN = ` are the writes."""
    index = {i.pc: k for k, i in enumerate(insns)}
    for c, (t, r) in inline.items():
        ret, call, set_back = insns[r], insns[c], insns[c - 1]
        joins = {t} | {index[_branch_target(insns[k])] for k in range(t, r) if insns[k].op == "BRA"}
        reg = ret.v("Ra")
        last = next((k for k in range(r - 1, t - 1, -1) if _writes(stmts[k], reg)), None)
        if last is not None:
            mov = insns[last]
            if mov.cls != "mov__RR" or not _plain(mov) or max(joins) > last:
                return False
            if any(insns[k].op == "BRA" for k in range(last, r)):
                return False
            reg = mov.v("Rb")
            if any(_writes(stmts[k], reg) for k in range(t, last)):
                return False
        back = (ret.pc + 16 + 4 * ret.sv("Ra_offset")) & 0xFFFFFFFF
        if set_back.v("Rd") != reg or (set_back.v("Sb") + back) & 0xFFFFFFFF != call.pc + 16:
            return False
    return True


def splice(
    stmts: list[str], branches: dict[int, tuple[int, str | None]], inline: dict[int, tuple[int, int]]
) -> tuple[list[str], dict[int, tuple[int, str | None]]]:
    """stmts and branches (as glsl/core.py builds them for flow.structure) with a copy of each body [t, RET) after its
    CALL, which then falls into it and out of it to the instruction after the CALL; the bodies and RETs in place become
    empty and lose their branches. The body's statements were translated in place, after a branch target that
    cleared the constant fold, so they hold for any caller."""
    dead = {k for t, r in inline.values() for k in range(t, r + 1)}
    pos: dict[int, int] = {}
    copies: dict[int, int] = {}  # CALL index -> where its copy starts
    s2: list[str] = []
    for k, s in enumerate(stmts):
        pos[k] = len(s2)
        s2.append("" if k in dead or k in inline else s)
        if k in inline:
            t, r = inline[k]
            copies[k] = len(s2)
            s2 += stmts[t:r]
    pos[len(stmts)] = len(s2)
    b2: dict[int, tuple[int, str | None]] = {}
    for k, (u, c) in branches.items():
        if k not in dead:
            b2[pos[k]] = (pos.get(u, pos[len(stmts)]), c)
    for k, (t, r) in inline.items():
        for j in range(t, r):
            if j in branches:
                u, c = branches[j]
                b2[copies[k] + j - t] = (copies[k] + u - t if u < r else pos[k + 1], c)
    return s2, b2
