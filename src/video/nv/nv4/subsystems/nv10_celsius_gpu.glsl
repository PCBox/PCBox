/* NV10/NV15 Celsius fragment operations. Inputs are the same screen-space
   planes and register bundles consumed by nv10_shade in nv4_pgraph_3d.c. */

uint
celsius_dxt(uint addr, ivec2 xy, int width, int fmt)
{
    uint i = uint((xy.y & 3) * 4 + (xy.x & 3));
    addr += uint((xy.y / 4) * ((width + 3) / 4) + xy.x / 4) * (fmt == 1 ? 8u : 16u);
    uint colors = addr + (fmt == 1 ? 0u : 8u);
    uint ends = tread(colors, 4);
    uint index = (tread(colors + 4u, 4) >> (2u * i)) & 3u;
    uint c0 = ends & 0xffffu, c1 = ends >> 16;
    ivec4 a = rgba(texel_argb(c0, 5)), b = rgba(texel_argb(c1, 5));
    ivec4 col;
    if (index == 0u)
        col = a;
    else if (index == 1u)
        col = b;
    else if (fmt == 1 && c0 <= c1)
        col = index == 2u ? (a + b) / 2 : ivec4(0);
    else
        col = index == 2u ? (2 * a + b) / 3 : (a + 2 * b) / 3;

    if (fmt == 3)
        col.a = int((tread(addr + i / 2u, 1) >> (4u * (i & 1u))) & 15u) * 17;
    else if (fmt == 5) {
        int a0 = int(tread(addr, 1)), a1 = int(tread(addr + 1u, 1));
        uint bit = i * 3u;
        int k = int((tread(addr + 2u + bit / 8u, 2) >> (bit & 7u)) & 7u);
        if (k < 2)
            col.a = k == 0 ? a0 : a1;
        else if (a0 > a1)
            col.a = (a0 * (8 - k) + a1 * (k - 1)) / 7;
        else if (k < 6)
            col.a = (a0 * (6 - k) + a1 * (k - 1)) / 5;
        else
            col.a = k == 6 ? 0 : 255;
    }
    return uint(col.a << 24 | col.r << 16 | col.g << 8 | col.b);
}

uint
celsius_texel(int n, uint base, ivec2 xy, ivec2 logs, int width, bool check_key, inout bool keyed)
{
    int fmt = S.tex[n].cfmt, bpp = S.tex[n].bpp;
    uint raw, color;
    if (S.tex[n].dxt != 0) {
        color = celsius_dxt(base, xy, width, S.tex[n].dxt);
        raw = color;
    } else {
        uint offset = S.tex[n].linear != 0 ? uint(xy.y) * S.tex[n].pitch + uint(xy.x * bpp)
                      : swizzle(uint(xy.x), uint(xy.y), logs.x, logs.y) * uint(bpp);
        raw = tread(base + offset, bpp);
        switch (fmt) {
            case 0:    color = 0xff000000u | (raw * 0x010101u); break;
            case 0x14: color = 0xff000000u | ((raw ^ 0x80u) * 0x010101u); break;
            case 1:
            case 0x13: color = raw * 0x01010101u; break;
            case 0x12: color = raw; break;
            case 0x10: color = texel_argb(raw, 2); break;
            case 0x11: color = texel_argb(raw, 5); break;
            case 0x15: color = 0xff000000u | ((((raw & 511u) >> 1) ^ 128u) * 0x010101u); break;
            case 0x16: color = 0xff000000u | ((raw & 0xff00u) << 8) | (raw & 255u); break;
            case 0x17:
            case 0x18: color = 0xff000000u | raw; break;
            case 8:
            case 9:
            case 10:
            case 11: {
                int bytes = fmt == 11 ? 4 : 2;
                uint entry = tread(S.tex[n].palette + (raw & 255u) * uint(bytes), bytes);
                color = fmt == 11 ? entry : texel_argb(entry, fmt == 8 ? 2 : (fmt == 10 ? 4 : 5));
                break;
            }
            default: color = texel_argb(raw, fmt); break;
        }
    }
    if (check_key && S.tex[n].ckey_op != 0) {
        uint key = S.ckey[n];
        keyed = ((color ^ key) & 0xffffffu) == 0u ||
                (S.tex[n].dxt == 0 && bpp < 4 && raw == (key & (bpp == 2 ? 0xffffu : 255u)));
    }
    return color;
}

