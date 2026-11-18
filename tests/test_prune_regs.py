"""Registers a recompiled function never touches are not loaded, stored or reloaded (recomp/x86rec.py prune_regs)."""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "recomp"))

import x86rec  # noqa: E402


def _func(body_lines):
    return "\n".join(["void F_00401000(CPU *c) {", "  uint8_t *const mb = g_mem;", x86rec.REG_DECL] + body_lines + ["}"])


def test_unused_registers_are_dropped_everywhere():
    src = _func([f"  esp-=4; W32(esp,0x401005u); {x86rec.SPILL} F_00402000(c); {x86rec.RELOAD}",
                 "  eax=(uint32_t)(R32(esp+4u));",
                 f"  esp+=4u; {x86rec.SPILL} return;"])
    out = x86rec.prune_regs(src)
    assert "uint32_t eax=c->eax,esp=c->esp;" in out
    assert "c->eax=eax;c->esp=esp; F_00402000(c); eax=c->eax;esp=c->esp;" in out
    assert "c->eax=eax;c->esp=esp; return;" in out
    for reg in ("ecx", "edx", "ebx", "ebp", "esi", "edi"):
        assert reg not in out


def test_register_used_by_one_instruction_is_kept():
    src = _func(["  edi=(uint32_t)(R32(esp+8u));", f"  esp+=4u; {x86rec.SPILL} return;"])
    out = x86rec.prune_regs(src)
    assert "edi=c->edi" in out and "c->edi=edi;" in out
    assert "ebx" not in out


def test_direct_use_of_the_cpu_field_keeps_the_register():
    src = _func(["  x=c->ebx;", f"  esp+=4u; {x86rec.SPILL} return;"])
    out = x86rec.prune_regs(src)
    assert "ebx=c->ebx" in out and "c->ebx=ebx;" in out


def test_function_writing_everything_is_unchanged():
    src = _func(["  eax=ecx+1; ecx=edx; edx=ebx; ebx=ebp; ebp=esi; esi=edi; edi--;", f"  esp+=4u; {x86rec.SPILL} return;"])
    assert x86rec.prune_regs(src) == src


def test_register_only_read_is_loaded_and_reloaded_but_never_stored():
    # ecx is read (also in a comparison and a bitwise AND, which are not writes); a callee may change it: reload kept.
    src = _func([f"  esp-=4; W32(esp,0x401005u); {x86rec.SPILL} F_00402000(c); {x86rec.RELOAD}",
                 "  { uint32_t r=ecx&ecx; zf=(r==0); }",
                 "  if (ecx==0) eax=(uint32_t)(R32(ecx+4u));",
                 f"  esp+=4u; {x86rec.SPILL} return;"])
    out = x86rec.prune_regs(src)
    assert "uint32_t eax=c->eax,ecx=c->ecx,esp=c->esp;" in out
    assert "c->eax=eax;c->esp=esp; F_00402000(c); eax=c->eax;ecx=c->ecx;esp=c->esp;" in out
    assert "c->ecx=ecx;" not in out


def test_every_write_form_keeps_the_store():
    for line in ("  ebx=5;", "  ebx+=4u;", "  ebx-=4;", "  ebx--;", "  rt_cpuid(c,&eax,&ebx,&ecx,&edx);", "  ebx|=1u;"):
        out = x86rec.prune_regs(_func([line, f"  esp+=4u; {x86rec.SPILL} return;"]))
        assert "c->ebx=ebx;" in out, line


# ---------- per call site: registers the callee (and everything it calls) never touches ----------
def _call(name):
    return f"  esp-=4; W32(esp,0x401005u); {x86rec.SPILL} {name}(c); {x86rec.RELOAD}"


def _caller(callee):
    return _func(["  ebx=5; esi=ebx; eax=esi;", _call(callee), f"  esp+=4u; {x86rec.SPILL} return;"])


def _leaf(lines):
    return _func(lines + [f"  esp+=4u; {x86rec.SPILL} return;"])


def _final(bodies, name, forced_all=()):
    infos = {n: x86rec.analyze_regs(b) for n, b in bodies.items()}
    touched = x86rec.touched_sets(infos, forced_all)
    return x86rec.finalize_regs(bodies[name], infos[name], touched)


def test_call_site_keeps_only_what_the_callee_touches():
    out = _final({"F_00401000": _caller("F_00402000"), "F_00402000": _leaf(["  eax=(uint32_t)(R32(esp+4u));"])},
                 "F_00401000")
    assert "c->eax=eax;c->esp=esp; F_00402000(c); eax=c->eax;esp=c->esp;" in out
    assert "c->eax=eax;c->ebx=ebx;c->esp=esp;c->esi=esi; return;" in out   # the return still stores everything written


def test_call_site_follows_the_callees_of_the_callee():
    out = _final({"F_00401000": _caller("F_00402000"),
                  "F_00402000": _leaf(["  eax=1;", _call("F_00403000")]),
                  "F_00403000": _leaf(["  esi=(uint32_t)(R32(esp+4u));"])}, "F_00401000")
    assert "c->eax=eax;c->esp=esp;c->esi=esi; F_00402000(c); eax=c->eax;esp=c->esp;esi=c->esi;" in out


def test_callee_with_an_unknown_call_touches_everything():
    for line in ("  { uint32_t t=eax; rt_call(c,t); }", "  imp_KERNEL32_Sleep(c);", "  some_hook(c);"):
        out = _final({"F_00401000": _caller("F_00402000"), "F_00402000": _leaf([line])}, "F_00401000")
        assert "c->eax=eax;c->ebx=ebx;c->esp=esp;c->esi=esi; F_00402000(c); eax=c->eax;ebx=c->ebx;esp=c->esp;esi=c->esi;" in out, line


