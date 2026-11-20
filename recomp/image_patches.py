"""The port's own changes to sh3.exe behaviour, applied at recompile time.

The PC Fix / WidescreenFix patch the game in memory at run time; the recompiled port cannot load them, so the
effects they produce are reimplemented here with our own code. Four mechanisms:
- IMAGE_PATCHES: bytes replaced in the image before translation (operands, constants, nops). The old bytes are checked:
  a different exe is refused instead of being silently mis-patched.
- ADDR_OVERRIDES: {va: native} the whole function is replaced by a native C function.
- ADDR_WRAPS: {va: native} the recompiled body becomes F_xxx_orig; F_xxx calls the native, which may call it back.
- HOOKS: {va: native} the native runs (registers synced through CPU) just before the instruction at va.
Native functions live in port/src/rt/widescreen.c (screen layout) and port/src/rt/game_fixes.c (other fixes).
"""
from dataclasses import dataclass

# Guest memory reserved for the port's own variables: between the end of the sh3.exe image (0x717d000) and the
# heap (G_HEAP_BASE 0x07200000). Mirrored by WS_VARS in port/src/rt/widescreen.h.
PORT_VARS = 0x071F0000

SPILL = "c->eax=eax;c->ecx=ecx;c->edx=edx;c->ebx=ebx;c->esp=esp;c->ebp=ebp;c->esi=esi;c->edi=edi;"
RELOAD = "eax=c->eax;ecx=c->ecx;edx=c->edx;ebx=c->ebx;esp=c->esp;ebp=c->ebp;esi=c->esi;edi=c->edi;"


@dataclass(frozen=True)
class Patch:
    va: int
    old: bytes
    new: bytes
    why: str


def apply_patches(pe, patches):
    """Replaces the bytes of each patch in pe.data (bytearray); all old bytes are checked before anything changes."""
    plan = []
    for p in patches:
        if len(p.old) != len(p.new):
            raise ValueError(f"patch {p.va:#x}: size change ({p.why})")
        o = pe.off(p.va)
        if o is None:
            raise ValueError(f"patch {p.va:#x}: address not in the image ({p.why})")
        if bytes(pe.data[o:o + len(p.old)]) != p.old:
            raise ValueError(f"patch {p.va:#x}: unexpected bytes {bytes(pe.data[o:o + len(p.old)]).hex()} "
                             f"(expected {p.old.hex()}): unsupported sh3.exe? ({p.why})")
        plan.append((o, p.new))
    for o, new in plan:
        pe.data[o:o + len(new)] = new


def hook_code(native):
    """C code that runs native(c) with the recompiled function's registers, then takes back what it changed."""
    return f"{{ void {native}(CPU *c); {SPILL} {native}(c); {RELOAD} }}"


FTOL2 = 0x662C40               # MSVC __ftol2: inlined by the recompiler in static x87 functions (x86rec.py FTOL)

# Wrapped natives that keep the Windows convention (x86rec.py touched_sets abi): they change only eax, ecx, edx, esp,
# and so do their originals (CRT, D3DX, and the game functions checked by hand: 0x48c290 never writes ebx/esi/edi/ebp,
# 0x48c4e0 and 0x4f3300 push and pop them). Callers then store and reload only those four around the call.
ABI_NATIVES = {0x662C40, 0x648639, 0x6483A7, 0x64856F, 0x6634A0, 0x667A7F, 0x48C290, 0x48C4E0, 0x4F3300,
               0x648432, 0x648491, 0x48E140, 0x66405E, 0x60BFA0, 0x663A30,
               0x436430, 0x478150, 0x475FB0, 0x48F040}

# D3DX maths dispatch (recomp/x86rec.py d3dx_thunk_fast): every entry point is `push 1; call D3DX_CPU_CHECK; jmp [slot]`.
D3DX_CPU_CHECK = 0x63B6AA      # builds the table of CPU-specific functions on its first call, returns the state word
D3DX_STATE = 0x72601C          # 0xffff until the table is built
D3DX_TABLE = 0x7257D8          # 0x39 slots, copied from D3DX_TABLE_DEFAULT then patched for the CPU
D3DX_TABLE_DEFAULT = 0x7258C0
D3DX_TABLE_SLOTS = 0x39
# Slots the CPU check (0x6487aa) overwrites with the x87 versions on our CPU (cpuid: FPU only): what the table holds at
# run time, so the thunk fast path calls these directly.
D3DX_X87 = {0x7257D8: 0x648309, 0x7257DC: 0x6483A7, 0x7257E0: 0x648491, 0x7257E4: 0x648639,
            0x7257E8: 0x64871C, 0x7257EC: 0x648372, 0x7257F0: 0x648432, 0x7257F4: 0x64856F}