int
celsius_wrap(int i, int size, int mode, inout bool border)
{
    if (mode == 3 || mode == 4 || mode == 5) {
        if (mode == 4 && (i < 0 || i >= size))
            border = true;
        return clamp(i, 0, size - 1);
    }
    int period = mode == 2 ? 2 * size : size;
    if ((period & (period - 1)) == 0)
        i &= period - 1;
    else
        i = i < 0 ? period - 1 - ((-1 - i) % period) : i % period;
    return i < size ? i : period - 1 - i;
}

uint
celsius_sample(int n, vec2 uv, float r, float lod, inout bool keyed)
{
    uint base = S.tex[n].base;
    if (S.tex[n].cube != 0) {
        vec3 dir = vec3(uv, r), mag = abs(dir);
        int face;
        float major;
        if (mag.x >= mag.y && mag.x >= mag.z) {
            face = dir.x >= 0.0 ? 0 : 1;
            uv = vec2(face == 0 ? -dir.z : dir.z, -dir.y);
            major = mag.x;
        } else if (mag.y >= mag.z) {
            face = dir.y >= 0.0 ? 2 : 3;
            uv = vec2(dir.x, face == 2 ? dir.z : -dir.z);
            major = mag.y;
        } else {
            face = dir.z >= 0.0 ? 4 : 5;
            uv = vec2(face == 4 ? dir.x : -dir.x, -dir.y);
            major = mag.z;
        }
        major = major > 0.0 ? major : 1.0;
        uv = 0.5 * (vec2(fdiv_cr(uv.x, major), fdiv_cr(uv.y, major)) + 1.0);
        base += uint(face) * S.tex[n].face;
    }
    lod = min(max(lod + S.tex[n].lod_bias, S.tex[n].lod_min), S.tex[n].lod_max);
    int filter_mode = lod <= 0.0 ? S.tex[n].mag_f : S.tex[n].min_f;
    bool bilinear = filter_mode == 2 || (lod > 0.0 && (filter_mode == 5 || filter_mode == 6));
    int first = 0, levels = 1, fraction = 0;
    if (lod > 0.0 && S.tex[n].levels > 1 && filter_mode >= 3 && filter_mode <= 6) {
        bool trilinear = filter_mode == 4 || filter_mode == 6;
        first = min(int(trilinear ? lod : lod + 0.5), S.tex[n].levels - 1);
        if (trilinear && first + 1 < S.tex[n].levels) {
            levels = 2;
            fraction = int((lod - float(first)) * 256.0);
        }
    }
    uint result = 0u;
    for (int mip = 0; mip < levels; mip++) {
        int level = first + mip;
        ivec2 logs = max(ivec2(S.tex[n].wlog, S.tex[n].hlog) - level, ivec2(0));
        ivec2 size = S.tex[n].linear != 0 ? ivec2(S.tex[n].w, S.tex[n].h) : (ivec2(1) << logs);
        precise vec2 pos = S.tex[n].linear != 0 ? uv : uv * vec2(size);
        if (bilinear && S.tex[n].center != 0)
            pos -= 0.5;
        ivec2 xy = ivec2(f2i(floor(pos.x)), f2i(floor(pos.y)));
        ivec2 weight = ivec2((pos - vec2(xy)) * 256.0);
        uint taps[4];
        for (int i = 0; i < (bilinear ? 4 : 1); i++) {
            bool border = false;
            int x = celsius_wrap(xy.x + (i & 1), size.x, S.tex[n].wrap_u, border);
            int y = celsius_wrap(xy.y + (i >> 1), size.y, S.tex[n].wrap_v, border);
            taps[i] = border ? 0u : celsius_texel(n, base + S.tex[n].level_off[level], ivec2(x, y),
                                                   logs, size.x, mip == 0 && i == 0, keyed);
        }
        uint color = bilinear ? lerp4(lerp4(taps[0], taps[1], weight.x),
                                      lerp4(taps[2], taps[3], weight.x), weight.y) : taps[0];
        result = mip == 0 ? color : lerp4(result, color, fraction);
    }
    return result;
}

