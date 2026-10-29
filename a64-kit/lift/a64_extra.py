"""Instructions suyu's translator (suyu/arm64_to_c.h, vendored unmodified) leaves unhandled, written here instead.

translate(word) -> C text for the instruction, or None when it is not one of ours. The text is a block against the
same guest model as suyu's (c->vreg[32][2] as Q0-Q31, c->fpsr), so a64_lift.py drops it in where suyu gave up.

Covered:
  - Saturating shift left by immediate (Advanced SIMD "shift by immediate", opcode 01100 / 01110):
    SQSHL, UQSHL, SQSHLU; vector (8B 16B 4H 8H 2S 4S 2D) and scalar (B H S D). TOTK 1.4.3's SDK uses
    SQSHL Vd.4S, Vn.4S, #8 (13 sites); TOTK 1.0.0 used none. Saturation sets FPSR.QC (bit 27).
Semantics: Arm ARM, SQSHL/UQSHL/SQSHLU (immediate). Checked against Unicorn by tests/extra_insn_test.py.
"""

QC = 1 << 27


def _shift_imm_fields(w):
    """(esize, shift) from immh:immb for a left shift, or None for a reserved immh."""
    immh, immb = (w >> 19) & 0xF, (w >> 16) & 7
    if immh == 0:
        return None
    esize = 8 << (immh.bit_length() - 1)   # immh 0001 -> 8, 001x -> 16, 01xx -> 32, 1xxx -> 64
    return esize, ((immh << 3) | immb) - esize


def _sat_shl(w):
    vector = (w & 0x9F800400) == 0x0F000400   # 0 Q U 011110 immh immb opcode 1 Rn Rd
    scalar = (w & 0xDF800400) == 0x5F000400   # 01 U 111110 immh immb opcode 1 Rn Rd
    if not (vector or scalar):
        return None
    u, opcode = (w >> 29) & 1, (w >> 11) & 0x1F
    if opcode == 0b01110:
        kind = "uqshl" if u else "sqshl"
    elif opcode == 0b01100 and u:
        kind = "sqshlu"
    else:
        return None
    f = _shift_imm_fields(w)
    if not f:
        return None
    esize, shift = f
    q = (w >> 30) & 1
    if vector and esize == 64 and not q:
        return None   # (reserved)
    lanes = (128 if q else 64) // esize if vector else 1
    rn, rd = (w >> 5) & 31, w & 31
    bits = esize
    smax, smin, umax = (1 << (bits - 1)) - 1, -(1 << (bits - 1)), (1 << bits) - 1
    st, ut = {8: ("int8_t", "uint8_t"), 16: ("int16_t", "uint16_t"), 32: ("int32_t", "uint32_t"),
              64: ("int64_t", "uint64_t")}[bits]
    lit = lambda v: ("%dLL" % v) if v >= -(1 << 62) else "(-%dLL-1)" % (-v - 1)   # (INT64_MIN has no literal)
    ulit = lambda v: "%dULL" % v
    if kind == "sqshl":
        body = ("%s x; memcpy(&x,_s+%d*_i,%d); %s r;"
                " if (x > (%s >> %d)) { r = %s; _q = 1; }"
                " else if (x < (%s >> %d)) { r = %s; _q = 1; }"
                " else r = (%s)((%s)x << %d);"
                " memcpy(_d+%d*_i,&r,%d);") % (
            st, bits // 8, bits // 8, st, lit(smax), shift, lit(smax), lit(smin), shift, lit(smin), st, ut, shift,
            bits // 8, bits // 8)
    elif kind == "uqshl":
        body = ("%s x; memcpy(&x,_s+%d*_i,%d); %s r;"
                " if (x > (%s >> %d)) { r = %s; _q = 1; } else r = (%s)(x << %d);"
                " memcpy(_d+%d*_i,&r,%d);") % (
            ut, bits // 8, bits // 8, ut, ulit(umax), shift, ulit(umax), ut, shift, bits // 8, bits // 8)
    else:   # sqshlu: signed in, unsigned saturated out
        body = ("%s x; memcpy(&x,_s+%d*_i,%d); %s r;"
                " if (x < 0) { r = 0; _q = 1; }"
                " else if ((%s)x > (%s >> %d)) { r = %s; _q = 1; } else r = (%s)((%s)x << %d);"
                " memcpy(_d+%d*_i,&r,%d);") % (
            st, bits // 8, bits // 8, ut, ut, ulit(umax), shift, ulit(umax), ut, ut, shift, bits // 8, bits // 8)
    return ("{ uint8_t _s[16], _d[16] = {0}; int _q = 0; memcpy(_s,c->vreg[%d],16);"
            " for (int _i = 0; _i < %d; _i++) { %s }"
            " memcpy(c->vreg[%d],_d,16); if (_q) c->fpsr |= %#xULL; }") % (rn, lanes, body, rd, QC)


def translate(w):
    return _sat_shl(w)