WS_ZERO = PORT_VARS + 0   # float 0.0 (guest memory starts zeroed; nothing writes it)
# PORT_VARS + 0x100: 4 vertices of a side band (widescreen.c WS_BAND_VERTS)
# PORT_VARS + 0x200: 4 vertices of an aligned feedback quad (d3d8/device.c TEXEL_ALIGN_VERTS)


def _operand(op, addr):
    """x87 'op dword ptr [addr]' encoding: the 2 opcode bytes then the little-endian absolute address."""
    return bytes(op) + addr.to_bytes(4, "little")


IMAGE_PATCHES = [
    # Cinematic bars (top/bottom) of in-engine scenes: height factor [0x68a8f0] -> 0 (PC Fix RemoveCutsceneBorders,
    # does the same). 16:9 already frames the scene.
    Patch(0x41BB4B, _operand((0xD9, 0x05), 0x68A8F0), _operand((0xD9, 0x05), WS_ZERO), "no cutscene bars"),
    # 2-pixel side bars drawn with the same quads: width factor [0x68e6d4] -> 0.
    Patch(0x41BB2B, _operand((0xD8, 0x0D), 0x68E6D4), _operand((0xD8, 0x0D), WS_ZERO), "no side bars"),
    Patch(0x41BE64, _operand((0xD8, 0x0D), 0x68E6D4), _operand((0xD8, 0x0D), WS_ZERO), "no side bars"),
    # Pause menu: the PC version draws an opaque black box (colour 0xff000000, alpha blending off) behind the options
    # (0x5e82e0, and the same box in 0x5e84a0); the PS2 shows only the text over the paused scene. The box's
    # DrawPrimitiveUP (stdcall, 5 arguments) becomes `add esp, 0x14`: same stack, the text is still drawn.
    Patch(0x5E8401, bytes.fromhex("ff9120010000"), bytes.fromhex("83c414909090"), "no black box behind the pause menu"),
    Patch(0x5E85E4, bytes.fromhex("ff9120010000"), bytes.fromhex("83c414909090"), "no black box behind the pause menu"),
    # Puzzle cursor (moved by the left stick / d-pad, 0x4dce70): its init 0x4dd270 sets the speed of both axes to 210
    # units/s (0..512 across the picture). 150 is easier to aim on the Vita's small stick.
    Patch(0x4DD2B2, bytes.fromhex("00005243"), bytes.fromhex("00001643"), "slower puzzle cursor (x)"),
    Patch(0x4DD2BC, bytes.fromhex("00005243"), bytes.fromhex("00001643"), "slower puzzle cursor (y)"),
]
ADDR_OVERRIDES = {
    0x416AE0: "ws_set_display",    # display mode (w, h): always the Vita screen, 960x544
    0x416B20: "ws_set_display3",   # display mode (w, h, x): same
    0x403E70: "ws_set_movie_rect", # movie rectangle: 16:9 movies fill the screen, 4:3 ones are not stretched
}
ADDR_WRAPS = {
    0x662C40: "nat_ftol2",            # MSVC __ftol2, native
    0x648639: "nat_d3dx_matmul",      # D3DXMatrixMultiply (x87 version), native
    # 0x62A867 is no longer wrapped by nat_thunk_matmul: its slot 0x7257dc holds D3DXVec3Transform
    # (0x6483a7), not MatrixMultiply, so that wrap never took its fast path and every call redid the CPU check (6.9% of
    # the Vita profile in the worst park moments). The generic thunk fast path (x86rec.py d3dx_thunk_fast) covers it.
    0x6483A7: "nat_d3dx_vec3transform",  # D3DXVec3Transform (x87 version), native
    0x64856F: "nat_d3dx_vec3normalize",  # D3DXVec3Normalize (x87 version, table rsqrt), native
    0x6634A0: "nat_floor",            # CRT floor, native
    0x667A7F: "nat_getptd",           # CRT __getptd without its three host calls
    0x48C290: "nat_clip_near",        # segment clipped against the camera's near plane (game code), native
    0x48C4E0: "nat_push_quad",        # quad appended to the game's polygon buffer (game code), native
    0x4F3300: "nat_seg_quad",         # segment against quad (collision, game code), native
    0x48E140: "nat_shadow_volume",    # shadow volume of one strip (flashlight shadows, game code), native
    0x66405E: "nat_rand",             # CRT rand, native
    0x60BFA0: "nat_prand",            # random float of the particles (game code), native
    0x663A30: "nat_qsort",            # CRT qsort, native (known comparisons in C)
    0x436430: "nat_pal_texture",      # 8-bit paletted picture into a 32-bit texture (game code), native
    0x478150: "nat_fx_list_add",      # add/update an entry of the effect list (game code), native
    0x475FB0: "nat_particle_fade",    # per-particle fade (game code), native
    0x48F040: "nat_shadow_strip",     # strip shadow (flashlight shadows of ground strips, game code), native
    0x648432: "nat_d3dx_vec3transformnormal",  # D3DXVec3TransformNormal (x87 version picked by D3DX), native
    0x648491: "nat_d3dx_vec4transform",        # D3DXVec4Transform (x87 version picked by D3DX), native
    0x41B2C0: "fix_frame_pacing",     # frame pacing: no wait for the next vblank when the frame is already late
    0x402E00: "ws_set_render_size",   # 3D render-target size (w, h): always SH3_RES (790x448)
    0x43AB80: "ws_scale_x",           # 3D horizontal projection scale: Hor+ for 16:9
    0x43ABC0: "ws_scale_y",           # 3D vertical projection scale
    0x682BA0: "ws_gsx",              # 2D x: PS2 coordinates into a centred 4:3 box (menus, HUD, logos)
    # full-screen effects that go through GsX keep the whole 16:9 width
    0x468B40: "ws_fx_lensflare",
    0x468DF0: "ws_fx_lensflare_rot",
    0x479E30: "ws_fx_haze",
    0x4794B0: "ws_fx_fog",
    0x49FAC0: "ws_fx_occlusion",
    0x59EF90: "ws_world_proj",        # effect system world -> GS projection (blood, footprints): full width (widescreen.c)
    0x5F09F0: "fix_no_safe_mode",     # never "A crash occurred..." / 640x480 safe mode (game_fixes.c)
    0x4DD2F0: "fix_puzzle_cursor",    # puzzle cursor up this frame: the pad drives the mouse pointer (game_fixes.c)
    0x5E68F0: "fix_options_frame",    # options menu frame: L/R turn the page, Display Mode off (game_fixes.c)
    0x5E3560: "fix_options_next_page",  # options page turn: Configuration (rendering settings) skipped
    0x5E35E0: "fix_options_prev_page",
}
# 0x458010 "diag_model_walk" is no longer installed: the bug is fixed (guest memory) and
# its full walk of every loaded model plus the file re-read cost part of the 160-300 ms hitches at cutscene cuts
#. To use it again, add the wrap back.
HOOKS = {
    0x4DDB93: "ws_native_scale_next",   # 2D code that reads ScaleX/ScaleY: keeps the game's own scale
    0x599C24: "ws_native_scale_next",
    0x5F6D22: "ws_stretch_next",        # single GsX calls of full-screen elements
    0x5F80EF: "ws_stretch_next",
    0x420320: "ws_cine_bars",           # cinematic bars of this frame: in-engine cutscene zoom (widescreen.c)
    0x5D74FA: "ws_logo43",              # Konami / KCET logo picture: centred 4:3; its end starts the credits screen
    0x59BEF9: "ws_quad43",              # credits picture: centred 4:3
    0x405810: "movie_sh3_reader",       # movie file reader: the port plays the file itself
    0x5F30FA: "fix_pause_uv",           # pause background: u, v of the power-of-two texture (game_fixes.c)
    0x5EE080: "fix_btncfg_seen",        # Button Config page drawn: Controls picture instead, no remapping (boot_credits.c)
}