/* The alpha and RGB portions see the same register file at stage entry.
   Each produces AB, CD and SUM outputs, written back in that order. */
ivec3
rc_input(ivec4 reg, uint selector, bool alpha)
{
    bool use_alpha = (selector & 16u) != 0u;
    ivec3 v = use_alpha ? reg.aaa : (alpha ? reg.bbb : reg.rgb);
    ivec3 p = max(v, ivec3(0));
    switch ((selector >> 5) & 7u) {
        case 1: return 255 - min(p, ivec3(255));
        case 2: return 2 * p - 255;
        case 3: return 255 - 2 * p;
        case 4: return p - 128;
        case 5: return 128 - p;
        case 6: return v;
        case 7: return -v;
        default: return p;
    }
}

void
rc_products(ivec4 regs[16], uint inputs, uint outputs, bool alpha, bool mux, out ivec3 products[3])
{
    ivec3 v[4];
    for (int k = 0; k < 4; k++) {
        uint sel = inputs >> (24 - 8 * k);
        v[k] = rc_input(regs[sel & 15u], sel, alpha);
    }
    ivec3 ab = v[0] * v[1] / 255, cd = v[2] * v[3] / 255;
    if (!alpha && (outputs & 0x2000u) != 0u)
        ab = ivec3(ab.x + ab.y + ab.z);
    if (!alpha && (outputs & 0x1000u) != 0u)
        cd = ivec3(cd.x + cd.y + cd.z);
    products[0] = ab;
    products[1] = cd;
    products[2] = (outputs & 0x4000u) != 0u ? (mux ? cd : ab) : ab + cd;
    uint scale = (outputs >> 15) & 7u;
    for (int k = 0; k < 3; k++) {
        ivec3 p = products[k];
        if (scale == 1u || scale == 3u)
            p -= 128;
        if (scale == 2u || scale == 3u)
            p *= 2;
        else if (scale == 4u)
            p *= 4;
        else if (scale == 6u)
            p /= 2;
        products[k] = clamp(p, ivec3(-255), ivec3(255));
    }
}

ivec4
rc_final(ivec4 regs[16], uint sel)
{
    ivec4 v = regs[sel & 15u];
    v = clamp((sel & 16u) != 0u ? v.aaaa : v, ivec4(0), ivec4(255));
    return (sel & 0xe0u) != 0u ? 255 - v : v;
}