def test_pure_runtime_helpers_do_not_make_a_callee_opaque():
    out = _final({"F_00401000": _caller("F_00402000"),
                  "F_00402000": _leaf(["  rt_fcom(c, 1.0, 2.0); eax=rt_fnstsw(c);", "  if (!eax) { rt_unimpl(c, 0x1u, \"x\"); }"])},
                 "F_00401000")
    assert "c->eax=eax;c->esp=esp; F_00402000(c); eax=c->eax;esp=c->esp;" in out


def test_unknown_or_forced_callee_touches_everything():
    full = "c->eax=eax;c->ebx=ebx;c->esp=esp;c->esi=esi; F_00402000(c); eax=c->eax;ebx=c->ebx;esp=c->esp;esi=c->esi;"
    assert full in _final({"F_00401000": _caller("F_00402000")}, "F_00401000")
    assert full in _final({"F_00401000": _caller("F_00402000"), "F_00402000": _leaf(["  eax=1;"])}, "F_00401000",
                          forced_all=("F_00402000",))


def test_abi_callee_touches_only_the_scratch_registers():
    # A native (or a D3DX thunk) follows the Windows convention: it may change eax, ecx, edx and esp, never ebx, esi,
    # edi or ebp. Around its call the caller stores and reloads only the first group.
    body = _func(["  ebx=5; esi=ebx; eax=esi; ecx=eax;", _call("F_00402000"), f"  esp+=4u; {x86rec.SPILL} return;"])
    infos = {"F_00401000": x86rec.analyze_regs(body)}
    touched = x86rec.touched_sets(infos, forced_all=(), abi=("F_00402000",))
    out = x86rec.finalize_regs(body, infos["F_00401000"], touched)
    assert "c->eax=eax;c->ecx=ecx;c->esp=esp; F_00402000(c); eax=c->eax;ecx=c->ecx;esp=c->esp;" in out


def test_abi_wins_over_forced_all():
    body = _func(["  ebx=5; eax=ebx;", _call("F_00402000"), f"  esp+=4u; {x86rec.SPILL} return;"])
    infos = {"F_00401000": x86rec.analyze_regs(body)}
    touched = x86rec.touched_sets(infos, forced_all=("F_00402000",), abi=("F_00402000",))
    assert touched["F_00402000"] == frozenset({"eax", "ecx", "edx", "esp"})


def test_recursion_converges():
    out = _final({"F_00401000": _caller("F_00402000"),
                  "F_00402000": _leaf(["  eax=1;", _call("F_00402000")])}, "F_00401000")
    assert "c->eax=eax;c->esp=esp; F_00402000(c); eax=c->eax;esp=c->esp;" in out


def test_x87_flush_between_spill_and_call_is_kept():
    site = (f"  esp-=4; W32(esp,0x401005u); {x86rec.SPILL}c->ftop=(ft0-(1))&7u;c->st[(ft0+(-1))&7u]=x0; F_00402000(c); "
            f"{x86rec.RELOAD}ft0=(c->ftop+(1))&7u;")
    out = _final({"F_00401000": _func(["  ebx=5; eax=ebx;", site, f"  esp+=4u; {x86rec.SPILL} return;"]),
                  "F_00402000": _leaf(["  eax=1;"])}, "F_00401000")
    assert ("c->eax=eax;c->esp=esp;c->ftop=(ft0-(1))&7u;c->st[(ft0+(-1))&7u]=x0; F_00402000(c); "
            "eax=c->eax;esp=c->esp;ft0=(c->ftop+(1))&7u;") in out


# ---------- D3DX dispatch thunks: push 1; call <cpu check>; jmp [table slot] ----------
THUNK = """void F_0062af8f(CPU *c) {
  uint8_t *const mb = g_mem;
  uint32_t esp=c->esp;
  // 0x62af8f: push 1
  { uint32_t v=0x1u; esp-=4; W32(esp,v); }
  // 0x62af91: call 0x63b6aa
  esp-=4; W32(esp,0x62af96u); c->esp=esp; F_0063b6aa(c); esp=c->esp;
  // 0x62af96: jmp dword ptr [0x7257e4]
  { uint32_t t=R32((uint32_t)(0x7257e4u)); c->esp=esp; rt_call(c,t); return; }
}"""


def test_d3dx_thunk_gets_a_fast_path_and_keeps_the_original():
    out = x86rec.d3dx_thunk_fast(THUNK, {0x7257E4: 0x6483A7}, {0x6483A7})
    assert "void F_0062af8f_slow(CPU *c) {" in out          # the original body, for the first call (table not built)
    assert "void F_0062af8f(CPU *c) {" in out
    fast = out.split("void F_0062af8f(CPU *c) {")[1]
    assert "!= 0xffffu" in fast and "c->eax = st;" in fast   # table built: eax as the check leaves it, no call to it
    assert "if (t == 0x6483a7u) { F_006483a7(c); return; }" in fast
    assert "rt_call(c, t); return;" in fast
    assert "F_0062af8f_slow(c);" in fast
    assert "F_0063b6aa" not in fast


def test_d3dx_thunk_without_a_known_default_only_dispatches():
    out = x86rec.d3dx_thunk_fast(THUNK, {}, set())
    fast = out.split("void F_0062af8f(CPU *c) {")[1]
    assert "rt_call(c, t); return;" in fast and "if (t ==" not in fast


def test_other_functions_are_not_thunks():
    body = THUNK.replace("  // 0x62af8f: push 1\n", "  // 0x62af8d: nop\n  // 0x62af8f: push 1\n")
    assert x86rec.d3dx_thunk_fast(body, {}, set()) is None
    assert x86rec.d3dx_thunk_fast(_leaf(["  eax=1;"]), {}, set()) is None
