// BC7 (mode 6) blocks from RGBA8 texels on the GPU, one invocation per 4x4 block: the second pass of the GPU ASTC
// decode (rx_texture_gpu.cpp astc_gpu_decode_bc7), so a decoded ASTC texture takes 8 bits per texel in video memory
// instead of RGBA8's 32. Mode 6 is one subset, RGBA endpoints of 7 bits plus a shared p-bit each (8-bit precision)
// and 4-bit indices (16 steps): finer than BC1 / BC3's 5:6:5 colours and 4 steps on gradients (the sky, fog), and
// alpha in the same line as the colour. A block whose alpha is 255 throughout keeps it exactly 255.
// The endpoints: the line through the block's mean along its principal axis (covariance of RGBA, power iteration
// from the row of the channel varying most),
// clipped to the texels' extent; each endpoint's p-bit the better of the two; every texel the nearest of the 16
// steps; then the endpoints fitted again by least squares on those steps, kept when the block's error goes down.
// Texels past the right or bottom edge repeat the last column or row. The bytes are what the texels hold: an sRGB
// texture keeps its sRGB-encoded values, as BC7's sRGB format expects.
#pragma once
namespace rx {
inline const char* const kBc7Wgsl = R"WGSL(
struct Params {
    width: u32,
    height: u32,
    bx: u32,           // blocks per row
    by: u32,           // block rows
    row_blocks: u32,   // blocks per row in the output (padded: rows 256 bytes apart)
    layer_blocks: u32, // blocks per layer in the output
    pad0: u32,
    pad1: u32,
};
@group(0) @binding(0) var src: texture_2d_array<f32>;
@group(0) @binding(1) var<storage, read_write> dst: array<vec4<u32>>;
@group(0) @binding(2) var<uniform> params: Params;

var<private> px: array<vec4<f32>, 16>;
var<private> idx: array<u32, 16>;

const WEIGHTS = array<u32, 16>(0u, 4u, 9u, 13u, 17u, 21u, 26u, 30u, 34u, 38u, 43u, 47u, 51u, 55u, 60u, 64u);

// an endpoint (0..255 per channel) as 7 bits a channel and a p-bit: the better of p = 0 and p = 1
struct Quant { q: vec4<u32>, p: u32, v: vec4<f32> };
var<private> opaque: bool;   // (every texel's alpha 255: the p-bit stays 1, so alpha stays exactly 255)
fn quantize(e: vec4<f32>) -> Quant {
    var best: Quant;
    var best_err = 1e30;
    for (var p = select(0u, 1u, opaque); p < 2u; p = p + 1u) {
        let q = vec4<u32>(clamp(round((e - f32(p)) * 0.5), vec4<f32>(0.0), vec4<f32>(127.0)));
        let v = vec4<f32>(q * 2u + p);
        let d = v - e;
        let err = dot(d, d);
        if (err < best_err) {
            best_err = err;
            best = Quant(q, p, v);
        }
    }
    return best;
}

// each texel's nearest step between two (quantized) endpoints, and the block's squared error
fn assign(a: vec4<f32>, b: vec4<f32>) -> f32 {
    var pal: array<vec4<f32>, 16>;
    for (var k = 0u; k < 16u; k = k + 1u) {
        let w = f32(WEIGHTS[k]);
        pal[k] = floor(((64.0 - w) * a + w * b + 32.0) / 64.0);
    }
    var total = 0.0;
    for (var i = 0u; i < 16u; i = i + 1u) {
        var bi = 0u;
        var be = 1e30;
        for (var k = 0u; k < 16u; k = k + 1u) {
            let d = pal[k] - px[i];
            let e = dot(d, d);
            if (e < be) {
                be = e;
                bi = k;
            }
        }
        idx[i] = bi;
        total = total + be;
    }
    return total;
}

fn put(bits: ptr<function, vec4<u32>>, pos: u32, n: u32, value: u32) {
    let w = pos >> 5u;
    let s = pos & 31u;
    (*bits)[w] = (*bits)[w] | (value << s);
    if (s + n > 32u) {
        (*bits)[w + 1u] = (*bits)[w + 1u] | (value >> (32u - s));
    }
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
    if (gid.x >= params.bx || gid.y >= params.by) {
        return;
    }
    var mean = vec4<f32>(0.0);
    var lo = vec4<f32>(255.0);
    var hi = vec4<f32>(0.0);
    for (var i = 0u; i < 16u; i = i + 1u) {
        let x = min(gid.x * 4u + (i & 3u), params.width - 1u);
        let y = min(gid.y * 4u + (i >> 2u), params.height - 1u);
        let t = round(textureLoad(src, vec2<i32>(i32(x), i32(y)), i32(gid.z), 0) * 255.0);
        px[i] = t;
        mean = mean + t;
        lo = min(lo, t);
        hi = max(hi, t);
    }
    mean = mean / 16.0;
    opaque = lo.w >= 255.0;

    // the principal axis: power iteration on the covariance
    var cov: array<vec4<f32>, 4>;
    for (var i = 0u; i < 16u; i = i + 1u) {
        let d = px[i] - mean;
        cov[0] = cov[0] + d * d.x;
        cov[1] = cov[1] + d * d.y;
        cov[2] = cov[2] + d * d.z;
        cov[3] = cov[3] + d * d.w;
    }
    // (starting from the covariance row of the channel that varies most: the extent's diagonal hi - lo would be at
    // right angles to the axis when channels run opposite ways, red rising as green falls, and the block come out flat)
    var axis = cov[0];
    var most = cov[0].x;
    if (cov[1].y > most) {
        axis = cov[1];
        most = cov[1].y;
    }
    if (cov[2].z > most) {
        axis = cov[2];
        most = cov[2].z;
    }
    if (cov[3].w > most) {
        axis = cov[3];
    }
    for (var it = 0u; it < 6u; it = it + 1u) {
        let v = cov[0] * axis.x + cov[1] * axis.y + cov[2] * axis.z + cov[3] * axis.w;
        let len = length(v);
        if (len < 1e-4) {
            break;
        }
        axis = v / len;
    }
    let alen = length(axis);
    var e0 = mean;
    var e1 = mean;
    if (alen > 1e-4) {
        axis = axis / alen;
        var tmin = 1e30;
        var tmax = -1e30;
        for (var i = 0u; i < 16u; i = i + 1u) {
            let t = dot(px[i] - mean, axis);
            tmin = min(tmin, t);
            tmax = max(tmax, t);
        }
        e0 = clamp(mean + axis * tmin, vec4<f32>(0.0), vec4<f32>(255.0));
        e1 = clamp(mean + axis * tmax, vec4<f32>(0.0), vec4<f32>(255.0));
    }
    var qa = quantize(e0);
    var qb = quantize(e1);
    var err = assign(qa.v, qb.v);

    // refit: the endpoints that best give each texel its step (least squares), kept if better
    if (err > 0.0) {
        var aa = 0.0;
        var ab = 0.0;
        var bb = 0.0;
        var xa = vec4<f32>(0.0);
        var xb = vec4<f32>(0.0);
        for (var i = 0u; i < 16u; i = i + 1u) {
            let w = f32(WEIGHTS[idx[i]]) / 64.0;
            let u = 1.0 - w;
            aa = aa + u * u;
            ab = ab + u * w;
            bb = bb + w * w;
            xa = xa + px[i] * u;
            xb = xb + px[i] * w;
        }
        let det = aa * bb - ab * ab;
        if (abs(det) > 1e-6) {
            let f0 = clamp((xa * bb - xb * ab) / det, vec4<f32>(0.0), vec4<f32>(255.0));
            let f1 = clamp((xb * aa - xa * ab) / det, vec4<f32>(0.0), vec4<f32>(255.0));
            let ra = quantize(f0);
            let rb = quantize(f1);
            var keep: array<u32, 16> = idx;
            let err2 = assign(ra.v, rb.v);
            if (err2 < err) {
                qa = ra;
                qb = rb;
                err = err2;
            } else {
                idx = keep;
            }
        }
    }

    // the first texel's index has an implied 0 top bit: if it is 8 or more, the endpoints swap and the indices turn
    if (idx[0] >= 8u) {
        let t = qa;
        qa = qb;
        qb = t;
        for (var i = 0u; i < 16u; i = i + 1u) {
            idx[i] = 15u - idx[i];
        }
    }
    var bits = vec4<u32>(0u);
    put(&bits, 0u, 7u, 64u);   // mode 6
    for (var c = 0u; c < 4u; c = c + 1u) {
        put(&bits, 7u + c * 14u, 7u, qa.q[c]);
        put(&bits, 14u + c * 14u, 7u, qb.q[c]);
    }
    put(&bits, 63u, 1u, qa.p);
    put(&bits, 64u, 1u, qb.p);
    put(&bits, 65u, 3u, idx[0]);
    for (var i = 1u; i < 16u; i = i + 1u) {
        put(&bits, 68u + (i - 1u) * 4u, 4u, idx[i]);
    }
    dst[gid.z * params.layer_blocks + gid.y * params.row_blocks + gid.x] = bits;
}
)WGSL";
}  // namespace rx