ivec4
celsius_shade(float at[14], float px, float py, ivec4 diffuse, ivec4 spec, out bool killed)
{
    ivec4 regs[16];
    for (int i = 0; i < 16; i++)
        regs[i] = ivec4(0);
    killed = false;
    precise float ax[5];
    for (int i = 0; i < 5; i++)
        ax[i] = E.c[i] + E.dx[i] * px + E.dy[i] * py;
    precise float fog = fdiv_cr(at[AT_FOG], 255.0);
    if ((S.fogmode & 1) != 0) {
        precise float d = max(1.5 - fog, 0.0);
        fog = exp2((S.fogmode & 2) != 0 ? -32.0 * d * d : -16.0 * d);
    }
    regs[1] = rgba(S.rc_k[0]);
    regs[2] = rgba(S.rc_k[1]);
    regs[3] = ivec4(rgba(S.fog_color).rgb, int(clamp(fog, 0.0, 1.0) * 255.0 + 0.5));
    regs[4] = diffuse;
    regs[5] = ivec4(spec.rgb, clamp255(f2i(ax[4])));
    for (int n = 0; n < 2; n++) {
        if (S.tex[n].on == 0)
            continue;
        int au = AT_U0 + 2 * n, aq = 2 * n + 1;
        float q = abs(ax[aq]) < 1e-12 ? 1e-12 : ax[aq];
        precise vec2 uv = vec2(fdiv_cr(at[au], q), fdiv_cr(at[au + 1], q));
        float lod = lod_of(au, au + 1, q, fdiv_cr(1.0, q), at[au], at[au + 1],
                           S.tex[n].linear != 0 ? 0 : S.tex[n].wlog, S.tex[n].linear != 0 ? 0 : S.tex[n].hlog,
                           E.dx[aq], E.dy[aq]);
        bool keyed = false;
        uint color = celsius_sample(n, uv, fdiv_cr(ax[aq - 1], q), lod, keyed);
        if (keyed) {
            if (S.tex[n].ckey_op == 3)
                killed = true;
            color = S.tex[n].ckey_op == 1 ? color & 0xffffffu : 0u;
        }
        if (S.tex[n].akill != 0 && (color >> 24) == 0u)
            killed = true;
        regs[8 + n] = rgba(color);
    }
    regs[12].a = regs[8].a;
    for (int i = 0; i < S.rc_n; i++) {
        bool mux = (S.rc_oc[1] & (1u << 27)) != 0u ? regs[12].a >= 128 : (regs[12].a & 1) != 0;
        ivec3 rgb[3], alpha[3];
        rc_products(regs, S.rc_ic[i], S.rc_oc[i], false, mux, rgb);
        rc_products(regs, S.rc_ia[i], S.rc_oa[i], true, mux, alpha);
        for (int k = 0; k < 3; k++) {
            int shift = k == 0 ? 4 : (k == 1 ? 0 : 8);
            uint rd = (S.rc_oc[i] >> shift) & 15u, ad = (S.rc_oa[i] >> shift) & 15u;
            if (rd != 0u)
                regs[rd].rgb = rgb[k];
            if (ad != 0u)
                regs[ad].a = alpha[k].x;
        }
    }
    regs[14] = ivec4(regs[12].rgb + max(regs[5].rgb, ivec3(0)), regs[12].a);
    if ((S.rc_f1 & 0x80u) != 0u)
        regs[14].rgb = min(regs[14].rgb, ivec3(255));
    regs[15] = rc_final(regs, S.rc_f1 >> 24) * rc_final(regs, S.rc_f1 >> 16) / 255;
    ivec3 a = rc_final(regs, S.rc_f0 >> 24).rgb;
    ivec3 b = rc_final(regs, S.rc_f0 >> 16).rgb;
    ivec3 c = rc_final(regs, S.rc_f0 >> 8).rgb;
    ivec3 d = rc_final(regs, S.rc_f0).rgb;
    return clamp(ivec4(a * b / 255 + (255 - a) * c / 255 + d, rc_final(regs, S.rc_f1 >> 8).a),
                 ivec4(0), ivec4(255));
}

ivec4
celsius_blend_factor(int f, ivec4 src, ivec4 dst, ivec4 k)
{
    switch (f) {
        case 0: return ivec4(0);
        case 2: return src;
        case 3: return 255 - src;
        case 4: return src.aaaa;
        case 5: return 255 - src.aaaa;
        case 6: return dst.aaaa;
        case 7: return 255 - dst.aaaa;
        case 8: return dst;
        case 9: return 255 - dst;
        case 10: return ivec4(ivec3(min(src.a, 255 - dst.a)), 255);
        case 12: return k;
        case 13: return 255 - k;
        case 14: return k.aaaa;
        case 15: return 255 - k.aaaa;
        default: return ivec4(255);
    }
}

ivec4
celsius_blend(ivec4 src, ivec4 dst)
{
    if (S.beq == 3) return min(src, dst);
    if (S.beq == 4) return max(src, dst);
    ivec4 k = rgba(S.bcolor);
    ivec4 s = src * celsius_blend_factor(S.sblend, src, dst, k) + 128;
    ivec4 d = dst * celsius_blend_factor(S.dblend, src, dst, k) + 128;
    s = (s + (s >> 8)) >> 8;
    d = (d + (d >> 8)) >> 8;
    return clamp(S.beq == 0 ? s - d : (S.beq == 1 ? d - s : s + d), ivec4(0), ivec4(255));
}
