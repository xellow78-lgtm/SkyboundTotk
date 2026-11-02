// Generated (tools: glslangValidator -> SPIR-V -> naga -> WGSL) from AMD FidelityFX Super Resolution 2.2's GLSL
// passes (github.com/GPUOpen-Effects/FidelityFX-FSR2), patched for WebGPU: image atomics on plain buffers, explicit
// storage formats, no wave operations, 32-bit math. Options: HDR input, render-size motion vectors (UV units, not
// jittered), standard depth, sharpening by RCAS.
//   Copyright (c) 2022-2023 Advanced Micro Devices, Inc. All rights reserved.
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
//   documentation files (the "Software"), to deal in the Software without restriction, including without limitation
//   the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
//   to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above
//   copyright notice and this permission notice shall be included in all copies or substantial portions of the
//   Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
//   LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
//   SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
//   OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//   DEALINGS IN THE SOFTWARE.
#pragma once
#include <cstdint>
namespace rx::fsr2gen {
enum Kind : uint8_t { kTexture, kStorageTexture, kUniform, kStorageRO, kStorageRW };
struct Binding { uint8_t binding; Kind kind; const char* name; const char* format; bool read_write; };
struct Pass { const char* name; const char* const* parts; int nparts; const Binding* bindings; int nbindings; uint32_t wg[3]; };

inline const char* const compute_luminance_pyramid_parts[] = {
R"FSR2(struct cbFSR2_t {
    iRenderSize: vec2<i32>,
    iMaxRenderSize: vec2<i32>,
    iDisplaySize: vec2<i32>,
    iInputColorResourceDimensions: vec2<i32>,
    iLumaMipDimensions: vec2<i32>,
    iLumaMipLevelToUse: i32,
    iFrameIndex: i32,
    fDeviceToViewDepth: vec4<f32>,
    fJitter: vec2<f32>,
    fMotionVectorScale: vec2<f32>,
    fDownscaleFactor: vec2<f32>,
    fMotionVectorJitterCancellation: vec2<f32>,
    fPreExposure: f32,
    fPreviousFramePreExposure: f32,
    fTanHalfFOV: f32,
    fJitterSequenceLength: f32,
    fDeltaTime: f32,
    fDynamicResChangeFactor: f32,
    fViewSpaceToMetersFactor: f32,
}
struct cbSPD_t {
    mips: u32,
    numWorkGroups: u32,
    workGroupOffset: vec2<u32>,
    renderSize: vec2<u32>,
}
struct SpdAtomic {
    rw_spd_global_atomic: array<u32>,
}
struct SpdAtomic_1 {
    rw_spd_global_atomic: array<atomic<u32>>,
}
@group(1) @binding(5)
var<uniform> cbFSR2_: cbFSR2_t;
@group(1) @binding(0)
var r_input_color_jittered: texture_2d<f32>;
@group(0) @binding(1)
var s_LinearClamp: sampler;
@group(1) @binding(6)
var<uniform> cbSPD: cbSPD_t;
@group(1) @binding(4)
var rw_auto_exposure: texture_storage_2d<rg32float,read_write>;
@group(1) @binding(3)
var rw_img_mip_5_: texture_storage_2d<r16float,read_write>;
@group(1) @binding(2)
var rw_img_mip_shading_change: texture_storage_2d<r16float,read_write>;
@group(1) @binding(1)
var<storage, read_write> unnamed: SpdAtomic_1;
var<workgroup> spdCounter_1: u32;
var<workgroup> spdIntermediateR: array<array<f32, 16>, 16>;
var<workgroup> spdIntermediateG: array<array<f32, 16>, 16>;
var<workgroup> spdIntermediateB: array<array<f32, 16>, 16>;
var<workgroup> spdIntermediateA: array<array<f32, 16>, 16>;
var<private> gl_WorkGroupID_1: vec3<u32>;
var<private> gl_LocalInvocationIndex_1: u32;
@group(0) @binding(0)
var s_PointClamp: sampler;
fn SpdStoreIntermediate_u0028_u1_u003b_u1_u003b_vf4_u003b(x: ptr<function, u32>, y: ptr<function, u32>, value: ptr<function, vec4<f32>>) {
    let _e74 = (*x);
    let _e75 = (*y);
    let _e77 = (*value)[0u];
    spdIntermediateR[_e74][_e75] = _e77;
    let _e80 = (*x);
    let _e81 = (*y);
    let _e83 = (*value)[1u];
    spdIntermediateG[_e80][_e81] = _e83;
    let _e86 = (*x);
    let _e87 = (*y);
    let _e89 = (*value)[2u];
    spdIntermediateB[_e86][_e87] = _e89;
    let _e92 = (*x);
    let _e93 = (*y);
    let _e95 = (*value)[3u];
    spdIntermediateA[_e92][_e93] = _e95;
    return;
}
fn SpdReduce4_u0028_vf4_u003b_vf4_u003b_vf4_u003b_vf4_u003b(v0_: ptr<function, vec4<f32>>, v1_: ptr<function, vec4<f32>>, v2_: ptr<function, vec4<f32>>, v3_: ptr<function, vec4<f32>>) -> vec4<f32> {
    let _e75 = (*v0_);
    let _e76 = (*v1_);
    let _e78 = (*v2_);
    let _e80 = (*v3_);
    return ((((_e75 + _e76) + _e78) + _e80) * 0.25f);
}
fn SPD_SetExposureBuffer_u0028_vf2_u003b(value_1: ptr<function, vec2<f32>>) {
    let _e72 = (*value_1);
    textureStore(rw_auto_exposure, vec2<i32>(0i, 0i), vec4<f32>(_e72.x, _e72.y, 0f, 0f));
    return;
}
fn ffxPow_u0028_f1_u003b_f1_u003b(x_1: ptr<function, f32>, y_1: ptr<function, f32>) -> f32 {
    let _e73 = (*x_1);
    let _e74 = (*y_1);
    return pow(_e73, _e74);
}
fn ComputeAutoExposureFromLavg_u0028_f1_u003b(Lavg: ptr<function, f32>) -> f32 {
    var ExposureISO100_: f32;
    var Lmax: f32;
    var param: f32;
    var param_1: f32;
    let _e76 = (*Lavg);
    (*Lavg) = exp(_e76);
    let _e78 = (*Lavg);
    ExposureISO100_ = log2(((_e78 * 100f) / 12.5f));
    param = 2f;
    let _e82 = ExposureISO100_;
    param_1 = _e82;
    let _e83 = ffxPow_u0028_f1_u003b_f1_u003b((&param), (&param_1));
    Lmax = (1.2f * _e83);
    let _e85 = Lmax;
    return (1f / _e85);
}
fn DeltaTime_u0028_() -> f32 {
    let _e72 = cbFSR2_.fDeltaTime;
    return _e72;
}
fn SPD_LoadExposureBuffer_u0028_() -> vec2<f32> {
    let _e71 = textureLoad(rw_auto_exposure, vec2<i32>(0i, 0i));
    return _e71.xy;
}
fn MipCount_u0028_() -> u32 {
    let _e72 = cbSPD.mips;
    return _e72;
}
fn SPD_SetMipmap_u0028_vi2_u003b_u1_u003b_f1_u003b(iPxPos: ptr<function, vec2<i32>>, slice: ptr<function, u32>, value_2: ptr<function, f32>) {
    let _e74 = (*slice);
    switch bitcast<i32>(_e74) {
        case 4: {
            let _e81 = (*iPxPos);
            let _e82 = (*value_2);
            textureStore(rw_img_mip_shading_change, _e81, vec4<f32>(_e82, 0f, 0f, 0f));
            break;
        }
        case 5: {
            let _e84 = (*iPxPos);
            let _e85 = (*value_2);
            textureStore(rw_img_mip_5_, _e84, vec4<f32>(_e85, 0f, 0f, 0f));
            break;
        }
        default: {
            let _e76 = (*iPxPos);
            let _e77 = (*iPxPos);
            let _e78 = textureLoad(rw_img_mip_shading_change, _e77);
            textureStore(rw_img_mip_shading_change, _e76, vec4<f32>(_e78.x, 0f, 0f, 0f));
            break;
        }
    }
    return;
}
fn LumaMipLevelToUse_u0028_() -> i32 {
    let _e72 = cbFSR2_.iLumaMipLevelToUse;
    return _e72;
}
fn SpdStore_u0028_vi2_u003b_vf4_u003b_u1_u003b_u1_u003b(pix: ptr<function, vec2<i32>>, outValue: ptr<function, vec4<f32>>, index: ptr<function, u32>, slice_1: ptr<function, u32>) {
    var param_2: vec2<i32>;
    var param_3: u32;
    var param_4: f32;
    var prev: f32;
    var result: f32;
    var rate: f32;
    var spdOutput: vec2<f32>;
    var param_5: f32;
    var param_6: vec2<f32>;
    let _e84 = (*index);
    let _e85 = LumaMipLevelToUse_u0028_();
    let _e88 = (*index);
    if ((_e84 == bitcast<u32>(_e85)) || (_e88 == 5u)) {
        let _e91 = (*pix);
        param_2 = _e91;
        let _e92 = (*index);
        param_3 = _e92;
        let _e94 = (*outValue)[0u];
        param_4 = _e94;
        SPD_SetMipmap_u0028_vi2_u003b_u1_u003b_f1_u003b((&param_2), (&param_3), (&param_4));
    }
    let _e95 = (*index);
    let _e96 = MipCount_u0028_();
    if (_e95 == (_e96 - 1u)) {
        let _e99 = (*pix);
        if all((_e99 == vec2<i32>(0i, 0i))) {
            let _e102 = SPD_LoadExposureBuffer_u0028_();
            prev = _e102.y;
            let _e105 = (*outValue)[0u];
            result = _e105;
            let _e106 = prev;
            if (_e106 < 100000000f) {
                rate = 1f;
                let _e108 = prev;
                let _e109 = result;
                let _e110 = prev;
                let _e112 = DeltaTime_u0028_();
                let _e114 = rate;
                result = (_e108 + ((_e109 - _e110) * (1f - exp((-(_e112) * _e114)))));
            }
            let _e120 = result;
            param_5 = _e120;
            let _e121 = ComputeAutoExposureFromLavg_u0028_f1_u003b((&param_5));
            let _e122 = result;
            spdOutput = vec2<f32>(_e121, _e122);
            let _e124 = spdOutput;
            param_6 = _e124;
            SPD_SetExposureBuffer_u0028_vf2_u003b((&param_6));
        }
    }
    return;
}
fn SPD_LoadMipmap5_u0028_vi2_u003b(iPxPos_1: ptr<function, vec2<i32>>) -> vec4<f32> {
    let _e72 = (*iPxPos_1);
    let _e73 = textureLoad(rw_img_mip_5_, _e72);
    return vec4<f32>(_e73.x, 0f, 0f, 0f);
}
fn SpdLoad_u0028_vi2_u003b_u1_u003b(tex: ptr<function, vec2<i32>>, slice_2: ptr<function, u32>) -> vec4<f32> {
    var param_7: vec2<i32>;
    let _e74 = (*tex);
    param_7 = _e74;
    let _e75 = SPD_LoadMipmap5_u0028_vi2_u003b((&param_7));
    return _e75;
}
fn SpdReduceLoad4_u0028_vu2_u003b_vu2_u003b_vu2_u003b_vu2_u003b_u1_u003b(i0_: ptr<function, vec2<u32>>, i1_: ptr<function, vec2<u32>>, i2_: ptr<function, vec2<u32>>, i3_: ptr<function, vec2<u32>>, slice_3: ptr<function, u32>) -> vec4<f32> {
    var v0_1: vec4<f32>;
    var param_8: vec2<i32>;
    var param_9: u32;
    var v1_1: vec4<f32>;
    var param_10: vec2<i32>;
    var param_11: u32;
    var v2_1: vec4<f32>;
    var param_12: vec2<i32>;
    var param_13: u32;
    var v3_1: vec4<f32>;
    var param_14: vec2<i32>;
    var param_15: u32;
    var param_16: vec4<f32>;
    var param_17: vec4<f32>;
    var param_18: vec4<f32>;
    var param_19: vec4<f32>;
    let _e92 = (*i0_);
    param_8 = bitcast<vec2<i32>>(_e92);
    let _e94 = (*slice_3);
    param_9 = _e94;
    let _e95 = SpdLoad_u0028_vi2_u003b_u1_u003b((&param_8), (&param_9));
    v0_1 = _e95;
    let _e96 = (*i1_);
    param_10 = bitcast<vec2<i32>>(_e96);
    let _e98 = (*slice_3);
    param_11 = _e98;
    let _e99 = SpdLoad_u0028_vi2_u003b_u1_u003b((&param_10), (&param_11));
    v1_1 = _e99;
    let _e100 = (*i2_);
    param_12 = bitcast<vec2<i32>>(_e100);
    let _e102 = (*slice_3);
    param_13 = _e102;
    let _e103 = SpdLoad_u0028_vi2_u003b_u1_u003b((&param_12), (&param_13));
    v2_1 = _e103;
    let _e104 = (*i3_);
    param_14 = bitcast<vec2<i32>>(_e104);
    let _e106 = (*slice_3);
    param_15 = _e106;
    let _e107 = SpdLoad_u0028_vi2_u003b_u1_u003b((&param_14), (&param_15));
    v3_1 = _e107;
    let _e108 = v0_1;
    param_16 = _e108;
    let _e109 = v1_1;
    param_17 = _e109;
    let _e110 = v2_1;
    param_18 = _e110;
    let _e111 = v3_1;
    param_19 = _e111;
    let _e112 = SpdReduce4_u0028_vf4_u003b_vf4_u003b_vf4_u003b_vf4_u003b((&param_16), (&param_17), (&param_18), (&param_19));
    return _e112;
}
fn SpdReduceLoad4_u0028_vu2_u003b_u1_u003b(base: ptr<function, vec2<u32>>, slice_4: ptr<function, u32>) -> vec4<f32> {
    var param_20: vec2<u32>;
    var param_21: vec2<u32>;
    var param_22: vec2<u32>;
    var param_23: vec2<u32>;
    var param_24: u32;
    let _e78 = (*base);
    let _e80 = (*base);
    let _e82 = (*base);
    let _e84 = (*base);
    param_20 = (_e78 + vec2<u32>(0u, 0u));
    param_21 = (_e80 + vec2<u32>(0u, 1u));
    param_22 = (_e82 + vec2<u32>(1u, 0u));
    param_23 = (_e84 + vec2<u32>(1u, 1u));
    let _e86 = (*slice_4);
    param_24 = _e86;
    let _e87 = SpdReduceLoad4_u0028_vu2_u003b_vu2_u003b_vu2_u003b_vu2_u003b_u1_u003b((&param_20), (&param_21), (&param_22), (&param_23), (&param_24));
    return _e87;
}
fn SpdDownsampleMips_6_7_u0028_u1_u003b_u1_u003b_u1_u003b_u1_u003b(x_2: ptr<function, u32>, y_2: ptr<function, u32>, mips: ptr<function, u32>, slice_5: ptr<function, u32>) {
    var tex_1: vec2<i32>;
    var pix_1: vec2<i32>;
    var v0_2: vec4<f32>;
    var param_25: vec2<u32>;
    var param_26: u32;
    var param_27: vec2<i32>;
    var param_28: vec4<f32>;
    var param_29: u32;
    var param_30: u32;
    var v1_2: vec4<f32>;
    var param_31: vec2<u32>;
    var param_32: u32;
    var param_33: vec2<i32>;
    var param_34: vec4<f32>;
    var param_35: u32;
    var param_36: u32;
    var v2_2: vec4<f32>;
    var param_37: vec2<u32>;
    var param_38: u32;
    var param_39: vec2<i32>;
    var param_40: vec4<f32>;
    var param_41: u32;
    var param_42: u32;
    var v3_2: vec4<f32>;
    var param_43: vec2<u32>;
    var param_44: u32;
    var param_45: vec2<i32>;
    var param_46: vec4<f32>;
    var param_47: u32;
    var param_48: u32;
    var v: vec4<f32>;
    var param_49: vec4<f32>;
    var param_50: vec4<f32>;
    var param_51: vec4<f32>;
    var param_52: vec4<f32>;
    var param_53: vec2<i32>;
    var param_54: vec4<f32>;
    var param_55: u32;
    var param_56: u32;
    var param_57: u32;
    var param_58: u32;
    var param_59: vec4<f32>;
    let _e117 = (*x_2);
    let _e121 = (*y_2);
    tex_1 = vec2<i32>(bitcast<i32>(((_e117 * 4u) + 0u)), bitcast<i32>(((_e121 * 4u) + 0u)));
    let _e126 = (*x_2);
    let _e130 = (*y_2);
    pix_1 = vec2<i32>(bitcast<i32>(((_e126 * 2u) + 0u)), bitcast<i32>(((_e130 * 2u) + 0u)));
    let _e135 = tex_1;
    param_25 = bitcast<vec2<u32>>(_e135);
    let _e137 = (*slice_5);
    param_26 = _e137;
    let _e138 = SpdReduceLoad4_u0028_vu2_u003b_u1_u003b((&param_25), (&param_26));
    v0_2 = _e138;
    let _e139 = pix_1;
    param_27 = _e139;
    let _e140 = v0_2;
    param_28 = _e140;
    param_29 = 6u;
    let _e141 = (*slice_5);
    param_30 = _e141;
    SpdStore_u0028_vi2_u003b_vf4_u003b_u1_u003b_u1_u003b((&param_27), (&param_28), (&param_29), (&param_30));
    let _e142 = (*x_2);
    let _e146 = (*y_2);
)FSR2",
R"FSR2(    tex_1 = vec2<i32>(bitcast<i32>(((_e142 * 4u) + 2u)), bitcast<i32>(((_e146 * 4u) + 0u)));
    let _e151 = (*x_2);
    let _e155 = (*y_2);
    pix_1 = vec2<i32>(bitcast<i32>(((_e151 * 2u) + 1u)), bitcast<i32>(((_e155 * 2u) + 0u)));
    let _e160 = tex_1;
    param_31 = bitcast<vec2<u32>>(_e160);
    let _e162 = (*slice_5);
    param_32 = _e162;
    let _e163 = SpdReduceLoad4_u0028_vu2_u003b_u1_u003b((&param_31), (&param_32));
    v1_2 = _e163;
    let _e164 = pix_1;
    param_33 = _e164;
    let _e165 = v1_2;
    param_34 = _e165;
    param_35 = 6u;
    let _e166 = (*slice_5);
    param_36 = _e166;
    SpdStore_u0028_vi2_u003b_vf4_u003b_u1_u003b_u1_u003b((&param_33), (&param_34), (&param_35), (&param_36));
    let _e167 = (*x_2);
    let _e171 = (*y_2);
    tex_1 = vec2<i32>(bitcast<i32>(((_e167 * 4u) + 0u)), bitcast<i32>(((_e171 * 4u) + 2u)));
    let _e176 = (*x_2);
    let _e180 = (*y_2);
    pix_1 = vec2<i32>(bitcast<i32>(((_e176 * 2u) + 0u)), bitcast<i32>(((_e180 * 2u) + 1u)));
    let _e185 = tex_1;
    param_37 = bitcast<vec2<u32>>(_e185);
    let _e187 = (*slice_5);
    param_38 = _e187;
    let _e188 = SpdReduceLoad4_u0028_vu2_u003b_u1_u003b((&param_37), (&param_38));
    v2_2 = _e188;
    let _e189 = pix_1;
    param_39 = _e189;
    let _e190 = v2_2;
    param_40 = _e190;
    param_41 = 6u;
    let _e191 = (*slice_5);
    param_42 = _e191;
    SpdStore_u0028_vi2_u003b_vf4_u003b_u1_u003b_u1_u003b((&param_39), (&param_40), (&param_41), (&param_42));
    let _e192 = (*x_2);
    let _e196 = (*y_2);
    tex_1 = vec2<i32>(bitcast<i32>(((_e192 * 4u) + 2u)), bitcast<i32>(((_e196 * 4u) + 2u)));
    let _e201 = (*x_2);
    let _e205 = (*y_2);
    pix_1 = vec2<i32>(bitcast<i32>(((_e201 * 2u) + 1u)), bitcast<i32>(((_e205 * 2u) + 1u)));
    let _e210 = tex_1;
    param_43 = bitcast<vec2<u32>>(_e210);
    let _e212 = (*slice_5);
    param_44 = _e212;
    let _e213 = SpdReduceLoad4_u0028_vu2_u003b_u1_u003b((&param_43), (&param_44));
    v3_2 = _e213;
    let _e214 = pix_1;
    param_45 = _e214;
    let _e215 = v3_2;
    param_46 = _e215;
    param_47 = 6u;
    let _e216 = (*slice_5);
    param_48 = _e216;
    SpdStore_u0028_vi2_u003b_vf4_u003b_u1_u003b_u1_u003b((&param_45), (&param_46), (&param_47), (&param_48));
    let _e217 = (*mips);
    if (_e217 <= 7u) {
        return;
    }
    let _e219 = v0_2;
    param_49 = _e219;
    let _e220 = v1_2;
    param_50 = _e220;
    let _e221 = v2_2;
    param_51 = _e221;
    let _e222 = v3_2;
    param_52 = _e222;
    let _e223 = SpdReduce4_u0028_vf4_u003b_vf4_u003b_vf4_u003b_vf4_u003b((&param_49), (&param_50), (&param_51), (&param_52));
    v = _e223;
    let _e224 = (*x_2);
    let _e226 = (*y_2);
    param_53 = vec2<i32>(bitcast<i32>(_e224), bitcast<i32>(_e226));
    let _e229 = v;
    param_54 = _e229;
    param_55 = 7u;
    let _e230 = (*slice_5);
    param_56 = _e230;
    SpdStore_u0028_vi2_u003b_vf4_u003b_u1_u003b_u1_u003b((&param_53), (&param_54), (&param_55), (&param_56));
    let _e231 = (*x_2);
    param_57 = _e231;
    let _e232 = (*y_2);
    param_58 = _e232;
    let _e233 = v;
    param_59 = _e233;
    SpdStoreIntermediate_u0028_u1_u003b_u1_u003b_vf4_u003b((&param_57), (&param_58), (&param_59));
    return;
}
fn SPD_ResetAtomicCounter_u0028_() {
    atomicStore((&unnamed.rw_spd_global_atomic[0i]), 0u);
    return;
}
fn SpdResetAtomicCounter_u0028_u1_u003b(slice_6: ptr<function, u32>) {
    SPD_ResetAtomicCounter_u0028_();
    return;
}
fn SpdGetAtomicCounter_u0028_() -> u32 {
    let _e71 = spdCounter_1;
    return _e71;
}
fn SpdWorkgroupShuffleBarrier_u0028_() {
    workgroupBarrier();
    return;
}
fn SPD_IncreaseAtomicCounter_u0028_u1_u003b(spdCounter: ptr<function, u32>) {
    let _e74 = atomicAdd((&unnamed.rw_spd_global_atomic[0i]), 1u);
    (*spdCounter) = _e74;
    return;
}
fn SpdIncreaseAtomicCounter_u0028_u1_u003b(slice_7: ptr<function, u32>) {
    var param_60: u32;
    let _e73 = spdCounter_1;
    param_60 = _e73;
    SPD_IncreaseAtomicCounter_u0028_u1_u003b((&param_60));
    let _e74 = param_60;
    spdCounter_1 = _e74;
    return;
}
fn SpdExitWorkgroup_u0028_u1_u003b_u1_u003b_u1_u003b(numWorkGroups: ptr<function, u32>, localInvocationIndex: ptr<function, u32>, slice_8: ptr<function, u32>) -> bool {
    var param_61: u32;
    let _e75 = (*localInvocationIndex);
    if (_e75 == 0u) {
        let _e77 = (*slice_8);
        param_61 = _e77;
        SpdIncreaseAtomicCounter_u0028_u1_u003b((&param_61));
    }
    SpdWorkgroupShuffleBarrier_u0028_();
    let _e78 = SpdGetAtomicCounter_u0028_();
    let _e79 = (*numWorkGroups);
    return (_e78 != (_e79 - 1u));
}
fn SpdLoadIntermediate_u0028_u1_u003b_u1_u003b(x_3: ptr<function, u32>, y_3: ptr<function, u32>) -> vec4<f32> {
    let _e73 = (*x_3);
    let _e74 = (*y_3);
    let _e77 = spdIntermediateR[_e73][_e74];
    let _e78 = (*x_3);
    let _e79 = (*y_3);
    let _e82 = spdIntermediateG[_e78][_e79];
    let _e83 = (*x_3);
    let _e84 = (*y_3);
    let _e87 = spdIntermediateB[_e83][_e84];
    let _e88 = (*x_3);
    let _e89 = (*y_3);
    let _e92 = spdIntermediateA[_e88][_e89];
    return vec4<f32>(_e77, _e82, _e87, _e92);
}
fn SpdReduceIntermediate_u0028_vu2_u003b_vu2_u003b_vu2_u003b_vu2_u003b(i0_1: ptr<function, vec2<u32>>, i1_1: ptr<function, vec2<u32>>, i2_1: ptr<function, vec2<u32>>, i3_1: ptr<function, vec2<u32>>) -> vec4<f32> {
    var v0_3: vec4<f32>;
    var param_62: u32;
    var param_63: u32;
    var v1_3: vec4<f32>;
    var param_64: u32;
    var param_65: u32;
    var v2_3: vec4<f32>;
    var param_66: u32;
    var param_67: u32;
    var v3_3: vec4<f32>;
    var param_68: u32;
    var param_69: u32;
    var param_70: vec4<f32>;
    var param_71: vec4<f32>;
    var param_72: vec4<f32>;
    var param_73: vec4<f32>;
    let _e92 = (*i0_1)[0u];
    param_62 = _e92;
    let _e94 = (*i0_1)[1u];
    param_63 = _e94;
    let _e95 = SpdLoadIntermediate_u0028_u1_u003b_u1_u003b((&param_62), (&param_63));
    v0_3 = _e95;
    let _e97 = (*i1_1)[0u];
    param_64 = _e97;
    let _e99 = (*i1_1)[1u];
    param_65 = _e99;
    let _e100 = SpdLoadIntermediate_u0028_u1_u003b_u1_u003b((&param_64), (&param_65));
    v1_3 = _e100;
    let _e102 = (*i2_1)[0u];
    param_66 = _e102;
    let _e104 = (*i2_1)[1u];
    param_67 = _e104;
    let _e105 = SpdLoadIntermediate_u0028_u1_u003b_u1_u003b((&param_66), (&param_67));
    v2_3 = _e105;
    let _e107 = (*i3_1)[0u];
    param_68 = _e107;
    let _e109 = (*i3_1)[1u];
    param_69 = _e109;
    let _e110 = SpdLoadIntermediate_u0028_u1_u003b_u1_u003b((&param_68), (&param_69));
    v3_3 = _e110;
    let _e111 = v0_3;
    param_70 = _e111;
    let _e112 = v1_3;
    param_71 = _e112;
    let _e113 = v2_3;
    param_72 = _e113;
    let _e114 = v3_3;
    param_73 = _e114;
    let _e115 = SpdReduce4_u0028_vf4_u003b_vf4_u003b_vf4_u003b_vf4_u003b((&param_70), (&param_71), (&param_72), (&param_73));
    return _e115;
}
fn SpdDownsampleMip_5_u0028_vu2_u003b_u1_u003b_u1_u003b_u1_u003b(workGroupID: ptr<function, vec2<u32>>, localInvocationIndex_1: ptr<function, u32>, mip: ptr<function, u32>, slice_9: ptr<function, u32>) {
    var v_1: vec4<f32>;
    var param_74: vec2<u32>;
    var param_75: vec2<u32>;
    var param_76: vec2<u32>;
    var param_77: vec2<u32>;
    var param_78: vec2<i32>;
    var param_79: vec4<f32>;
    var param_80: u32;
    var param_81: u32;
    let _e84 = (*localInvocationIndex_1);
    if (_e84 < 1u) {
        param_74 = vec2<u32>(0u, 0u);
        param_75 = vec2<u32>(1u, 0u);
        param_76 = vec2<u32>(2u, 0u);
        param_77 = vec2<u32>(3u, 0u);
        let _e86 = SpdReduceIntermediate_u0028_vu2_u003b_vu2_u003b_vu2_u003b_vu2_u003b((&param_74), (&param_75), (&param_76), (&param_77));
        v_1 = _e86;
        let _e87 = (*workGroupID);
        param_78 = bitcast<vec2<i32>>(_e87);
        let _e89 = v_1;
        param_79 = _e89;
        let _e90 = (*mip);
        param_80 = _e90;
        let _e91 = (*slice_9);
        param_81 = _e91;
        SpdStore_u0028_vi2_u003b_vf4_u003b_u1_u003b_u1_u003b((&param_78), (&param_79), (&param_80), (&param_81));
    }
    return;
}
fn SpdDownsampleMip_4_u0028_u1_u003b_u1_u003b_vu2_u003b_u1_u003b_u1_u003b_u1_u003b(x_4: ptr<function, u32>, y_4: ptr<function, u32>, workGroupID_1: ptr<function, vec2<u32>>, localInvocationIndex_2: ptr<function, u32>, mip_1: ptr<function, u32>, slice_10: ptr<function, u32>) {
    var v_2: vec4<f32>;
    var param_82: vec2<u32>;
    var param_83: vec2<u32>;
    var param_84: vec2<u32>;
    var param_85: vec2<u32>;
    var param_86: vec2<i32>;
    var param_87: vec4<f32>;
    var param_88: u32;
    var param_89: u32;
    var param_90: u32;
    var param_91: u32;
    var param_92: vec4<f32>;
    let _e89 = (*localInvocationIndex_2);
    if (_e89 < 4u) {
        let _e91 = (*x_4);
        let _e95 = (*y_4);
        let _e98 = (*y_4);
        let _e102 = (*x_4);
        let _e106 = (*y_4);
        let _e109 = (*y_4);
        let _e113 = (*x_4);
        let _e117 = (*y_4);
        let _e120 = (*y_4);
        let _e124 = (*x_4);
        let _e128 = (*y_4);
        let _e131 = (*y_4);
        param_82 = vec2<u32>(((((_e91 * 8u) + 0u) + 0u) + (_e95 * 2u)), ((_e98 * 8u) + 0u));
        param_83 = vec2<u32>(((((_e102 * 8u) + 4u) + 0u) + (_e106 * 2u)), ((_e109 * 8u) + 0u));
        param_84 = vec2<u32>(((((_e113 * 8u) + 0u) + 1u) + (_e117 * 2u)), ((_e120 * 8u) + 4u));
        param_85 = vec2<u32>(((((_e124 * 8u) + 4u) + 1u) + (_e128 * 2u)), ((_e131 * 8u) + 4u));
        let _e135 = SpdReduceIntermediate_u0028_vu2_u003b_vu2_u003b_vu2_u003b_vu2_u003b((&param_82), (&param_83), (&param_84), (&param_85));
        v_2 = _e135;
        let _e136 = (*workGroupID_1);
        let _e140 = (*x_4);
        let _e142 = (*y_4);
        param_86 = (bitcast<vec2<i32>>((_e136 * vec2(2u))) + vec2<i32>(bitcast<i32>(_e140), bitcast<i32>(_e142)));
        let _e146 = v_2;
        param_87 = _e146;
        let _e147 = (*mip_1);
        param_88 = _e147;
        let _e148 = (*slice_10);
        param_89 = _e148;
        SpdStore_u0028_vi2_u003b_vf4_u003b_u1_u003b_u1_u003b((&param_86), (&param_87), (&param_88), (&param_89));
        let _e149 = (*x_4);
        let _e150 = (*y_4);
        param_90 = (_e149 + (_e150 * 2u));
        param_91 = 0u;
        let _e153 = v_2;
        param_92 = _e153;
        SpdStoreIntermediate_u0028_u1_u003b_u1_u003b_vf4_u003b((&param_90), (&param_91), (&param_92));
    }
    return;
}
fn SpdDownsampleMip_3_u0028_u1_u003b_u1_u003b_vu2_u003b_u1_u003b_u1_u003b_u1_u003b(x_5: ptr<function, u32>, y_5: ptr<function, u32>, workGroupID_2: ptr<function, vec2<u32>>, localInvocationIndex_3: ptr<function, u32>, mip_2: ptr<function, u32>, slice_11: ptr<function, u32>) {
    var v_3: vec4<f32>;
    var param_93: vec2<u32>;
    var param_94: vec2<u32>;
    var param_95: vec2<u32>;
    var param_96: vec2<u32>;
    var param_97: vec2<i32>;
    var param_98: vec4<f32>;
    var param_99: u32;
    var param_100: u32;
    var param_101: u32;
    var param_102: u32;
    var param_103: vec4<f32>;
    let _e89 = (*localInvocationIndex_3);
    if (_e89 < 16u) {
        let _e91 = (*x_5);
        let _e95 = (*y_5);
        let _e99 = (*x_5);
        let _e103 = (*y_5);
        let _e107 = (*x_5);
        let _e111 = (*y_5);
        let _e115 = (*x_5);
        let _e119 = (*y_5);
        param_93 = vec2<u32>((((_e91 * 4u) + 0u) + 0u), ((_e95 * 4u) + 0u));
        param_94 = vec2<u32>((((_e99 * 4u) + 2u) + 0u), ((_e103 * 4u) + 0u));
        param_95 = vec2<u32>((((_e107 * 4u) + 0u) + 1u), ((_e111 * 4u) + 2u));
        param_96 = vec2<u32>((((_e115 * 4u) + 2u) + 1u), ((_e119 * 4u) + 2u));
        let _e123 = SpdReduceIntermediate_u0028_vu2_u003b_vu2_u003b_vu2_u003b_vu2_u003b((&param_93), (&param_94), (&param_95), (&param_96));
        v_3 = _e123;
        let _e124 = (*workGroupID_2);
        let _e128 = (*x_5);
        let _e130 = (*y_5);
        param_97 = (bitcast<vec2<i32>>((_e124 * vec2(4u))) + vec2<i32>(bitcast<i32>(_e128), bitcast<i32>(_e130)));
        let _e134 = v_3;
)FSR2",
R"FSR2(        param_98 = _e134;
        let _e135 = (*mip_2);
        param_99 = _e135;
        let _e136 = (*slice_11);
        param_100 = _e136;
        SpdStore_u0028_vi2_u003b_vf4_u003b_u1_u003b_u1_u003b((&param_97), (&param_98), (&param_99), (&param_100));
        let _e137 = (*x_5);
        let _e139 = (*y_5);
        let _e141 = (*y_5);
        param_101 = ((_e137 * 4u) + _e139);
        param_102 = (_e141 * 4u);
        let _e143 = v_3;
        param_103 = _e143;
        SpdStoreIntermediate_u0028_u1_u003b_u1_u003b_vf4_u003b((&param_101), (&param_102), (&param_103));
    }
    return;
}
fn SpdDownsampleMip_2_u0028_u1_u003b_u1_u003b_vu2_u003b_u1_u003b_u1_u003b_u1_u003b(x_6: ptr<function, u32>, y_6: ptr<function, u32>, workGroupID_3: ptr<function, vec2<u32>>, localInvocationIndex_4: ptr<function, u32>, mip_3: ptr<function, u32>, slice_12: ptr<function, u32>) {
    var v_4: vec4<f32>;
    var param_104: vec2<u32>;
    var param_105: vec2<u32>;
    var param_106: vec2<u32>;
    var param_107: vec2<u32>;
    var param_108: vec2<i32>;
    var param_109: vec4<f32>;
    var param_110: u32;
    var param_111: u32;
    var param_112: u32;
    var param_113: u32;
    var param_114: vec4<f32>;
    let _e89 = (*localInvocationIndex_4);
    if (_e89 < 64u) {
        let _e91 = (*x_6);
        let _e94 = (*y_6);
        let _e98 = (*x_6);
        let _e101 = (*y_6);
        let _e105 = (*x_6);
        let _e108 = (*y_6);
        let _e112 = (*x_6);
        let _e115 = (*y_6);
        param_104 = vec2<u32>(((_e91 * 2u) + 0u), ((_e94 * 2u) + 0u));
        param_105 = vec2<u32>(((_e98 * 2u) + 1u), ((_e101 * 2u) + 0u));
        param_106 = vec2<u32>(((_e105 * 2u) + 0u), ((_e108 * 2u) + 1u));
        param_107 = vec2<u32>(((_e112 * 2u) + 1u), ((_e115 * 2u) + 1u));
        let _e119 = SpdReduceIntermediate_u0028_vu2_u003b_vu2_u003b_vu2_u003b_vu2_u003b((&param_104), (&param_105), (&param_106), (&param_107));
        v_4 = _e119;
        let _e120 = (*workGroupID_3);
        let _e124 = (*x_6);
        let _e126 = (*y_6);
        param_108 = (bitcast<vec2<i32>>((_e120 * vec2(8u))) + vec2<i32>(bitcast<i32>(_e124), bitcast<i32>(_e126)));
        let _e130 = v_4;
        param_109 = _e130;
        let _e131 = (*mip_3);
        param_110 = _e131;
        let _e132 = (*slice_12);
        param_111 = _e132;
        SpdStore_u0028_vi2_u003b_vf4_u003b_u1_u003b_u1_u003b((&param_108), (&param_109), (&param_110), (&param_111));
        let _e133 = (*x_6);
        let _e135 = (*y_6);
        let _e138 = (*y_6);
        param_112 = ((_e133 * 2u) + (_e135 % 2u));
        param_113 = (_e138 * 2u);
        let _e140 = v_4;
        param_114 = _e140;
        SpdStoreIntermediate_u0028_u1_u003b_u1_u003b_vf4_u003b((&param_112), (&param_113), (&param_114));
    }
    return;
}
fn SpdDownsampleNextFour_u0028_u1_u003b_u1_u003b_vu2_u003b_u1_u003b_u1_u003b_u1_u003b_u1_u003b(x_7: ptr<function, u32>, y_7: ptr<function, u32>, workGroupID_4: ptr<function, vec2<u32>>, localInvocationIndex_5: ptr<function, u32>, baseMip: ptr<function, u32>, mips_1: ptr<function, u32>, slice_13: ptr<function, u32>) {
    var param_115: u32;
    var param_116: u32;
    var param_117: vec2<u32>;
    var param_118: u32;
    var param_119: u32;
    var param_120: u32;
    var param_121: u32;
    var param_122: u32;
    var param_123: vec2<u32>;
    var param_124: u32;
    var param_125: u32;
    var param_126: u32;
    var param_127: u32;
    var param_128: u32;
    var param_129: vec2<u32>;
    var param_130: u32;
    var param_131: u32;
    var param_132: u32;
    var param_133: vec2<u32>;
    var param_134: u32;
    var param_135: u32;
    var param_136: u32;
    let _e100 = (*mips_1);
    let _e101 = (*baseMip);
    if (_e100 <= _e101) {
        return;
    }
    SpdWorkgroupShuffleBarrier_u0028_();
    let _e103 = (*x_7);
    param_115 = _e103;
    let _e104 = (*y_7);
    param_116 = _e104;
    let _e105 = (*workGroupID_4);
    param_117 = _e105;
    let _e106 = (*localInvocationIndex_5);
    param_118 = _e106;
    let _e107 = (*baseMip);
    param_119 = _e107;
    let _e108 = (*slice_13);
    param_120 = _e108;
    SpdDownsampleMip_2_u0028_u1_u003b_u1_u003b_vu2_u003b_u1_u003b_u1_u003b_u1_u003b((&param_115), (&param_116), (&param_117), (&param_118), (&param_119), (&param_120));
    let _e109 = (*mips_1);
    let _e110 = (*baseMip);
    if (_e109 <= (_e110 + 1u)) {
        return;
    }
    SpdWorkgroupShuffleBarrier_u0028_();
    let _e113 = (*baseMip);
    let _e115 = (*x_7);
    param_121 = _e115;
    let _e116 = (*y_7);
    param_122 = _e116;
    let _e117 = (*workGroupID_4);
    param_123 = _e117;
    let _e118 = (*localInvocationIndex_5);
    param_124 = _e118;
    param_125 = (_e113 + 1u);
    let _e119 = (*slice_13);
    param_126 = _e119;
    SpdDownsampleMip_3_u0028_u1_u003b_u1_u003b_vu2_u003b_u1_u003b_u1_u003b_u1_u003b((&param_121), (&param_122), (&param_123), (&param_124), (&param_125), (&param_126));
    let _e120 = (*mips_1);
    let _e121 = (*baseMip);
    if (_e120 <= (_e121 + 2u)) {
        return;
    }
    SpdWorkgroupShuffleBarrier_u0028_();
    let _e124 = (*baseMip);
    let _e126 = (*x_7);
    param_127 = _e126;
    let _e127 = (*y_7);
    param_128 = _e127;
    let _e128 = (*workGroupID_4);
    param_129 = _e128;
    let _e129 = (*localInvocationIndex_5);
    param_130 = _e129;
    param_131 = (_e124 + 2u);
    let _e130 = (*slice_13);
    param_132 = _e130;
    SpdDownsampleMip_4_u0028_u1_u003b_u1_u003b_vu2_u003b_u1_u003b_u1_u003b_u1_u003b((&param_127), (&param_128), (&param_129), (&param_130), (&param_131), (&param_132));
    let _e131 = (*mips_1);
    let _e132 = (*baseMip);
    if (_e131 <= (_e132 + 3u)) {
        return;
    }
    SpdWorkgroupShuffleBarrier_u0028_();
    let _e135 = (*baseMip);
    let _e137 = (*workGroupID_4);
    param_133 = _e137;
    let _e138 = (*localInvocationIndex_5);
    param_134 = _e138;
    param_135 = (_e135 + 3u);
    let _e139 = (*slice_13);
    param_136 = _e139;
    SpdDownsampleMip_5_u0028_vu2_u003b_u1_u003b_u1_u003b_u1_u003b((&param_133), (&param_134), (&param_135), (&param_136));
    return;
}
fn ffxMax_u0028_f1_u003b_f1_u003b(x_8: ptr<function, f32>, y_8: ptr<function, f32>) -> f32 {
    let _e73 = (*x_8);
    let _e74 = (*y_8);
    return max(_e73, _e74);
}
fn RGBToLuma_u0028_vf3_u003b(fLinearRgb: ptr<function, vec3<f32>>) -> f32 {
    let _e72 = (*fLinearRgb);
    return dot(_e72, vec3<f32>(0.2126f, 0.7152f, 0.0722f));
}
fn PreExposure_u0028_() -> f32 {
    let _e72 = cbFSR2_.fPreExposure;
    return _e72;
}
fn SampleInputColor_u0028_vf2_u003b(fUV: ptr<function, vec2<f32>>) -> vec3<f32> {
    let _e72 = (*fUV);
    let _e73 = textureSampleLevel(r_input_color_jittered, s_LinearClamp, _e72, 0f);
    return _e73.xyz;
}
fn ffxMax_u0028_vf2_u003b_vf2_u003b(x_9: ptr<function, vec2<f32>>, y_9: ptr<function, vec2<f32>>) -> vec2<f32> {
    let _e73 = (*x_9);
    let _e74 = (*y_9);
    return max(_e73, _e74);
}
fn ffxMin_u0028_vf2_u003b_vf2_u003b(x_10: ptr<function, vec2<f32>>, y_10: ptr<function, vec2<f32>>) -> vec2<f32> {
    let _e73 = (*x_10);
    let _e74 = (*y_10);
    return min(_e73, _e74);
}
fn ClampUv_u0028_vf2_u003b_vi2_u003b_vi2_u003b(fUv: ptr<function, vec2<f32>>, iTextureSize: ptr<function, vec2<i32>>, iResourceSize: ptr<function, vec2<i32>>) -> vec2<f32> {
    var fSampleLocation: vec2<f32>;
    var fClampedLocation: vec2<f32>;
    var param_137: vec2<f32>;
    var param_138: vec2<f32>;
    var param_139: vec2<f32>;
    var param_140: vec2<f32>;
    var fClampedUv: vec2<f32>;
    let _e81 = (*fUv);
    let _e82 = (*iTextureSize);
    fSampleLocation = (_e81 * vec2<f32>(_e82));
    let _e85 = (*iTextureSize);
    let _e88 = fSampleLocation;
    param_137 = _e88;
    param_138 = (vec2<f32>(_e85) - vec2<f32>(0.5f, 0.5f));
    let _e89 = ffxMin_u0028_vf2_u003b_vf2_u003b((&param_137), (&param_138));
    param_139 = vec2<f32>(0.5f, 0.5f);
    param_140 = _e89;
    let _e90 = ffxMax_u0028_vf2_u003b_vf2_u003b((&param_139), (&param_140));
    fClampedLocation = _e90;
    let _e91 = fClampedLocation;
    let _e92 = (*iResourceSize);
    fClampedUv = (_e91 / vec2<f32>(_e92));
    let _e95 = fClampedUv;
    return _e95;
}
fn InputColorResourceDimensions_u0028_() -> vec2<i32> {
    let _e72 = cbFSR2_.iInputColorResourceDimensions;
    return _e72;
}
fn RenderSize_u0028_() -> vec2<i32> {
    let _e72 = cbFSR2_.iRenderSize;
    return _e72;
}
fn Jitter_u0028_() -> vec2<f32> {
    let _e72 = cbFSR2_.fJitter;
    return _e72;
}
fn SpdLoadSourceImage_u0028_vf2_u003b_u1_u003b(tex_2: ptr<function, vec2<f32>>, slice_14: ptr<function, u32>) -> vec4<f32> {
    var fUv_1: vec2<f32>;
    var param_141: vec2<f32>;
    var param_142: vec2<i32>;
    var param_143: vec2<i32>;
    var fRgb: vec3<f32>;
    var param_144: vec2<f32>;
    var fLogLuma: f32;
    var param_145: vec3<f32>;
    var param_146: f32;
    var param_147: f32;
    var result_1: f32;
    let _e84 = (*tex_2);
    let _e87 = Jitter_u0028_();
    let _e89 = RenderSize_u0028_();
    fUv_1 = (((_e84 + vec2(0.5f)) + _e87) / vec2<f32>(_e89));
    let _e92 = RenderSize_u0028_();
    let _e93 = InputColorResourceDimensions_u0028_();
    let _e94 = fUv_1;
    param_141 = _e94;
    param_142 = _e92;
    param_143 = _e93;
    let _e95 = ClampUv_u0028_vf2_u003b_vi2_u003b_vi2_u003b((&param_141), (&param_142), (&param_143));
    fUv_1 = _e95;
    let _e96 = fUv_1;
    param_144 = _e96;
    let _e97 = SampleInputColor_u0028_vf2_u003b((&param_144));
    fRgb = _e97;
    let _e98 = PreExposure_u0028_();
    let _e99 = fRgb;
    fRgb = (_e99 / vec3(_e98));
    let _e102 = fRgb;
    param_145 = _e102;
    let _e103 = RGBToLuma_u0028_vf3_u003b((&param_145));
    param_146 = 0.001f;
    param_147 = _e103;
    let _e104 = ffxMax_u0028_f1_u003b_f1_u003b((&param_146), (&param_147));
    fLogLuma = log(_e104);
    let _e106 = (*tex_2);
    let _e107 = RenderSize_u0028_();
    let _e111 = fLogLuma;
    result_1 = select(0f, _e111, all((_e106 < vec2<f32>(_e107))));
    let _e113 = result_1;
    return vec4<f32>(_e113, 0f, 0f, 0f);
}
fn SpdReduceLoadSourceImage4_u0028_vu2_u003b_vu2_u003b_vu2_u003b_vu2_u003b_u1_u003b(i0_2: ptr<function, vec2<u32>>, i1_2: ptr<function, vec2<u32>>, i2_2: ptr<function, vec2<u32>>, i3_2: ptr<function, vec2<u32>>, slice_15: ptr<function, u32>) -> vec4<f32> {
    var v0_4: vec4<f32>;
    var param_148: vec2<f32>;
    var param_149: u32;
    var v1_4: vec4<f32>;
    var param_150: vec2<f32>;
    var param_151: u32;
    var v2_4: vec4<f32>;
    var param_152: vec2<f32>;
    var param_153: u32;
    var v3_4: vec4<f32>;
    var param_154: vec2<f32>;
    var param_155: u32;
    var param_156: vec4<f32>;
    var param_157: vec4<f32>;
    var param_158: vec4<f32>;
    var param_159: vec4<f32>;
    let _e92 = (*i0_2);
    param_148 = vec2<f32>(bitcast<vec2<i32>>(_e92));
    let _e95 = (*slice_15);
    param_149 = _e95;
    let _e96 = SpdLoadSourceImage_u0028_vf2_u003b_u1_u003b((&param_148), (&param_149));
    v0_4 = _e96;
    let _e97 = (*i1_2);
    param_150 = vec2<f32>(bitcast<vec2<i32>>(_e97));
    let _e100 = (*slice_15);
    param_151 = _e100;
    let _e101 = SpdLoadSourceImage_u0028_vf2_u003b_u1_u003b((&param_150), (&param_151));
    v1_4 = _e101;
    let _e102 = (*i2_2);
    param_152 = vec2<f32>(bitcast<vec2<i32>>(_e102));
    let _e105 = (*slice_15);
    param_153 = _e105;
    let _e106 = SpdLoadSourceImage_u0028_vf2_u003b_u1_u003b((&param_152), (&param_153));
    v2_4 = _e106;
    let _e107 = (*i3_2);
    param_154 = vec2<f32>(bitcast<vec2<i32>>(_e107));
    let _e110 = (*slice_15);
    param_155 = _e110;
    let _e111 = SpdLoadSourceImage_u0028_vf2_u003b_u1_u003b((&param_154), (&param_155));
    v3_4 = _e111;
    let _e112 = v0_4;
    param_156 = _e112;
    let _e113 = v1_4;
    param_157 = _e113;
    let _e114 = v2_4;
    param_158 = _e114;
    let _e115 = v3_4;
    param_159 = _e115;
    let _e116 = SpdReduce4_u0028_vf4_u003b_vf4_u003b_vf4_u003b_vf4_u003b((&param_156), (&param_157), (&param_158), (&param_159));
)FSR2",
R"FSR2(    return _e116;
}
fn SpdReduceLoadSourceImage_u0028_vu2_u003b_u1_u003b(base_1: ptr<function, vec2<u32>>, slice_16: ptr<function, u32>) -> vec4<f32> {
    var param_160: vec2<u32>;
    var param_161: vec2<u32>;
    var param_162: vec2<u32>;
    var param_163: vec2<u32>;
    var param_164: u32;
    let _e78 = (*base_1);
    let _e80 = (*base_1);
    let _e82 = (*base_1);
    let _e84 = (*base_1);
    param_160 = (_e78 + vec2<u32>(0u, 0u));
    param_161 = (_e80 + vec2<u32>(0u, 1u));
    param_162 = (_e82 + vec2<u32>(1u, 0u));
    param_163 = (_e84 + vec2<u32>(1u, 1u));
    let _e86 = (*slice_16);
    param_164 = _e86;
    let _e87 = SpdReduceLoadSourceImage4_u0028_vu2_u003b_vu2_u003b_vu2_u003b_vu2_u003b_u1_u003b((&param_160), (&param_161), (&param_162), (&param_163), (&param_164));
    return _e87;
}
fn SpdDownsampleMips_0_1_LDS_u0028_u1_u003b_u1_u003b_vu2_u003b_u1_u003b_u1_u003b_u1_u003b(x_11: ptr<function, u32>, y_11: ptr<function, u32>, workGroupID_5: ptr<function, vec2<u32>>, localInvocationIndex_6: ptr<function, u32>, mip_4: ptr<function, u32>, slice_17: ptr<function, u32>) {
    var tex_3: vec2<i32>;
    var pix_2: vec2<i32>;
    var v_5: array<vec4<f32>, 4>;
    var param_165: vec2<u32>;
    var param_166: u32;
    var param_167: vec2<i32>;
    var param_168: vec4<f32>;
    var param_169: u32;
    var param_170: u32;
    var param_171: vec2<u32>;
    var param_172: u32;
    var param_173: vec2<i32>;
    var param_174: vec4<f32>;
    var param_175: u32;
    var param_176: u32;
    var param_177: vec2<u32>;
    var param_178: u32;
    var param_179: vec2<i32>;
    var param_180: vec4<f32>;
    var param_181: u32;
    var param_182: u32;
    var param_183: vec2<u32>;
    var param_184: u32;
    var param_185: vec2<i32>;
    var param_186: vec4<f32>;
    var param_187: u32;
    var param_188: u32;
    var i: u32;
    var param_189: u32;
    var param_190: u32;
    var param_191: vec4<f32>;
    var param_192: vec2<u32>;
    var param_193: vec2<u32>;
    var param_194: vec2<u32>;
    var param_195: vec2<u32>;
    var param_196: vec2<i32>;
    var param_197: vec4<f32>;
    var param_198: u32;
    var param_199: u32;
    var param_200: u32;
    var param_201: u32;
    var param_202: vec4<f32>;
    var param_203: u32;
    var param_204: u32;
    var param_205: vec4<f32>;
    var param_206: u32;
    var param_207: u32;
    var param_208: vec4<f32>;
    var param_209: u32;
    var param_210: u32;
    var param_211: vec4<f32>;
    let _e128 = (*workGroupID_5);
    let _e132 = (*x_11);
    let _e135 = (*y_11);
    tex_3 = (bitcast<vec2<i32>>((_e128 * vec2(64u))) + vec2<i32>(bitcast<i32>((_e132 * 2u)), bitcast<i32>((_e135 * 2u))));
    let _e140 = (*workGroupID_5);
    let _e144 = (*x_11);
    let _e146 = (*y_11);
    pix_2 = (bitcast<vec2<i32>>((_e140 * vec2(32u))) + vec2<i32>(bitcast<i32>(_e144), bitcast<i32>(_e146)));
    let _e150 = tex_3;
    param_165 = bitcast<vec2<u32>>(_e150);
    let _e152 = (*slice_17);
    param_166 = _e152;
    let _e153 = SpdReduceLoadSourceImage_u0028_vu2_u003b_u1_u003b((&param_165), (&param_166));
    v_5[0i] = _e153;
    let _e155 = pix_2;
    param_167 = _e155;
    let _e157 = v_5[0i];
    param_168 = _e157;
    param_169 = 0u;
    let _e158 = (*slice_17);
    param_170 = _e158;
    SpdStore_u0028_vi2_u003b_vf4_u003b_u1_u003b_u1_u003b((&param_167), (&param_168), (&param_169), (&param_170));
    let _e159 = (*workGroupID_5);
    let _e163 = (*x_11);
    let _e167 = (*y_11);
    tex_3 = (bitcast<vec2<i32>>((_e159 * vec2(64u))) + vec2<i32>(bitcast<i32>(((_e163 * 2u) + 32u)), bitcast<i32>((_e167 * 2u))));
    let _e172 = (*workGroupID_5);
    let _e176 = (*x_11);
    let _e179 = (*y_11);
    pix_2 = (bitcast<vec2<i32>>((_e172 * vec2(32u))) + vec2<i32>(bitcast<i32>((_e176 + 16u)), bitcast<i32>(_e179)));
    let _e183 = tex_3;
    param_171 = bitcast<vec2<u32>>(_e183);
    let _e185 = (*slice_17);
    param_172 = _e185;
    let _e186 = SpdReduceLoadSourceImage_u0028_vu2_u003b_u1_u003b((&param_171), (&param_172));
    v_5[1i] = _e186;
    let _e188 = pix_2;
    param_173 = _e188;
    let _e190 = v_5[1i];
    param_174 = _e190;
    param_175 = 0u;
    let _e191 = (*slice_17);
    param_176 = _e191;
    SpdStore_u0028_vi2_u003b_vf4_u003b_u1_u003b_u1_u003b((&param_173), (&param_174), (&param_175), (&param_176));
    let _e192 = (*workGroupID_5);
    let _e196 = (*x_11);
    let _e199 = (*y_11);
    tex_3 = (bitcast<vec2<i32>>((_e192 * vec2(64u))) + vec2<i32>(bitcast<i32>((_e196 * 2u)), bitcast<i32>(((_e199 * 2u) + 32u))));
    let _e205 = (*workGroupID_5);
    let _e209 = (*x_11);
    let _e211 = (*y_11);
    pix_2 = (bitcast<vec2<i32>>((_e205 * vec2(32u))) + vec2<i32>(bitcast<i32>(_e209), bitcast<i32>((_e211 + 16u))));
    let _e216 = tex_3;
    param_177 = bitcast<vec2<u32>>(_e216);
    let _e218 = (*slice_17);
    param_178 = _e218;
    let _e219 = SpdReduceLoadSourceImage_u0028_vu2_u003b_u1_u003b((&param_177), (&param_178));
    v_5[2i] = _e219;
    let _e221 = pix_2;
    param_179 = _e221;
    let _e223 = v_5[2i];
    param_180 = _e223;
    param_181 = 0u;
    let _e224 = (*slice_17);
    param_182 = _e224;
    SpdStore_u0028_vi2_u003b_vf4_u003b_u1_u003b_u1_u003b((&param_179), (&param_180), (&param_181), (&param_182));
    let _e225 = (*workGroupID_5);
    let _e229 = (*x_11);
    let _e233 = (*y_11);
    tex_3 = (bitcast<vec2<i32>>((_e225 * vec2(64u))) + vec2<i32>(bitcast<i32>(((_e229 * 2u) + 32u)), bitcast<i32>(((_e233 * 2u) + 32u))));
    let _e239 = (*workGroupID_5);
    let _e243 = (*x_11);
    let _e246 = (*y_11);
    pix_2 = (bitcast<vec2<i32>>((_e239 * vec2(32u))) + vec2<i32>(bitcast<i32>((_e243 + 16u)), bitcast<i32>((_e246 + 16u))));
    let _e251 = tex_3;
    param_183 = bitcast<vec2<u32>>(_e251);
    let _e253 = (*slice_17);
    param_184 = _e253;
    let _e254 = SpdReduceLoadSourceImage_u0028_vu2_u003b_u1_u003b((&param_183), (&param_184));
    v_5[3i] = _e254;
    let _e256 = pix_2;
    param_185 = _e256;
    let _e258 = v_5[3i];
    param_186 = _e258;
    param_187 = 0u;
    let _e259 = (*slice_17);
    param_188 = _e259;
    SpdStore_u0028_vi2_u003b_vf4_u003b_u1_u003b_u1_u003b((&param_185), (&param_186), (&param_187), (&param_188));
    let _e260 = (*mip_4);
    if (_e260 <= 1u) {
        return;
    }
    i = 0u;
    loop {
        let _e262 = i;
        if (_e262 < 4u) {
            let _e264 = i;
            let _e265 = (*x_11);
            param_189 = _e265;
            let _e266 = (*y_11);
            param_190 = _e266;
            let _e268 = v_5[_e264];
            param_191 = _e268;
            SpdStoreIntermediate_u0028_u1_u003b_u1_u003b_vf4_u003b((&param_189), (&param_190), (&param_191));
            SpdWorkgroupShuffleBarrier_u0028_();
            let _e269 = (*localInvocationIndex_6);
            if (_e269 < 64u) {
                let _e271 = i;
                let _e272 = (*x_11);
                let _e275 = (*y_11);
                let _e279 = (*x_11);
                let _e282 = (*y_11);
                let _e286 = (*x_11);
                let _e289 = (*y_11);
                let _e293 = (*x_11);
                let _e296 = (*y_11);
                param_192 = vec2<u32>(((_e272 * 2u) + 0u), ((_e275 * 2u) + 0u));
                param_193 = vec2<u32>(((_e279 * 2u) + 1u), ((_e282 * 2u) + 0u));
                param_194 = vec2<u32>(((_e286 * 2u) + 0u), ((_e289 * 2u) + 1u));
                param_195 = vec2<u32>(((_e293 * 2u) + 1u), ((_e296 * 2u) + 1u));
                let _e300 = SpdReduceIntermediate_u0028_vu2_u003b_vu2_u003b_vu2_u003b_vu2_u003b((&param_192), (&param_193), (&param_194), (&param_195));
                v_5[_e271] = _e300;
                let _e302 = (*workGroupID_5);
                let _e306 = (*x_11);
                let _e307 = i;
                let _e312 = (*y_11);
                let _e313 = i;
                let _e320 = i;
                param_196 = (bitcast<vec2<i32>>((_e302 * vec2(16u))) + vec2<i32>(bitcast<i32>((_e306 + ((_e307 % 2u) * 8u))), bitcast<i32>((_e312 + ((_e313 / 2u) * 8u)))));
                let _e322 = v_5[_e320];
                param_197 = _e322;
                param_198 = 1u;
                let _e323 = (*slice_17);
                param_199 = _e323;
                SpdStore_u0028_vi2_u003b_vf4_u003b_u1_u003b_u1_u003b((&param_196), (&param_197), (&param_198), (&param_199));
            }
            SpdWorkgroupShuffleBarrier_u0028_();
            continue;
        } else {
            break;
        }
        continuing {
            let _e324 = i;
            i = (_e324 + bitcast<u32>(1i));
        }
    }
    let _e327 = (*localInvocationIndex_6);
    if (_e327 < 64u) {
        let _e329 = (*x_11);
        let _e331 = (*y_11);
        param_200 = (_e329 + 0u);
        param_201 = (_e331 + 0u);
        let _e334 = v_5[0i];
        param_202 = _e334;
        SpdStoreIntermediate_u0028_u1_u003b_u1_u003b_vf4_u003b((&param_200), (&param_201), (&param_202));
        let _e335 = (*x_11);
        let _e337 = (*y_11);
        param_203 = (_e335 + 8u);
        param_204 = (_e337 + 0u);
        let _e340 = v_5[1i];
        param_205 = _e340;
        SpdStoreIntermediate_u0028_u1_u003b_u1_u003b_vf4_u003b((&param_203), (&param_204), (&param_205));
        let _e341 = (*x_11);
        let _e343 = (*y_11);
        param_206 = (_e341 + 0u);
        param_207 = (_e343 + 8u);
        let _e346 = v_5[2i];
        param_208 = _e346;
        SpdStoreIntermediate_u0028_u1_u003b_u1_u003b_vf4_u003b((&param_206), (&param_207), (&param_208));
        let _e347 = (*x_11);
        let _e349 = (*y_11);
        param_209 = (_e347 + 8u);
        param_210 = (_e349 + 8u);
        let _e352 = v_5[3i];
        param_211 = _e352;
        SpdStoreIntermediate_u0028_u1_u003b_u1_u003b_vf4_u003b((&param_209), (&param_210), (&param_211));
    }
    return;
}
fn SpdDownsampleMips_0_1_u0028_u1_u003b_u1_u003b_vu2_u003b_u1_u003b_u1_u003b_u1_u003b(x_12: ptr<function, u32>, y_12: ptr<function, u32>, workGroupID_6: ptr<function, vec2<u32>>, localInvocationIndex_7: ptr<function, u32>, mip_5: ptr<function, u32>, slice_18: ptr<function, u32>) {
    var param_212: u32;
    var param_213: u32;
    var param_214: vec2<u32>;
    var param_215: u32;
    var param_216: u32;
    var param_217: u32;
    let _e83 = (*x_12);
    param_212 = _e83;
    let _e84 = (*y_12);
    param_213 = _e84;
    let _e85 = (*workGroupID_6);
    param_214 = _e85;
    let _e86 = (*localInvocationIndex_7);
    param_215 = _e86;
    let _e87 = (*mip_5);
    param_216 = _e87;
    let _e88 = (*slice_18);
    param_217 = _e88;
    SpdDownsampleMips_0_1_LDS_u0028_u1_u003b_u1_u003b_vu2_u003b_u1_u003b_u1_u003b_u1_u003b((&param_212), (&param_213), (&param_214), (&param_215), (&param_216), (&param_217));
    return;
}
fn bitfieldInsertMask_u0028_u1_u003b_u1_u003b_u1_u003b(src: ptr<function, u32>, ins: ptr<function, u32>, bits: ptr<function, u32>) -> u32 {
    let _e74 = (*src);
    let _e75 = (*ins);
    let _e76 = (*bits);
    return insertBits(_e74, _e75, bitcast<u32>(0i), bitcast<u32>(bitcast<i32>(_e76)));
}
fn bitfieldExtract_u0028_u1_u003b_u1_u003b_u1_u003b(src_1: ptr<function, u32>, off: ptr<function, u32>, bits_1: ptr<function, u32>) -> u32 {
    let _e74 = (*src_1);
    let _e75 = (*off);
    let _e77 = (*bits_1);
    return extractBits(_e74, bitcast<u32>(bitcast<i32>(_e75)), bitcast<u32>(bitcast<i32>(_e77)));
}
fn ffxRemapForWaveReduction_u0028_u1_u003b(a: ptr<function, u32>) -> vec2<u32> {
    var param_218: u32;
    var param_219: u32;
    var param_220: u32;
    var param_221: u32;
    var param_222: u32;
    var param_223: u32;
    var param_224: u32;
    var param_225: u32;
    var param_226: u32;
    var param_227: u32;
    var param_228: u32;
    var param_229: u32;
    var param_230: u32;
    var param_231: u32;
    var param_232: u32;
    let _e87 = (*a);
    param_218 = _e87;
    param_219 = 2u;
    param_220 = 3u;
    let _e88 = bitfieldExtract_u0028_u1_u003b_u1_u003b_u1_u003b((&param_218), (&param_219), (&param_220));
)FSR2",
R"FSR2(    param_221 = _e88;
    let _e89 = (*a);
    param_222 = _e89;
    param_223 = 1u;
    let _e90 = bitfieldInsertMask_u0028_u1_u003b_u1_u003b_u1_u003b((&param_221), (&param_222), (&param_223));
    let _e91 = (*a);
    param_224 = _e91;
    param_225 = 3u;
    param_226 = 3u;
    let _e92 = bitfieldExtract_u0028_u1_u003b_u1_u003b_u1_u003b((&param_224), (&param_225), (&param_226));
    let _e93 = (*a);
    param_227 = _e93;
    param_228 = 1u;
    param_229 = 2u;
    let _e94 = bitfieldExtract_u0028_u1_u003b_u1_u003b_u1_u003b((&param_227), (&param_228), (&param_229));
    param_230 = _e92;
    param_231 = _e94;
    param_232 = 2u;
    let _e95 = bitfieldInsertMask_u0028_u1_u003b_u1_u003b_u1_u003b((&param_230), (&param_231), (&param_232));
    return vec2<u32>(_e90, _e95);
}
fn SpdDownsample_u0028_vu2_u003b_u1_u003b_u1_u003b_u1_u003b_u1_u003b(workGroupID_7: ptr<function, vec2<u32>>, localInvocationIndex_8: ptr<function, u32>, mips_2: ptr<function, u32>, numWorkGroups_1: ptr<function, u32>, slice_19: ptr<function, u32>) {
    var sub_xy: vec2<u32>;
    var param_233: u32;
    var x_13: u32;
    var y_13: u32;
    var param_234: u32;
    var param_235: u32;
    var param_236: vec2<u32>;
    var param_237: u32;
    var param_238: u32;
    var param_239: u32;
    var param_240: u32;
    var param_241: u32;
    var param_242: vec2<u32>;
    var param_243: u32;
    var param_244: u32;
    var param_245: u32;
    var param_246: u32;
    var param_247: u32;
    var param_248: u32;
    var param_249: u32;
    var param_250: u32;
    var param_251: u32;
    var param_252: u32;
    var param_253: u32;
    var param_254: u32;
    var param_255: u32;
    var param_256: u32;
    var param_257: vec2<u32>;
    var param_258: u32;
    var param_259: u32;
    var param_260: u32;
    var param_261: u32;
    let _e108 = (*localInvocationIndex_8);
    param_233 = (_e108 % 64u);
    let _e110 = ffxRemapForWaveReduction_u0028_u1_u003b((&param_233));
    sub_xy = _e110;
    let _e112 = sub_xy[0u];
    let _e113 = (*localInvocationIndex_8);
    x_13 = (_e112 + (8u * ((_e113 >> bitcast<u32>(6i)) % 2u)));
    let _e120 = sub_xy[1u];
    let _e121 = (*localInvocationIndex_8);
    y_13 = (_e120 + (8u * (_e121 >> bitcast<u32>(7i))));
    let _e126 = x_13;
    param_234 = _e126;
    let _e127 = y_13;
    param_235 = _e127;
    let _e128 = (*workGroupID_7);
    param_236 = _e128;
    let _e129 = (*localInvocationIndex_8);
    param_237 = _e129;
    let _e130 = (*mips_2);
    param_238 = _e130;
    let _e131 = (*slice_19);
    param_239 = _e131;
    SpdDownsampleMips_0_1_u0028_u1_u003b_u1_u003b_vu2_u003b_u1_u003b_u1_u003b_u1_u003b((&param_234), (&param_235), (&param_236), (&param_237), (&param_238), (&param_239));
    let _e132 = x_13;
    param_240 = _e132;
    let _e133 = y_13;
    param_241 = _e133;
    let _e134 = (*workGroupID_7);
    param_242 = _e134;
    let _e135 = (*localInvocationIndex_8);
    param_243 = _e135;
    param_244 = 2u;
    let _e136 = (*mips_2);
    param_245 = _e136;
    let _e137 = (*slice_19);
    param_246 = _e137;
    SpdDownsampleNextFour_u0028_u1_u003b_u1_u003b_vu2_u003b_u1_u003b_u1_u003b_u1_u003b_u1_u003b((&param_240), (&param_241), (&param_242), (&param_243), (&param_244), (&param_245), (&param_246));
    let _e138 = (*mips_2);
    if (_e138 <= 6u) {
        return;
    }
    let _e140 = (*numWorkGroups_1);
    param_247 = _e140;
    let _e141 = (*localInvocationIndex_8);
    param_248 = _e141;
    let _e142 = (*slice_19);
    param_249 = _e142;
    let _e143 = SpdExitWorkgroup_u0028_u1_u003b_u1_u003b_u1_u003b((&param_247), (&param_248), (&param_249));
    if _e143 {
        return;
    }
    let _e144 = (*slice_19);
    param_250 = _e144;
    SpdResetAtomicCounter_u0028_u1_u003b((&param_250));
    let _e145 = x_13;
    param_251 = _e145;
    let _e146 = y_13;
    param_252 = _e146;
    let _e147 = (*mips_2);
    param_253 = _e147;
    let _e148 = (*slice_19);
    param_254 = _e148;
    SpdDownsampleMips_6_7_u0028_u1_u003b_u1_u003b_u1_u003b_u1_u003b((&param_251), (&param_252), (&param_253), (&param_254));
    let _e149 = x_13;
    param_255 = _e149;
    let _e150 = y_13;
    param_256 = _e150;
    param_257 = vec2<u32>(0u, 0u);
    let _e151 = (*localInvocationIndex_8);
    param_258 = _e151;
    param_259 = 8u;
    let _e152 = (*mips_2);
    param_260 = _e152;
    let _e153 = (*slice_19);
    param_261 = _e153;
    SpdDownsampleNextFour_u0028_u1_u003b_u1_u003b_vu2_u003b_u1_u003b_u1_u003b_u1_u003b_u1_u003b((&param_255), (&param_256), (&param_257), (&param_258), (&param_259), (&param_260), (&param_261));
    return;
}
fn SpdDownsample_u0028_vu2_u003b_u1_u003b_u1_u003b_u1_u003b_u1_u003b_vu2_u003b(workGroupID_8: ptr<function, vec2<u32>>, localInvocationIndex_9: ptr<function, u32>, mips_3: ptr<function, u32>, numWorkGroups_2: ptr<function, u32>, slice_20: ptr<function, u32>, workGroupOffset: ptr<function, vec2<u32>>) {
    var param_262: vec2<u32>;
    var param_263: u32;
    var param_264: u32;
    var param_265: u32;
    var param_266: u32;
    let _e82 = (*workGroupID_8);
    let _e83 = (*workGroupOffset);
    param_262 = (_e82 + _e83);
    let _e85 = (*localInvocationIndex_9);
    param_263 = _e85;
    let _e86 = (*mips_3);
    param_264 = _e86;
    let _e87 = (*numWorkGroups_2);
    param_265 = _e87;
    let _e88 = (*slice_20);
    param_266 = _e88;
    SpdDownsample_u0028_vu2_u003b_u1_u003b_u1_u003b_u1_u003b_u1_u003b((&param_262), (&param_263), (&param_264), (&param_265), (&param_266));
    return;
}
fn WorkGroupOffset_u0028_() -> vec2<u32> {
    let _e72 = cbSPD.workGroupOffset;
    return _e72;
}
fn NumWorkGroups_u0028_() -> u32 {
    let _e72 = cbSPD.numWorkGroups;
    return _e72;
}
fn ComputeAutoExposure_u0028_vu3_u003b_u1_u003b(WorkGroupId: ptr<function, vec3<u32>>, LocalThreadIndex: ptr<function, u32>) {
    var param_267: vec2<u32>;
    var param_268: u32;
    var param_269: u32;
    var param_270: u32;
    var param_271: u32;
    var param_272: vec2<u32>;
    let _e79 = (*WorkGroupId);
    let _e81 = (*LocalThreadIndex);
    let _e82 = MipCount_u0028_();
    let _e83 = NumWorkGroups_u0028_();
    let _e85 = (*WorkGroupId)[2u];
    let _e86 = WorkGroupOffset_u0028_();
    param_267 = _e79.xy;
    param_268 = _e81;
    param_269 = _e82;
    param_270 = _e83;
    param_271 = _e85;
    param_272 = _e86;
    SpdDownsample_u0028_vu2_u003b_u1_u003b_u1_u003b_u1_u003b_u1_u003b_vu2_u003b((&param_267), (&param_268), (&param_269), (&param_270), (&param_271), (&param_272));
    return;
}
fn main_1() {
    var param_273: vec3<u32>;
    var param_274: u32;
    let _e73 = gl_WorkGroupID_1;
    param_273 = _e73;
    let _e74 = gl_LocalInvocationIndex_1;
    param_274 = _e74;
    ComputeAutoExposure_u0028_vu3_u003b_u1_u003b((&param_273), (&param_274));
    return;
}
@compute @workgroup_size(256, 1, 1)
fn main(@builtin(workgroup_id) gl_WorkGroupID: vec3<u32>, @builtin(local_invocation_index) gl_LocalInvocationIndex: u32) {
    gl_WorkGroupID_1 = gl_WorkGroupID;
    gl_LocalInvocationIndex_1 = gl_LocalInvocationIndex;
    main_1();
}

)FSR2",
};
inline const Binding compute_luminance_pyramid_bindings[] = {
    {5, kUniform, "cbFSR2_", "cbFSR2_", false},
    {0, kTexture, "r_input_color_jittered", "", false},
    {6, kUniform, "cbSPD", "cbSPD", false},
    {4, kStorageTexture, "rw_auto_exposure", "rg32float", true},
    {3, kStorageTexture, "rw_img_mip_5", "r16float", true},
    {2, kStorageTexture, "rw_img_mip_shading_change", "r16float", true},
    {1, kStorageRW, "SpdAtomic", "SpdAtomic_1", true},
};
inline const char* const reconstruct_previous_depth_parts[] = {
R"FSR2(struct BilinearSamplingData {
    iOffsets: array<vec2<i32>, 4>,
    fWeights: array<f32, 4>,
    iBasePos: vec2<i32>,
}
struct cbFSR2_t {
    iRenderSize: vec2<i32>,
    iMaxRenderSize: vec2<i32>,
    iDisplaySize: vec2<i32>,
    iInputColorResourceDimensions: vec2<i32>,
    iLumaMipDimensions: vec2<i32>,
    iLumaMipLevelToUse: i32,
    iFrameIndex: i32,
    fDeviceToViewDepth: vec4<f32>,
    fJitter: vec2<f32>,
    fMotionVectorScale: vec2<f32>,
    fDownscaleFactor: vec2<f32>,
    fMotionVectorJitterCancellation: vec2<f32>,
    fPreExposure: f32,
    fPreviousFramePreExposure: f32,
    fTanHalfFOV: f32,
    fJitterSequenceLength: f32,
    fDeltaTime: f32,
    fDynamicResChangeFactor: f32,
    fViewSpaceToMetersFactor: f32,
}
struct RecPrevDepthW {
    rw_reconstructed_previous_nearest_depth: array<u32>,
}
struct RecPrevDepthW_1 {
    rw_reconstructed_previous_nearest_depth: array<atomic<u32>>,
}
@group(1) @binding(12)
var<uniform> cbFSR2_: cbFSR2_t;
@group(1) @binding(1)
var r_input_depth: texture_2d<f32>;
@group(1) @binding(2)
var r_input_color_jittered: texture_2d<f32>;
@group(1) @binding(0)
var r_input_motion_vectors: texture_2d<f32>;
@group(1) @binding(11)
var rw_lock_input_luma: texture_storage_2d<r16float,write>;
@group(1) @binding(5)
var<storage, read_write> unnamed: RecPrevDepthW_1;
@group(1) @binding(7)
var rw_dilatedDepth: texture_storage_2d<r16float,write>;
@group(1) @binding(6)
var rw_dilated_motion_vectors: texture_storage_2d<rg16float,write>;
@group(1) @binding(3)
var r_input_exposure: texture_2d<f32>;
var<private> gl_GlobalInvocationID_1: vec3<u32>;
@group(0) @binding(0)
var s_PointClamp: sampler;
@group(0) @binding(1)
var s_LinearClamp: sampler;
@group(1) @binding(4)
var r_luma_history: texture_2d<f32>;
@group(1) @binding(8)
var rw_prepared_input_color: texture_storage_2d<rgba16float,write>;
@group(1) @binding(9)
var rw_luma_history: texture_storage_2d<rgba8unorm,read_write>;
fn StoreLockInputLuma_u0028_vi2_u003b_f1_u003b(iPxPos: ptr<function, vec2<i32>>, fLuma: ptr<function, f32>) {
    let _e69 = (*iPxPos);
    let _e70 = (*fLuma);
    textureStore(rw_lock_input_luma, _e69, vec4<f32>(_e70, 0f, 0f, 0f));
    return;
}
fn ffxPow_u0028_f1_u003b_f1_u003b(x: ptr<function, f32>, y: ptr<function, f32>) -> f32 {
    let _e69 = (*x);
    let _e70 = (*y);
    return pow(_e69, _e70);
}
fn RGBToLuma_u0028_vf3_u003b(fLinearRgb: ptr<function, vec3<f32>>) -> f32 {
    let _e68 = (*fLinearRgb);
    return dot(_e68, vec3<f32>(0.2126f, 0.7152f, 0.0722f));
}
fn RGBToPerceivedLuma_u0028_vf3_u003b(fLinearRgb_1: ptr<function, vec3<f32>>) -> f32 {
    var fLuminance: f32;
    var param: vec3<f32>;
    var fPercievedLuminance: f32;
    var param_1: f32;
    var param_2: f32;
    let _e73 = (*fLinearRgb_1);
    param = _e73;
    let _e74 = RGBToLuma_u0028_vf3_u003b((&param));
    fLuminance = _e74;
    fPercievedLuminance = 0f;
    let _e75 = fLuminance;
    if (_e75 <= 0.008856452f) {
        let _e77 = fLuminance;
        fPercievedLuminance = (_e77 * 903.2963f);
    } else {
        let _e79 = fLuminance;
        param_1 = _e79;
        param_2 = 0.33333334f;
        let _e80 = ffxPow_u0028_f1_u003b_f1_u003b((&param_1), (&param_2));
        fPercievedLuminance = ((_e80 * 116f) - 16f);
    }
    let _e83 = fPercievedLuminance;
    return (_e83 * 0.01f);
}
fn ffxMax_u0028_f1_u003b_f1_u003b(x_1: ptr<function, f32>, y_1: ptr<function, f32>) -> f32 {
    let _e69 = (*x_1);
    let _e70 = (*y_1);
    return max(_e69, _e70);
}
fn Tonemap_u0028_vf3_u003b(fRgb: ptr<function, vec3<f32>>) -> vec3<f32> {
    var param_3: f32;
    var param_4: f32;
    var param_5: f32;
    var param_6: f32;
    var param_7: f32;
    var param_8: f32;
    let _e74 = (*fRgb);
    param_3 = 0f;
    let _e76 = (*fRgb)[0u];
    param_4 = _e76;
    let _e77 = ffxMax_u0028_f1_u003b_f1_u003b((&param_3), (&param_4));
    let _e79 = (*fRgb)[1u];
    param_5 = _e79;
    let _e81 = (*fRgb)[2u];
    param_6 = _e81;
    let _e82 = ffxMax_u0028_f1_u003b_f1_u003b((&param_5), (&param_6));
    param_7 = _e77;
    param_8 = _e82;
    let _e83 = ffxMax_u0028_f1_u003b_f1_u003b((&param_7), (&param_8));
    return (_e74 / vec3((_e83 + 1f)));
}
fn Exposure_u0028_() -> f32 {
    var exposure: f32;
    let _e68 = textureLoad(r_input_exposure, vec2<i32>(0i, 0i), 0i);
    exposure = _e68.x;
    let _e70 = exposure;
    if (_e70 == 0f) {
        exposure = 1f;
    }
    let _e72 = exposure;
    return _e72;
}
fn PreExposure_u0028_() -> f32 {
    let _e68 = cbFSR2_.fPreExposure;
    return _e68;
}
fn ffxMax_u0028_vf3_u003b_vf3_u003b(x_2: ptr<function, vec3<f32>>, y_2: ptr<function, vec3<f32>>) -> vec3<f32> {
    let _e69 = (*x_2);
    let _e70 = (*y_2);
    return max(_e69, _e70);
}
fn LoadInputColor_u0028_vi2_u003b(iPxPos_1: ptr<function, vec2<i32>>) -> vec3<f32> {
    let _e68 = (*iPxPos_1);
    let _e69 = textureLoad(r_input_color_jittered, _e68, 0i);
    return _e69.xyz;
}
fn ComputeLockInputLuma_u0028_vi2_u003b(iPxLrPos: ptr<function, vec2<i32>>) -> f32 {
    var fRgb_1: vec3<f32>;
    var param_9: vec2<i32>;
    var param_10: vec3<f32>;
    var param_11: vec3<f32>;
    var param_12: vec3<f32>;
    var fLockInputLuma: f32;
    var param_13: vec3<f32>;
    var param_14: f32;
    var param_15: f32;
    let _e77 = (*iPxLrPos);
    param_9 = _e77;
    let _e78 = LoadInputColor_u0028_vi2_u003b((&param_9));
    param_10 = vec3<f32>(0f, 0f, 0f);
    param_11 = _e78;
    let _e79 = ffxMax_u0028_vf3_u003b_vf3_u003b((&param_10), (&param_11));
    fRgb_1 = _e79;
    let _e80 = PreExposure_u0028_();
    let _e81 = fRgb_1;
    fRgb_1 = (_e81 / vec3(_e80));
    let _e84 = Exposure_u0028_();
    let _e85 = fRgb_1;
    fRgb_1 = (_e85 * _e84);
    let _e87 = fRgb_1;
    param_12 = _e87;
    let _e88 = Tonemap_u0028_vf3_u003b((&param_12));
    fRgb_1 = _e88;
    let _e89 = fRgb_1;
    param_13 = _e89;
    let _e90 = RGBToPerceivedLuma_u0028_vf3_u003b((&param_13));
    param_14 = _e90;
    param_15 = 0.16666667f;
    let _e91 = ffxPow_u0028_f1_u003b_f1_u003b((&param_14), (&param_15));
    fLockInputLuma = _e91;
    let _e92 = fLockInputLuma;
    return _e92;
}
fn StoreReconstructedDepth_u0028_vi2_u003b_f1_u003b(iPxSample: ptr<function, vec2<i32>>, fDepth: ptr<function, f32>) {
    var uDepth: u32;
    let _e70 = (*fDepth);
    uDepth = bitcast<u32>(_e70);
    let _e73 = (*iPxSample)[1u];
    let _e77 = cbFSR2_.iMaxRenderSize[0u];
    let _e81 = (*iPxSample)[0u];
    let _e86 = uDepth;
    let _e87 = atomicMin((&unnamed.rw_reconstructed_previous_nearest_depth[((bitcast<u32>(_e73) * bitcast<u32>(_e77)) + bitcast<u32>(_e81))]), _e86);
    return;
}
fn IsOnScreen_u0028_vi2_u003b_vi2_u003b(pos: ptr<function, vec2<i32>>, size: ptr<function, vec2<i32>>) -> bool {
    let _e69 = (*pos);
    let _e71 = (*size);
    return all((bitcast<vec2<u32>>(_e69) < bitcast<vec2<u32>>(_e71)));
}
fn ffxFract_u0028_vf2_u003b(x_3: ptr<function, vec2<f32>>) -> vec2<f32> {
    let _e68 = (*x_3);
    return fract(_e68);
}
fn GetBilinearSamplingData_u0028_vf2_u003b_vi2_u003b(fUv: ptr<function, vec2<f32>>, iSize: ptr<function, vec2<i32>>) -> BilinearSamplingData {
    var fPxSample: vec2<f32>;
    var data: BilinearSamplingData;
    var fPxFrac: vec2<f32>;
    var param_16: vec2<f32>;
    let _e73 = (*fUv);
    let _e74 = (*iSize);
    fPxSample = ((_e73 * vec2<f32>(_e74)) - vec2<f32>(0.5f, 0.5f));
    let _e78 = fPxSample;
    data.iBasePos = vec2<i32>(floor(_e78));
    let _e82 = fPxSample;
    param_16 = _e82;
    let _e83 = ffxFract_u0028_vf2_u003b((&param_16));
    fPxFrac = _e83;
    data.iOffsets[0i] = vec2<i32>(0i, 0i);
    data.iOffsets[1i] = vec2<i32>(1i, 0i);
    data.iOffsets[2i] = vec2<i32>(0i, 1i);
    data.iOffsets[3i] = vec2<i32>(1i, 1i);
    let _e93 = fPxFrac[0u];
    let _e96 = fPxFrac[1u];
    data.fWeights[0i] = ((1f - _e93) * (1f - _e96));
    let _e102 = fPxFrac[0u];
    let _e104 = fPxFrac[1u];
    data.fWeights[1i] = (_e102 * (1f - _e104));
    let _e110 = fPxFrac[0u];
    let _e113 = fPxFrac[1u];
    data.fWeights[2i] = ((1f - _e110) * _e113);
    let _e118 = fPxFrac[0u];
    let _e120 = fPxFrac[1u];
    data.fWeights[3i] = (_e118 * _e120);
    let _e124 = data;
    return _e124;
}
fn RenderSize_u0028_() -> vec2<i32> {
    let _e68 = cbFSR2_.iRenderSize;
    return _e68;
}
fn DisplaySize_u0028_() -> vec2<i32> {
    let _e68 = cbFSR2_.iDisplaySize;
    return _e68;
}
fn ReconstructPrevDepth_u0028_vi2_u003b_f1_u003b_vf2_u003b_vi2_u003b(iPxPos_2: ptr<function, vec2<i32>>, fDepth_1: ptr<function, f32>, fMotionVector: ptr<function, vec2<f32>>, iPxDepthSize: ptr<function, vec2<i32>>) {
    var fUv_1: vec2<f32>;
    var fReprojectedUv: vec2<f32>;
    var bilinearInfo: BilinearSamplingData;
    var param_17: vec2<f32>;
    var param_18: vec2<i32>;
    var iSampleIndex: i32;
    var iOffset: vec2<i32>;
    var fWeight: f32;
    var iStorePos: vec2<i32>;
    var param_19: vec2<i32>;
    var param_20: vec2<i32>;
    var param_21: vec2<i32>;
    var param_22: f32;
    let _e84 = (*fMotionVector);
    let _e85 = DisplaySize_u0028_();
    let _e91 = (*fMotionVector);
    (*fMotionVector) = (_e91 * select(0f, 1f, (length((_e84 * vec2<f32>(_e85))) > 0.1f)));
    let _e93 = (*iPxPos_2);
    let _e97 = (*iPxDepthSize);
    fUv_1 = ((vec2<f32>(_e93) + vec2(0.5f)) / vec2<f32>(_e97));
    let _e100 = fUv_1;
    let _e101 = (*fMotionVector);
    fReprojectedUv = (_e100 + _e101);
    let _e103 = RenderSize_u0028_();
    let _e104 = fReprojectedUv;
    param_17 = _e104;
    param_18 = _e103;
    let _e105 = GetBilinearSamplingData_u0028_vf2_u003b_vi2_u003b((&param_17), (&param_18));
    bilinearInfo = _e105;
    iSampleIndex = 0i;
    loop {
        let _e106 = iSampleIndex;
        if (_e106 < 4i) {
            let _e108 = iSampleIndex;
            let _e111 = bilinearInfo.iOffsets[_e108];
            iOffset = _e111;
            let _e112 = iSampleIndex;
            let _e115 = bilinearInfo.fWeights[_e112];
            fWeight = _e115;
            let _e116 = fWeight;
            if (_e116 > 0.01f) {
                let _e119 = bilinearInfo.iBasePos;
                let _e120 = iOffset;
                iStorePos = (_e119 + _e120);
                let _e122 = iStorePos;
                param_19 = _e122;
                let _e123 = (*iPxDepthSize);
                param_20 = _e123;
                let _e124 = IsOnScreen_u0028_vi2_u003b_vi2_u003b((&param_19), (&param_20));
                if _e124 {
                    let _e125 = iStorePos;
                    param_21 = _e125;
                    let _e126 = (*fDepth_1);
                    param_22 = _e126;
                    StoreReconstructedDepth_u0028_vi2_u003b_f1_u003b((&param_21), (&param_22));
                }
            }
            continue;
        } else {
            break;
        }
        continuing {
            let _e127 = iSampleIndex;
            iSampleIndex = (_e127 + 1i);
        }
    }
    return;
}
fn StoreDilatedMotionVector_u0028_vi2_u003b_vf2_u003b(iPxPos_3: ptr<function, vec2<i32>>, fMotionVector_1: ptr<function, vec2<f32>>) {
    let _e69 = (*iPxPos_3);
    let _e70 = (*fMotionVector_1);
    textureStore(rw_dilated_motion_vectors, _e69, vec4<f32>(_e70.x, _e70.y, 0f, 0f));
    return;
}
fn StoreDilatedDepth_u0028_vi2_u003b_f1_u003b(iPxPos_4: ptr<function, vec2<i32>>, fDepth_2: ptr<function, f32>) {
    let _e69 = (*iPxPos_4);
    let _e70 = (*fDepth_2);
    textureStore(rw_dilatedDepth, _e69, vec4<f32>(_e70, 0f, 0f, 0f));
    return;
}
fn MotionVectorScale_u0028_() -> vec2<f32> {
    let _e68 = cbFSR2_.fMotionVectorScale;
    return _e68;
}
fn LoadInputMotionVector_u0028_vi2_u003b(iPxDilatedMotionVectorPos: ptr<function, vec2<i32>>) -> vec2<f32> {
    var fSrcMotionVector: vec2<f32>;
    var fUvMotionVector: vec2<f32>;
    let _e70 = (*iPxDilatedMotionVectorPos);
    let _e71 = textureLoad(r_input_motion_vectors, _e70, 0i);
    fSrcMotionVector = _e71.xy;
    let _e73 = fSrcMotionVector;
    let _e74 = MotionVectorScale_u0028_();
    fUvMotionVector = (_e73 * _e74);
)FSR2",
R"FSR2(    let _e76 = fUvMotionVector;
    return _e76;
}
fn LoadInputDepth_u0028_vi2_u003b(iPxPos_5: ptr<function, vec2<i32>>) -> f32 {
    let _e68 = (*iPxPos_5);
    let _e69 = textureLoad(r_input_depth, _e68, 0i);
    return _e69.x;
}
fn FindNearestDepth_u0028_vi2_u003b_vi2_u003b_f1_u003b_vi2_u003b(iPxPos_6: ptr<function, vec2<i32>>, iPxSize: ptr<function, vec2<i32>>, fNearestDepth: ptr<function, f32>, fNearestDepthCoord: ptr<function, vec2<i32>>) {
    var iSampleIndex_1: i32;
    var iPos: vec2<i32>;
    var indexable: array<vec2<i32>, 9>;
    var depth: array<f32, 9>;
    var param_23: vec2<i32>;
    var iPos_1: vec2<i32>;
    var indexable_1: array<vec2<i32>, 9>;
    var param_24: vec2<i32>;
    var param_25: vec2<i32>;
    var fNdDepth: f32;
    iSampleIndex_1 = 0i;
    iSampleIndex_1 = 0i;
    loop {
        let _e81 = iSampleIndex_1;
        if (_e81 < 9i) {
            let _e83 = (*iPxPos_6);
            let _e84 = iSampleIndex_1;
            indexable = array<vec2<i32>, 9>(vec2<i32>(0i, 0i), vec2<i32>(1i, 0i), vec2<i32>(0i, 1i), vec2<i32>(0i, -1i), vec2<i32>(-1i, 0i), vec2<i32>(-1i, 1i), vec2<i32>(1i, 1i), vec2<i32>(-1i, -1i), vec2<i32>(1i, -1i));
            let _e86 = indexable[_e84];
            iPos = (_e83 + _e86);
            let _e88 = iSampleIndex_1;
            let _e89 = iPos;
            param_23 = _e89;
            let _e90 = LoadInputDepth_u0028_vi2_u003b((&param_23));
            depth[_e88] = _e90;
            continue;
        } else {
            break;
        }
        continuing {
            let _e92 = iSampleIndex_1;
            iSampleIndex_1 = (_e92 + 1i);
        }
    }
    let _e94 = (*iPxPos_6);
    (*fNearestDepthCoord) = _e94;
    let _e96 = depth[0i];
    (*fNearestDepth) = _e96;
    iSampleIndex_1 = 1i;
    loop {
        let _e97 = iSampleIndex_1;
        if (_e97 < 9i) {
            let _e99 = (*iPxPos_6);
            let _e100 = iSampleIndex_1;
            indexable_1 = array<vec2<i32>, 9>(vec2<i32>(0i, 0i), vec2<i32>(1i, 0i), vec2<i32>(0i, 1i), vec2<i32>(0i, -1i), vec2<i32>(-1i, 0i), vec2<i32>(-1i, 1i), vec2<i32>(1i, 1i), vec2<i32>(-1i, -1i), vec2<i32>(1i, -1i));
            let _e102 = indexable_1[_e100];
            iPos_1 = (_e99 + _e102);
            let _e104 = iPos_1;
            param_24 = _e104;
            let _e105 = (*iPxSize);
            param_25 = _e105;
            let _e106 = IsOnScreen_u0028_vi2_u003b_vi2_u003b((&param_24), (&param_25));
            if _e106 {
                let _e107 = iSampleIndex_1;
                let _e109 = depth[_e107];
                fNdDepth = _e109;
                let _e110 = fNdDepth;
                let _e111 = (*fNearestDepth);
                if (_e110 < _e111) {
                    let _e113 = iPos_1;
                    (*fNearestDepthCoord) = _e113;
                    let _e114 = fNdDepth;
                    (*fNearestDepth) = _e114;
                }
            }
            continue;
        } else {
            break;
        }
        continuing {
            let _e115 = iSampleIndex_1;
            iSampleIndex_1 = (_e115 + 1i);
        }
    }
    return;
}
fn ReconstructAndDilate_u0028_vi2_u003b(iPxLrPos_1: ptr<function, vec2<i32>>) {
    var fDilatedDepth: f32;
    var iNearestDepthCoord: vec2<i32>;
    var param_26: vec2<i32>;
    var param_27: vec2<i32>;
    var param_28: f32;
    var param_29: vec2<i32>;
    var iSamplePos: vec2<i32>;
    var iMotionVectorPos: vec2<i32>;
    var fDilatedMotionVector: vec2<f32>;
    var param_30: vec2<i32>;
    var param_31: vec2<i32>;
    var param_32: f32;
    var param_33: vec2<i32>;
    var param_34: vec2<f32>;
    var param_35: vec2<i32>;
    var param_36: f32;
    var param_37: vec2<f32>;
    var param_38: vec2<i32>;
    var fLockInputLuma_1: f32;
    var param_39: vec2<i32>;
    var param_40: vec2<i32>;
    var param_41: f32;
    let _e90 = RenderSize_u0028_();
    let _e91 = (*iPxLrPos_1);
    param_26 = _e91;
    param_27 = _e90;
    FindNearestDepth_u0028_vi2_u003b_vi2_u003b_f1_u003b_vi2_u003b((&param_26), (&param_27), (&param_28), (&param_29));
    let _e92 = param_28;
    fDilatedDepth = _e92;
    let _e93 = param_29;
    iNearestDepthCoord = _e93;
    let _e94 = (*iPxLrPos_1);
    iSamplePos = _e94;
    let _e95 = iNearestDepthCoord;
    iMotionVectorPos = _e95;
    let _e96 = iMotionVectorPos;
    param_30 = _e96;
    let _e97 = LoadInputMotionVector_u0028_vi2_u003b((&param_30));
    fDilatedMotionVector = _e97;
    let _e98 = (*iPxLrPos_1);
    param_31 = _e98;
    let _e99 = fDilatedDepth;
    param_32 = _e99;
    StoreDilatedDepth_u0028_vi2_u003b_f1_u003b((&param_31), (&param_32));
    let _e100 = (*iPxLrPos_1);
    param_33 = _e100;
    let _e101 = fDilatedMotionVector;
    param_34 = _e101;
    StoreDilatedMotionVector_u0028_vi2_u003b_vf2_u003b((&param_33), (&param_34));
    let _e102 = RenderSize_u0028_();
    let _e103 = (*iPxLrPos_1);
    param_35 = _e103;
    let _e104 = fDilatedDepth;
    param_36 = _e104;
    let _e105 = fDilatedMotionVector;
    param_37 = _e105;
    param_38 = _e102;
    ReconstructPrevDepth_u0028_vi2_u003b_f1_u003b_vf2_u003b_vi2_u003b((&param_35), (&param_36), (&param_37), (&param_38));
    let _e106 = (*iPxLrPos_1);
    param_39 = _e106;
    let _e107 = ComputeLockInputLuma_u0028_vi2_u003b((&param_39));
    fLockInputLuma_1 = _e107;
    let _e108 = (*iPxLrPos_1);
    param_40 = _e108;
    let _e109 = fLockInputLuma_1;
    param_41 = _e109;
    StoreLockInputLuma_u0028_vi2_u003b_f1_u003b((&param_40), (&param_41));
    return;
}
fn main_1() {
    var param_42: vec2<i32>;
    let _e68 = gl_GlobalInvocationID_1;
    param_42 = bitcast<vec2<i32>>(_e68.xy);
    ReconstructAndDilate_u0028_vi2_u003b((&param_42));
    return;
}
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gl_GlobalInvocationID: vec3<u32>) {
    gl_GlobalInvocationID_1 = gl_GlobalInvocationID;
    main_1();
}

)FSR2",
};
inline const Binding reconstruct_previous_depth_bindings[] = {
    {12, kUniform, "cbFSR2_", "cbFSR2_", false},
    {1, kTexture, "r_input_depth", "", false},
    {2, kTexture, "r_input_color_jittered", "", false},
    {0, kTexture, "r_input_motion_vectors", "", false},
    {11, kStorageTexture, "rw_lock_input_luma", "r16float", false},
    {5, kStorageRW, "RecPrevDepthW", "RecPrevDepthW_1", true},
    {7, kStorageTexture, "rw_dilatedDepth", "r16float", false},
    {6, kStorageTexture, "rw_dilated_motion_vectors", "rg16float", false},
    {3, kTexture, "r_input_exposure", "", false},
    {4, kTexture, "r_luma_history", "", false},
    {8, kStorageTexture, "rw_prepared_input_color", "rgba16float", false},
    {9, kStorageTexture, "rw_luma_history", "rgba8unorm", true},
};
inline const char* const depth_clip_parts[] = {
R"FSR2(struct BilinearSamplingData {
    iOffsets: array<vec2<i32>, 4>,
    fWeights: array<f32, 4>,
    iBasePos: vec2<i32>,
}
struct cbFSR2_t {
    iRenderSize: vec2<i32>,
    iMaxRenderSize: vec2<i32>,
    iDisplaySize: vec2<i32>,
    iInputColorResourceDimensions: vec2<i32>,
    iLumaMipDimensions: vec2<i32>,
    iLumaMipLevelToUse: i32,
    iFrameIndex: i32,
    fDeviceToViewDepth: vec4<f32>,
    fJitter: vec2<f32>,
    fMotionVectorScale: vec2<f32>,
    fDownscaleFactor: vec2<f32>,
    fMotionVectorJitterCancellation: vec2<f32>,
    fPreExposure: f32,
    fPreviousFramePreExposure: f32,
    fTanHalfFOV: f32,
    fJitterSequenceLength: f32,
    fDeltaTime: f32,
    fDynamicResChangeFactor: f32,
    fViewSpaceToMetersFactor: f32,
}
struct RecPrevDepthR {
    r_reconstructed_previous_nearest_depth: array<u32>,
}
@group(1) @binding(14)
var<uniform> cbFSR2_: cbFSR2_t;
@group(1) @binding(9)
var r_input_depth: texture_2d<f32>;
@group(1) @binding(3)
var r_reactive_mask: texture_2d<f32>;
@group(1) @binding(4)
var r_transparency_and_composition_mask: texture_2d<f32>;
@group(1) @binding(8)
var r_input_color_jittered: texture_2d<f32>;
@group(1) @binding(7)
var r_input_motion_vectors: texture_2d<f32>;
@group(1) @binding(13)
var rw_prepared_input_color: texture_storage_2d<rgba16float,write>;
@group(1) @binding(0)
var<storage> unnamed: RecPrevDepthR;
@group(1) @binding(1)
var r_dilated_motion_vectors: texture_2d<f32>;
@group(1) @binding(6)
var r_previous_dilated_motion_vectors: texture_2d<f32>;
@group(0) @binding(1)
var s_LinearClamp: sampler;
@group(1) @binding(2)
var r_dilatedDepth: texture_2d<f32>;
@group(1) @binding(10)
var r_input_exposure: texture_2d<f32>;
@group(1) @binding(12)
var rw_dilated_reactive_masks: texture_storage_2d<rg8unorm,write>;
var<private> gl_GlobalInvocationID_1: vec3<u32>;
@group(0) @binding(0)
var s_PointClamp: sampler;
@group(1) @binding(5)
var r_prepared_input_color: texture_2d<f32>;
fn StoreDilatedReactiveMasks_u0028_vi2_u003b_vf2_u003b(iPxPos: ptr<function, vec2<i32>>, fDilatedReactiveMasks: ptr<function, vec2<f32>>) {
    let _e70 = (*iPxPos);
    let _e71 = (*fDilatedReactiveMasks);
    textureStore(rw_dilated_reactive_masks, _e70, vec4<f32>(_e71.x, _e71.y, 0f, 0f));
    return;
}
fn ffxMax_u0028_vf2_u003b_vf2_u003b(x: ptr<function, vec2<f32>>, y: ptr<function, vec2<f32>>) -> vec2<f32> {
    let _e70 = (*x);
    let _e71 = (*y);
    return max(_e70, _e71);
}
fn ffxPow_u0028_f1_u003b_f1_u003b(x_1: ptr<function, f32>, y_1: ptr<function, f32>) -> f32 {
    let _e70 = (*x_1);
    let _e71 = (*y_1);
    return pow(_e70, _e71);
}
fn ffxMax_u0028_f1_u003b_f1_u003b(x_2: ptr<function, f32>, y_2: ptr<function, f32>) -> f32 {
    let _e70 = (*x_2);
    let _e71 = (*y_2);
    return max(_e70, _e71);
}
fn LoadTransparencyAndCompositionMask_u0028_vu2_u003b(iPxPos_1: ptr<function, vec2<u32>>) -> f32 {
    let _e69 = (*iPxPos_1);
    let _e71 = textureLoad(r_transparency_and_composition_mask, bitcast<vec2<i32>>(_e69), 0i);
    return _e71.x;
}
fn LoadReactiveMask_u0028_vi2_u003b(iPxPos_2: ptr<function, vec2<i32>>) -> f32 {
    let _e69 = (*iPxPos_2);
    let _e70 = textureLoad(r_reactive_mask, _e69, 0i);
    return _e70.x;
}
fn ffxMin_u0028_i1_u003b_i1_u003b(x_3: ptr<function, i32>, y_3: ptr<function, i32>) -> i32 {
    let _e70 = (*x_3);
    let _e71 = (*y_3);
    return min(_e70, _e71);
}
fn ffxMax_u0028_i1_u003b_i1_u003b(x_4: ptr<function, i32>, y_4: ptr<function, i32>) -> i32 {
    let _e70 = (*x_4);
    let _e71 = (*y_4);
    return max(_e70, _e71);
}
fn ClampLoad_u0028_vi2_u003b_vi2_u003b_vi2_u003b(iPxSample: ptr<function, vec2<i32>>, iPxOffset: ptr<function, vec2<i32>>, iTextureSize: ptr<function, vec2<i32>>) -> vec2<i32> {
    var result: vec2<i32>;
    var local: i32;
    var param: i32;
    var param_1: i32;
    var local_1: i32;
    var param_2: i32;
    var param_3: i32;
    var local_2: i32;
    var param_4: i32;
    var param_5: i32;
    var local_3: i32;
    var param_6: i32;
    var param_7: i32;
    let _e84 = (*iPxSample);
    let _e85 = (*iPxOffset);
    result = (_e84 + _e85);
    let _e88 = (*iPxOffset)[0u];
    if (_e88 < 0i) {
        let _e91 = result[0u];
        param = _e91;
        param_1 = 0i;
        let _e92 = ffxMax_u0028_i1_u003b_i1_u003b((&param), (&param_1));
        local = _e92;
    } else {
        let _e94 = result[0u];
        local = _e94;
    }
    let _e95 = local;
    result[0u] = _e95;
    let _e98 = (*iPxOffset)[0u];
    if (_e98 > 0i) {
        let _e101 = (*iTextureSize)[0u];
        let _e104 = result[0u];
        param_2 = _e104;
        param_3 = (_e101 - 1i);
        let _e105 = ffxMin_u0028_i1_u003b_i1_u003b((&param_2), (&param_3));
        local_1 = _e105;
    } else {
        let _e107 = result[0u];
        local_1 = _e107;
    }
    let _e108 = local_1;
    result[0u] = _e108;
    let _e111 = (*iPxOffset)[1u];
    if (_e111 < 0i) {
        let _e114 = result[1u];
        param_4 = _e114;
        param_5 = 0i;
        let _e115 = ffxMax_u0028_i1_u003b_i1_u003b((&param_4), (&param_5));
        local_2 = _e115;
    } else {
        let _e117 = result[1u];
        local_2 = _e117;
    }
    let _e118 = local_2;
    result[1u] = _e118;
    let _e121 = (*iPxOffset)[1u];
    if (_e121 > 0i) {
        let _e124 = (*iTextureSize)[1u];
        let _e127 = result[1u];
        param_6 = _e127;
        param_7 = (_e124 - 1i);
        let _e128 = ffxMin_u0028_i1_u003b_i1_u003b((&param_6), (&param_7));
        local_3 = _e128;
    } else {
        let _e130 = result[1u];
        local_3 = _e130;
    }
    let _e131 = local_3;
    result[1u] = _e131;
    let _e133 = result;
    return _e133;
}
fn RenderSize_u0028_() -> vec2<i32> {
    let _e69 = cbFSR2_.iRenderSize;
    return _e69;
}
fn LoadInputColor_u0028_vi2_u003b(iPxPos_3: ptr<function, vec2<i32>>) -> vec3<f32> {
    let _e69 = (*iPxPos_3);
    let _e70 = textureLoad(r_input_color_jittered, _e69, 0i);
    return _e70.xyz;
}
fn PreProcessReactiveMasks_u0028_vi2_u003b_f1_u003b(iPxLrPos: ptr<function, vec2<i32>>, fMotionDivergence: ptr<function, f32>) {
    var fReferenceColor: vec3<f32>;
    var param_8: vec2<i32>;
    var fReactiveFactor: vec2<f32>;
    var fMasksSum: f32;
    var y_5: i32;
    var x_5: i32;
    var sampleCoord: vec2<i32>;
    var param_9: vec2<i32>;
    var param_10: vec2<i32>;
    var param_11: vec2<i32>;
    var sampleIdx: i32;
    var fColorSample: vec3<f32>;
    var param_12: vec2<i32>;
    var fReactiveSample: f32;
    var param_13: vec2<i32>;
    var fTransparencyAndCompositionSample: f32;
    var param_14: vec2<u32>;
    var fColorSamples: array<vec3<f32>, 9>;
    var fReactiveSamples: array<f32, 9>;
    var fTransparencyAndCompositionSamples: array<f32, 9>;
    var sampleIdx_1: i32;
    var fColorSample_1: vec3<f32>;
    var fReactiveSample_1: f32;
    var fTransparencyAndCompositionSample_1: f32;
    var fMaxLenSq: f32;
    var param_15: f32;
    var param_16: f32;
    var fSimilarity: f32;
    var fSimilarityPower: f32;
    var fWeightedReactiveSample: f32;
    var param_17: f32;
    var param_18: f32;
    var fWeightedTransparencyAndCompositionSample: f32;
    var param_19: f32;
    var param_20: f32;
    var param_21: vec2<f32>;
    var param_22: vec2<f32>;
    var param_23: vec2<i32>;
    var param_24: vec2<f32>;
    let _e109 = (*iPxLrPos);
    param_8 = _e109;
    let _e110 = LoadInputColor_u0028_vi2_u003b((&param_8));
    fReferenceColor = _e110;
    let _e111 = (*fMotionDivergence);
    fReactiveFactor = vec2<f32>(0f, _e111);
    fMasksSum = 0f;
    y_5 = -1i;
    loop {
        let _e113 = y_5;
        if (_e113 < 2i) {
            x_5 = -1i;
            loop {
                let _e115 = x_5;
                if (_e115 < 2i) {
                    let _e117 = x_5;
                    let _e118 = y_5;
                    let _e120 = RenderSize_u0028_();
                    let _e121 = (*iPxLrPos);
                    param_9 = _e121;
                    param_10 = vec2<i32>(_e117, _e118);
                    param_11 = _e120;
                    let _e122 = ClampLoad_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_9), (&param_10), (&param_11));
                    sampleCoord = _e122;
                    let _e123 = y_5;
                    let _e126 = x_5;
                    sampleIdx = ((((_e123 + 1i) * 3i) + _e126) + 1i);
                    let _e129 = sampleCoord;
                    param_12 = _e129;
                    let _e130 = LoadInputColor_u0028_vi2_u003b((&param_12));
                    fColorSample = _e130;
                    let _e131 = sampleCoord;
                    param_13 = _e131;
                    let _e132 = LoadReactiveMask_u0028_vi2_u003b((&param_13));
                    fReactiveSample = _e132;
                    let _e133 = sampleCoord;
                    param_14 = bitcast<vec2<u32>>(_e133);
                    let _e135 = LoadTransparencyAndCompositionMask_u0028_vu2_u003b((&param_14));
                    fTransparencyAndCompositionSample = _e135;
                    let _e136 = sampleIdx;
                    let _e137 = fColorSample;
                    fColorSamples[_e136] = _e137;
                    let _e139 = sampleIdx;
                    let _e140 = fReactiveSample;
                    fReactiveSamples[_e139] = _e140;
                    let _e142 = sampleIdx;
                    let _e143 = fTransparencyAndCompositionSample;
                    fTransparencyAndCompositionSamples[_e142] = _e143;
                    let _e145 = fReactiveSample;
                    let _e146 = fTransparencyAndCompositionSample;
                    let _e148 = fMasksSum;
                    fMasksSum = (_e148 + (_e145 + _e146));
                    continue;
                } else {
                    break;
                }
                continuing {
                    let _e150 = x_5;
                    x_5 = (_e150 + 1i);
                }
            }
            continue;
        } else {
            break;
        }
        continuing {
            let _e152 = y_5;
            y_5 = (_e152 + 1i);
        }
    }
    let _e154 = fMasksSum;
    if (_e154 > 0f) {
        sampleIdx_1 = 0i;
        loop {
            let _e156 = sampleIdx_1;
            if (_e156 < 9i) {
                let _e158 = sampleIdx_1;
                let _e160 = fColorSamples[_e158];
                fColorSample_1 = _e160;
                let _e161 = sampleIdx_1;
                let _e163 = fReactiveSamples[_e161];
                fReactiveSample_1 = _e163;
                let _e164 = sampleIdx_1;
                let _e166 = fTransparencyAndCompositionSamples[_e164];
                fTransparencyAndCompositionSample_1 = _e166;
                let _e167 = fReferenceColor;
                let _e168 = fReferenceColor;
                let _e170 = fColorSample_1;
                let _e171 = fColorSample_1;
                param_15 = dot(_e167, _e168);
                param_16 = dot(_e170, _e171);
                let _e173 = ffxMax_u0028_f1_u003b_f1_u003b((&param_15), (&param_16));
                fMaxLenSq = _e173;
                let _e174 = fReferenceColor;
                let _e175 = fColorSample_1;
                let _e177 = fMaxLenSq;
                fSimilarity = (dot(_e174, _e175) / _e177);
                let _e179 = fSimilarity;
                fSimilarityPower = (1f + (6f - (_e179 * 6f)));
                let _e183 = fReactiveSample_1;
                param_17 = _e183;
                let _e184 = fSimilarityPower;
                param_18 = _e184;
                let _e185 = ffxPow_u0028_f1_u003b_f1_u003b((&param_17), (&param_18));
                fWeightedReactiveSample = _e185;
                let _e186 = fTransparencyAndCompositionSample_1;
                param_19 = _e186;
                let _e187 = fSimilarityPower;
                param_20 = _e187;
                let _e188 = ffxPow_u0028_f1_u003b_f1_u003b((&param_19), (&param_20));
                fWeightedTransparencyAndCompositionSample = _e188;
)FSR2",
R"FSR2(                let _e189 = fWeightedReactiveSample;
                let _e190 = fWeightedTransparencyAndCompositionSample;
                let _e192 = fReactiveFactor;
                param_21 = _e192;
                param_22 = vec2<f32>(_e189, _e190);
                let _e193 = ffxMax_u0028_vf2_u003b_vf2_u003b((&param_21), (&param_22));
                fReactiveFactor = _e193;
                continue;
            } else {
                break;
            }
            continuing {
                let _e194 = sampleIdx_1;
                sampleIdx_1 = (_e194 + 1i);
            }
        }
    }
    let _e196 = (*iPxLrPos);
    param_23 = _e196;
    let _e197 = fReactiveFactor;
    param_24 = _e197;
    StoreDilatedReactiveMasks_u0028_vi2_u003b_vf2_u003b((&param_23), (&param_24));
    return;
}
fn ffxSaturate_u0028_f1_u003b(x_6: ptr<function, f32>) -> f32 {
    let _e69 = (*x_6);
    return clamp(_e69, 0f, 1f);
}
fn ffxMin_u0028_f1_u003b_f1_u003b(x_7: ptr<function, f32>, y_6: ptr<function, f32>) -> f32 {
    let _e70 = (*x_7);
    let _e71 = (*y_6);
    return min(_e70, _e71);
}
fn ViewSpaceToMetersFactor_u0028_() -> f32 {
    let _e69 = cbFSR2_.fViewSpaceToMetersFactor;
    return _e69;
}
fn DeviceToViewSpaceTransformFactors_u0028_() -> vec4<f32> {
    let _e69 = cbFSR2_.fDeviceToViewDepth;
    return _e69;
}
fn GetViewSpaceDepth_u0028_f1_u003b(fDeviceDepth: ptr<function, f32>) -> f32 {
    var fDeviceToViewDepth: vec4<f32>;
    let _e70 = DeviceToViewSpaceTransformFactors_u0028_();
    fDeviceToViewDepth = _e70;
    let _e72 = fDeviceToViewDepth[1u];
    let _e73 = (*fDeviceDepth);
    let _e75 = fDeviceToViewDepth[0u];
    return (_e72 / (_e73 - _e75));
}
fn GetViewSpaceDepthInMeters_u0028_f1_u003b(fDeviceDepth_1: ptr<function, f32>) -> f32 {
    var param_25: f32;
    let _e70 = (*fDeviceDepth_1);
    param_25 = _e70;
    let _e71 = GetViewSpaceDepth_u0028_f1_u003b((&param_25));
    let _e72 = ViewSpaceToMetersFactor_u0028_();
    return (_e71 * _e72);
}
fn LoadDilatedDepth_u0028_vi2_u003b(iPxInput: ptr<function, vec2<i32>>) -> f32 {
    let _e69 = (*iPxInput);
    let _e70 = textureLoad(r_dilatedDepth, _e69, 0i);
    return _e70.x;
}
fn IsOnScreen_u0028_vi2_u003b_vi2_u003b(pos: ptr<function, vec2<i32>>, size: ptr<function, vec2<i32>>) -> bool {
    let _e70 = (*pos);
    let _e72 = (*size);
    return all((bitcast<vec2<u32>>(_e70) < bitcast<vec2<u32>>(_e72)));
}
fn GetMaxDistanceInMeters_u0028_() -> f32 {
    var param_26: f32;
    param_26 = 1f;
    let _e69 = GetViewSpaceDepth_u0028_f1_u003b((&param_26));
    let _e70 = ViewSpaceToMetersFactor_u0028_();
    return (_e69 * _e70);
}
fn ComputeDepthDivergence_u0028_vi2_u003b(iPxPos_4: ptr<function, vec2<i32>>) -> f32 {
    var fMaxDistInMeters: f32;
    var fDepthMax: f32;
    var fDepthMin: f32;
    var iMaxDistFound: i32;
    var y_7: i32;
    var x_8: i32;
    var iOffset: vec2<i32>;
    var iSamplePos: vec2<i32>;
    var fOnScreenFactor: f32;
    var param_27: vec2<i32>;
    var param_28: vec2<i32>;
    var fDepth: f32;
    var param_29: vec2<i32>;
    var param_30: f32;
    var param_31: f32;
    var param_32: f32;
    var param_33: f32;
    var param_34: f32;
    let _e87 = GetMaxDistanceInMeters_u0028_();
    fMaxDistInMeters = _e87;
    fDepthMax = 0f;
    let _e88 = fMaxDistInMeters;
    fDepthMin = _e88;
    iMaxDistFound = 0i;
    y_7 = -1i;
    loop {
        let _e89 = y_7;
        if (_e89 < 2i) {
            x_8 = -1i;
            loop {
                let _e91 = x_8;
                if (_e91 < 2i) {
                    let _e93 = x_8;
                    let _e94 = y_7;
                    iOffset = vec2<i32>(_e93, _e94);
                    let _e96 = (*iPxPos_4);
                    let _e97 = iOffset;
                    iSamplePos = (_e96 + _e97);
                    let _e99 = RenderSize_u0028_();
                    let _e100 = iSamplePos;
                    param_27 = _e100;
                    param_28 = _e99;
                    let _e101 = IsOnScreen_u0028_vi2_u003b_vi2_u003b((&param_27), (&param_28));
                    fOnScreenFactor = select(0f, 1f, _e101);
                    let _e103 = iSamplePos;
                    param_29 = _e103;
                    let _e104 = LoadDilatedDepth_u0028_vi2_u003b((&param_29));
                    param_30 = _e104;
                    let _e105 = GetViewSpaceDepthInMeters_u0028_f1_u003b((&param_30));
                    let _e106 = fOnScreenFactor;
                    fDepth = (_e105 * _e106);
                    let _e108 = fMaxDistInMeters;
                    let _e109 = fDepth;
                    let _e112 = iMaxDistFound;
                    iMaxDistFound = (_e112 | select(0i, 1i, (_e108 == _e109)));
                    let _e114 = fDepthMin;
                    param_31 = _e114;
                    let _e115 = fDepth;
                    param_32 = _e115;
                    let _e116 = ffxMin_u0028_f1_u003b_f1_u003b((&param_31), (&param_32));
                    fDepthMin = _e116;
                    let _e117 = fDepthMax;
                    param_33 = _e117;
                    let _e118 = fDepth;
                    param_34 = _e118;
                    let _e119 = ffxMax_u0028_f1_u003b_f1_u003b((&param_33), (&param_34));
                    fDepthMax = _e119;
                    continue;
                } else {
                    break;
                }
                continuing {
                    let _e120 = x_8;
                    x_8 = (_e120 + 1i);
                }
            }
            continue;
        } else {
            break;
        }
        continuing {
            let _e122 = y_7;
            y_7 = (_e122 + 1i);
        }
    }
    let _e124 = fDepthMin;
    let _e125 = fDepthMax;
    let _e128 = iMaxDistFound;
    return ((1f - (_e124 / _e125)) * select(1f, 0f, (_e128 != bitcast<i32>(0u))));
}
fn ffxLerp_u0028_f1_u003b_f1_u003b_f1_u003b(x_9: ptr<function, f32>, y_8: ptr<function, f32>, t: ptr<function, f32>) -> f32 {
    let _e71 = (*x_9);
    let _e72 = (*y_8);
    let _e73 = (*t);
    return mix(_e71, _e72, _e73);
}
fn DisplaySize_u0028_() -> vec2<i32> {
    let _e69 = cbFSR2_.iDisplaySize;
    return _e69;
}
fn SamplePreviousDilatedMotionVector_u0028_vf2_u003b(fUV: ptr<function, vec2<f32>>) -> vec2<f32> {
    let _e69 = (*fUV);
    let _e70 = textureSampleLevel(r_previous_dilated_motion_vectors, s_LinearClamp, _e69, 0f);
    return _e70.xy;
}
fn ffxMin_u0028_vf2_u003b_vf2_u003b(x_10: ptr<function, vec2<f32>>, y_9: ptr<function, vec2<f32>>) -> vec2<f32> {
    let _e70 = (*x_10);
    let _e71 = (*y_9);
    return min(_e70, _e71);
}
fn ClampUv_u0028_vf2_u003b_vi2_u003b_vi2_u003b(fUv: ptr<function, vec2<f32>>, iTextureSize_1: ptr<function, vec2<i32>>, iResourceSize: ptr<function, vec2<i32>>) -> vec2<f32> {
    var fSampleLocation: vec2<f32>;
    var fClampedLocation: vec2<f32>;
    var param_35: vec2<f32>;
    var param_36: vec2<f32>;
    var param_37: vec2<f32>;
    var param_38: vec2<f32>;
    var fClampedUv: vec2<f32>;
    let _e78 = (*fUv);
    let _e79 = (*iTextureSize_1);
    fSampleLocation = (_e78 * vec2<f32>(_e79));
    let _e82 = (*iTextureSize_1);
    let _e85 = fSampleLocation;
    param_35 = _e85;
    param_36 = (vec2<f32>(_e82) - vec2<f32>(0.5f, 0.5f));
    let _e86 = ffxMin_u0028_vf2_u003b_vf2_u003b((&param_35), (&param_36));
    param_37 = vec2<f32>(0.5f, 0.5f);
    param_38 = _e86;
    let _e87 = ffxMax_u0028_vf2_u003b_vf2_u003b((&param_37), (&param_38));
    fClampedLocation = _e87;
    let _e88 = fClampedLocation;
    let _e89 = (*iResourceSize);
    fClampedUv = (_e88 / vec2<f32>(_e89));
    let _e92 = fClampedUv;
    return _e92;
}
fn MaxRenderSize_u0028_() -> vec2<i32> {
    let _e69 = cbFSR2_.iMaxRenderSize;
    return _e69;
}
fn LoadDilatedMotionVector_u0028_vi2_u003b(iPxInput_1: ptr<function, vec2<i32>>) -> vec2<f32> {
    let _e69 = (*iPxInput_1);
    let _e70 = textureLoad(r_dilated_motion_vectors, _e69, 0i);
    return _e70.xy;
}
fn ComputeTemporalMotionDivergence_u0028_vi2_u003b(iPxPos_5: ptr<function, vec2<i32>>) -> f32 {
    var fUv_1: vec2<f32>;
    var fMotionVector: vec2<f32>;
    var param_39: vec2<i32>;
    var fReprojectedUv: vec2<f32>;
    var param_40: vec2<f32>;
    var param_41: vec2<i32>;
    var param_42: vec2<i32>;
    var fPrevMotionVector: vec2<f32>;
    var param_43: vec2<f32>;
    var fPxDistance: f32;
    var local_4: f32;
    var param_44: f32;
    var param_45: f32;
    var param_46: f32;
    var param_47: f32;
    var param_48: f32;
    var param_49: f32;
    var param_50: f32;
    let _e87 = (*iPxPos_5);
    let _e91 = RenderSize_u0028_();
    fUv_1 = ((vec2<f32>(_e87) + vec2(0.5f)) / vec2<f32>(_e91));
    let _e94 = (*iPxPos_5);
    param_39 = _e94;
    let _e95 = LoadDilatedMotionVector_u0028_vi2_u003b((&param_39));
    fMotionVector = _e95;
    let _e96 = fUv_1;
    let _e97 = fMotionVector;
    fReprojectedUv = (_e96 + _e97);
    let _e99 = RenderSize_u0028_();
    let _e100 = MaxRenderSize_u0028_();
    let _e101 = fReprojectedUv;
    param_40 = _e101;
    param_41 = _e99;
    param_42 = _e100;
    let _e102 = ClampUv_u0028_vf2_u003b_vi2_u003b_vi2_u003b((&param_40), (&param_41), (&param_42));
    fReprojectedUv = _e102;
    let _e103 = fReprojectedUv;
    param_43 = _e103;
    let _e104 = SamplePreviousDilatedMotionVector_u0028_vf2_u003b((&param_43));
    fPrevMotionVector = _e104;
    let _e105 = fMotionVector;
    let _e106 = DisplaySize_u0028_();
    fPxDistance = length((_e105 * vec2<f32>(_e106)));
    let _e110 = fPxDistance;
    if (_e110 > 1f) {
        let _e112 = fPrevMotionVector;
        let _e114 = fMotionVector;
        param_44 = (length(_e112) / length(_e114));
        let _e117 = ffxSaturate_u0028_f1_u003b((&param_44));
        let _e119 = fPxDistance;
        param_45 = (_e119 / 20f);
        param_46 = 3f;
        let _e121 = ffxPow_u0028_f1_u003b_f1_u003b((&param_45), (&param_46));
        param_47 = _e121;
        let _e122 = ffxSaturate_u0028_f1_u003b((&param_47));
        param_48 = 0f;
        param_49 = (1f - _e117);
        param_50 = _e122;
        let _e123 = ffxLerp_u0028_f1_u003b_f1_u003b_f1_u003b((&param_48), (&param_49), (&param_50));
        local_4 = _e123;
    } else {
        local_4 = 0f;
    }
    let _e124 = local_4;
    return _e124;
}
fn MotionVectorScale_u0028_() -> vec2<f32> {
    let _e69 = cbFSR2_.fMotionVectorScale;
    return _e69;
}
fn LoadInputMotionVector_u0028_vi2_u003b(iPxDilatedMotionVectorPos: ptr<function, vec2<i32>>) -> vec2<f32> {
    var fSrcMotionVector: vec2<f32>;
    var fUvMotionVector: vec2<f32>;
    let _e71 = (*iPxDilatedMotionVectorPos);
    let _e72 = textureLoad(r_input_motion_vectors, _e71, 0i);
    fSrcMotionVector = _e72.xy;
    let _e74 = fSrcMotionVector;
    let _e75 = MotionVectorScale_u0028_();
    fUvMotionVector = (_e74 * _e75);
    let _e77 = fUvMotionVector;
    return _e77;
}
fn ComputeMotionDivergence_u0028_vi2_u003b_vi2_u003b(iPxPos_6: ptr<function, vec2<i32>>, iPxInputMotionVectorSize: ptr<function, vec2<i32>>) -> f32 {
    var minconvergence: f32;
    var fMotionVectorNucleus: vec2<f32>;
    var param_51: vec2<i32>;
    var fNucleusVelocityLr: f32;
    var fMaxVelocityUv: f32;
    var y_10: i32;
    var x_11: i32;
    var sp: vec2<i32>;
    var param_52: vec2<i32>;
    var param_53: vec2<i32>;
    var param_54: vec2<i32>;
    var fMotionVector_1: vec2<f32>;
    var param_55: vec2<i32>;
    var fVelocityUv: f32;
    var param_56: f32;
    var param_57: f32;
    var param_58: f32;
    var param_59: f32;
    var param_60: f32;
    var param_61: f32;
    var param_62: f32;
    var param_63: f32;
    minconvergence = 1f;
    let _e92 = (*iPxPos_6);
    param_51 = _e92;
    let _e93 = LoadInputMotionVector_u0028_vi2_u003b((&param_51));
    fMotionVectorNucleus = _e93;
    let _e94 = fMotionVectorNucleus;
    let _e95 = RenderSize_u0028_();
    fNucleusVelocityLr = length((_e94 * vec2<f32>(_e95)));
    let _e99 = fMotionVectorNucleus;
    fMaxVelocityUv = length(_e99);
)FSR2",
R"FSR2(    let _e101 = fNucleusVelocityLr;
    if (_e101 > 0.01f) {
        y_10 = -1i;
        loop {
            let _e103 = y_10;
            if (_e103 <= 1i) {
                x_11 = -1i;
                loop {
                    let _e105 = x_11;
                    if (_e105 <= 1i) {
                        let _e107 = x_11;
                        let _e108 = y_10;
                        let _e110 = (*iPxPos_6);
                        param_52 = _e110;
                        param_53 = vec2<i32>(_e107, _e108);
                        let _e111 = (*iPxInputMotionVectorSize);
                        param_54 = _e111;
                        let _e112 = ClampLoad_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_52), (&param_53), (&param_54));
                        sp = _e112;
                        let _e113 = sp;
                        param_55 = _e113;
                        let _e114 = LoadInputMotionVector_u0028_vi2_u003b((&param_55));
                        fMotionVector_1 = _e114;
                        let _e115 = fMotionVector_1;
                        fVelocityUv = length(_e115);
                        let _e117 = fVelocityUv;
                        param_56 = _e117;
                        let _e118 = fMaxVelocityUv;
                        param_57 = _e118;
                        let _e119 = ffxMax_u0028_f1_u003b_f1_u003b((&param_56), (&param_57));
                        fMaxVelocityUv = _e119;
                        let _e120 = fVelocityUv;
                        param_58 = _e120;
                        let _e121 = fMaxVelocityUv;
                        param_59 = _e121;
                        let _e122 = ffxMax_u0028_f1_u003b_f1_u003b((&param_58), (&param_59));
                        fVelocityUv = _e122;
                        let _e123 = fMotionVector_1;
                        let _e124 = fVelocityUv;
                        let _e127 = fMotionVectorNucleus;
                        let _e128 = fVelocityUv;
                        let _e132 = minconvergence;
                        param_60 = _e132;
                        param_61 = dot((_e123 / vec2(_e124)), (_e127 / vec2(_e128)));
                        let _e133 = ffxMin_u0028_f1_u003b_f1_u003b((&param_60), (&param_61));
                        minconvergence = _e133;
                        continue;
                    } else {
                        break;
                    }
                    continuing {
                        let _e134 = x_11;
                        x_11 = (_e134 + 1i);
                    }
                }
                continue;
            } else {
                break;
            }
            continuing {
                let _e136 = y_10;
                y_10 = (_e136 + 1i);
            }
        }
    }
    let _e138 = minconvergence;
    param_62 = (1f - _e138);
    let _e140 = ffxSaturate_u0028_f1_u003b((&param_62));
    let _e141 = fMaxVelocityUv;
    param_63 = (_e141 / 0.01f);
    let _e143 = ffxSaturate_u0028_f1_u003b((&param_63));
    return (_e140 * _e143);
}
fn StorePreparedInputColor_u0028_vi2_u003b_vf4_u003b(iPxPos_7: ptr<function, vec2<i32>>, fTonemapped: ptr<function, vec4<f32>>) {
    let _e70 = (*iPxPos_7);
    let _e71 = (*fTonemapped);
    textureStore(rw_prepared_input_color, _e70, _e71);
    return;
}
fn RGBToYCoCg_u0028_vf3_u003b(fRgb: ptr<function, vec3<f32>>) -> vec3<f32> {
    var fYCoCg: vec3<f32>;
    let _e71 = (*fRgb)[0u];
    let _e74 = (*fRgb)[1u];
    let _e78 = (*fRgb)[2u];
    let _e82 = (*fRgb)[0u];
    let _e85 = (*fRgb)[2u];
    let _e89 = (*fRgb)[0u];
    let _e92 = (*fRgb)[1u];
    let _e96 = (*fRgb)[2u];
    fYCoCg = vec3<f32>((((0.25f * _e71) + (0.5f * _e74)) + (0.25f * _e78)), ((0.5f * _e82) - (0.5f * _e85)), (((-0.25f * _e89) + (0.5f * _e92)) - (0.25f * _e96)));
    let _e100 = fYCoCg;
    return _e100;
}
fn PrepareRgb_u0028_vf3_u003b_f1_u003b_f1_u003b(fRgb_1: ptr<function, vec3<f32>>, fExposure: ptr<function, f32>, fPreExposure: ptr<function, f32>) -> vec3<f32> {
    let _e71 = (*fPreExposure);
    let _e72 = (*fRgb_1);
    (*fRgb_1) = (_e72 / vec3(_e71));
    let _e75 = (*fExposure);
    let _e76 = (*fRgb_1);
    (*fRgb_1) = (_e76 * _e75);
    let _e78 = (*fRgb_1);
    (*fRgb_1) = clamp(_e78, vec3(0f), vec3(65504f));
    let _e82 = (*fRgb_1);
    return _e82;
}
fn PreExposure_u0028_() -> f32 {
    let _e69 = cbFSR2_.fPreExposure;
    return _e69;
}
fn Exposure_u0028_() -> f32 {
    var exposure: f32;
    let _e69 = textureLoad(r_input_exposure, vec2<i32>(0i, 0i), 0i);
    exposure = _e69.x;
    let _e71 = exposure;
    if (_e71 == 0f) {
        exposure = 1f;
    }
    let _e73 = exposure;
    return _e73;
}
fn ffxMax_u0028_vf3_u003b_vf3_u003b(x_12: ptr<function, vec3<f32>>, y_11: ptr<function, vec3<f32>>) -> vec3<f32> {
    let _e70 = (*x_12);
    let _e71 = (*y_11);
    return max(_e70, _e71);
}
fn ComputePreparedInputColor_u0028_vi2_u003b(iPxLrPos_1: ptr<function, vec2<i32>>) -> vec3<f32> {
    var fRgb_2: vec3<f32>;
    var param_64: vec2<i32>;
    var param_65: vec3<f32>;
    var param_66: vec3<f32>;
    var param_67: vec3<f32>;
    var param_68: f32;
    var param_69: f32;
    var fPreparedYCoCg: vec3<f32>;
    var param_70: vec3<f32>;
    let _e78 = (*iPxLrPos_1);
    param_64 = _e78;
    let _e79 = LoadInputColor_u0028_vi2_u003b((&param_64));
    param_65 = vec3<f32>(0f, 0f, 0f);
    param_66 = _e79;
    let _e80 = ffxMax_u0028_vf3_u003b_vf3_u003b((&param_65), (&param_66));
    fRgb_2 = _e80;
    let _e81 = Exposure_u0028_();
    let _e82 = PreExposure_u0028_();
    let _e83 = fRgb_2;
    param_67 = _e83;
    param_68 = _e81;
    param_69 = _e82;
    let _e84 = PrepareRgb_u0028_vf3_u003b_f1_u003b_f1_u003b((&param_67), (&param_68), (&param_69));
    fRgb_2 = _e84;
    let _e85 = fRgb_2;
    param_70 = _e85;
    let _e86 = RGBToYCoCg_u0028_vf3_u003b((&param_70));
    fPreparedYCoCg = _e86;
    let _e87 = fPreparedYCoCg;
    return _e87;
}
fn LoadReconstructedPrevDepth_u0028_vi2_u003b(iPxPos_8: ptr<function, vec2<i32>>) -> f32 {
    let _e70 = (*iPxPos_8)[1u];
    let _e74 = cbFSR2_.iMaxRenderSize[0u];
    let _e78 = (*iPxPos_8)[0u];
    let _e83 = unnamed.r_reconstructed_previous_nearest_depth[((bitcast<u32>(_e70) * bitcast<u32>(_e74)) + bitcast<u32>(_e78))];
    return bitcast<f32>(_e83);
}
fn EvaluateSurface_u0028_vi2_u003b_vf2_u003b(iPxPos_9: ptr<function, vec2<i32>>, fMotionVector_2: ptr<function, vec2<f32>>) -> f32 {
    var d0_: f32;
    var param_71: vec2<i32>;
    var param_72: f32;
    var d1_: f32;
    var param_73: vec2<i32>;
    var param_74: f32;
    var d2_: f32;
    var param_75: vec2<i32>;
    var param_76: f32;
    var phi_1330_: bool;
    let _e79 = (*iPxPos_9);
    param_71 = (_e79 + vec2<i32>(0i, -1i));
    let _e81 = LoadReconstructedPrevDepth_u0028_vi2_u003b((&param_71));
    param_72 = _e81;
    let _e82 = GetViewSpaceDepth_u0028_f1_u003b((&param_72));
    d0_ = _e82;
    let _e83 = (*iPxPos_9);
    param_73 = (_e83 + vec2<i32>(0i, 0i));
    let _e85 = LoadReconstructedPrevDepth_u0028_vi2_u003b((&param_73));
    param_74 = _e85;
    let _e86 = GetViewSpaceDepth_u0028_f1_u003b((&param_74));
    d1_ = _e86;
    let _e87 = (*iPxPos_9);
    param_75 = (_e87 + vec2<i32>(0i, 1i));
    let _e89 = LoadReconstructedPrevDepth_u0028_vi2_u003b((&param_75));
    param_76 = _e89;
    let _e90 = GetViewSpaceDepth_u0028_f1_u003b((&param_76));
    d2_ = _e90;
    let _e91 = d0_;
    let _e92 = d1_;
    let _e94 = d1_;
    let _e96 = ((_e91 - _e92) > (_e94 * 0.01f));
    phi_1330_ = _e96;
    if _e96 {
        let _e97 = d1_;
        let _e98 = d2_;
        let _e100 = d2_;
        phi_1330_ = ((_e97 - _e98) > (_e100 * 0.01f));
    }
    let _e104 = phi_1330_;
    return (1f - select(0f, 1f, _e104));
}
fn ComputeNdc_u0028_vf2_u003b_vi2_u003b(fPxPos: ptr<function, vec2<f32>>, iSize: ptr<function, vec2<i32>>) -> vec2<f32> {
    let _e70 = (*fPxPos);
    let _e71 = (*iSize);
    return (((_e70 / vec2<f32>(_e71)) * vec2<f32>(2f, -2f)) + vec2<f32>(-1f, 1f));
}
fn GetViewSpacePosition_u0028_vi2_u003b_vi2_u003b_f1_u003b(iViewportPos: ptr<function, vec2<i32>>, iViewportSize: ptr<function, vec2<i32>>, fDeviceDepth_2: ptr<function, f32>) -> vec3<f32> {
    var fDeviceToViewDepth_1: vec4<f32>;
    var Z: f32;
    var param_77: f32;
    var fNdcPos: vec2<f32>;
    var param_78: vec2<f32>;
    var param_79: vec2<i32>;
    var X: f32;
    var Y: f32;
    let _e79 = DeviceToViewSpaceTransformFactors_u0028_();
    fDeviceToViewDepth_1 = _e79;
    let _e80 = (*fDeviceDepth_2);
    param_77 = _e80;
    let _e81 = GetViewSpaceDepth_u0028_f1_u003b((&param_77));
    Z = _e81;
    let _e82 = (*iViewportPos);
    param_78 = vec2<f32>(_e82);
    let _e84 = (*iViewportSize);
    param_79 = _e84;
    let _e85 = ComputeNdc_u0028_vf2_u003b_vi2_u003b((&param_78), (&param_79));
    fNdcPos = _e85;
    let _e87 = fDeviceToViewDepth_1[2u];
    let _e89 = fNdcPos[0u];
    let _e91 = Z;
    X = ((_e87 * _e89) * _e91);
    let _e94 = fDeviceToViewDepth_1[3u];
    let _e96 = fNdcPos[1u];
    let _e98 = Z;
    Y = ((_e94 * _e96) * _e98);
    let _e100 = X;
    let _e101 = Y;
    let _e102 = Z;
    return vec3<f32>(_e100, _e101, _e102);
}
fn ffxFract_u0028_vf2_u003b(x_13: ptr<function, vec2<f32>>) -> vec2<f32> {
    let _e69 = (*x_13);
    return fract(_e69);
}
fn GetBilinearSamplingData_u0028_vf2_u003b_vi2_u003b(fUv_2: ptr<function, vec2<f32>>, iSize_1: ptr<function, vec2<i32>>) -> BilinearSamplingData {
    var fPxSample: vec2<f32>;
    var data: BilinearSamplingData;
    var fPxFrac: vec2<f32>;
    var param_80: vec2<f32>;
    let _e74 = (*fUv_2);
    let _e75 = (*iSize_1);
    fPxSample = ((_e74 * vec2<f32>(_e75)) - vec2<f32>(0.5f, 0.5f));
    let _e79 = fPxSample;
    data.iBasePos = vec2<i32>(floor(_e79));
    let _e83 = fPxSample;
    param_80 = _e83;
    let _e84 = ffxFract_u0028_vf2_u003b((&param_80));
    fPxFrac = _e84;
    data.iOffsets[0i] = vec2<i32>(0i, 0i);
    data.iOffsets[1i] = vec2<i32>(1i, 0i);
    data.iOffsets[2i] = vec2<i32>(0i, 1i);
    data.iOffsets[3i] = vec2<i32>(1i, 1i);
    let _e94 = fPxFrac[0u];
    let _e97 = fPxFrac[1u];
    data.fWeights[0i] = ((1f - _e94) * (1f - _e97));
    let _e103 = fPxFrac[0u];
    let _e105 = fPxFrac[1u];
    data.fWeights[1i] = (_e103 * (1f - _e105));
    let _e111 = fPxFrac[0u];
    let _e114 = fPxFrac[1u];
    data.fWeights[2i] = ((1f - _e111) * _e114);
    let _e119 = fPxFrac[0u];
    let _e121 = fPxFrac[1u];
    data.fWeights[3i] = (_e119 * _e121);
    let _e125 = data;
    return _e125;
}
fn ComputeDepthClip_u0028_vf2_u003b_f1_u003b(fUvSample: ptr<function, vec2<f32>>, fCurrentDepthSample: ptr<function, f32>) -> f32 {
    var fCurrentDepthViewSpace: f32;
    var param_81: f32;
    var bilinearInfo: BilinearSamplingData;
    var param_82: vec2<f32>;
    var param_83: vec2<i32>;
    var fDilatedSum: f32;
    var fDepth_1: f32;
    var fWeightSum: f32;
    var iSampleIndex: i32;
    var iOffset_1: vec2<i32>;
    var iSamplePos_1: vec2<i32>;
    var param_84: vec2<i32>;
    var param_85: vec2<i32>;
    var fWeight: f32;
    var fPrevDepthSample: f32;
    var param_86: vec2<i32>;
    var fPrevNearestDepthViewSpace: f32;
    var param_87: f32;
    var fDepthDiff: f32;
    var fPlaneDepth: f32;
    var param_88: f32;
    var param_89: f32;
    var fCenter: vec3<f32>;
    var param_90: vec2<i32>;
    var param_91: vec2<i32>;
    var param_92: f32;
    var fCorner: vec3<f32>;
    var param_93: vec2<i32>;
    var param_94: vec2<i32>;
    var param_95: f32;
    var fHalfViewportWidth: f32;
    var fDepthThreshold: f32;
    var param_96: f32;
    var param_97: f32;
    var Kfov: f32;
    var fRequiredDepthSeparation: f32;
    var fResolutionFactor: f32;
    var param_98: f32;
    var fPower: f32;
    var param_99: f32;
    var param_100: f32;
    var param_101: f32;
    var param_102: f32;
    var param_103: f32;
    var param_104: f32;
    var local_5: f32;
    var param_105: f32;
    let _e117 = (*fCurrentDepthSample);
    param_81 = _e117;
)FSR2",
R"FSR2(    let _e118 = GetViewSpaceDepth_u0028_f1_u003b((&param_81));
    fCurrentDepthViewSpace = _e118;
    let _e119 = RenderSize_u0028_();
    let _e120 = (*fUvSample);
    param_82 = _e120;
    param_83 = _e119;
    let _e121 = GetBilinearSamplingData_u0028_vf2_u003b_vi2_u003b((&param_82), (&param_83));
    bilinearInfo = _e121;
    fDilatedSum = 0f;
    fDepth_1 = 0f;
    fWeightSum = 0f;
    iSampleIndex = 0i;
    loop {
        let _e122 = iSampleIndex;
        if (_e122 < 4i) {
            let _e124 = iSampleIndex;
            let _e127 = bilinearInfo.iOffsets[_e124];
            iOffset_1 = _e127;
            let _e129 = bilinearInfo.iBasePos;
            let _e130 = iOffset_1;
            iSamplePos_1 = (_e129 + _e130);
            let _e132 = RenderSize_u0028_();
            let _e133 = iSamplePos_1;
            param_84 = _e133;
            param_85 = _e132;
            let _e134 = IsOnScreen_u0028_vi2_u003b_vi2_u003b((&param_84), (&param_85));
            if _e134 {
                let _e135 = iSampleIndex;
                let _e138 = bilinearInfo.fWeights[_e135];
                fWeight = _e138;
                let _e139 = fWeight;
                if (_e139 > 0.01f) {
                    let _e141 = iSamplePos_1;
                    param_86 = _e141;
                    let _e142 = LoadReconstructedPrevDepth_u0028_vi2_u003b((&param_86));
                    fPrevDepthSample = _e142;
                    let _e143 = fPrevDepthSample;
                    param_87 = _e143;
                    let _e144 = GetViewSpaceDepth_u0028_f1_u003b((&param_87));
                    fPrevNearestDepthViewSpace = _e144;
                    let _e145 = fCurrentDepthViewSpace;
                    let _e146 = fPrevNearestDepthViewSpace;
                    fDepthDiff = (_e145 - _e146);
                    let _e148 = fDepthDiff;
                    if (_e148 > 0f) {
                        let _e150 = fPrevDepthSample;
                        param_88 = _e150;
                        let _e151 = (*fCurrentDepthSample);
                        param_89 = _e151;
                        let _e152 = ffxMax_u0028_f1_u003b_f1_u003b((&param_88), (&param_89));
                        fPlaneDepth = _e152;
                        let _e153 = RenderSize_u0028_();
                        let _e157 = RenderSize_u0028_();
                        param_90 = vec2<i32>((vec2<f32>(_e153) * 0.5f));
                        param_91 = _e157;
                        let _e158 = fPlaneDepth;
                        param_92 = _e158;
                        let _e159 = GetViewSpacePosition_u0028_vi2_u003b_vi2_u003b_f1_u003b((&param_90), (&param_91), (&param_92));
                        fCenter = _e159;
                        let _e160 = RenderSize_u0028_();
                        param_93 = vec2<i32>(0i, 0i);
                        param_94 = _e160;
                        let _e161 = fPlaneDepth;
                        param_95 = _e161;
                        let _e162 = GetViewSpacePosition_u0028_vi2_u003b_vi2_u003b_f1_u003b((&param_93), (&param_94), (&param_95));
                        fCorner = _e162;
                        let _e163 = RenderSize_u0028_();
                        fHalfViewportWidth = length(vec2<f32>(_e163));
                        let _e166 = fCurrentDepthViewSpace;
                        param_96 = _e166;
                        let _e167 = fPrevNearestDepthViewSpace;
                        param_97 = _e167;
                        let _e168 = ffxMax_u0028_f1_u003b_f1_u003b((&param_96), (&param_97));
                        fDepthThreshold = _e168;
                        let _e169 = fCorner;
                        let _e171 = fCenter;
                        Kfov = (length(_e169) / length(_e171));
                        let _e174 = Kfov;
                        let _e176 = fHalfViewportWidth;
                        let _e178 = fDepthThreshold;
                        fRequiredDepthSeparation = (((0.0000137f * _e174) * _e176) * _e178);
                        let _e180 = RenderSize_u0028_();
                        param_98 = (length(vec2<f32>(_e180)) / 2202.9072f);
                        let _e184 = ffxSaturate_u0028_f1_u003b((&param_98));
                        fResolutionFactor = _e184;
                        param_99 = 1f;
                        param_100 = 3f;
                        let _e185 = fResolutionFactor;
                        param_101 = _e185;
                        let _e186 = ffxLerp_u0028_f1_u003b_f1_u003b_f1_u003b((&param_99), (&param_100), (&param_101));
                        fPower = _e186;
                        let _e187 = fRequiredDepthSeparation;
                        let _e188 = fDepthDiff;
                        param_102 = (_e187 / _e188);
                        let _e190 = ffxSaturate_u0028_f1_u003b((&param_102));
                        param_103 = _e190;
                        let _e191 = fPower;
                        param_104 = _e191;
                        let _e192 = ffxPow_u0028_f1_u003b_f1_u003b((&param_103), (&param_104));
                        let _e193 = fWeight;
                        let _e195 = fDepth_1;
                        fDepth_1 = (_e195 + (_e192 * _e193));
                        let _e197 = fWeight;
                        let _e198 = fWeightSum;
                        fWeightSum = (_e198 + _e197);
                    }
                }
            }
            continue;
        } else {
            break;
        }
        continuing {
            let _e200 = iSampleIndex;
            iSampleIndex = (_e200 + 1i);
        }
    }
    let _e202 = fWeightSum;
    if (_e202 > 0f) {
        let _e204 = fDepth_1;
        let _e205 = fWeightSum;
        param_105 = (1f - (_e204 / _e205));
        let _e208 = ffxSaturate_u0028_f1_u003b((&param_105));
        local_5 = _e208;
    } else {
        local_5 = 0f;
    }
    let _e209 = local_5;
    return _e209;
}
fn LoadInputDepth_u0028_vi2_u003b(iPxPos_10: ptr<function, vec2<i32>>) -> f32 {
    let _e69 = (*iPxPos_10);
    let _e70 = textureLoad(r_input_depth, _e69, 0i);
    return _e70.x;
}
fn DepthClip_u0028_vi2_u003b(iPxPos_11: ptr<function, vec2<i32>>) {
    var fDepthUv: vec2<f32>;
    var fMotionVector_3: vec2<f32>;
    var param_106: vec2<i32>;
    var fDilatedUv: vec2<f32>;
    var fDilatedDepth: f32;
    var param_107: vec2<i32>;
    var fCurrentDepthViewSpace_1: f32;
    var param_108: vec2<i32>;
    var param_109: f32;
    var fDepthClip: f32;
    var param_110: vec2<f32>;
    var param_111: f32;
    var param_112: vec2<i32>;
    var param_113: vec2<f32>;
    var fPreparedYCoCg_1: vec3<f32>;
    var param_114: vec2<i32>;
    var param_115: vec2<i32>;
    var param_116: vec4<f32>;
    var iSamplePos_2: vec2<i32>;
    var fMotionDivergence_1: f32;
    var param_117: vec2<i32>;
    var param_118: vec2<i32>;
    var fTemporalMotionDifference: f32;
    var param_119: vec2<i32>;
    var param_120: vec2<i32>;
    var param_121: f32;
    var param_122: f32;
    var param_123: f32;
    var param_124: vec2<i32>;
    var param_125: f32;
    let _e99 = (*iPxPos_11);
    let _e103 = RenderSize_u0028_();
    fDepthUv = ((vec2<f32>(_e99) + vec2(0.5f)) / vec2<f32>(_e103));
    let _e106 = (*iPxPos_11);
    param_106 = _e106;
    let _e107 = LoadDilatedMotionVector_u0028_vi2_u003b((&param_106));
    fMotionVector_3 = _e107;
    let _e108 = fMotionVector_3;
    let _e109 = DisplaySize_u0028_();
    let _e115 = fMotionVector_3;
    fMotionVector_3 = (_e115 * select(0f, 1f, (length((_e108 * vec2<f32>(_e109))) > 0.01f)));
    let _e117 = fDepthUv;
    let _e118 = fMotionVector_3;
    fDilatedUv = (_e117 + _e118);
    let _e120 = (*iPxPos_11);
    param_107 = _e120;
    let _e121 = LoadDilatedDepth_u0028_vi2_u003b((&param_107));
    fDilatedDepth = _e121;
    let _e122 = (*iPxPos_11);
    param_108 = _e122;
    let _e123 = LoadInputDepth_u0028_vi2_u003b((&param_108));
    param_109 = _e123;
    let _e124 = GetViewSpaceDepth_u0028_f1_u003b((&param_109));
    fCurrentDepthViewSpace_1 = _e124;
    let _e125 = fDilatedUv;
    param_110 = _e125;
    let _e126 = fDilatedDepth;
    param_111 = _e126;
    let _e127 = ComputeDepthClip_u0028_vf2_u003b_f1_u003b((&param_110), (&param_111));
    let _e128 = (*iPxPos_11);
    param_112 = _e128;
    let _e129 = fMotionVector_3;
    param_113 = _e129;
    let _e130 = EvaluateSurface_u0028_vi2_u003b_vf2_u003b((&param_112), (&param_113));
    fDepthClip = (_e127 * _e130);
    let _e132 = (*iPxPos_11);
    param_114 = _e132;
    let _e133 = ComputePreparedInputColor_u0028_vi2_u003b((&param_114));
    fPreparedYCoCg_1 = _e133;
    let _e134 = fPreparedYCoCg_1;
    let _e135 = fDepthClip;
    let _e140 = (*iPxPos_11);
    param_115 = _e140;
    param_116 = vec4<f32>(_e134.x, _e134.y, _e134.z, _e135);
    StorePreparedInputColor_u0028_vi2_u003b_vf4_u003b((&param_115), (&param_116));
    let _e141 = (*iPxPos_11);
    iSamplePos_2 = _e141;
    let _e142 = RenderSize_u0028_();
    let _e143 = iSamplePos_2;
    param_117 = _e143;
    param_118 = _e142;
    let _e144 = ComputeMotionDivergence_u0028_vi2_u003b_vi2_u003b((&param_117), (&param_118));
    fMotionDivergence_1 = _e144;
    let _e145 = (*iPxPos_11);
    param_119 = _e145;
    let _e146 = ComputeTemporalMotionDivergence_u0028_vi2_u003b((&param_119));
    let _e147 = (*iPxPos_11);
    param_120 = _e147;
    let _e148 = ComputeDepthDivergence_u0028_vi2_u003b((&param_120));
    param_121 = (_e146 - _e148);
    let _e150 = ffxSaturate_u0028_f1_u003b((&param_121));
    fTemporalMotionDifference = _e150;
    let _e151 = fTemporalMotionDifference;
    param_122 = _e151;
    let _e152 = fMotionDivergence_1;
    param_123 = _e152;
    let _e153 = ffxMax_u0028_f1_u003b_f1_u003b((&param_122), (&param_123));
    let _e154 = (*iPxPos_11);
    param_124 = _e154;
    param_125 = _e153;
    PreProcessReactiveMasks_u0028_vi2_u003b_f1_u003b((&param_124), (&param_125));
    return;
}
fn main_1() {
    var param_126: vec2<i32>;
    let _e69 = gl_GlobalInvocationID_1;
    param_126 = bitcast<vec2<i32>>(_e69.xy);
    DepthClip_u0028_vi2_u003b((&param_126));
    return;
}
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) gl_GlobalInvocationID: vec3<u32>) {
    gl_GlobalInvocationID_1 = gl_GlobalInvocationID;
    main_1();
}

)FSR2",
};
inline const Binding depth_clip_bindings[] = {
    {14, kUniform, "cbFSR2_", "cbFSR2_", false},
    {9, kTexture, "r_input_depth", "", false},
    {3, kTexture, "r_reactive_mask", "", false},
    {4, kTexture, "r_transparency_and_composition_mask", "", false},
    {8, kTexture, "r_input_color_jittered", "", false},
    {7, kTexture, "r_input_motion_vectors", "", false},
    {13, kStorageTexture, "rw_prepared_input_color", "rgba16float", false},
    {0, kStorageRO, "RecPrevDepthR", "RecPrevDepthR", false},
    {1, kTexture, "r_dilated_motion_vectors", "", false},
    {6, kTexture, "r_previous_dilated_motion_vectors", "", false},
    {2, kTexture, "r_dilatedDepth", "", false},
    {10, kTexture, "r_input_exposure", "", false},
    {12, kStorageTexture, "rw_dilated_reactive_masks", "rg8unorm", false},
    {5, kTexture, "r_prepared_input_color", "", false},
};
inline const char* const lock_parts[] = {
R"FSR2(struct cbFSR2_t {
    iRenderSize: vec2<i32>,
    iMaxRenderSize: vec2<i32>,
    iDisplaySize: vec2<i32>,
    iInputColorResourceDimensions: vec2<i32>,
    iLumaMipDimensions: vec2<i32>,
    iLumaMipLevelToUse: i32,
    iFrameIndex: i32,
    fDeviceToViewDepth: vec4<f32>,
    fJitter: vec2<f32>,
    fMotionVectorScale: vec2<f32>,
    fDownscaleFactor: vec2<f32>,
    fMotionVectorJitterCancellation: vec2<f32>,
    fPreExposure: f32,
    fPreviousFramePreExposure: f32,
    fTanHalfFOV: f32,
    fJitterSequenceLength: f32,
    fDeltaTime: f32,
    fDynamicResChangeFactor: f32,
    fViewSpaceToMetersFactor: f32,
}
struct RecPrevDepthW {
    rw_reconstructed_previous_nearest_depth: array<u32>,
}
@group(1) @binding(3)
var<uniform> cbFSR2_: cbFSR2_t;
@group(1) @binding(0)
var r_lock_input_luma: texture_2d<f32>;
@group(1) @binding(1)
var rw_new_locks: texture_storage_2d<r8unorm,read_write>;
@group(1) @binding(2)
var<storage, read_write> unnamed: RecPrevDepthW;
var<private> gl_WorkGroupID_1: vec3<u32>;
var<private> gl_LocalInvocationID_1: vec3<u32>;
@group(0) @binding(0)
var s_PointClamp: sampler;
@group(0) @binding(1)
var s_LinearClamp: sampler;
fn SetReconstructedDepth_u0028_vi2_u003b_u1_u003b(iPxSample: ptr<function, vec2<i32>>, uValue: ptr<function, u32>) {
    let _e47 = (*iPxSample)[1u];
    let _e51 = cbFSR2_.iMaxRenderSize[0u];
    let _e55 = (*iPxSample)[0u];
    let _e58 = (*uValue);
    unnamed.rw_reconstructed_previous_nearest_depth[((bitcast<u32>(_e47) * bitcast<u32>(_e51)) + bitcast<u32>(_e55))] = _e58;
    return;
}
fn RenderSize_u0028_() -> vec2<i32> {
    let _e45 = cbFSR2_.iRenderSize;
    return _e45;
}
fn ClearResourcesForNextFrame_u0028_vi2_u003b(iPxHrPos: ptr<function, vec2<i32>>) {
    var param: vec2<i32>;
    var param_1: u32;
    let _e47 = (*iPxHrPos);
    let _e48 = RenderSize_u0028_();
    if all((_e47 < _e48)) {
        let _e51 = (*iPxHrPos);
        param = _e51;
        param_1 = 1065353216u;
        SetReconstructedDepth_u0028_vi2_u003b_u1_u003b((&param), (&param_1));
    }
    return;
}
fn StoreNewLocks_u0028_vi2_u003b_f1_u003b(iPxPos: ptr<function, vec2<i32>>, newLock: ptr<function, f32>) {
    let _e46 = (*iPxPos);
    let _e47 = (*newLock);
    textureStore(rw_new_locks, _e46, vec4<f32>(_e47, 0f, 0f, 0f));
    return;
}
fn DisplaySize_u0028_() -> vec2<i32> {
    let _e45 = cbFSR2_.iDisplaySize;
    return _e45;
}
fn Jitter_u0028_() -> vec2<f32> {
    let _e45 = cbFSR2_.fJitter;
    return _e45;
}
fn ComputeHrPosFromLrPos_u0028_vi2_u003b(iPxLrPos: ptr<function, vec2<i32>>) -> vec2<i32> {
    var fSrcJitteredPos: vec2<f32>;
    var fLrPosInHr: vec2<f32>;
    var iPxHrPos_1: vec2<i32>;
    let _e48 = (*iPxLrPos);
    let _e52 = Jitter_u0028_();
    fSrcJitteredPos = ((vec2<f32>(_e48) + vec2(0.5f)) - _e52);
    let _e54 = fSrcJitteredPos;
    let _e55 = RenderSize_u0028_();
    let _e58 = DisplaySize_u0028_();
    fLrPosInHr = ((_e54 / vec2<f32>(_e55)) * vec2<f32>(_e58));
    let _e61 = fLrPosInHr;
    iPxHrPos_1 = vec2<i32>(floor(_e61));
    let _e64 = iPxHrPos_1;
    return _e64;
}
fn ffxMin_u0028_f1_u003b_f1_u003b(x: ptr<function, f32>, y: ptr<function, f32>) -> f32 {
    let _e46 = (*x);
    let _e47 = (*y);
    return min(_e46, _e47);
}
fn ffxMax_u0028_f1_u003b_f1_u003b(x_1: ptr<function, f32>, y_1: ptr<function, f32>) -> f32 {
    let _e46 = (*x_1);
    let _e47 = (*y_1);
    return max(_e46, _e47);
}
fn ffxMin_u0028_i1_u003b_i1_u003b(x_2: ptr<function, i32>, y_2: ptr<function, i32>) -> i32 {
    let _e46 = (*x_2);
    let _e47 = (*y_2);
    return min(_e46, _e47);
}
fn ffxMax_u0028_i1_u003b_i1_u003b(x_3: ptr<function, i32>, y_3: ptr<function, i32>) -> i32 {
    let _e46 = (*x_3);
    let _e47 = (*y_3);
    return max(_e46, _e47);
}
fn ClampLoad_u0028_vi2_u003b_vi2_u003b_vi2_u003b(iPxSample_1: ptr<function, vec2<i32>>, iPxOffset: ptr<function, vec2<i32>>, iTextureSize: ptr<function, vec2<i32>>) -> vec2<i32> {
    var result: vec2<i32>;
    var local: i32;
    var param_2: i32;
    var param_3: i32;
    var local_1: i32;
    var param_4: i32;
    var param_5: i32;
    var local_2: i32;
    var param_6: i32;
    var param_7: i32;
    var local_3: i32;
    var param_8: i32;
    var param_9: i32;
    let _e60 = (*iPxSample_1);
    let _e61 = (*iPxOffset);
    result = (_e60 + _e61);
    let _e64 = (*iPxOffset)[0u];
    if (_e64 < 0i) {
        let _e67 = result[0u];
        param_2 = _e67;
        param_3 = 0i;
        let _e68 = ffxMax_u0028_i1_u003b_i1_u003b((&param_2), (&param_3));
        local = _e68;
    } else {
        let _e70 = result[0u];
        local = _e70;
    }
    let _e71 = local;
    result[0u] = _e71;
    let _e74 = (*iPxOffset)[0u];
    if (_e74 > 0i) {
        let _e77 = (*iTextureSize)[0u];
        let _e80 = result[0u];
        param_4 = _e80;
        param_5 = (_e77 - 1i);
        let _e81 = ffxMin_u0028_i1_u003b_i1_u003b((&param_4), (&param_5));
        local_1 = _e81;
    } else {
        let _e83 = result[0u];
        local_1 = _e83;
    }
    let _e84 = local_1;
    result[0u] = _e84;
    let _e87 = (*iPxOffset)[1u];
    if (_e87 < 0i) {
        let _e90 = result[1u];
        param_6 = _e90;
        param_7 = 0i;
        let _e91 = ffxMax_u0028_i1_u003b_i1_u003b((&param_6), (&param_7));
        local_2 = _e91;
    } else {
        let _e93 = result[1u];
        local_2 = _e93;
    }
    let _e94 = local_2;
    result[1u] = _e94;
    let _e97 = (*iPxOffset)[1u];
    if (_e97 > 0i) {
        let _e100 = (*iTextureSize)[1u];
        let _e103 = result[1u];
        param_8 = _e103;
        param_9 = (_e100 - 1i);
        let _e104 = ffxMin_u0028_i1_u003b_i1_u003b((&param_8), (&param_9));
        local_3 = _e104;
    } else {
        let _e106 = result[1u];
        local_3 = _e106;
    }
    let _e107 = local_3;
    result[1u] = _e107;
    let _e109 = result;
    return _e109;
}
fn LoadLockInputLuma_u0028_vi2_u003b(iPxPos_1: ptr<function, vec2<i32>>) -> f32 {
    let _e45 = (*iPxPos_1);
    let _e46 = textureLoad(r_lock_input_luma, _e45, 0i);
    return _e46.x;
}
fn ComputeThinFeatureConfidence_u0028_vi2_u003b(pos: ptr<function, vec2<i32>>) -> bool {
    var fNucleus: f32;
    var param_10: vec2<i32>;
    var similar_threshold: f32;
    var dissimilarLumaMin: f32;
    var dissimilarLumaMax: f32;
    var mask: u32;
    var idx: i32;
    var y_4: i32;
    var x_4: i32;
    var samplePos: vec2<i32>;
    var param_11: vec2<i32>;
    var param_12: vec2<i32>;
    var param_13: vec2<i32>;
    var sampleLuma: f32;
    var param_14: vec2<i32>;
    var difference: f32;
    var param_15: f32;
    var param_16: f32;
    var param_17: f32;
    var param_18: f32;
    var param_19: f32;
    var param_20: f32;
    var param_21: f32;
    var param_22: f32;
    var isRidge: bool;
    var i: i32;
    var indexable: array<u32, 4>;
    var indexable_1: array<u32, 4>;
    let _e73 = (*pos);
    param_10 = _e73;
    let _e74 = LoadLockInputLuma_u0028_vi2_u003b((&param_10));
    fNucleus = _e74;
    similar_threshold = 1.05f;
    dissimilarLumaMin = 340282350000000000000000000000000000000f;
    dissimilarLumaMax = 0f;
    mask = 16u;
    idx = 0i;
    y_4 = -1i;
    loop {
        let _e75 = y_4;
        if (_e75 <= 1i) {
            x_4 = -1i;
            loop {
                let _e77 = x_4;
                if (_e77 <= 1i) {
                    let _e79 = x_4;
                    let _e81 = y_4;
                    if ((_e79 == 0i) && (_e81 == 0i)) {
                        continue;
                    }
                    let _e84 = x_4;
                    let _e85 = y_4;
                    let _e87 = RenderSize_u0028_();
                    let _e88 = (*pos);
                    param_11 = _e88;
                    param_12 = vec2<i32>(_e84, _e85);
                    param_13 = _e87;
                    let _e89 = ClampLoad_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_11), (&param_12), (&param_13));
                    samplePos = _e89;
                    let _e90 = samplePos;
                    param_14 = _e90;
                    let _e91 = LoadLockInputLuma_u0028_vi2_u003b((&param_14));
                    sampleLuma = _e91;
                    let _e92 = sampleLuma;
                    param_15 = _e92;
                    let _e93 = fNucleus;
                    param_16 = _e93;
                    let _e94 = ffxMax_u0028_f1_u003b_f1_u003b((&param_15), (&param_16));
                    let _e95 = sampleLuma;
                    param_17 = _e95;
                    let _e96 = fNucleus;
                    param_18 = _e96;
                    let _e97 = ffxMin_u0028_f1_u003b_f1_u003b((&param_17), (&param_18));
                    difference = (_e94 / _e97);
                    let _e99 = difference;
                    let _e101 = difference;
                    let _e102 = similar_threshold;
                    if ((_e99 > 0f) && (_e101 < _e102)) {
                        let _e105 = idx;
                        let _e108 = mask;
                        mask = (_e108 | (1u << bitcast<u32>(_e105)));
                    } else {
                        let _e110 = dissimilarLumaMin;
                        param_19 = _e110;
                        let _e111 = sampleLuma;
                        param_20 = _e111;
                        let _e112 = ffxMin_u0028_f1_u003b_f1_u003b((&param_19), (&param_20));
                        dissimilarLumaMin = _e112;
                        let _e113 = dissimilarLumaMax;
                        param_21 = _e113;
                        let _e114 = sampleLuma;
                        param_22 = _e114;
                        let _e115 = ffxMax_u0028_f1_u003b_f1_u003b((&param_21), (&param_22));
                        dissimilarLumaMax = _e115;
                    }
                    continue;
                } else {
                    break;
                }
                continuing {
                    let _e116 = x_4;
                    x_4 = (_e116 + 1i);
                    let _e118 = idx;
                    idx = (_e118 + 1i);
                }
            }
            continue;
        } else {
            break;
        }
        continuing {
            let _e120 = y_4;
            y_4 = (_e120 + 1i);
        }
    }
    let _e122 = fNucleus;
    let _e123 = dissimilarLumaMax;
    let _e125 = fNucleus;
    let _e126 = dissimilarLumaMin;
    isRidge = ((_e122 > _e123) || (_e125 < _e126));
    let _e129 = isRidge;
    if (false == _e129) {
        return false;
    }
    i = 0i;
    loop {
        let _e131 = i;
        if (_e131 < 4i) {
            let _e133 = mask;
            let _e134 = i;
            indexable = array<u32, 4>(27u, 54u, 216u, 432u);
            let _e136 = indexable[_e134];
            let _e138 = i;
            indexable_1 = array<u32, 4>(27u, 54u, 216u, 432u);
            let _e140 = indexable_1[_e138];
            if ((_e133 & _e136) == _e140) {
                return false;
            }
            continue;
        } else {
            break;
        }
        continuing {
            let _e142 = i;
            i = (_e142 + 1i);
        }
    }
    return true;
}
fn ComputeLock_u0028_vi2_u003b(iPxLrPos_1: ptr<function, vec2<i32>>) {
    var param_23: vec2<i32>;
    var param_24: vec2<i32>;
    var param_25: vec2<i32>;
    var param_26: f32;
    var param_27: vec2<i32>;
    let _e50 = (*iPxLrPos_1);
    param_23 = _e50;
    let _e51 = ComputeThinFeatureConfidence_u0028_vi2_u003b((&param_23));
    if _e51 {
        let _e52 = (*iPxLrPos_1);
        param_24 = _e52;
        let _e53 = ComputeHrPosFromLrPos_u0028_vi2_u003b((&param_24));
        param_25 = _e53;
        param_26 = 1f;
        StoreNewLocks_u0028_vi2_u003b_f1_u003b((&param_25), (&param_26));
    }
    let _e54 = (*iPxLrPos_1);
    param_27 = _e54;
    ClearResourcesForNextFrame_u0028_vi2_u003b((&param_27));
    return;
}
fn main_1() {
    var uDispatchThreadId: vec2<u32>;
    var param_28: vec2<i32>;
    let _e46 = gl_WorkGroupID_1;
    let _e49 = gl_LocalInvocationID_1;
)FSR2",
R"FSR2(    uDispatchThreadId = ((_e46.xy * vec2<u32>(8u, 8u)) + _e49.xy);
    let _e52 = uDispatchThreadId;
    param_28 = bitcast<vec2<i32>>(_e52);
    ComputeLock_u0028_vi2_u003b((&param_28));
    return;
}
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) gl_WorkGroupID: vec3<u32>, @builtin(local_invocation_id) gl_LocalInvocationID: vec3<u32>) {
    gl_WorkGroupID_1 = gl_WorkGroupID;
    gl_LocalInvocationID_1 = gl_LocalInvocationID;
    main_1();
}

)FSR2",
};
inline const Binding lock_bindings[] = {
    {3, kUniform, "cbFSR2_", "cbFSR2_", false},
    {0, kTexture, "r_lock_input_luma", "", false},
    {1, kStorageTexture, "rw_new_locks", "r8unorm", true},
    {2, kStorageRW, "RecPrevDepthW", "RecPrevDepthW", true},
};
inline const char* const accumulate_parts[] = {
R"FSR2(struct RectificationBox {
    boxCenter: vec3<f32>,
    boxVec: vec3<f32>,
    aabbMin: vec3<f32>,
    aabbMax: vec3<f32>,
    fBoxCenterWeight: f32,
}
struct FetchedBicubicSamples {
    fColor00_: vec4<f32>,
    fColor10_: vec4<f32>,
    fColor20_: vec4<f32>,
    fColor30_: vec4<f32>,
    fColor01_: vec4<f32>,
    fColor11_: vec4<f32>,
    fColor21_: vec4<f32>,
    fColor31_: vec4<f32>,
    fColor02_: vec4<f32>,
    fColor12_: vec4<f32>,
    fColor22_: vec4<f32>,
    fColor32_: vec4<f32>,
    fColor03_: vec4<f32>,
    fColor13_: vec4<f32>,
    fColor23_: vec4<f32>,
    fColor33_: vec4<f32>,
}
struct AccumulationPassCommonParams {
    iPxHrPos: vec2<i32>,
    fHrUv: vec2<f32>,
    fLrUv_HwSampler: vec2<f32>,
    fMotionVector: vec2<f32>,
    fReprojectedHrUv: vec2<f32>,
    fHrVelocity: f32,
    fDepthClipFactor: f32,
    fDilatedReactiveFactor: f32,
    fAccumulationMask: f32,
    bIsResetFrame: bool,
    bIsExistingSample: bool,
    bIsNewSample: bool,
}
struct LockState {
    NewLock: bool,
    WasLockedPrevFrame: bool,
}
struct cbFSR2_t {
    iRenderSize: vec2<i32>,
    iMaxRenderSize: vec2<i32>,
    iDisplaySize: vec2<i32>,
    iInputColorResourceDimensions: vec2<i32>,
    iLumaMipDimensions: vec2<i32>,
    iLumaMipLevelToUse: i32,
    iFrameIndex: i32,
    fDeviceToViewDepth: vec4<f32>,
    fJitter: vec2<f32>,
    fMotionVectorScale: vec2<f32>,
    fDownscaleFactor: vec2<f32>,
    fMotionVectorJitterCancellation: vec2<f32>,
    fPreExposure: f32,
    fPreviousFramePreExposure: f32,
    fTanHalfFOV: f32,
    fJitterSequenceLength: f32,
    fDeltaTime: f32,
    fDynamicResChangeFactor: f32,
    fViewSpaceToMetersFactor: f32,
}
@group(1) @binding(18)
var<uniform> cbFSR2_: cbFSR2_t;
@group(1) @binding(10)
var r_imgMips: texture_2d<f32>;
@group(0) @binding(1)
var s_LinearClamp: sampler;
@group(1) @binding(6)
var r_prepared_input_color: texture_2d<f32>;
@group(1) @binding(3)
var r_internal_upscaled_color: texture_2d<f32>;
@group(1) @binding(17)
var rw_luma_history: texture_storage_2d<rgba8unorm,read_write>;
@group(1) @binding(12)
var r_luma_history: texture_2d<f32>;
@group(1) @binding(13)
var rw_internal_upscaled_color: texture_storage_2d<rgba16float,write>;
@group(1) @binding(14)
var rw_lock_status: texture_storage_2d<rg16float,read_write>;
@group(1) @binding(16)
var rw_new_locks: texture_storage_2d<r8unorm,read_write>;
@group(1) @binding(4)
var r_lock_status: texture_2d<f32>;
@group(1) @binding(2)
var r_dilated_motion_vectors: texture_2d<f32>;
@group(1) @binding(0)
var r_input_exposure: texture_2d<f32>;
@group(1) @binding(1)
var r_dilated_reactive_masks: texture_2d<f32>;
var<private> gl_WorkGroupID_1: vec3<u32>;
var<private> gl_LocalInvocationID_1: vec3<u32>;
@group(0) @binding(0)
var s_PointClamp: sampler;
@group(1) @binding(11)
var r_auto_exposure: texture_2d<f32>;
@group(1) @binding(8)
var r_lanczos_lut: texture_2d<f32>;
@group(1) @binding(9)
var r_upsample_maximum_bias_lut: texture_2d<f32>;
@group(1) @binding(15)
var rw_upscaled_output: texture_storage_2d<rgba16float,write>;
fn StoreNewLocks_u0028_vi2_u003b_f1_u003b(iPxPos: ptr<function, vec2<i32>>, newLock: ptr<function, f32>) {
    let _e114 = (*iPxPos);
    let _e115 = (*newLock);
    textureStore(rw_new_locks, _e114, vec4<f32>(_e115, 0f, 0f, 0f));
    return;
}
fn StoreInternalColorAndWeight_u0028_vi2_u003b_vf4_u003b(iPxPos_1: ptr<function, vec2<i32>>, fColorAndWeight: ptr<function, vec4<f32>>) {
    let _e114 = (*iPxPos_1);
    let _e115 = (*fColorAndWeight);
    textureStore(rw_internal_upscaled_color, _e114, _e115);
    return;
}
fn ffxMax_u0028_f1_u003b_f1_u003b(x: ptr<function, f32>, y: ptr<function, f32>) -> f32 {
    let _e114 = (*x);
    let _e115 = (*y);
    return max(_e114, _e115);
}
fn ffxLerp_u0028_f1_u003b_f1_u003b_f1_u003b(x_1: ptr<function, f32>, y_1: ptr<function, f32>, t: ptr<function, f32>) -> f32 {
    let _e115 = (*x_1);
    let _e116 = (*y_1);
    let _e117 = (*t);
    return mix(_e115, _e116, _e117);
}
fn ffxSaturate_u0028_f1_u003b(x_2: ptr<function, f32>) -> f32 {
    let _e113 = (*x_2);
    return clamp(_e113, 0f, 1f);
}
fn ffxMin_u0028_f1_u003b_f1_u003b(x_3: ptr<function, f32>, y_2: ptr<function, f32>) -> f32 {
    let _e114 = (*x_3);
    let _e115 = (*y_2);
    return min(_e114, _e115);
}
fn ComputeTemporalReactiveFactor_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_f1_u003b(params: AccumulationPassCommonParams, fTemporalReactiveFactor: ptr<function, f32>) -> f32 {
    var fNewFactor: f32;
    var param: f32;
    var param_1: f32;
    var param_2: f32;
    var param_3: f32;
    var param_4: f32;
    var param_5: f32;
    var param_6: f32;
    var param_7: f32;
    var param_8: f32;
    var param_9: f32;
    var param_10: f32;
    var param_11: f32;
    var param_12: f32;
    var param_13: f32;
    var param_14: f32;
    param = 0.99f;
    let _e130 = (*fTemporalReactiveFactor);
    param_1 = _e130;
    let _e131 = ffxMin_u0028_f1_u003b_f1_u003b((&param), (&param_1));
    fNewFactor = _e131;
    param_2 = params.fHrVelocity;
    let _e133 = ffxSaturate_u0028_f1_u003b((&param_2));
    let _e134 = fNewFactor;
    param_3 = _e134;
    param_4 = 0.4f;
    param_5 = _e133;
    let _e135 = ffxLerp_u0028_f1_u003b_f1_u003b_f1_u003b((&param_3), (&param_4), (&param_5));
    let _e136 = fNewFactor;
    param_6 = _e136;
    param_7 = _e135;
    let _e137 = ffxMax_u0028_f1_u003b_f1_u003b((&param_6), (&param_7));
    fNewFactor = _e137;
    let _e138 = fNewFactor;
    let _e139 = fNewFactor;
    param_8 = (params.fDepthClipFactor * 0.1f);
    param_9 = params.fDilatedReactiveFactor;
    let _e144 = ffxMax_u0028_f1_u003b_f1_u003b((&param_8), (&param_9));
    param_10 = (_e138 * _e139);
    param_11 = _e144;
    let _e145 = ffxMax_u0028_f1_u003b_f1_u003b((&param_10), (&param_11));
    fNewFactor = _e145;
    let _e147 = fNewFactor;
    fNewFactor = select(_e147, 1f, params.bIsNewSample);
    param_12 = (params.fHrVelocity * 10f);
    let _e151 = ffxSaturate_u0028_f1_u003b((&param_12));
    if (_e151 >= 1f) {
        param_13 = 0.001f;
        let _e153 = fNewFactor;
        param_14 = _e153;
        let _e154 = ffxMax_u0028_f1_u003b_f1_u003b((&param_13), (&param_14));
        fNewFactor = (_e154 * -1f);
    }
    let _e156 = fNewFactor;
    return _e156;
}
fn StoreLockStatus_u0028_vi2_u003b_vf2_u003b(iPxPos_2: ptr<function, vec2<i32>>, fLockstatus: ptr<function, vec2<f32>>) {
    let _e114 = (*iPxPos_2);
    let _e115 = (*fLockstatus);
    textureStore(rw_lock_status, _e114, vec4<f32>(_e115.x, _e115.y, 0f, 0f));
    return;
}
fn JitterSequenceLength_u0028_() -> f32 {
    let _e113 = cbFSR2_.fJitterSequenceLength;
    return _e113;
}
fn KillLock_u0028_vf2_u003b(fLockStatus: ptr<function, vec2<f32>>) {
    (*fLockStatus)[0u] = 0f;
    return;
}
fn IsUvInside_u0028_vf2_u003b(fUv: ptr<function, vec2<f32>>) -> bool {
    var phi_2127_: bool;
    var phi_2138_: bool;
    var phi_2139_: bool;
    let _e114 = (*fUv)[0u];
    let _e115 = (_e114 >= 0f);
    phi_2127_ = _e115;
    if _e115 {
        let _e117 = (*fUv)[0u];
        phi_2127_ = (_e117 <= 1f);
    }
    let _e120 = phi_2127_;
    phi_2139_ = _e120;
    if _e120 {
        let _e122 = (*fUv)[1u];
        let _e123 = (_e122 >= 0f);
        phi_2138_ = _e123;
        if _e123 {
            let _e125 = (*fUv)[1u];
            phi_2138_ = (_e125 <= 1f);
        }
        let _e128 = phi_2138_;
        phi_2139_ = _e128;
    }
    let _e130 = phi_2139_;
    return _e130;
}
fn FinalizeLockStatus_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_vf2_u003b_f1_u003b(params_1: AccumulationPassCommonParams, fLockStatus_1: ptr<function, vec2<f32>>, fUpsampledWeight: ptr<function, f32>) {
    var fEstimatedUvNextFrame: vec2<f32>;
    var param_15: vec2<f32>;
    var param_16: vec2<f32>;
    var fLifetimeDecreaseLanczosMax: f32;
    var fLifetimeDecrease: f32;
    var param_17: f32;
    var param_18: f32;
    var param_19: vec2<i32>;
    var param_20: vec2<f32>;
    fEstimatedUvNextFrame = (params_1.fHrUv - params_1.fMotionVector);
    let _e127 = fEstimatedUvNextFrame;
    param_15 = _e127;
    let _e128 = IsUvInside_u0028_vf2_u003b((&param_15));
    if (_e128 == false) {
        let _e130 = (*fLockStatus_1);
        param_16 = _e130;
        KillLock_u0028_vf2_u003b((&param_16));
        let _e131 = param_16;
        (*fLockStatus_1) = _e131;
    } else {
        let _e132 = JitterSequenceLength_u0028_();
        fLifetimeDecreaseLanczosMax = (_e132 * 0.061666667f);
        let _e134 = (*fUpsampledWeight);
        let _e135 = fLifetimeDecreaseLanczosMax;
        fLifetimeDecrease = (_e134 / _e135);
        let _e138 = (*fLockStatus_1)[0u];
        let _e139 = fLifetimeDecrease;
        param_17 = 0f;
        param_18 = (_e138 - _e139);
        let _e141 = ffxMax_u0028_f1_u003b_f1_u003b((&param_17), (&param_18));
        (*fLockStatus_1)[0u] = _e141;
    }
    param_19 = params_1.iPxHrPos;
    let _e144 = (*fLockStatus_1);
    param_20 = _e144;
    StoreLockStatus_u0028_vi2_u003b_vf2_u003b((&param_19), (&param_20));
    return;
}
fn PreExposure_u0028_() -> f32 {
    let _e113 = cbFSR2_.fPreExposure;
    return _e113;
}
fn UnprepareRgb_u0028_vf3_u003b_f1_u003b(fRgb: ptr<function, vec3<f32>>, fExposure: ptr<function, f32>) -> vec3<f32> {
    let _e114 = (*fExposure);
    let _e115 = (*fRgb);
    (*fRgb) = (_e115 / vec3(_e114));
    let _e118 = PreExposure_u0028_();
    let _e119 = (*fRgb);
    (*fRgb) = (_e119 * _e118);
    let _e121 = (*fRgb);
    return _e121;
}
fn Exposure_u0028_() -> f32 {
    var exposure: f32;
    let _e113 = textureLoad(r_input_exposure, vec2<i32>(0i, 0i), 0i);
    exposure = _e113.x;
    let _e115 = exposure;
    if (_e115 == 0f) {
        exposure = 1f;
    }
    let _e117 = exposure;
    return _e117;
}
fn InverseTonemap_u0028_vf3_u003b(fRgb_1: ptr<function, vec3<f32>>) -> vec3<f32> {
    var param_21: f32;
    var param_22: f32;
    var param_23: f32;
    var param_24: f32;
    var param_25: f32;
    var param_26: f32;
    let _e119 = (*fRgb_1);
    let _e121 = (*fRgb_1)[1u];
    param_21 = _e121;
    let _e123 = (*fRgb_1)[2u];
    param_22 = _e123;
    let _e124 = ffxMax_u0028_f1_u003b_f1_u003b((&param_21), (&param_22));
    let _e126 = (*fRgb_1)[0u];
    param_23 = _e126;
    param_24 = _e124;
    let _e127 = ffxMax_u0028_f1_u003b_f1_u003b((&param_23), (&param_24));
    param_25 = 0.000015266243f;
    param_26 = (1f - _e127);
    let _e129 = ffxMax_u0028_f1_u003b_f1_u003b((&param_25), (&param_26));
    return (_e119 / vec3(_e129));
}
fn ffxLerp_u0028_vf3_u003b_vf3_u003b_vf3_u003b(x_4: ptr<function, vec3<f32>>, y_3: ptr<function, vec3<f32>>, t_1: ptr<function, vec3<f32>>) -> vec3<f32> {
    let _e115 = (*x_4);
    let _e116 = (*y_3);
    let _e117 = (*t_1);
    return mix(_e115, _e116, _e117);
}
fn RGBToYCoCg_u0028_vf3_u003b(fRgb_2: ptr<function, vec3<f32>>) -> vec3<f32> {
    var fYCoCg: vec3<f32>;
    let _e115 = (*fRgb_2)[0u];
    let _e118 = (*fRgb_2)[1u];
    let _e122 = (*fRgb_2)[2u];
    let _e126 = (*fRgb_2)[0u];
    let _e129 = (*fRgb_2)[2u];
    let _e133 = (*fRgb_2)[0u];
    let _e136 = (*fRgb_2)[1u];
    let _e140 = (*fRgb_2)[2u];
    fYCoCg = vec3<f32>((((0.25f * _e115) + (0.5f * _e118)) + (0.25f * _e122)), ((0.5f * _e126) - (0.5f * _e129)), (((-0.25f * _e133) + (0.5f * _e136)) - (0.25f * _e140)));
    let _e144 = fYCoCg;
    return _e144;
}
fn Tonemap_u0028_vf3_u003b(fRgb_3: ptr<function, vec3<f32>>) -> vec3<f32> {
    var param_27: f32;
    var param_28: f32;
    var param_29: f32;
    var param_30: f32;
    var param_31: f32;
    var param_32: f32;
    let _e119 = (*fRgb_3);
    param_27 = 0f;
    let _e121 = (*fRgb_3)[0u];
    param_28 = _e121;
    let _e122 = ffxMax_u0028_f1_u003b_f1_u003b((&param_27), (&param_28));
    let _e124 = (*fRgb_3)[1u];
    param_29 = _e124;
)FSR2",
R"FSR2(    let _e126 = (*fRgb_3)[2u];
    param_30 = _e126;
    let _e127 = ffxMax_u0028_f1_u003b_f1_u003b((&param_29), (&param_30));
    param_31 = _e122;
    param_32 = _e127;
    let _e128 = ffxMax_u0028_f1_u003b_f1_u003b((&param_31), (&param_32));
    return (_e119 / vec3((_e128 + 1f)));
}
fn YCoCgToRGB_u0028_vf3_u003b(fYCoCg_1: ptr<function, vec3<f32>>) -> vec3<f32> {
    var fRgb_4: vec3<f32>;
    let _e115 = (*fYCoCg_1)[0u];
    let _e117 = (*fYCoCg_1)[1u];
    let _e120 = (*fYCoCg_1)[2u];
    let _e123 = (*fYCoCg_1)[0u];
    let _e125 = (*fYCoCg_1)[2u];
    let _e128 = (*fYCoCg_1)[0u];
    let _e130 = (*fYCoCg_1)[1u];
    let _e133 = (*fYCoCg_1)[2u];
    fRgb_4 = vec3<f32>(((_e115 + _e117) - _e120), (_e123 + _e125), ((_e128 - _e130) - _e133));
    let _e136 = fRgb_4;
    return _e136;
}
fn ffxMax_u0028_vf3_u003b_vf3_u003b(x_5: ptr<function, vec3<f32>>, y_4: ptr<function, vec3<f32>>) -> vec3<f32> {
    let _e114 = (*x_5);
    let _e115 = (*y_4);
    return max(_e114, _e115);
}
fn Accumulate_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_vf3_u003b_vf3_u003b_vf4_u003b(params_2: AccumulationPassCommonParams, fHistoryColor: ptr<function, vec3<f32>>, fAccumulation: ptr<function, vec3<f32>>, fUpsampledColorAndWeight: ptr<function, vec4<f32>>) {
    var param_33: vec3<f32>;
    var param_34: vec3<f32>;
    var param_35: vec3<f32>;
    var param_36: vec3<f32>;
    var param_37: vec3<f32>;
    var param_38: vec3<f32>;
    var param_39: vec3<f32>;
    var param_40: vec3<f32>;
    var fAlpha: vec3<f32>;
    var param_41: vec3<f32>;
    var param_42: vec3<f32>;
    var param_43: vec3<f32>;
    var param_44: vec3<f32>;
    var param_45: vec3<f32>;
    let _e130 = (*fAccumulation);
    let _e131 = (*fUpsampledColorAndWeight);
    param_33 = vec3<f32>(0.001f, 0.001f, 0.001f);
    param_34 = (_e130 + _e131.www);
    let _e134 = ffxMax_u0028_vf3_u003b_vf3_u003b((&param_33), (&param_34));
    (*fAccumulation) = _e134;
    let _e135 = (*fUpsampledColorAndWeight);
    param_35 = _e135.xyz;
    let _e137 = YCoCgToRGB_u0028_vf3_u003b((&param_35));
    param_36 = _e137;
    let _e138 = Tonemap_u0028_vf3_u003b((&param_36));
    param_37 = _e138;
    let _e139 = RGBToYCoCg_u0028_vf3_u003b((&param_37));
    (*fUpsampledColorAndWeight)[0u] = _e139.x;
    (*fUpsampledColorAndWeight)[1u] = _e139.y;
    (*fUpsampledColorAndWeight)[2u] = _e139.z;
    let _e146 = (*fHistoryColor);
    param_38 = _e146;
    let _e147 = YCoCgToRGB_u0028_vf3_u003b((&param_38));
    param_39 = _e147;
    let _e148 = Tonemap_u0028_vf3_u003b((&param_39));
    param_40 = _e148;
    let _e149 = RGBToYCoCg_u0028_vf3_u003b((&param_40));
    (*fHistoryColor) = _e149;
    let _e150 = (*fUpsampledColorAndWeight);
    let _e152 = (*fAccumulation);
    fAlpha = (_e150.www / _e152);
    let _e154 = (*fHistoryColor);
    param_41 = _e154;
    let _e155 = (*fUpsampledColorAndWeight);
    param_42 = _e155.xyz;
    let _e157 = fAlpha;
    param_43 = _e157;
    let _e158 = ffxLerp_u0028_vf3_u003b_vf3_u003b_vf3_u003b((&param_41), (&param_42), (&param_43));
    (*fHistoryColor) = _e158;
    let _e159 = (*fHistoryColor);
    param_44 = _e159;
    let _e160 = YCoCgToRGB_u0028_vf3_u003b((&param_44));
    (*fHistoryColor) = _e160;
    let _e161 = (*fHistoryColor);
    param_45 = _e161;
    let _e162 = InverseTonemap_u0028_vf3_u003b((&param_45));
    (*fHistoryColor) = _e162;
    return;
}
fn ffxBroadcast3_u0028_f1_u003b(value: ptr<function, f32>) -> vec3<f32> {
    let _e113 = (*value);
    return vec3(_e113);
}
fn ffxSaturate_u0028_vf3_u003b(x_6: ptr<function, vec3<f32>>) -> vec3<f32> {
    var param_46: f32;
    var param_47: f32;
    let _e115 = (*x_6);
    param_46 = 0f;
    let _e116 = ffxBroadcast3_u0028_f1_u003b((&param_46));
    param_47 = 1f;
    let _e117 = ffxBroadcast3_u0028_f1_u003b((&param_47));
    return clamp(_e115, _e116, _e117);
}
fn ffxMin_u0028_vf3_u003b_vf3_u003b(x_7: ptr<function, vec3<f32>>, y_5: ptr<function, vec3<f32>>) -> vec3<f32> {
    let _e114 = (*x_7);
    let _e115 = (*y_5);
    return min(_e114, _e115);
}
fn ffxPow_u0028_f1_u003b_f1_u003b(x_8: ptr<function, f32>, y_6: ptr<function, f32>) -> f32 {
    let _e114 = (*x_8);
    let _e115 = (*y_6);
    return pow(_e114, _e115);
}
fn DownscaleFactor_u0028_() -> vec2<f32> {
    let _e113 = cbFSR2_.fDownscaleFactor;
    return _e113;
}
fn RectifyHistory_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_struct_u002d_RectificationBox_u002d_vf3_u002d_vf3_u002d_vf3_u002d_vf3_u002d_f11_u003b_vf3_u003b_vf3_u003b_f1_u003b_f1_u003b_f1_u003b(params_3: AccumulationPassCommonParams, clippingBox: ptr<function, RectificationBox>, fHistoryColor_1: ptr<function, vec3<f32>>, fAccumulation_1: ptr<function, vec3<f32>>, fLockContributionThisFrame: ptr<function, f32>, fTemporalReactiveFactor_1: ptr<function, f32>, fLumaInstabilityFactor: ptr<function, f32>) {
    var fScaleFactorInfluence: f32;
    var param_48: f32;
    var param_49: f32;
    var param_50: f32;
    var param_51: f32;
    var fVecolityFactor: f32;
    var param_52: f32;
    var fBoxScaleT: f32;
    var param_53: f32;
    var param_54: f32;
    var param_55: f32;
    var param_56: f32;
    var fBoxScale: f32;
    var param_57: f32;
    var param_58: f32;
    var param_59: f32;
    var fScaledBoxVec: vec3<f32>;
    var boxMin: vec3<f32>;
    var boxMax: vec3<f32>;
    var boxCenter: vec3<f32>;
    var boxVecSize: f32;
    var param_60: vec3<f32>;
    var param_61: vec3<f32>;
    var param_62: vec3<f32>;
    var param_63: vec3<f32>;
    var fClampedHistoryColor: vec3<f32>;
    var fHistoryContribution: vec3<f32>;
    var param_64: f32;
    var param_65: f32;
    var fReactiveFactor: f32;
    var fReactiveContribution: f32;
    var param_66: f32;
    var param_67: f32;
    var param_68: vec3<f32>;
    var param_69: vec3<f32>;
    var param_70: vec3<f32>;
    var param_71: vec3<f32>;
    var fAccumulationMin: vec3<f32>;
    var param_72: vec3<f32>;
    var param_73: vec3<f32>;
    var param_74: vec3<f32>;
    var param_75: vec3<f32>;
    var param_76: vec3<f32>;
    var param_77: vec3<f32>;
    var phi_2341_: bool;
    let _e163 = DownscaleFactor_u0028_();
    let _e165 = DownscaleFactor_u0028_();
    param_48 = (1f / length((_e163.x * _e165.y)));
    param_49 = 3f;
    let _e170 = ffxPow_u0028_f1_u003b_f1_u003b((&param_48), (&param_49));
    param_50 = 20f;
    param_51 = _e170;
    let _e171 = ffxMin_u0028_f1_u003b_f1_u003b((&param_50), (&param_51));
    fScaleFactorInfluence = _e171;
    param_52 = (params_3.fHrVelocity / 20f);
    let _e174 = ffxSaturate_u0028_f1_u003b((&param_52));
    fVecolityFactor = _e174;
    param_53 = params_3.fAccumulationMask;
    let _e176 = fVecolityFactor;
    param_54 = _e176;
    let _e177 = ffxMax_u0028_f1_u003b_f1_u003b((&param_53), (&param_54));
    param_55 = params_3.fDepthClipFactor;
    param_56 = _e177;
    let _e179 = ffxMax_u0028_f1_u003b_f1_u003b((&param_55), (&param_56));
    fBoxScaleT = _e179;
    let _e180 = fScaleFactorInfluence;
    param_57 = _e180;
    param_58 = 1f;
    let _e181 = fBoxScaleT;
    param_59 = _e181;
    let _e182 = ffxLerp_u0028_f1_u003b_f1_u003b_f1_u003b((&param_57), (&param_58), (&param_59));
    fBoxScale = _e182;
    let _e184 = (*clippingBox).boxVec;
    let _e185 = fBoxScale;
    fScaledBoxVec = (_e184 * _e185);
    let _e188 = (*clippingBox).boxCenter;
    let _e189 = fScaledBoxVec;
    boxMin = (_e188 - _e189);
    let _e192 = (*clippingBox).boxCenter;
    let _e193 = fScaledBoxVec;
    boxMax = (_e192 + _e193);
    let _e196 = (*clippingBox).boxCenter;
    boxCenter = _e196;
    let _e198 = (*clippingBox).boxVec;
    boxVecSize = length(_e198);
    let _e201 = (*clippingBox).aabbMin;
    param_60 = _e201;
    let _e202 = boxMin;
    param_61 = _e202;
    let _e203 = ffxMax_u0028_vf3_u003b_vf3_u003b((&param_60), (&param_61));
    boxMin = _e203;
    let _e205 = (*clippingBox).aabbMax;
    param_62 = _e205;
    let _e206 = boxMax;
    param_63 = _e206;
    let _e207 = ffxMin_u0028_vf3_u003b_vf3_u003b((&param_62), (&param_63));
    boxMax = _e207;
    let _e208 = boxMin;
    let _e209 = (*fHistoryColor_1);
    let _e211 = any((_e208 > _e209));
    phi_2341_ = _e211;
    if !(_e211) {
        let _e213 = (*fHistoryColor_1);
        let _e214 = boxMax;
        phi_2341_ = any((_e213 > _e214));
    }
    let _e218 = phi_2341_;
    if _e218 {
        let _e219 = (*fHistoryColor_1);
        let _e220 = boxMin;
        let _e221 = boxMax;
        fClampedHistoryColor = clamp(_e219, _e220, _e221);
        let _e223 = (*fLumaInstabilityFactor);
        param_64 = _e223;
        let _e224 = (*fLockContributionThisFrame);
        param_65 = _e224;
        let _e225 = ffxMax_u0028_f1_u003b_f1_u003b((&param_64), (&param_65));
        fHistoryContribution = vec3(_e225);
        fReactiveFactor = params_3.fDilatedReactiveFactor;
        let _e228 = fReactiveFactor;
        param_66 = _e228;
        param_67 = 0.5f;
        let _e229 = ffxPow_u0028_f1_u003b_f1_u003b((&param_66), (&param_67));
        fReactiveContribution = (1f - _e229);
        let _e231 = fReactiveContribution;
        let _e232 = fHistoryContribution;
        fHistoryContribution = (_e232 * _e231);
        let _e234 = fHistoryContribution;
        param_68 = _e234;
        let _e235 = ffxSaturate_u0028_vf3_u003b((&param_68));
        let _e236 = fClampedHistoryColor;
        param_69 = _e236;
        let _e237 = (*fHistoryColor_1);
        param_70 = _e237;
        param_71 = _e235;
        let _e238 = ffxLerp_u0028_vf3_u003b_vf3_u003b_vf3_u003b((&param_69), (&param_70), (&param_71));
        (*fHistoryColor_1) = _e238;
        let _e239 = (*fAccumulation_1);
        param_72 = _e239;
        param_73 = vec3<f32>(0.1f, 0.1f, 0.1f);
        let _e240 = ffxMin_u0028_vf3_u003b_vf3_u003b((&param_72), (&param_73));
        fAccumulationMin = _e240;
        let _e241 = fHistoryContribution;
        param_74 = _e241;
        let _e242 = ffxSaturate_u0028_vf3_u003b((&param_74));
        let _e243 = fAccumulationMin;
        param_75 = _e243;
        let _e244 = (*fAccumulation_1);
        param_76 = _e244;
        param_77 = _e242;
        let _e245 = ffxLerp_u0028_vf3_u003b_vf3_u003b_vf3_u003b((&param_75), (&param_76), (&param_77));
        (*fAccumulation_1) = _e245;
    }
    return;
}
fn ComputeBaseAccumulationWeight_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_f1_u003b_b1_u003b_f1_u003b_struct_u002d_LockState_u002d_b1_u002d_b11_u003b(params_4: AccumulationPassCommonParams, fThisFrameReactiveFactor: ptr<function, f32>, bInMotionLastFrame: ptr<function, bool>, fUpsampledWeight_1: ptr<function, f32>, lockState: ptr<function, LockState>) -> vec3<f32> {
    var fBaseAccumulation: f32;
    var param_78: f32;
    var param_79: f32;
    var param_80: f32;
    var param_81: f32;
    var param_82: f32;
    var param_83: f32;
    var param_84: f32;
    var param_85: f32;
    var param_86: f32;
    var param_87: f32;
    var param_88: f32;
    var param_89: f32;
    var param_90: f32;
    var param_91: f32;
    let _e135 = (*fThisFrameReactiveFactor);
    fBaseAccumulation = (((1f * select(0f, 1f, params_4.bIsExistingSample)) * (1f - _e135)) * (1f - params_4.fDepthClipFactor));
    let _e141 = (*fUpsampledWeight_1);
    let _e143 = (*bInMotionLastFrame);
    param_78 = (params_4.fHrVelocity * 10f);
    let _e147 = ffxSaturate_u0028_f1_u003b((&param_78));
    param_79 = select(0f, 1f, _e143);
    param_80 = _e147;
    let _e148 = ffxMax_u0028_f1_u003b_f1_u003b((&param_79), (&param_80));
    let _e149 = fBaseAccumulation;
    param_81 = _e149;
    param_82 = (_e141 * 10f);
    param_83 = _e148;
)FSR2",
R"FSR2(    let _e150 = ffxLerp_u0028_f1_u003b_f1_u003b_f1_u003b((&param_81), (&param_82), (&param_83));
    let _e151 = fBaseAccumulation;
    param_84 = _e151;
    param_85 = _e150;
    let _e152 = ffxMin_u0028_f1_u003b_f1_u003b((&param_84), (&param_85));
    fBaseAccumulation = _e152;
    param_86 = (params_4.fHrVelocity / 20f);
    let _e155 = ffxSaturate_u0028_f1_u003b((&param_86));
    let _e156 = fBaseAccumulation;
    param_87 = _e156;
    let _e157 = (*fUpsampledWeight_1);
    param_88 = _e157;
    param_89 = _e155;
    let _e158 = ffxLerp_u0028_f1_u003b_f1_u003b_f1_u003b((&param_87), (&param_88), (&param_89));
    let _e159 = fBaseAccumulation;
    param_90 = _e159;
    param_91 = _e158;
    let _e160 = ffxMin_u0028_f1_u003b_f1_u003b((&param_90), (&param_91));
    fBaseAccumulation = _e160;
    let _e161 = fBaseAccumulation;
    return vec3(_e161);
}
fn StoreLumaHistory_u0028_vi2_u003b_vf4_u003b(iPxPos_3: ptr<function, vec2<i32>>, fLumaHistory: ptr<function, vec4<f32>>) {
    let _e114 = (*iPxPos_3);
    let _e115 = (*fLumaHistory);
    textureStore(rw_luma_history, _e114, _e115);
    return;
}
fn SampleLumaHistory_u0028_vf2_u003b(fUV: ptr<function, vec2<f32>>) -> vec4<f32> {
    let _e113 = (*fUV);
    let _e114 = textureSampleLevel(r_luma_history, s_LinearClamp, _e113, 0f);
    return _e114;
}
fn ComputeLumaInstabilityFactor_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_struct_u002d_RectificationBox_u002d_vf3_u002d_vf3_u002d_vf3_u002d_vf3_u002d_f11_u003b_f1_u003b_f1_u003b(params_5: AccumulationPassCommonParams, clippingBox_1: ptr<function, RectificationBox>, fThisFrameReactiveFactor_1: ptr<function, f32>, fLuminanceDiff: ptr<function, f32>) -> f32 {
    var fCurrentFrameLuma: f32;
    var param_92: f32;
    var param_93: f32;
    var bSampleLumaHistory: bool;
    var param_94: f32;
    var param_95: f32;
    var param_96: f32;
    var param_97: f32;
    var fCurrentFrameLumaHistory: vec4<f32>;
    var local: vec4<f32>;
    var param_98: vec2<f32>;
    var fLumaInstability: f32;
    var fDiffs0_: f32;
    var fMin: f32;
    var i: i32;
    var fDiffs1_: f32;
    var param_99: f32;
    var param_100: f32;
    var fBoxSize: f32;
    var fBoxSizeFactor: f32;
    var param_101: f32;
    var param_102: f32;
    var param_103: f32;
    var param_104: f32;
    var param_105: f32;
    var param_106: f32;
    var param_107: f32;
    var param_108: vec2<i32>;
    var param_109: vec4<f32>;
    var phi_2505_: bool;
    let _e147 = (*clippingBox_1).boxCenter[0u];
    fCurrentFrameLuma = _e147;
    let _e148 = fCurrentFrameLuma;
    param_92 = 0f;
    let _e149 = fCurrentFrameLuma;
    param_93 = _e149;
    let _e150 = ffxMax_u0028_f1_u003b_f1_u003b((&param_92), (&param_93));
    fCurrentFrameLuma = (_e148 / (1f + _e150));
    let _e153 = fCurrentFrameLuma;
    fCurrentFrameLuma = (round((_e153 * 255f)) / 255f);
    param_94 = params_5.fDepthClipFactor;
    param_95 = params_5.fAccumulationMask;
    let _e159 = ffxMax_u0028_f1_u003b_f1_u003b((&param_94), (&param_95));
    param_96 = _e159;
    let _e160 = (*fLuminanceDiff);
    param_97 = _e160;
    let _e161 = ffxMax_u0028_f1_u003b_f1_u003b((&param_96), (&param_97));
    let _e162 = (_e161 < 0.1f);
    phi_2505_ = _e162;
    if _e162 {
        phi_2505_ = (params_5.bIsNewSample == false);
    }
    let _e166 = phi_2505_;
    bSampleLumaHistory = _e166;
    let _e167 = bSampleLumaHistory;
    if _e167 {
        param_98 = params_5.fReprojectedHrUv;
        let _e169 = SampleLumaHistory_u0028_vf2_u003b((&param_98));
        local = _e169;
    } else {
        local = vec4<f32>(0f, 0f, 0f, 0f);
    }
    let _e170 = local;
    fCurrentFrameLumaHistory = _e170;
    fLumaInstability = 0f;
    let _e171 = fCurrentFrameLuma;
    let _e173 = fCurrentFrameLumaHistory[0u];
    fDiffs0_ = (_e171 - _e173);
    let _e175 = fDiffs0_;
    fMin = abs(_e175);
    let _e177 = fMin;
    if (_e177 >= 0.003921569f) {
        i = 1i;
        loop {
            let _e179 = i;
            if (_e179 <= 3i) {
                let _e181 = fCurrentFrameLuma;
                let _e182 = i;
                let _e184 = fCurrentFrameLumaHistory[_e182];
                fDiffs1_ = (_e181 - _e184);
                let _e186 = fDiffs0_;
                let _e188 = fDiffs1_;
                if (sign(_e186) == sign(_e188)) {
                    let _e191 = fDiffs1_;
                    let _e194 = fMin;
                    param_99 = _e194;
                    param_100 = (abs(_e191) * 1f);
                    let _e195 = ffxMin_u0028_f1_u003b_f1_u003b((&param_99), (&param_100));
                    fMin = _e195;
                }
                continue;
            } else {
                break;
            }
            continuing {
                let _e196 = i;
                i = (_e196 + 1i);
            }
        }
        let _e200 = (*clippingBox_1).boxVec[0u];
        fBoxSize = _e200;
        let _e201 = fBoxSize;
        param_101 = (_e201 / 0.1f);
        let _e203 = ffxSaturate_u0028_f1_u003b((&param_101));
        param_102 = _e203;
        param_103 = 6f;
        let _e204 = ffxPow_u0028_f1_u003b_f1_u003b((&param_102), (&param_103));
        fBoxSizeFactor = _e204;
        let _e205 = fMin;
        let _e206 = fDiffs0_;
        let _e210 = fBoxSizeFactor;
        fLumaInstability = (select(0f, 1f, (_e205 != abs(_e206))) * _e210);
        let _e212 = fLumaInstability;
        fLumaInstability = select(0f, 1f, (_e212 > 0.003921569f));
        let _e215 = (*fThisFrameReactiveFactor_1);
        param_104 = _e215;
        param_105 = 0.16666667f;
        let _e216 = ffxPow_u0028_f1_u003b_f1_u003b((&param_104), (&param_105));
        param_106 = params_5.fAccumulationMask;
        param_107 = _e216;
        let _e218 = ffxMax_u0028_f1_u003b_f1_u003b((&param_106), (&param_107));
        let _e220 = fLumaInstability;
        fLumaInstability = (_e220 * (1f - _e218));
    }
    let _e223 = fCurrentFrameLumaHistory[2u];
    fCurrentFrameLumaHistory[3u] = _e223;
    let _e226 = fCurrentFrameLumaHistory[1u];
    fCurrentFrameLumaHistory[2u] = _e226;
    let _e229 = fCurrentFrameLumaHistory[0u];
    fCurrentFrameLumaHistory[1u] = _e229;
    let _e231 = fCurrentFrameLuma;
    fCurrentFrameLumaHistory[0u] = _e231;
    param_108 = params_5.iPxHrPos;
    let _e234 = fCurrentFrameLumaHistory;
    param_109 = _e234;
    StoreLumaHistory_u0028_vi2_u003b_vf4_u003b((&param_108), (&param_109));
    let _e235 = fLumaInstability;
    let _e237 = fCurrentFrameLumaHistory[3u];
    return (_e235 * select(0f, 1f, (_e237 != 0f)));
}
fn Deringing_u0028_struct_u002d_RectificationBox_u002d_vf3_u002d_vf3_u002d_vf3_u002d_vf3_u002d_f11_u003b_vf3_u003b(clippingBox_2: ptr<function, RectificationBox>, fColor: ptr<function, vec3<f32>>) {
    let _e114 = (*fColor);
    let _e116 = (*clippingBox_2).aabbMin;
    let _e118 = (*clippingBox_2).aabbMax;
    (*fColor) = clamp(_e114, _e116, _e118);
    return;
}
fn RectificationBoxComputeVarianceBoxData_u0028_struct_u002d_RectificationBox_u002d_vf3_u002d_vf3_u002d_vf3_u002d_vf3_u002d_f11_u003b(rectificationBox: ptr<function, RectificationBox>) {
    var local_1: f32;
    var stdDev: vec3<f32>;
    let _e116 = (*rectificationBox).fBoxCenterWeight;
    if (abs(_e116) > 0.001f) {
        let _e120 = (*rectificationBox).fBoxCenterWeight;
        local_1 = _e120;
    } else {
        local_1 = 1f;
    }
    let _e121 = local_1;
    (*rectificationBox).fBoxCenterWeight = _e121;
    let _e124 = (*rectificationBox).fBoxCenterWeight;
    let _e126 = (*rectificationBox).boxCenter;
    (*rectificationBox).boxCenter = (_e126 / vec3(_e124));
    let _e131 = (*rectificationBox).fBoxCenterWeight;
    let _e133 = (*rectificationBox).boxVec;
    (*rectificationBox).boxVec = (_e133 / vec3(_e131));
    let _e138 = (*rectificationBox).boxVec;
    let _e140 = (*rectificationBox).boxCenter;
    let _e142 = (*rectificationBox).boxCenter;
    stdDev = sqrt(abs((_e138 - (_e140 * _e142))));
    let _e147 = stdDev;
    (*rectificationBox).boxVec = _e147;
    return;
}
fn RectificationBoxAddInitialSample_u0028_struct_u002d_RectificationBox_u002d_vf3_u002d_vf3_u002d_vf3_u002d_vf3_u002d_f11_u003b_vf3_u003b_f1_u003b(rectificationBox_1: ptr<function, RectificationBox>, colorSample: vec3<f32>, fSampleWeight: f32) {
    var weightedSample: vec3<f32>;
    (*rectificationBox_1).aabbMin = colorSample;
    (*rectificationBox_1).aabbMax = colorSample;
    weightedSample = (colorSample * fSampleWeight);
    let _e119 = weightedSample;
    (*rectificationBox_1).boxCenter = _e119;
    let _e121 = weightedSample;
    (*rectificationBox_1).boxVec = (colorSample * _e121);
    (*rectificationBox_1).fBoxCenterWeight = fSampleWeight;
    return;
}
fn RectificationBoxAddSample_u0028_b1_u003b_struct_u002d_RectificationBox_u002d_vf3_u002d_vf3_u002d_vf3_u002d_vf3_u002d_f11_u003b_vf3_u003b_f1_u003b(bInitialSample: ptr<function, bool>, rectificationBox_2: ptr<function, RectificationBox>, colorSample_1: vec3<f32>, fSampleWeight_1: f32) {
    var param_110: RectificationBox;
    var param_111: vec3<f32>;
    var param_112: vec3<f32>;
    var param_113: vec3<f32>;
    var param_114: vec3<f32>;
    var weightedSample_1: vec3<f32>;
    let _e122 = (*bInitialSample);
    if _e122 {
        let _e123 = (*rectificationBox_2);
        param_110 = _e123;
        RectificationBoxAddInitialSample_u0028_struct_u002d_RectificationBox_u002d_vf3_u002d_vf3_u002d_vf3_u002d_vf3_u002d_f11_u003b_vf3_u003b_f1_u003b((&param_110), colorSample_1, fSampleWeight_1);
        let _e124 = param_110;
        (*rectificationBox_2) = _e124;
    } else {
        let _e126 = (*rectificationBox_2).aabbMin;
        param_111 = _e126;
        param_112 = colorSample_1;
        let _e127 = ffxMin_u0028_vf3_u003b_vf3_u003b((&param_111), (&param_112));
        (*rectificationBox_2).aabbMin = _e127;
        let _e130 = (*rectificationBox_2).aabbMax;
        param_113 = _e130;
        param_114 = colorSample_1;
        let _e131 = ffxMax_u0028_vf3_u003b_vf3_u003b((&param_113), (&param_114));
        (*rectificationBox_2).aabbMax = _e131;
        weightedSample_1 = (colorSample_1 * fSampleWeight_1);
        let _e134 = weightedSample_1;
        let _e136 = (*rectificationBox_2).boxCenter;
        (*rectificationBox_2).boxCenter = (_e136 + _e134);
        let _e139 = weightedSample_1;
        let _e142 = (*rectificationBox_2).boxVec;
        (*rectificationBox_2).boxVec = (_e142 + (colorSample_1 * _e139));
        let _e146 = (*rectificationBox_2).fBoxCenterWeight;
        (*rectificationBox_2).fBoxCenterWeight = (_e146 + fSampleWeight_1);
    }
    return;
}
fn Lanczos2ApproxSqNoClamp_u0028_f1_u003b(x2_: ptr<function, f32>) -> f32 {
    var a: f32;
    var b: f32;
    let _e115 = (*x2_);
    a = ((0.4f * _e115) - 1f);
    let _e118 = (*x2_);
    b = ((0.25f * _e118) - 1f);
    let _e121 = a;
    let _e123 = a;
    let _e126 = b;
    let _e127 = b;
    return ((((1.5625f * _e121) * _e123) - 0.5625f) * (_e126 * _e127));
}
fn Lanczos2ApproxSq_u0028_f1_u003b(x2_1: ptr<function, f32>) -> f32 {
    var param_115: f32;
    var param_116: f32;
    var param_117: f32;
    let _e116 = (*x2_1);
    param_115 = _e116;
    param_116 = 4f;
    let _e117 = ffxMin_u0028_f1_u003b_f1_u003b((&param_115), (&param_116));
    (*x2_1) = _e117;
    let _e118 = (*x2_1);
    param_117 = _e118;
    let _e119 = Lanczos2ApproxSqNoClamp_u0028_f1_u003b((&param_117));
    return _e119;
}
fn GetUpsampleLanczosWeight_u0028_vf2_u003b_f1_u003b(fSrcSampleOffset: ptr<function, vec2<f32>>, fKernelWeight: ptr<function, f32>) -> f32 {
    var fSrcSampleOffsetBiased: vec2<f32>;
    var fSampleWeight_2: f32;
    var param_118: f32;
    let _e117 = (*fSrcSampleOffset);
    let _e118 = (*fKernelWeight);
    fSrcSampleOffsetBiased = (_e117 * vec2(_e118));
    let _e121 = fSrcSampleOffsetBiased;
    let _e122 = fSrcSampleOffsetBiased;
    param_118 = dot(_e121, _e122);
)FSR2",
R"FSR2(    let _e124 = Lanczos2ApproxSq_u0028_f1_u003b((&param_118));
    fSampleWeight_2 = _e124;
    let _e125 = fSampleWeight_2;
    return _e125;
}
fn IsOnScreen_u0028_vi2_u003b_vi2_u003b(pos: ptr<function, vec2<i32>>, size: ptr<function, vec2<i32>>) -> bool {
    let _e114 = (*pos);
    let _e116 = (*size);
    return all((bitcast<vec2<u32>>(_e114) < bitcast<vec2<u32>>(_e116)));
}
fn ComputeMaxKernelWeight_u0028_() -> f32 {
    var fKernelWeight_1: f32;
    var param_119: f32;
    var param_120: f32;
    let _e115 = DownscaleFactor_u0028_();
    fKernelWeight_1 = (1f + (((vec2(1f) / _e115) - vec2(1f)).x * 1f));
    param_119 = 1.99f;
    let _e123 = fKernelWeight_1;
    param_120 = _e123;
    let _e124 = ffxMin_u0028_f1_u003b_f1_u003b((&param_119), (&param_120));
    return _e124;
}
fn LoadPreparedInputColor_u0028_vi2_u003b(iPxPos_4: ptr<function, vec2<i32>>) -> vec3<f32> {
    let _e113 = (*iPxPos_4);
    let _e114 = textureLoad(r_prepared_input_color, _e113, 0i);
    return _e114.xyz;
}
fn ffxMin_u0028_i1_u003b_i1_u003b(x_9: ptr<function, i32>, y_7: ptr<function, i32>) -> i32 {
    let _e114 = (*x_9);
    let _e115 = (*y_7);
    return min(_e114, _e115);
}
fn ffxMax_u0028_i1_u003b_i1_u003b(x_10: ptr<function, i32>, y_8: ptr<function, i32>) -> i32 {
    let _e114 = (*x_10);
    let _e115 = (*y_8);
    return max(_e114, _e115);
}
fn ClampLoad_u0028_vi2_u003b_vi2_u003b_vi2_u003b(iPxSample: ptr<function, vec2<i32>>, iPxOffset: ptr<function, vec2<i32>>, iTextureSize: ptr<function, vec2<i32>>) -> vec2<i32> {
    var result: vec2<i32>;
    var local_2: i32;
    var param_121: i32;
    var param_122: i32;
    var local_3: i32;
    var param_123: i32;
    var param_124: i32;
    var local_4: i32;
    var param_125: i32;
    var param_126: i32;
    var local_5: i32;
    var param_127: i32;
    var param_128: i32;
    let _e128 = (*iPxSample);
    let _e129 = (*iPxOffset);
    result = (_e128 + _e129);
    let _e132 = (*iPxOffset)[0u];
    if (_e132 < 0i) {
        let _e135 = result[0u];
        param_121 = _e135;
        param_122 = 0i;
        let _e136 = ffxMax_u0028_i1_u003b_i1_u003b((&param_121), (&param_122));
        local_2 = _e136;
    } else {
        let _e138 = result[0u];
        local_2 = _e138;
    }
    let _e139 = local_2;
    result[0u] = _e139;
    let _e142 = (*iPxOffset)[0u];
    if (_e142 > 0i) {
        let _e145 = (*iTextureSize)[0u];
        let _e148 = result[0u];
        param_123 = _e148;
        param_124 = (_e145 - 1i);
        let _e149 = ffxMin_u0028_i1_u003b_i1_u003b((&param_123), (&param_124));
        local_3 = _e149;
    } else {
        let _e151 = result[0u];
        local_3 = _e151;
    }
    let _e152 = local_3;
    result[0u] = _e152;
    let _e155 = (*iPxOffset)[1u];
    if (_e155 < 0i) {
        let _e158 = result[1u];
        param_125 = _e158;
        param_126 = 0i;
        let _e159 = ffxMax_u0028_i1_u003b_i1_u003b((&param_125), (&param_126));
        local_4 = _e159;
    } else {
        let _e161 = result[1u];
        local_4 = _e161;
    }
    let _e162 = local_4;
    result[1u] = _e162;
    let _e165 = (*iPxOffset)[1u];
    if (_e165 > 0i) {
        let _e168 = (*iTextureSize)[1u];
        let _e171 = result[1u];
        param_127 = _e171;
        param_128 = (_e168 - 1i);
        let _e172 = ffxMin_u0028_i1_u003b_i1_u003b((&param_127), (&param_128));
        local_5 = _e172;
    } else {
        let _e174 = result[1u];
        local_5 = _e174;
    }
    let _e175 = local_5;
    result[1u] = _e175;
    let _e177 = result;
    return _e177;
}
fn RenderSize_u0028_() -> vec2<i32> {
    let _e113 = cbFSR2_.iRenderSize;
    return _e113;
}
fn Jitter_u0028_() -> vec2<f32> {
    let _e113 = cbFSR2_.fJitter;
    return _e113;
}
fn ComputeUpsampledColorAndWeight_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_struct_u002d_RectificationBox_u002d_vf3_u002d_vf3_u002d_vf3_u002d_vf3_u002d_f11_u003b_f1_u003b(params_6: AccumulationPassCommonParams, clippingBox_3: ptr<function, RectificationBox>, fReactiveFactor_1: ptr<function, f32>) -> vec4<f32> {
    var fDstOutputPos: vec2<f32>;
    var fSrcOutputPos: vec2<f32>;
    var iSrcInputPos: vec2<i32>;
    var fSrcUnjitteredPos: vec2<f32>;
    var offsetTL: vec2<i32>;
    var bFlipRow: bool;
    var bFlipCol: bool;
    var fOffsetTL: vec2<f32>;
    var row: i32;
    var col: i32;
    var iSampleIndex: i32;
    var sampleColRow: vec2<i32>;
    var local_6: i32;
    var local_7: i32;
    var iSrcSamplePos: vec2<i32>;
    var sampleCoord: vec2<i32>;
    var param_129: vec2<i32>;
    var param_130: vec2<i32>;
    var param_131: vec2<i32>;
    var fSamples: array<vec3<f32>, 16>;
    var param_132: vec2<i32>;
    var fColorAndWeight_1: vec4<f32>;
    var fBaseSampleOffset: vec2<f32>;
    var fKernelReactiveFactor: f32;
    var param_133: f32;
    var param_134: f32;
    var fKernelBiasMax: f32;
    var fKernelBiasMin: f32;
    var param_135: f32;
    var param_136: f32;
    var fKernelBiasFactor: f32;
    var param_137: f32;
    var param_138: f32;
    var param_139: f32;
    var param_140: f32;
    var fKernelBias: f32;
    var param_141: f32;
    var param_142: f32;
    var param_143: f32;
    var fRectificationCurveBias: f32;
    var param_144: f32;
    var param_145: f32;
    var param_146: f32;
    var param_147: f32;
    var row_1: i32;
    var col_1: i32;
    var iSampleIndex_1: i32;
    var sampleColRow_1: vec2<i32>;
    var local_8: i32;
    var local_9: i32;
    var fOffset: vec2<f32>;
    var fSrcSampleOffset_1: vec2<f32>;
    var iSrcSamplePos_1: vec2<i32>;
    var fOnScreenFactor: f32;
    var param_148: vec2<i32>;
    var param_149: vec2<i32>;
    var fSampleWeight_3: f32;
    var param_150: vec2<f32>;
    var param_151: f32;
    var fSrcSampleOffsetSq: f32;
    var fBoxSampleWeight: f32;
    var bInitialSample_1: bool;
    var param_152: bool;
    var param_153: RectificationBox;
    var param_154: RectificationBox;
    var param_155: RectificationBox;
    var param_156: vec3<f32>;
    fDstOutputPos = (vec2<f32>(params_6.iPxHrPos) + vec2<f32>(0.5f, 0.5f));
    let _e185 = fDstOutputPos;
    let _e186 = DownscaleFactor_u0028_();
    fSrcOutputPos = (_e185 * _e186);
    let _e188 = fSrcOutputPos;
    iSrcInputPos = vec2<i32>(floor(_e188));
    let _e191 = iSrcInputPos;
    let _e194 = Jitter_u0028_();
    fSrcUnjitteredPos = ((vec2<f32>(_e191) + vec2<f32>(0.5f, 0.5f)) - _e194);
    let _e197 = fSrcUnjitteredPos[0u];
    let _e199 = fSrcOutputPos[0u];
    offsetTL[0u] = select(-1i, -2i, (_e197 > _e199));
    let _e204 = fSrcUnjitteredPos[1u];
    let _e206 = fSrcOutputPos[1u];
    offsetTL[1u] = select(-1i, -2i, (_e204 > _e206));
    let _e211 = fSrcUnjitteredPos[1u];
    let _e213 = fSrcOutputPos[1u];
    bFlipRow = (_e211 > _e213);
    let _e216 = fSrcUnjitteredPos[0u];
    let _e218 = fSrcOutputPos[0u];
    bFlipCol = (_e216 > _e218);
    let _e220 = offsetTL;
    fOffsetTL = vec2<f32>(_e220);
    row = 0i;
    loop {
        let _e222 = row;
        if (_e222 < 3i) {
            col = 0i;
            loop {
                let _e224 = col;
                if (_e224 < 3i) {
                    let _e226 = col;
                    let _e227 = row;
                    iSampleIndex = (_e226 + (_e227 << bitcast<u32>(2i)));
                    let _e231 = bFlipCol;
                    if _e231 {
                        let _e232 = col;
                        local_6 = (3i - _e232);
                    } else {
                        let _e234 = col;
                        local_6 = _e234;
                    }
                    let _e235 = local_6;
                    let _e236 = bFlipRow;
                    if _e236 {
                        let _e237 = row;
                        local_7 = (3i - _e237);
                    } else {
                        let _e239 = row;
                        local_7 = _e239;
                    }
                    let _e240 = local_7;
                    sampleColRow = vec2<i32>(_e235, _e240);
                    let _e242 = iSrcInputPos;
                    let _e243 = offsetTL;
                    let _e245 = sampleColRow;
                    iSrcSamplePos = ((_e242 + _e243) + _e245);
                    let _e247 = RenderSize_u0028_();
                    let _e248 = iSrcSamplePos;
                    param_129 = _e248;
                    param_130 = vec2<i32>(0i, 0i);
                    param_131 = _e247;
                    let _e249 = ClampLoad_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_129), (&param_130), (&param_131));
                    sampleCoord = _e249;
                    let _e250 = iSampleIndex;
                    let _e251 = sampleCoord;
                    param_132 = _e251;
                    let _e252 = LoadPreparedInputColor_u0028_vi2_u003b((&param_132));
                    fSamples[_e250] = _e252;
                    continue;
                } else {
                    break;
                }
                continuing {
                    let _e254 = col;
                    col = (_e254 + 1i);
                }
            }
            continue;
        } else {
            break;
        }
        continuing {
            let _e256 = row;
            row = (_e256 + 1i);
        }
    }
    fColorAndWeight_1 = vec4<f32>(0f, 0f, 0f, 0f);
    let _e258 = fSrcUnjitteredPos;
    let _e259 = fSrcOutputPos;
    fBaseSampleOffset = (_e258 - _e259);
    let _e263 = (*fReactiveFactor_1);
    param_133 = _e263;
    param_134 = select(0f, 1f, params_6.bIsNewSample);
    let _e264 = ffxMax_u0028_f1_u003b_f1_u003b((&param_133), (&param_134));
    fKernelReactiveFactor = _e264;
    let _e265 = ComputeMaxKernelWeight_u0028_();
    let _e266 = fKernelReactiveFactor;
    fKernelBiasMax = (_e265 * (1f - _e266));
    let _e269 = fKernelBiasMax;
    param_135 = 1f;
    param_136 = ((1f + _e269) * 0.3f);
    let _e272 = ffxMax_u0028_f1_u003b_f1_u003b((&param_135), (&param_136));
    fKernelBiasMin = _e272;
    param_137 = (0.25f * params_6.fDepthClipFactor);
    let _e275 = fKernelReactiveFactor;
    param_138 = _e275;
    let _e276 = ffxMax_u0028_f1_u003b_f1_u003b((&param_137), (&param_138));
    param_139 = 0f;
    param_140 = _e276;
    let _e277 = ffxMax_u0028_f1_u003b_f1_u003b((&param_139), (&param_140));
    fKernelBiasFactor = _e277;
    let _e278 = fKernelBiasMax;
    param_141 = _e278;
    let _e279 = fKernelBiasMin;
    param_142 = _e279;
    let _e280 = fKernelBiasFactor;
    param_143 = _e280;
    let _e281 = ffxLerp_u0028_f1_u003b_f1_u003b_f1_u003b((&param_141), (&param_142), (&param_143));
    fKernelBias = _e281;
    param_144 = (params_6.fHrVelocity / 50f);
    let _e284 = ffxSaturate_u0028_f1_u003b((&param_144));
    param_145 = -2f;
    param_146 = -3f;
    param_147 = _e284;
    let _e285 = ffxLerp_u0028_f1_u003b_f1_u003b_f1_u003b((&param_145), (&param_146), (&param_147));
    fRectificationCurveBias = _e285;
    row_1 = 0i;
    loop {
        let _e286 = row_1;
        if (_e286 < 3i) {
            col_1 = 0i;
            loop {
                let _e288 = col_1;
                if (_e288 < 3i) {
                    let _e290 = col_1;
                    let _e291 = row_1;
                    iSampleIndex_1 = (_e290 + (_e291 << bitcast<u32>(2i)));
                    let _e295 = bFlipCol;
                    if _e295 {
                        let _e296 = col_1;
                        local_8 = (3i - _e296);
                    } else {
                        let _e298 = col_1;
                        local_8 = _e298;
                    }
                    let _e299 = local_8;
                    let _e300 = bFlipRow;
                    if _e300 {
                        let _e301 = row_1;
                        local_9 = (3i - _e301);
                    } else {
                        let _e303 = row_1;
                        local_9 = _e303;
                    }
)FSR2",
R"FSR2(                    let _e304 = local_9;
                    sampleColRow_1 = vec2<i32>(_e299, _e304);
                    let _e306 = fOffsetTL;
                    let _e307 = sampleColRow_1;
                    fOffset = (_e306 + vec2<f32>(_e307));
                    let _e310 = fBaseSampleOffset;
                    let _e311 = fOffset;
                    fSrcSampleOffset_1 = (_e310 + _e311);
                    let _e313 = iSrcInputPos;
                    let _e314 = offsetTL;
                    let _e316 = sampleColRow_1;
                    iSrcSamplePos_1 = ((_e313 + _e314) + _e316);
                    let _e318 = iSrcSamplePos_1;
                    let _e319 = RenderSize_u0028_();
                    param_148 = _e318;
                    param_149 = _e319;
                    let _e320 = IsOnScreen_u0028_vi2_u003b_vi2_u003b((&param_148), (&param_149));
                    fOnScreenFactor = select(0f, 1f, _e320);
                    let _e322 = fOnScreenFactor;
                    let _e323 = fSrcSampleOffset_1;
                    param_150 = _e323;
                    let _e324 = fKernelBias;
                    param_151 = _e324;
                    let _e325 = GetUpsampleLanczosWeight_u0028_vf2_u003b_f1_u003b((&param_150), (&param_151));
                    fSampleWeight_3 = (_e322 * _e325);
                    let _e327 = iSampleIndex_1;
                    let _e329 = fSamples[_e327];
                    let _e330 = fSampleWeight_3;
                    let _e331 = (_e329 * _e330);
                    let _e332 = fSampleWeight_3;
                    let _e337 = fColorAndWeight_1;
                    fColorAndWeight_1 = (_e337 + vec4<f32>(_e331.x, _e331.y, _e331.z, _e332));
                    let _e339 = fSrcSampleOffset_1;
                    let _e340 = fSrcSampleOffset_1;
                    fSrcSampleOffsetSq = dot(_e339, _e340);
                    let _e342 = fRectificationCurveBias;
                    let _e343 = fSrcSampleOffsetSq;
                    fBoxSampleWeight = exp((_e342 * _e343));
                    let _e346 = row_1;
                    let _e348 = col_1;
                    bInitialSample_1 = ((_e346 == 0i) && (_e348 == 0i));
                    let _e351 = iSampleIndex_1;
                    let _e353 = fSamples[_e351];
                    let _e354 = fBoxSampleWeight;
                    let _e355 = bInitialSample_1;
                    param_152 = _e355;
                    let _e356 = (*clippingBox_3);
                    param_153 = _e356;
                    RectificationBoxAddSample_u0028_b1_u003b_struct_u002d_RectificationBox_u002d_vf3_u002d_vf3_u002d_vf3_u002d_vf3_u002d_f11_u003b_vf3_u003b_f1_u003b((&param_152), (&param_153), _e353, _e354);
                    let _e357 = param_153;
                    (*clippingBox_3) = _e357;
                    continue;
                } else {
                    break;
                }
                continuing {
                    let _e358 = col_1;
                    col_1 = (_e358 + 1i);
                }
            }
            continue;
        } else {
            break;
        }
        continuing {
            let _e360 = row_1;
            row_1 = (_e360 + 1i);
        }
    }
    let _e362 = (*clippingBox_3);
    param_154 = _e362;
    RectificationBoxComputeVarianceBoxData_u0028_struct_u002d_RectificationBox_u002d_vf3_u002d_vf3_u002d_vf3_u002d_vf3_u002d_f11_u003b((&param_154));
    let _e363 = param_154;
    (*clippingBox_3) = _e363;
    let _e365 = fColorAndWeight_1[3u];
    let _e369 = fColorAndWeight_1[3u];
    fColorAndWeight_1[3u] = (_e369 * select(0f, 1f, (_e365 > 0.001f)));
    let _e373 = fColorAndWeight_1[3u];
    if (_e373 > 0.001f) {
        let _e375 = fColorAndWeight_1;
        let _e378 = fColorAndWeight_1[3u];
        let _e380 = (_e375.xyz / vec3(_e378));
        fColorAndWeight_1[0u] = _e380.x;
        fColorAndWeight_1[1u] = _e380.y;
        fColorAndWeight_1[2u] = _e380.z;
        let _e388 = fColorAndWeight_1[3u];
        fColorAndWeight_1[3u] = (_e388 * 0.083333336f);
        let _e391 = (*clippingBox_3);
        param_155 = _e391;
        let _e392 = fColorAndWeight_1;
        param_156 = _e392.xyz;
        Deringing_u0028_struct_u002d_RectificationBox_u002d_vf3_u002d_vf3_u002d_vf3_u002d_vf3_u002d_f11_u003b_vf3_u003b((&param_155), (&param_156));
        let _e394 = param_156;
        fColorAndWeight_1[0u] = _e394.x;
        fColorAndWeight_1[1u] = _e394.y;
        fColorAndWeight_1[2u] = _e394.z;
    }
    let _e401 = fColorAndWeight_1;
    return _e401;
}
fn MinDividedByMax_u0028_f1_u003b_f1_u003b(v0_: f32, v1_: f32) -> f32 {
    var m: f32;
    var param_157: f32;
    var param_158: f32;
    var local_10: f32;
    var param_159: f32;
    var param_160: f32;
    param_157 = v0_;
    param_158 = v1_;
    let _e120 = ffxMax_u0028_f1_u003b_f1_u003b((&param_157), (&param_158));
    m = _e120;
    let _e121 = m;
    if (_e121 != 0f) {
        param_159 = v0_;
        param_160 = v1_;
        let _e123 = ffxMin_u0028_f1_u003b_f1_u003b((&param_159), (&param_160));
        let _e124 = m;
        local_10 = (_e123 / _e124);
    } else {
        local_10 = 0f;
    }
    let _e126 = local_10;
    return _e126;
}
fn SampleMipLuma_u0028_vf2_u003b_i1_u003b(fUV_1: ptr<function, vec2<f32>>, mipLevel: ptr<function, i32>) -> f32 {
    let _e114 = (*fUV_1);
    let _e115 = (*mipLevel);
    let _e117 = textureSampleLevel(r_imgMips, s_LinearClamp, _e114, f32(_e115));
    return _e117.x;
}
fn ffxMax_u0028_vf2_u003b_vf2_u003b(x_11: ptr<function, vec2<f32>>, y_9: ptr<function, vec2<f32>>) -> vec2<f32> {
    let _e114 = (*x_11);
    let _e115 = (*y_9);
    return max(_e114, _e115);
}
fn ffxMin_u0028_vf2_u003b_vf2_u003b(x_12: ptr<function, vec2<f32>>, y_10: ptr<function, vec2<f32>>) -> vec2<f32> {
    let _e114 = (*x_12);
    let _e115 = (*y_10);
    return min(_e114, _e115);
}
fn ClampUv_u0028_vf2_u003b_vi2_u003b_vi2_u003b(fUv_1: ptr<function, vec2<f32>>, iTextureSize_1: ptr<function, vec2<i32>>, iResourceSize: ptr<function, vec2<i32>>) -> vec2<f32> {
    var fSampleLocation: vec2<f32>;
    var fClampedLocation: vec2<f32>;
    var param_161: vec2<f32>;
    var param_162: vec2<f32>;
    var param_163: vec2<f32>;
    var param_164: vec2<f32>;
    var fClampedUv: vec2<f32>;
    let _e122 = (*fUv_1);
    let _e123 = (*iTextureSize_1);
    fSampleLocation = (_e122 * vec2<f32>(_e123));
    let _e126 = (*iTextureSize_1);
    let _e129 = fSampleLocation;
    param_161 = _e129;
    param_162 = (vec2<f32>(_e126) - vec2<f32>(0.5f, 0.5f));
    let _e130 = ffxMin_u0028_vf2_u003b_vf2_u003b((&param_161), (&param_162));
    param_163 = vec2<f32>(0.5f, 0.5f);
    param_164 = _e130;
    let _e131 = ffxMax_u0028_vf2_u003b_vf2_u003b((&param_163), (&param_164));
    fClampedLocation = _e131;
    let _e132 = fClampedLocation;
    let _e133 = (*iResourceSize);
    fClampedUv = (_e132 / vec2<f32>(_e133));
    let _e136 = fClampedUv;
    return _e136;
}
fn LumaMipDimensions_u0028_() -> vec2<i32> {
    let _e113 = cbFSR2_.iLumaMipDimensions;
    return _e113;
}
fn LumaMipLevelToUse_u0028_() -> i32 {
    let _e113 = cbFSR2_.iLumaMipLevelToUse;
    return _e113;
}
fn GetShadingChangeLuma_u0028_vi2_u003b_vf2_u003b(iPxHrPos: ptr<function, vec2<i32>>, fUvCoord: ptr<function, vec2<f32>>) -> f32 {
    var fShadingChangeLuma: f32;
    var fDiv: f32;
    var iMipRenderSize: vec2<i32>;
    var param_165: vec2<f32>;
    var param_166: vec2<i32>;
    var param_167: vec2<i32>;
    var param_168: vec2<f32>;
    var param_169: i32;
    var param_170: f32;
    var param_171: f32;
    fShadingChangeLuma = 0f;
    let _e124 = LumaMipLevelToUse_u0028_();
    fDiv = f32((2i << bitcast<u32>(_e124)));
    let _e128 = RenderSize_u0028_();
    let _e130 = fDiv;
    iMipRenderSize = vec2<i32>((vec2<f32>(_e128) / vec2(_e130)));
    let _e134 = LumaMipDimensions_u0028_();
    let _e135 = (*fUvCoord);
    param_165 = _e135;
    let _e136 = iMipRenderSize;
    param_166 = _e136;
    param_167 = _e134;
    let _e137 = ClampUv_u0028_vf2_u003b_vi2_u003b_vi2_u003b((&param_165), (&param_166), (&param_167));
    (*fUvCoord) = _e137;
    let _e138 = Exposure_u0028_();
    let _e139 = LumaMipLevelToUse_u0028_();
    let _e140 = (*fUvCoord);
    param_168 = _e140;
    param_169 = _e139;
    let _e141 = SampleMipLuma_u0028_vf2_u003b_i1_u003b((&param_168), (&param_169));
    fShadingChangeLuma = (_e138 * exp(_e141));
    let _e144 = fShadingChangeLuma;
    param_170 = _e144;
    param_171 = 0.16666667f;
    let _e145 = ffxPow_u0028_f1_u003b_f1_u003b((&param_170), (&param_171));
    fShadingChangeLuma = _e145;
    let _e146 = fShadingChangeLuma;
    return _e146;
}
fn UpdateLockStatus_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_f1_u003b_struct_u002d_LockState_u002d_b1_u002d_b11_u003b_vf2_u003b_f1_u003b_f1_u003b(params_7: ptr<function, AccumulationPassCommonParams>, fReactiveFactor_2: ptr<function, f32>, state: ptr<function, LockState>, fLockStatus_2: ptr<function, vec2<f32>>, fLockContributionThisFrame_1: ptr<function, f32>, fLuminanceDiff_1: ptr<function, f32>) {
    var fShadingChangeLuma_1: f32;
    var param_172: vec2<i32>;
    var param_173: vec2<f32>;
    var local_11: f32;
    var fPreviousShadingChangeLuma: f32;
    var param_174: f32;
    var param_175: f32;
    var param_176: f32;
    var param_177: vec2<f32>;
    var param_178: f32;
    var param_179: f32;
    var param_180: f32;
    var param_181: f32;
    var fLifetimeContribution: f32;
    var param_182: f32;
    var fShadingChangeContribution: f32;
    var param_183: f32;
    var param_184: f32;
    var param_185: f32;
    let _e138 = (*params_7).iPxHrPos;
    param_172 = _e138;
    let _e140 = (*params_7).fHrUv;
    param_173 = _e140;
    let _e141 = GetShadingChangeLuma_u0028_vi2_u003b_vf2_u003b((&param_172), (&param_173));
    fShadingChangeLuma_1 = _e141;
    let _e143 = (*fLockStatus_2)[1u];
    if (_e143 == 0f) {
        let _e145 = fShadingChangeLuma_1;
        local_11 = _e145;
    } else {
        let _e147 = (*fLockStatus_2)[1u];
        local_11 = _e147;
    }
    let _e148 = local_11;
    (*fLockStatus_2)[1u] = _e148;
    let _e151 = (*fLockStatus_2)[1u];
    fPreviousShadingChangeLuma = _e151;
    let _e152 = fPreviousShadingChangeLuma;
    let _e153 = fShadingChangeLuma_1;
    let _e154 = MinDividedByMax_u0028_f1_u003b_f1_u003b(_e152, _e153);
    (*fLuminanceDiff_1) = (1f - _e154);
    let _e157 = (*state).NewLock;
    if _e157 {
        let _e158 = fShadingChangeLuma_1;
        (*fLockStatus_2)[1u] = _e158;
        let _e161 = (*fLockStatus_2)[0u];
        (*fLockStatus_2)[0u] = select(1f, 2f, (_e161 != 0f));
    } else {
        let _e166 = (*fLockStatus_2)[0u];
        if (_e166 <= 1f) {
            let _e168 = fShadingChangeLuma_1;
            let _e170 = (*fLockStatus_2)[1u];
            param_174 = _e170;
            param_175 = _e168;
            param_176 = 0.5f;
            let _e171 = ffxLerp_u0028_f1_u003b_f1_u003b_f1_u003b((&param_174), (&param_175), (&param_176));
            (*fLockStatus_2)[1u] = _e171;
        } else {
            let _e173 = (*fLuminanceDiff_1);
            if (_e173 > 0.1f) {
                let _e175 = (*fLockStatus_2);
                param_177 = _e175;
                KillLock_u0028_vf2_u003b((&param_177));
                let _e176 = param_177;
                (*fLockStatus_2) = _e176;
            }
        }
    }
    let _e177 = (*fLuminanceDiff_1);
    param_178 = ((_e177 - 0.1f) * 10f);
    let _e180 = ffxSaturate_u0028_f1_u003b((&param_178));
    let _e181 = (*fReactiveFactor_2);
    param_179 = _e181;
    param_180 = _e180;
    let _e182 = ffxMax_u0028_f1_u003b_f1_u003b((&param_179), (&param_180));
    (*fReactiveFactor_2) = _e182;
    let _e183 = (*fReactiveFactor_2);
    let _e186 = (*fLockStatus_2)[0u];
    (*fLockStatus_2)[0u] = (_e186 * (1f - _e183));
)FSR2",
R"FSR2(    let _e190 = (*params_7).fAccumulationMask;
    param_181 = (1f - _e190);
    let _e192 = ffxSaturate_u0028_f1_u003b((&param_181));
    let _e194 = (*fLockStatus_2)[0u];
    (*fLockStatus_2)[0u] = (_e194 * _e192);
    let _e198 = (*params_7).fDepthClipFactor;
    let _e202 = (*fLockStatus_2)[0u];
    (*fLockStatus_2)[0u] = (_e202 * select(0f, 1f, (_e198 < 0.1f)));
    let _e206 = (*fLockStatus_2)[0u];
    param_182 = (_e206 - 1f);
    let _e208 = ffxSaturate_u0028_f1_u003b((&param_182));
    fLifetimeContribution = _e208;
    let _e210 = (*fLockStatus_2)[1u];
    let _e211 = fShadingChangeLuma_1;
    let _e212 = MinDividedByMax_u0028_f1_u003b_f1_u003b(_e210, _e211);
    param_183 = _e212;
    let _e213 = ffxSaturate_u0028_f1_u003b((&param_183));
    fShadingChangeContribution = _e213;
    let _e214 = fLifetimeContribution;
    param_184 = (_e214 * 4f);
    let _e216 = ffxSaturate_u0028_f1_u003b((&param_184));
    let _e217 = fShadingChangeContribution;
    param_185 = (_e216 * _e217);
    let _e219 = ffxSaturate_u0028_f1_u003b((&param_185));
    (*fLockContributionThisFrame_1) = _e219;
    return;
}
fn SampleLockStatus_u0028_vf2_u003b(fUV_2: ptr<function, vec2<f32>>) -> vec2<f32> {
    var fLockStatus_3: vec2<f32>;
    let _e114 = (*fUV_2);
    let _e115 = textureSampleLevel(r_lock_status, s_LinearClamp, _e114, 0f);
    fLockStatus_3 = _e115.xy;
    let _e117 = fLockStatus_3;
    return _e117;
}
fn LoadRwNewLocks_u0028_vi2_u003b(iPxPos_5: ptr<function, vec2<i32>>) -> f32 {
    let _e113 = (*iPxPos_5);
    let _e114 = textureLoad(rw_new_locks, _e113);
    return _e114.x;
}
fn ReprojectHistoryLockStatus_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_vf2_u003b(params_8: AccumulationPassCommonParams, fReprojectedLockStatus: ptr<function, vec2<f32>>) -> LockState {
    var state_1: LockState;
    var fNewLockIntensity: f32;
    var param_186: vec2<i32>;
    var fInPlaceLockLifetime: f32;
    var param_187: vec2<f32>;
    state_1 = LockState(false, false);
    param_186 = params_8.iPxHrPos;
    let _e120 = LoadRwNewLocks_u0028_vi2_u003b((&param_186));
    fNewLockIntensity = _e120;
    let _e121 = fNewLockIntensity;
    state_1.NewLock = (_e121 > 0.49803922f);
    let _e125 = state_1.NewLock;
    let _e126 = fNewLockIntensity;
    fInPlaceLockLifetime = select(0f, _e126, _e125);
    param_187 = params_8.fReprojectedHrUv;
    let _e129 = SampleLockStatus_u0028_vf2_u003b((&param_187));
    (*fReprojectedLockStatus) = _e129;
    let _e131 = (*fReprojectedLockStatus)[0u];
    if (_e131 != 0f) {
        state_1.WasLockedPrevFrame = true;
    }
    let _e134 = state_1;
    return _e134;
}
fn PrepareRgb_u0028_vf3_u003b_f1_u003b_f1_u003b(fRgb_5: ptr<function, vec3<f32>>, fExposure_1: ptr<function, f32>, fPreExposure: ptr<function, f32>) -> vec3<f32> {
    let _e115 = (*fPreExposure);
    let _e116 = (*fRgb_5);
    (*fRgb_5) = (_e116 / vec3(_e115));
    let _e119 = (*fExposure_1);
    let _e120 = (*fRgb_5);
    (*fRgb_5) = (_e120 * _e119);
    let _e122 = (*fRgb_5);
    (*fRgb_5) = clamp(_e122, vec3(0f), vec3(65504f));
    let _e126 = (*fRgb_5);
    return _e126;
}
fn PreviousFramePreExposure_u0028_() -> f32 {
    let _e113 = cbFSR2_.fPreviousFramePreExposure;
    return _e113;
}
fn ffxMax_u0028_vf4_u003b_vf4_u003b(x_13: ptr<function, vec4<f32>>, y_11: ptr<function, vec4<f32>>) -> vec4<f32> {
    let _e114 = (*x_13);
    let _e115 = (*y_11);
    return max(_e114, _e115);
}
fn ffxMin_u0028_vf4_u003b_vf4_u003b(x_14: ptr<function, vec4<f32>>, y_12: ptr<function, vec4<f32>>) -> vec4<f32> {
    let _e114 = (*x_14);
    let _e115 = (*y_12);
    return min(_e114, _e115);
}
fn Lanczos2NoClamp_u0028_f1_u003b(x_15: ptr<function, f32>) -> f32 {
    var local_12: f32;
    let _e114 = (*x_15);
    if (abs(_e114) < 0.001f) {
        local_12 = 1f;
    } else {
        let _e117 = (*x_15);
        let _e120 = (*x_15);
        let _e123 = (*x_15);
        let _e126 = (*x_15);
        local_12 = ((sin((3.1415927f * _e117)) / (3.1415927f * _e120)) * (sin((1.5707964f * _e123)) / (1.5707964f * _e126)));
    }
    let _e130 = local_12;
    return _e130;
}
fn Lanczos2_u0028_f1_u003b(x_16: ptr<function, f32>) -> f32 {
    var param_188: f32;
    var param_189: f32;
    var param_190: f32;
    let _e116 = (*x_16);
    param_188 = abs(_e116);
    param_189 = 2f;
    let _e118 = ffxMin_u0028_f1_u003b_f1_u003b((&param_188), (&param_189));
    (*x_16) = _e118;
    let _e119 = (*x_16);
    param_190 = _e119;
    let _e120 = Lanczos2NoClamp_u0028_f1_u003b((&param_190));
    return _e120;
}
fn Lanczos2_u0028_vf4_u003b_vf4_u003b_vf4_u003b_vf4_u003b_f1_u003b(fColor0_: ptr<function, vec4<f32>>, fColor1_: ptr<function, vec4<f32>>, fColor2_: ptr<function, vec4<f32>>, fColor3_: ptr<function, vec4<f32>>, t_2: ptr<function, f32>) -> vec4<f32> {
    var fWeight0_: f32;
    var param_191: f32;
    var fWeight1_: f32;
    var param_192: f32;
    var fWeight2_: f32;
    var param_193: f32;
    var fWeight3_: f32;
    var param_194: f32;
    let _e125 = (*t_2);
    param_191 = (-1f - _e125);
    let _e127 = Lanczos2_u0028_f1_u003b((&param_191));
    fWeight0_ = _e127;
    let _e128 = (*t_2);
    param_192 = (-0f - _e128);
    let _e130 = Lanczos2_u0028_f1_u003b((&param_192));
    fWeight1_ = _e130;
    let _e131 = (*t_2);
    param_193 = (1f - _e131);
    let _e133 = Lanczos2_u0028_f1_u003b((&param_193));
    fWeight2_ = _e133;
    let _e134 = (*t_2);
    param_194 = (2f - _e134);
    let _e136 = Lanczos2_u0028_f1_u003b((&param_194));
    fWeight3_ = _e136;
    let _e137 = fWeight0_;
    let _e138 = (*fColor0_);
    let _e140 = fWeight1_;
    let _e141 = (*fColor1_);
    let _e144 = fWeight2_;
    let _e145 = (*fColor2_);
    let _e148 = fWeight3_;
    let _e149 = (*fColor3_);
    let _e152 = fWeight0_;
    let _e153 = fWeight1_;
    let _e155 = fWeight2_;
    let _e157 = fWeight3_;
    return (((((_e138 * _e137) + (_e141 * _e140)) + (_e145 * _e144)) + (_e149 * _e148)) / vec4((((_e152 + _e153) + _e155) + _e157)));
}
fn Lanczos2_u0028_struct_u002d_FetchedBicubicSamples_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf41_u003b_vf2_u003b(Samples: ptr<function, FetchedBicubicSamples>, fPxFrac: ptr<function, vec2<f32>>) -> vec4<f32> {
    var fColorX0_: vec4<f32>;
    var param_195: vec4<f32>;
    var param_196: vec4<f32>;
    var param_197: vec4<f32>;
    var param_198: vec4<f32>;
    var param_199: f32;
    var fColorX1_: vec4<f32>;
    var param_200: vec4<f32>;
    var param_201: vec4<f32>;
    var param_202: vec4<f32>;
    var param_203: vec4<f32>;
    var param_204: f32;
    var fColorX2_: vec4<f32>;
    var param_205: vec4<f32>;
    var param_206: vec4<f32>;
    var param_207: vec4<f32>;
    var param_208: vec4<f32>;
    var param_209: f32;
    var fColorX3_: vec4<f32>;
    var param_210: vec4<f32>;
    var param_211: vec4<f32>;
    var param_212: vec4<f32>;
    var param_213: vec4<f32>;
    var param_214: f32;
    var fColorXY: vec4<f32>;
    var param_215: vec4<f32>;
    var param_216: vec4<f32>;
    var param_217: vec4<f32>;
    var param_218: vec4<f32>;
    var param_219: f32;
    var fDeringingSamples: array<vec4<f32>, 4>;
    var fDeringingMin: vec4<f32>;
    var fDeringingMax: vec4<f32>;
    var iSampleIndex_2: i32;
    var param_220: vec4<f32>;
    var param_221: vec4<f32>;
    var param_222: vec4<f32>;
    var param_223: vec4<f32>;
    let _e153 = (*Samples).fColor00_;
    param_195 = _e153;
    let _e155 = (*Samples).fColor10_;
    param_196 = _e155;
    let _e157 = (*Samples).fColor20_;
    param_197 = _e157;
    let _e159 = (*Samples).fColor30_;
    param_198 = _e159;
    let _e161 = (*fPxFrac)[0u];
    param_199 = _e161;
    let _e162 = Lanczos2_u0028_vf4_u003b_vf4_u003b_vf4_u003b_vf4_u003b_f1_u003b((&param_195), (&param_196), (&param_197), (&param_198), (&param_199));
    fColorX0_ = _e162;
    let _e164 = (*Samples).fColor01_;
    param_200 = _e164;
    let _e166 = (*Samples).fColor11_;
    param_201 = _e166;
    let _e168 = (*Samples).fColor21_;
    param_202 = _e168;
    let _e170 = (*Samples).fColor31_;
    param_203 = _e170;
    let _e172 = (*fPxFrac)[0u];
    param_204 = _e172;
    let _e173 = Lanczos2_u0028_vf4_u003b_vf4_u003b_vf4_u003b_vf4_u003b_f1_u003b((&param_200), (&param_201), (&param_202), (&param_203), (&param_204));
    fColorX1_ = _e173;
    let _e175 = (*Samples).fColor02_;
    param_205 = _e175;
    let _e177 = (*Samples).fColor12_;
    param_206 = _e177;
    let _e179 = (*Samples).fColor22_;
    param_207 = _e179;
    let _e181 = (*Samples).fColor32_;
    param_208 = _e181;
    let _e183 = (*fPxFrac)[0u];
    param_209 = _e183;
    let _e184 = Lanczos2_u0028_vf4_u003b_vf4_u003b_vf4_u003b_vf4_u003b_f1_u003b((&param_205), (&param_206), (&param_207), (&param_208), (&param_209));
    fColorX2_ = _e184;
    let _e186 = (*Samples).fColor03_;
    param_210 = _e186;
    let _e188 = (*Samples).fColor13_;
    param_211 = _e188;
    let _e190 = (*Samples).fColor23_;
    param_212 = _e190;
    let _e192 = (*Samples).fColor33_;
    param_213 = _e192;
    let _e194 = (*fPxFrac)[0u];
    param_214 = _e194;
    let _e195 = Lanczos2_u0028_vf4_u003b_vf4_u003b_vf4_u003b_vf4_u003b_f1_u003b((&param_210), (&param_211), (&param_212), (&param_213), (&param_214));
    fColorX3_ = _e195;
    let _e196 = fColorX0_;
    param_215 = _e196;
    let _e197 = fColorX1_;
    param_216 = _e197;
    let _e198 = fColorX2_;
    param_217 = _e198;
    let _e199 = fColorX3_;
    param_218 = _e199;
    let _e201 = (*fPxFrac)[1u];
    param_219 = _e201;
    let _e202 = Lanczos2_u0028_vf4_u003b_vf4_u003b_vf4_u003b_vf4_u003b_f1_u003b((&param_215), (&param_216), (&param_217), (&param_218), (&param_219));
    fColorXY = _e202;
    let _e204 = (*Samples).fColor11_;
    let _e206 = (*Samples).fColor21_;
    let _e208 = (*Samples).fColor12_;
    let _e210 = (*Samples).fColor22_;
    fDeringingSamples = array<vec4<f32>, 4>(_e204, _e206, _e208, _e210);
    let _e213 = fDeringingSamples[0i];
    fDeringingMin = _e213;
    let _e215 = fDeringingSamples[0i];
    fDeringingMax = _e215;
    iSampleIndex_2 = 1i;
    loop {
        let _e216 = iSampleIndex_2;
        if (_e216 < 4i) {
            let _e218 = iSampleIndex_2;
            let _e219 = fDeringingMin;
            param_220 = _e219;
            let _e221 = fDeringingSamples[_e218];
            param_221 = _e221;
            let _e222 = ffxMin_u0028_vf4_u003b_vf4_u003b((&param_220), (&param_221));
            fDeringingMin = _e222;
            let _e223 = iSampleIndex_2;
            let _e224 = fDeringingMax;
            param_222 = _e224;
            let _e226 = fDeringingSamples[_e223];
            param_223 = _e226;
            let _e227 = ffxMax_u0028_vf4_u003b_vf4_u003b((&param_222), (&param_223));
            fDeringingMax = _e227;
            continue;
        } else {
            break;
        }
        continuing {
            let _e228 = iSampleIndex_2;
            iSampleIndex_2 = (_e228 + 1i);
        }
    }
    let _e230 = fColorXY;
    let _e231 = fDeringingMin;
    let _e232 = fDeringingMax;
    fColorXY = clamp(_e230, _e231, _e232);
    let _e234 = fColorXY;
    return _e234;
}
fn LoadHistory_u0028_vi2_u003b(iPxHistory: ptr<function, vec2<i32>>) -> vec4<f32> {
    let _e113 = (*iPxHistory);
    let _e114 = textureLoad(r_internal_upscaled_color, _e113, 0i);
    return _e114;
}
fn WrapHistory_u0028_vi2_u003b(iPxSample_1: ptr<function, vec2<i32>>) -> vec4<f32> {
    var param_224: vec2<i32>;
    let _e114 = (*iPxSample_1);
    param_224 = _e114;
    let _e115 = LoadHistory_u0028_vi2_u003b((&param_224));
    return _e115;
}
fn ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b(iPxSample_2: ptr<function, vec2<i32>>, iPxOffset_1: ptr<function, vec2<i32>>, iTextureSize_2: ptr<function, vec2<i32>>) -> vec2<i32> {
    var result_1: vec2<i32>;
)FSR2",
R"FSR2(    var local_13: i32;
    var param_225: i32;
    var param_226: i32;
    var local_14: i32;
    var param_227: i32;
    var param_228: i32;
    var local_15: i32;
    var param_229: i32;
    var param_230: i32;
    var local_16: i32;
    var param_231: i32;
    var param_232: i32;
    let _e128 = (*iPxSample_2);
    let _e129 = (*iPxOffset_1);
    result_1 = (_e128 + _e129);
    let _e132 = (*iPxOffset_1)[0u];
    if (_e132 < 0i) {
        let _e135 = result_1[0u];
        param_225 = _e135;
        param_226 = 0i;
        let _e136 = ffxMax_u0028_i1_u003b_i1_u003b((&param_225), (&param_226));
        local_13 = _e136;
    } else {
        let _e138 = result_1[0u];
        local_13 = _e138;
    }
    let _e139 = local_13;
    result_1[0u] = _e139;
    let _e142 = (*iPxOffset_1)[0u];
    if (_e142 > 0i) {
        let _e145 = (*iTextureSize_2)[0u];
        let _e148 = result_1[0u];
        param_227 = _e148;
        param_228 = (_e145 - 1i);
        let _e149 = ffxMin_u0028_i1_u003b_i1_u003b((&param_227), (&param_228));
        local_14 = _e149;
    } else {
        let _e151 = result_1[0u];
        local_14 = _e151;
    }
    let _e152 = local_14;
    result_1[0u] = _e152;
    let _e155 = (*iPxOffset_1)[1u];
    if (_e155 < 0i) {
        let _e158 = result_1[1u];
        param_229 = _e158;
        param_230 = 0i;
        let _e159 = ffxMax_u0028_i1_u003b_i1_u003b((&param_229), (&param_230));
        local_15 = _e159;
    } else {
        let _e161 = result_1[1u];
        local_15 = _e161;
    }
    let _e162 = local_15;
    result_1[1u] = _e162;
    let _e165 = (*iPxOffset_1)[1u];
    if (_e165 > 0i) {
        let _e168 = (*iTextureSize_2)[1u];
        let _e171 = result_1[1u];
        param_231 = _e171;
        param_232 = (_e168 - 1i);
        let _e172 = ffxMin_u0028_i1_u003b_i1_u003b((&param_231), (&param_232));
        local_16 = _e172;
    } else {
        let _e174 = result_1[1u];
        local_16 = _e174;
    }
    let _e175 = local_16;
    result_1[1u] = _e175;
    let _e177 = result_1;
    return _e177;
}
fn FetchHistorySamples_u0028_vi2_u003b_vi2_u003b(iPxSample_3: ptr<function, vec2<i32>>, iTextureSize_3: ptr<function, vec2<i32>>) -> FetchedBicubicSamples {
    var Samples_1: FetchedBicubicSamples;
    var param_233: vec2<i32>;
    var param_234: vec2<i32>;
    var param_235: vec2<i32>;
    var param_236: vec2<i32>;
    var param_237: vec2<i32>;
    var param_238: vec2<i32>;
    var param_239: vec2<i32>;
    var param_240: vec2<i32>;
    var param_241: vec2<i32>;
    var param_242: vec2<i32>;
    var param_243: vec2<i32>;
    var param_244: vec2<i32>;
    var param_245: vec2<i32>;
    var param_246: vec2<i32>;
    var param_247: vec2<i32>;
    var param_248: vec2<i32>;
    var param_249: vec2<i32>;
    var param_250: vec2<i32>;
    var param_251: vec2<i32>;
    var param_252: vec2<i32>;
    var param_253: vec2<i32>;
    var param_254: vec2<i32>;
    var param_255: vec2<i32>;
    var param_256: vec2<i32>;
    var param_257: vec2<i32>;
    var param_258: vec2<i32>;
    var param_259: vec2<i32>;
    var param_260: vec2<i32>;
    var param_261: vec2<i32>;
    var param_262: vec2<i32>;
    var param_263: vec2<i32>;
    var param_264: vec2<i32>;
    var param_265: vec2<i32>;
    var param_266: vec2<i32>;
    var param_267: vec2<i32>;
    var param_268: vec2<i32>;
    var param_269: vec2<i32>;
    var param_270: vec2<i32>;
    var param_271: vec2<i32>;
    var param_272: vec2<i32>;
    var param_273: vec2<i32>;
    var param_274: vec2<i32>;
    var param_275: vec2<i32>;
    var param_276: vec2<i32>;
    var param_277: vec2<i32>;
    var param_278: vec2<i32>;
    var param_279: vec2<i32>;
    var param_280: vec2<i32>;
    var param_281: vec2<i32>;
    var param_282: vec2<i32>;
    var param_283: vec2<i32>;
    var param_284: vec2<i32>;
    var param_285: vec2<i32>;
    var param_286: vec2<i32>;
    var param_287: vec2<i32>;
    var param_288: vec2<i32>;
    var param_289: vec2<i32>;
    var param_290: vec2<i32>;
    var param_291: vec2<i32>;
    var param_292: vec2<i32>;
    var param_293: vec2<i32>;
    var param_294: vec2<i32>;
    var param_295: vec2<i32>;
    var param_296: vec2<i32>;
    let _e179 = (*iPxSample_3);
    param_233 = _e179;
    param_234 = vec2<i32>(-1i, -1i);
    let _e180 = (*iTextureSize_3);
    param_235 = _e180;
    let _e181 = ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_233), (&param_234), (&param_235));
    param_236 = _e181;
    let _e182 = WrapHistory_u0028_vi2_u003b((&param_236));
    Samples_1.fColor00_ = _e182;
    let _e184 = (*iPxSample_3);
    param_237 = _e184;
    param_238 = vec2<i32>(0i, -1i);
    let _e185 = (*iTextureSize_3);
    param_239 = _e185;
    let _e186 = ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_237), (&param_238), (&param_239));
    param_240 = _e186;
    let _e187 = WrapHistory_u0028_vi2_u003b((&param_240));
    Samples_1.fColor10_ = _e187;
    let _e189 = (*iPxSample_3);
    param_241 = _e189;
    param_242 = vec2<i32>(1i, -1i);
    let _e190 = (*iTextureSize_3);
    param_243 = _e190;
    let _e191 = ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_241), (&param_242), (&param_243));
    param_244 = _e191;
    let _e192 = WrapHistory_u0028_vi2_u003b((&param_244));
    Samples_1.fColor20_ = _e192;
    let _e194 = (*iPxSample_3);
    param_245 = _e194;
    param_246 = vec2<i32>(2i, -1i);
    let _e195 = (*iTextureSize_3);
    param_247 = _e195;
    let _e196 = ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_245), (&param_246), (&param_247));
    param_248 = _e196;
    let _e197 = WrapHistory_u0028_vi2_u003b((&param_248));
    Samples_1.fColor30_ = _e197;
    let _e199 = (*iPxSample_3);
    param_249 = _e199;
    param_250 = vec2<i32>(-1i, 0i);
    let _e200 = (*iTextureSize_3);
    param_251 = _e200;
    let _e201 = ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_249), (&param_250), (&param_251));
    param_252 = _e201;
    let _e202 = WrapHistory_u0028_vi2_u003b((&param_252));
    Samples_1.fColor01_ = _e202;
    let _e204 = (*iPxSample_3);
    param_253 = _e204;
    param_254 = vec2<i32>(0i, 0i);
    let _e205 = (*iTextureSize_3);
    param_255 = _e205;
    let _e206 = ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_253), (&param_254), (&param_255));
    param_256 = _e206;
    let _e207 = WrapHistory_u0028_vi2_u003b((&param_256));
    Samples_1.fColor11_ = _e207;
    let _e209 = (*iPxSample_3);
    param_257 = _e209;
    param_258 = vec2<i32>(1i, 0i);
    let _e210 = (*iTextureSize_3);
    param_259 = _e210;
    let _e211 = ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_257), (&param_258), (&param_259));
    param_260 = _e211;
    let _e212 = WrapHistory_u0028_vi2_u003b((&param_260));
    Samples_1.fColor21_ = _e212;
    let _e214 = (*iPxSample_3);
    param_261 = _e214;
    param_262 = vec2<i32>(2i, 0i);
    let _e215 = (*iTextureSize_3);
    param_263 = _e215;
    let _e216 = ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_261), (&param_262), (&param_263));
    param_264 = _e216;
    let _e217 = WrapHistory_u0028_vi2_u003b((&param_264));
    Samples_1.fColor31_ = _e217;
    let _e219 = (*iPxSample_3);
    param_265 = _e219;
    param_266 = vec2<i32>(-1i, 1i);
    let _e220 = (*iTextureSize_3);
    param_267 = _e220;
    let _e221 = ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_265), (&param_266), (&param_267));
    param_268 = _e221;
    let _e222 = WrapHistory_u0028_vi2_u003b((&param_268));
    Samples_1.fColor02_ = _e222;
    let _e224 = (*iPxSample_3);
    param_269 = _e224;
    param_270 = vec2<i32>(0i, 1i);
    let _e225 = (*iTextureSize_3);
    param_271 = _e225;
    let _e226 = ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_269), (&param_270), (&param_271));
    param_272 = _e226;
    let _e227 = WrapHistory_u0028_vi2_u003b((&param_272));
    Samples_1.fColor12_ = _e227;
    let _e229 = (*iPxSample_3);
    param_273 = _e229;
    param_274 = vec2<i32>(1i, 1i);
    let _e230 = (*iTextureSize_3);
    param_275 = _e230;
    let _e231 = ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_273), (&param_274), (&param_275));
    param_276 = _e231;
    let _e232 = WrapHistory_u0028_vi2_u003b((&param_276));
    Samples_1.fColor22_ = _e232;
    let _e234 = (*iPxSample_3);
    param_277 = _e234;
    param_278 = vec2<i32>(2i, 1i);
    let _e235 = (*iTextureSize_3);
    param_279 = _e235;
    let _e236 = ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_277), (&param_278), (&param_279));
    param_280 = _e236;
    let _e237 = WrapHistory_u0028_vi2_u003b((&param_280));
    Samples_1.fColor32_ = _e237;
    let _e239 = (*iPxSample_3);
    param_281 = _e239;
    param_282 = vec2<i32>(-1i, 2i);
    let _e240 = (*iTextureSize_3);
    param_283 = _e240;
    let _e241 = ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_281), (&param_282), (&param_283));
    param_284 = _e241;
    let _e242 = WrapHistory_u0028_vi2_u003b((&param_284));
    Samples_1.fColor03_ = _e242;
    let _e244 = (*iPxSample_3);
    param_285 = _e244;
    param_286 = vec2<i32>(0i, 2i);
    let _e245 = (*iTextureSize_3);
    param_287 = _e245;
    let _e246 = ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_285), (&param_286), (&param_287));
    param_288 = _e246;
    let _e247 = WrapHistory_u0028_vi2_u003b((&param_288));
    Samples_1.fColor13_ = _e247;
    let _e249 = (*iPxSample_3);
    param_289 = _e249;
    param_290 = vec2<i32>(1i, 2i);
    let _e250 = (*iTextureSize_3);
    param_291 = _e250;
    let _e251 = ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_289), (&param_290), (&param_291));
    param_292 = _e251;
    let _e252 = WrapHistory_u0028_vi2_u003b((&param_292));
    Samples_1.fColor23_ = _e252;
    let _e254 = (*iPxSample_3);
    param_293 = _e254;
    param_294 = vec2<i32>(2i, 2i);
    let _e255 = (*iTextureSize_3);
    param_295 = _e255;
    let _e256 = ClampCoord_u0028_vi2_u003b_vi2_u003b_vi2_u003b((&param_293), (&param_294), (&param_295));
    param_296 = _e256;
    let _e257 = WrapHistory_u0028_vi2_u003b((&param_296));
    Samples_1.fColor33_ = _e257;
    let _e259 = Samples_1;
    return _e259;
}
fn ffxFract_u0028_vf2_u003b(x_17: ptr<function, vec2<f32>>) -> vec2<f32> {
    let _e113 = (*x_17);
    return fract(_e113);
}
fn HistorySample_u0028_vf2_u003b_vi2_u003b(fUvSample: ptr<function, vec2<f32>>, iTextureSize_4: ptr<function, vec2<i32>>) -> vec4<f32> {
    var fPxSample: vec2<f32>;
    var param_297: f32;
    var param_298: f32;
    var param_299: f32;
    var param_300: f32;
    var param_301: f32;
    var param_302: f32;
    var param_303: f32;
    var param_304: f32;
    var iPxSample_4: vec2<i32>;
    var fPxFrac_1: vec2<f32>;
    var param_305: vec2<f32>;
    var fColorXY_1: vec4<f32>;
    var param_306: vec2<i32>;
    var param_307: vec2<i32>;
    var param_308: FetchedBicubicSamples;
    var param_309: vec2<f32>;
    let _e131 = (*fUvSample);
    let _e132 = (*iTextureSize_4);
    fPxSample = ((_e131 * vec2<f32>(_e132)) - vec2<f32>(0.5f, 0.5f));
    let _e137 = (*iTextureSize_4)[0u];
    param_297 = f32(_e137);
    let _e140 = fPxSample[0u];
    param_298 = _e140;
    let _e141 = ffxMin_u0028_f1_u003b_f1_u003b((&param_297), (&param_298));
    param_299 = 0f;
    param_300 = _e141;
    let _e142 = ffxMax_u0028_f1_u003b_f1_u003b((&param_299), (&param_300));
    fPxSample[0u] = _e142;
    let _e145 = (*iTextureSize_4)[1u];
    param_301 = f32(_e145);
    let _e148 = fPxSample[1u];
    param_302 = _e148;
    let _e149 = ffxMin_u0028_f1_u003b_f1_u003b((&param_301), (&param_302));
    param_303 = 0f;
    param_304 = _e149;
    let _e150 = ffxMax_u0028_f1_u003b_f1_u003b((&param_303), (&param_304));
    fPxSample[1u] = _e150;
    let _e152 = fPxSample;
    iPxSample_4 = vec2<i32>(floor(_e152));
    let _e155 = fPxSample;
    param_305 = _e155;
    let _e156 = ffxFract_u0028_vf2_u003b((&param_305));
    fPxFrac_1 = _e156;
    let _e157 = iPxSample_4;
    param_306 = _e157;
    let _e158 = (*iTextureSize_4);
)FSR2",
R"FSR2(    param_307 = _e158;
    let _e159 = FetchHistorySamples_u0028_vi2_u003b_vi2_u003b((&param_306), (&param_307));
    param_308 = _e159;
    let _e160 = fPxFrac_1;
    param_309 = _e160;
    let _e161 = Lanczos2_u0028_struct_u002d_FetchedBicubicSamples_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf4_u002d_vf41_u003b_vf2_u003b((&param_308), (&param_309));
    fColorXY_1 = _e161;
    let _e162 = fColorXY_1;
    return _e162;
}
fn DisplaySize_u0028_() -> vec2<i32> {
    let _e113 = cbFSR2_.iDisplaySize;
    return _e113;
}
fn ReprojectHistoryColor_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_vf3_u003b_f1_u003b_b1_u003b(params_9: AccumulationPassCommonParams, fHistoryColor_2: ptr<function, vec3<f32>>, fTemporalReactiveFactor_2: ptr<function, f32>, bInMotionLastFrame_1: ptr<function, bool>) {
    var fHistory: vec4<f32>;
    var param_310: vec2<f32>;
    var param_311: vec2<i32>;
    var param_312: vec3<f32>;
    var param_313: f32;
    var param_314: f32;
    var param_315: vec3<f32>;
    var param_316: f32;
    let _e124 = DisplaySize_u0028_();
    param_310 = params_9.fReprojectedHrUv;
    param_311 = _e124;
    let _e126 = HistorySample_u0028_vf2_u003b_vi2_u003b((&param_310), (&param_311));
    fHistory = _e126;
    let _e127 = Exposure_u0028_();
    let _e128 = PreviousFramePreExposure_u0028_();
    let _e129 = fHistory;
    param_312 = _e129.xyz;
    param_313 = _e127;
    param_314 = _e128;
    let _e131 = PrepareRgb_u0028_vf3_u003b_f1_u003b_f1_u003b((&param_312), (&param_313), (&param_314));
    (*fHistoryColor_2) = _e131;
    let _e132 = (*fHistoryColor_2);
    param_315 = _e132;
    let _e133 = RGBToYCoCg_u0028_vf3_u003b((&param_315));
    (*fHistoryColor_2) = _e133;
    let _e135 = fHistory[3u];
    param_316 = abs(_e135);
    let _e137 = ffxSaturate_u0028_f1_u003b((&param_316));
    (*fTemporalReactiveFactor_2) = _e137;
    let _e139 = fHistory[3u];
    (*bInMotionLastFrame_1) = (_e139 < 0f);
    return;
}
fn InitializeNewLockSample_u0028_vf2_u003b(fLockStatus_4: ptr<function, vec2<f32>>) {
    (*fLockStatus_4) = vec2<f32>(0f, 0f);
    return;
}
fn FrameIndex_u0028_() -> i32 {
    let _e113 = cbFSR2_.iFrameIndex;
    return _e113;
}
fn SampleDilatedReactiveMasks_u0028_vf2_u003b(fUV_3: ptr<function, vec2<f32>>) -> vec2<f32> {
    let _e113 = (*fUV_3);
    let _e114 = textureSampleLevel(r_dilated_reactive_masks, s_LinearClamp, _e113, 0f);
    return _e114.xy;
}
fn SampleDepthClip_u0028_vf2_u003b(fUV_4: ptr<function, vec2<f32>>) -> f32 {
    let _e113 = (*fUV_4);
    let _e114 = textureSampleLevel(r_prepared_input_color, s_LinearClamp, _e113, 0f);
    return _e114.w;
}
fn ComputeReprojectedUVs_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_vf2_u003b_b1_u003b(params_10: AccumulationPassCommonParams, fReprojectedHrUv: ptr<function, vec2<f32>>, bIsExistingSample: ptr<function, bool>) {
    var param_317: vec2<f32>;
    (*fReprojectedHrUv) = (params_10.fHrUv + params_10.fMotionVector);
    let _e119 = (*fReprojectedHrUv);
    param_317 = _e119;
    let _e120 = IsUvInside_u0028_vf2_u003b((&param_317));
    (*bIsExistingSample) = _e120;
    return;
}
fn GetPxHrVelocity_u0028_vf2_u003b(fMotionVector: ptr<function, vec2<f32>>) -> f32 {
    let _e113 = (*fMotionVector);
    let _e114 = DisplaySize_u0028_();
    return length((_e113 * vec2<f32>(_e114)));
}
fn LoadDilatedMotionVector_u0028_vi2_u003b(iPxInput: ptr<function, vec2<i32>>) -> vec2<f32> {
    let _e113 = (*iPxInput);
    let _e114 = textureLoad(r_dilated_motion_vectors, _e113, 0i);
    return _e114.xy;
}
fn GetMotionVector_u0028_vi2_u003b_vf2_u003b(iPxHrPos_1: ptr<function, vec2<i32>>, fHrUv: ptr<function, vec2<f32>>) -> vec2<f32> {
    var fDilatedMotionVector: vec2<f32>;
    var param_318: vec2<i32>;
    let _e116 = (*fHrUv);
    let _e117 = RenderSize_u0028_();
    param_318 = vec2<i32>((_e116 * vec2<f32>(_e117)));
    let _e121 = LoadDilatedMotionVector_u0028_vi2_u003b((&param_318));
    fDilatedMotionVector = _e121;
    let _e122 = fDilatedMotionVector;
    return _e122;
}
fn MaxRenderSize_u0028_() -> vec2<i32> {
    let _e113 = cbFSR2_.iMaxRenderSize;
    return _e113;
}
fn InitParams_u0028_vi2_u003b(iPxHrPos_2: ptr<function, vec2<i32>>) -> AccumulationPassCommonParams {
    var params_11: AccumulationPassCommonParams;
    var fHrUv_1: vec2<f32>;
    var fLrUvJittered: vec2<f32>;
    var param_319: vec2<f32>;
    var param_320: vec2<i32>;
    var param_321: vec2<i32>;
    var param_322: vec2<i32>;
    var param_323: vec2<f32>;
    var param_324: vec2<f32>;
    var param_325: vec2<f32>;
    var param_326: bool;
    var param_327: vec2<f32>;
    var param_328: f32;
    var fDilatedReactiveMasks: vec2<f32>;
    var param_329: vec2<f32>;
    let _e128 = (*iPxHrPos_2);
    params_11.iPxHrPos = _e128;
    let _e130 = (*iPxHrPos_2);
    let _e134 = DisplaySize_u0028_();
    fHrUv_1 = ((vec2<f32>(_e130) + vec2(0.5f)) / vec2<f32>(_e134));
    let _e137 = fHrUv_1;
    params_11.fHrUv = _e137;
    let _e139 = fHrUv_1;
    let _e140 = Jitter_u0028_();
    let _e141 = RenderSize_u0028_();
    fLrUvJittered = (_e139 + (_e140 / vec2<f32>(_e141)));
    let _e145 = RenderSize_u0028_();
    let _e146 = MaxRenderSize_u0028_();
    let _e147 = fLrUvJittered;
    param_319 = _e147;
    param_320 = _e145;
    param_321 = _e146;
    let _e148 = ClampUv_u0028_vf2_u003b_vi2_u003b_vi2_u003b((&param_319), (&param_320), (&param_321));
    params_11.fLrUv_HwSampler = _e148;
    let _e150 = (*iPxHrPos_2);
    param_322 = _e150;
    let _e151 = fHrUv_1;
    param_323 = _e151;
    let _e152 = GetMotionVector_u0028_vi2_u003b_vf2_u003b((&param_322), (&param_323));
    params_11.fMotionVector = _e152;
    let _e155 = params_11.fMotionVector;
    param_324 = _e155;
    let _e156 = GetPxHrVelocity_u0028_vf2_u003b((&param_324));
    params_11.fHrVelocity = _e156;
    let _e158 = params_11;
    ComputeReprojectedUVs_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_vf2_u003b_b1_u003b(_e158, (&param_325), (&param_326));
    let _e159 = param_325;
    params_11.fReprojectedHrUv = _e159;
    let _e161 = param_326;
    params_11.bIsExistingSample = _e161;
    let _e164 = params_11.fLrUv_HwSampler;
    param_327 = _e164;
    let _e165 = SampleDepthClip_u0028_vf2_u003b((&param_327));
    param_328 = _e165;
    let _e166 = ffxSaturate_u0028_f1_u003b((&param_328));
    params_11.fDepthClipFactor = _e166;
    let _e169 = params_11.fLrUv_HwSampler;
    param_329 = _e169;
    let _e170 = SampleDilatedReactiveMasks_u0028_vf2_u003b((&param_329));
    fDilatedReactiveMasks = _e170;
    let _e172 = fDilatedReactiveMasks[0u];
    params_11.fDilatedReactiveFactor = _e172;
    let _e175 = fDilatedReactiveMasks[1u];
    params_11.fAccumulationMask = _e175;
    let _e177 = FrameIndex_u0028_();
    params_11.bIsResetFrame = (0i == _e177);
    let _e181 = params_11.bIsExistingSample;
    let _e184 = params_11.bIsResetFrame;
    params_11.bIsNewSample = ((_e181 == false) || _e184);
    let _e187 = params_11;
    return _e187;
}
fn Accumulate_u0028_vi2_u003b(iPxHrPos_3: ptr<function, vec2<i32>>) {
    var params_12: AccumulationPassCommonParams;
    var param_330: vec2<i32>;
    var fHistoryColor_3: vec3<f32>;
    var fLockStatus_5: vec2<f32>;
    var param_331: vec2<f32>;
    var fTemporalReactiveFactor_3: f32;
    var bInMotionLastFrame_2: bool;
    var lockState_1: LockState;
    var param_332: vec3<f32>;
    var param_333: f32;
    var param_334: bool;
    var param_335: vec2<f32>;
    var fThisFrameReactiveFactor_2: f32;
    var param_336: f32;
    var param_337: f32;
    var fLuminanceDiff_2: f32;
    var fLockContributionThisFrame_2: f32;
    var param_338: AccumulationPassCommonParams;
    var param_339: f32;
    var param_340: LockState;
    var param_341: vec2<f32>;
    var param_342: f32;
    var param_343: f32;
    var fUpsampledColorAndWeight_1: vec4<f32>;
    var clippingBox_4: RectificationBox;
    var param_344: RectificationBox;
    var param_345: f32;
    var fLumaInstabilityFactor_1: f32;
    var param_346: RectificationBox;
    var param_347: f32;
    var param_348: f32;
    var fAccumulation_2: vec3<f32>;
    var param_349: f32;
    var param_350: bool;
    var param_351: f32;
    var param_352: LockState;
    var param_353: vec3<f32>;
    var param_354: RectificationBox;
    var param_355: vec3<f32>;
    var param_356: vec3<f32>;
    var param_357: f32;
    var param_358: f32;
    var param_359: f32;
    var param_360: vec3<f32>;
    var param_361: vec3<f32>;
    var param_362: vec4<f32>;
    var param_363: vec3<f32>;
    var param_364: f32;
    var param_365: vec2<f32>;
    var param_366: f32;
    var param_367: f32;
    var param_368: vec2<i32>;
    var param_369: vec4<f32>;
    var param_370: vec2<i32>;
    var param_371: f32;
    var phi_2763_: bool;
    let _e168 = (*iPxHrPos_3);
    param_330 = _e168;
    let _e169 = InitParams_u0028_vi2_u003b((&param_330));
    params_12 = _e169;
    fHistoryColor_3 = vec3<f32>(0f, 0f, 0f);
    InitializeNewLockSample_u0028_vf2_u003b((&param_331));
    let _e170 = param_331;
    fLockStatus_5 = _e170;
    fTemporalReactiveFactor_3 = 0f;
    bInMotionLastFrame_2 = false;
    lockState_1 = LockState(false, false);
    let _e172 = params_12.bIsExistingSample;
    phi_2763_ = _e172;
    if _e172 {
        let _e174 = params_12.bIsResetFrame;
        phi_2763_ = !(_e174);
    }
    let _e177 = phi_2763_;
    if _e177 {
        let _e178 = params_12;
        ReprojectHistoryColor_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_vf3_u003b_f1_u003b_b1_u003b(_e178, (&param_332), (&param_333), (&param_334));
        let _e179 = param_332;
        fHistoryColor_3 = _e179;
        let _e180 = param_333;
        fTemporalReactiveFactor_3 = _e180;
        let _e181 = param_334;
        bInMotionLastFrame_2 = _e181;
        let _e182 = params_12;
        let _e183 = ReprojectHistoryLockStatus_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_vf2_u003b(_e182, (&param_335));
        let _e184 = param_335;
        fLockStatus_5 = _e184;
        lockState_1 = _e183;
    }
    let _e186 = params_12.fDilatedReactiveFactor;
    param_336 = _e186;
    let _e187 = fTemporalReactiveFactor_3;
    param_337 = _e187;
    let _e188 = ffxMax_u0028_f1_u003b_f1_u003b((&param_336), (&param_337));
    fThisFrameReactiveFactor_2 = _e188;
    fLuminanceDiff_2 = 0f;
    fLockContributionThisFrame_2 = 0f;
    let _e189 = params_12;
    param_338 = _e189;
    let _e190 = fThisFrameReactiveFactor_2;
    param_339 = _e190;
    let _e191 = lockState_1;
    param_340 = _e191;
    let _e192 = fLockStatus_5;
    param_341 = _e192;
    UpdateLockStatus_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_f1_u003b_struct_u002d_LockState_u002d_b1_u002d_b11_u003b_vf2_u003b_f1_u003b_f1_u003b((&param_338), (&param_339), (&param_340), (&param_341), (&param_342), (&param_343));
    let _e193 = param_339;
    fThisFrameReactiveFactor_2 = _e193;
    let _e194 = param_341;
    fLockStatus_5 = _e194;
    let _e195 = param_342;
    fLockContributionThisFrame_2 = _e195;
    let _e196 = param_343;
    fLuminanceDiff_2 = _e196;
    let _e197 = params_12;
    let _e198 = clippingBox_4;
    param_344 = _e198;
    let _e199 = fThisFrameReactiveFactor_2;
)FSR2",
R"FSR2(    param_345 = _e199;
    let _e200 = ComputeUpsampledColorAndWeight_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_struct_u002d_RectificationBox_u002d_vf3_u002d_vf3_u002d_vf3_u002d_vf3_u002d_f11_u003b_f1_u003b(_e197, (&param_344), (&param_345));
    let _e201 = param_344;
    clippingBox_4 = _e201;
    fUpsampledColorAndWeight_1 = _e200;
    let _e202 = params_12;
    let _e203 = clippingBox_4;
    param_346 = _e203;
    let _e204 = fThisFrameReactiveFactor_2;
    param_347 = _e204;
    let _e205 = fLuminanceDiff_2;
    param_348 = _e205;
    let _e206 = ComputeLumaInstabilityFactor_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_struct_u002d_RectificationBox_u002d_vf3_u002d_vf3_u002d_vf3_u002d_vf3_u002d_f11_u003b_f1_u003b_f1_u003b(_e202, (&param_346), (&param_347), (&param_348));
    fLumaInstabilityFactor_1 = _e206;
    let _e207 = params_12;
    let _e208 = fThisFrameReactiveFactor_2;
    param_349 = _e208;
    let _e209 = bInMotionLastFrame_2;
    param_350 = _e209;
    let _e211 = fUpsampledColorAndWeight_1[3u];
    param_351 = _e211;
    let _e212 = lockState_1;
    param_352 = _e212;
    let _e213 = ComputeBaseAccumulationWeight_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_f1_u003b_b1_u003b_f1_u003b_struct_u002d_LockState_u002d_b1_u002d_b11_u003b(_e207, (&param_349), (&param_350), (&param_351), (&param_352));
    fAccumulation_2 = _e213;
    let _e215 = params_12.bIsNewSample;
    if _e215 {
        let _e216 = fUpsampledColorAndWeight_1;
        param_353 = _e216.xyz;
        let _e218 = YCoCgToRGB_u0028_vf3_u003b((&param_353));
        fHistoryColor_3 = _e218;
    } else {
        let _e219 = params_12;
        let _e220 = clippingBox_4;
        param_354 = _e220;
        let _e221 = fHistoryColor_3;
        param_355 = _e221;
        let _e222 = fAccumulation_2;
        param_356 = _e222;
        let _e223 = fLockContributionThisFrame_2;
        param_357 = _e223;
        let _e224 = fThisFrameReactiveFactor_2;
        param_358 = _e224;
        let _e225 = fLumaInstabilityFactor_1;
        param_359 = _e225;
        RectifyHistory_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_struct_u002d_RectificationBox_u002d_vf3_u002d_vf3_u002d_vf3_u002d_vf3_u002d_f11_u003b_vf3_u003b_vf3_u003b_f1_u003b_f1_u003b_f1_u003b(_e219, (&param_354), (&param_355), (&param_356), (&param_357), (&param_358), (&param_359));
        let _e226 = param_355;
        fHistoryColor_3 = _e226;
        let _e227 = param_356;
        fAccumulation_2 = _e227;
        let _e228 = params_12;
        let _e229 = fHistoryColor_3;
        param_360 = _e229;
        let _e230 = fAccumulation_2;
        param_361 = _e230;
        let _e231 = fUpsampledColorAndWeight_1;
        param_362 = _e231;
        Accumulate_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_vf3_u003b_vf3_u003b_vf4_u003b(_e228, (&param_360), (&param_361), (&param_362));
        let _e232 = param_360;
        fHistoryColor_3 = _e232;
    }
    let _e233 = Exposure_u0028_();
    let _e234 = fHistoryColor_3;
    param_363 = _e234;
    param_364 = _e233;
    let _e235 = UnprepareRgb_u0028_vf3_u003b_f1_u003b((&param_363), (&param_364));
    fHistoryColor_3 = _e235;
    let _e236 = params_12;
    let _e237 = fLockStatus_5;
    param_365 = _e237;
    let _e239 = fUpsampledColorAndWeight_1[3u];
    param_366 = _e239;
    FinalizeLockStatus_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_vf2_u003b_f1_u003b(_e236, (&param_365), (&param_366));
    let _e240 = params_12;
    let _e241 = fThisFrameReactiveFactor_2;
    param_367 = _e241;
    let _e242 = ComputeTemporalReactiveFactor_u0028_struct_u002d_AccumulationPassCommonParams_u002d_vi2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_vf2_u002d_f1_u002d_f1_u002d_f1_u002d_f1_u002d_b1_u002d_b1_u002d_b11_u003b_f1_u003b(_e240, (&param_367));
    fTemporalReactiveFactor_3 = _e242;
    let _e243 = fHistoryColor_3;
    let _e244 = fTemporalReactiveFactor_3;
    let _e249 = (*iPxHrPos_3);
    param_368 = _e249;
    param_369 = vec4<f32>(_e243.x, _e243.y, _e243.z, _e244);
    StoreInternalColorAndWeight_u0028_vi2_u003b_vf4_u003b((&param_368), (&param_369));
    let _e250 = (*iPxHrPos_3);
    param_370 = _e250;
    param_371 = 0f;
    StoreNewLocks_u0028_vi2_u003b_f1_u003b((&param_370), (&param_371));
    return;
}
fn main_1() {
    var uGroupId: vec2<u32>;
    var GroupRows: u32;
    var uDispatchThreadId: vec2<u32>;
    var param_372: vec2<i32>;
    let _e116 = gl_WorkGroupID_1;
    uGroupId = _e116.xy;
    let _e118 = DisplaySize_u0028_();
    GroupRows = (((bitcast<u32>(_e118.y) + 8u) - 1u) / 8u);
    let _e124 = GroupRows;
    let _e126 = uGroupId[1u];
    uGroupId[1u] = ((_e124 - _e126) - 1u);
    let _e130 = uGroupId;
    let _e132 = gl_LocalInvocationID_1;
    uDispatchThreadId = ((_e130 * vec2<u32>(8u, 8u)) + _e132.xy);
    let _e135 = uDispatchThreadId;
    param_372 = bitcast<vec2<i32>>(_e135);
    Accumulate_u0028_vi2_u003b((&param_372));
    return;
}
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) gl_WorkGroupID: vec3<u32>, @builtin(local_invocation_id) gl_LocalInvocationID: vec3<u32>) {
    gl_WorkGroupID_1 = gl_WorkGroupID;
    gl_LocalInvocationID_1 = gl_LocalInvocationID;
    main_1();
}

)FSR2",
};
inline const Binding accumulate_bindings[] = {
    {18, kUniform, "cbFSR2_", "cbFSR2_", false},
    {10, kTexture, "r_imgMips", "", false},
    {6, kTexture, "r_prepared_input_color", "", false},
    {3, kTexture, "r_internal_upscaled_color", "", false},
    {17, kStorageTexture, "rw_luma_history", "rgba8unorm", true},
    {12, kTexture, "r_luma_history", "", false},
    {13, kStorageTexture, "rw_internal_upscaled_color", "rgba16float", false},
    {14, kStorageTexture, "rw_lock_status", "rg16float", true},
    {16, kStorageTexture, "rw_new_locks", "r8unorm", true},
    {4, kTexture, "r_lock_status", "", false},
    {2, kTexture, "r_dilated_motion_vectors", "", false},
    {0, kTexture, "r_input_exposure", "", false},
    {1, kTexture, "r_dilated_reactive_masks", "", false},
    {11, kTexture, "r_auto_exposure", "", false},
    {8, kTexture, "r_lanczos_lut", "", false},
    {9, kTexture, "r_upsample_maximum_bias_lut", "", false},
    {15, kStorageTexture, "rw_upscaled_output", "rgba16float", false},
};
inline const char* const rcas_parts[] = {
R"FSR2(struct cbFSR2_t {
    iRenderSize: vec2<i32>,
    iMaxRenderSize: vec2<i32>,
    iDisplaySize: vec2<i32>,
    iInputColorResourceDimensions: vec2<i32>,
    iLumaMipDimensions: vec2<i32>,
    iLumaMipLevelToUse: i32,
    iFrameIndex: i32,
    fDeviceToViewDepth: vec4<f32>,
    fJitter: vec2<f32>,
    fMotionVectorScale: vec2<f32>,
    fDownscaleFactor: vec2<f32>,
    fMotionVectorJitterCancellation: vec2<f32>,
    fPreExposure: f32,
    fPreviousFramePreExposure: f32,
    fTanHalfFOV: f32,
    fJitterSequenceLength: f32,
    fDeltaTime: f32,
    fDynamicResChangeFactor: f32,
    fViewSpaceToMetersFactor: f32,
}
struct cbRCAS_t {
    rcasConfig: vec4<u32>,
}
@group(1) @binding(3)
var<uniform> cbFSR2_: cbFSR2_t;
@group(1) @binding(2)
var rw_upscaled_output: texture_storage_2d<rgba16float,write>;
@group(1) @binding(0)
var r_input_exposure: texture_2d<f32>;
@group(1) @binding(4)
var<uniform> cbRCAS: cbRCAS_t;
@group(1) @binding(1)
var r_rcas_input: texture_2d<f32>;
var<private> gl_LocalInvocationID_1: vec3<u32>;
var<private> gl_WorkGroupID_1: vec3<u32>;
var<private> gl_GlobalInvocationID_1: vec3<u32>;
@group(0) @binding(0)
var s_PointClamp: sampler;
@group(0) @binding(1)
var s_LinearClamp: sampler;
fn StoreUpscaledOutput_u0028_vi2_u003b_vf3_u003b(iPxPos: ptr<function, vec2<i32>>, fColor: ptr<function, vec3<f32>>) {
    let _e50 = (*iPxPos);
    let _e51 = (*fColor);
    textureStore(rw_upscaled_output, _e50, vec4<f32>(_e51.x, _e51.y, _e51.z, 1f));
    return;
}
fn WriteUpscaledOutput_u0028_vu2_u003b_vf3_u003b(iPxHrPos: ptr<function, vec2<u32>>, fUpscaledColor: ptr<function, vec3<f32>>) {
    var param: vec2<i32>;
    var param_1: vec3<f32>;
    let _e52 = (*iPxHrPos);
    param = bitcast<vec2<i32>>(_e52);
    let _e54 = (*fUpscaledColor);
    param_1 = _e54;
    StoreUpscaledOutput_u0028_vi2_u003b_vf3_u003b((&param), (&param_1));
    return;
}
fn PreExposure_u0028_() -> f32 {
    let _e49 = cbFSR2_.fPreExposure;
    return _e49;
}
fn UnprepareRgb_u0028_vf3_u003b_f1_u003b(fRgb: ptr<function, vec3<f32>>, fExposure: ptr<function, f32>) -> vec3<f32> {
    let _e50 = (*fExposure);
    let _e51 = (*fRgb);
    (*fRgb) = (_e51 / vec3(_e50));
    let _e54 = PreExposure_u0028_();
    let _e55 = (*fRgb);
    (*fRgb) = (_e55 * _e54);
    let _e57 = (*fRgb);
    return _e57;
}
fn Exposure_u0028_() -> f32 {
    var exposure: f32;
    let _e49 = textureLoad(r_input_exposure, vec2<i32>(0i, 0i), 0i);
    exposure = _e49.x;
    let _e51 = exposure;
    if (_e51 == 0f) {
        exposure = 1f;
    }
    let _e53 = exposure;
    return _e53;
}
fn ffxAsFloat_u0028_u1_u003b(x: ptr<function, u32>) -> f32 {
    let _e49 = (*x);
    return bitcast<f32>(_e49);
}
fn rcp_u0028_f1_u003b(x_1: ptr<function, f32>) -> f32 {
    let _e49 = (*x_1);
    return (1f / _e49);
}
fn ffxMin_u0028_f1_u003b_f1_u003b(x_2: ptr<function, f32>, y: ptr<function, f32>) -> f32 {
    let _e50 = (*x_2);
    let _e51 = (*y);
    return min(_e50, _e51);
}
fn ffxSaturate_u0028_f1_u003b(x_3: ptr<function, f32>) -> f32 {
    let _e49 = (*x_3);
    return clamp(_e49, 0f, 1f);
}
fn ffxAsUInt32_u0028_f1_u003b(x_4: ptr<function, f32>) -> u32 {
    let _e49 = (*x_4);
    return bitcast<u32>(_e49);
}
fn ffxApproximateReciprocalMedium_u0028_f1_u003b(value: ptr<function, f32>) -> f32 {
    var b: f32;
    var param_2: f32;
    var param_3: u32;
    let _e52 = (*value);
    param_2 = _e52;
    let _e53 = ffxAsUInt32_u0028_f1_u003b((&param_2));
    param_3 = (2129764351u - _e53);
    let _e55 = ffxAsFloat_u0028_u1_u003b((&param_3));
    b = _e55;
    let _e56 = b;
    let _e57 = b;
    let _e59 = (*value);
    return (_e56 * ((-(_e57) * _e59) + 2f));
}
fn ffxMin3_u0028_f1_u003b_f1_u003b_f1_u003b(x_5: ptr<function, f32>, y_1: ptr<function, f32>, z: ptr<function, f32>) -> f32 {
    let _e51 = (*x_5);
    let _e52 = (*y_1);
    let _e53 = (*z);
    return min(_e51, min(_e52, _e53));
}
fn ffxMax3_u0028_f1_u003b_f1_u003b_f1_u003b(x_6: ptr<function, f32>, y_2: ptr<function, f32>, z_1: ptr<function, f32>) -> f32 {
    let _e51 = (*x_6);
    let _e52 = (*y_2);
    let _e53 = (*z_1);
    return max(_e51, max(_e52, _e53));
}
fn FsrRcasInputF_u0028_f1_u003b_f1_u003b_f1_u003b(r: ptr<function, f32>, g: ptr<function, f32>, b_1: ptr<function, f32>) {
    return;
}
fn PrepareRgb_u0028_vf3_u003b_f1_u003b_f1_u003b(fRgb_1: ptr<function, vec3<f32>>, fExposure_1: ptr<function, f32>, fPreExposure: ptr<function, f32>) -> vec3<f32> {
    let _e51 = (*fPreExposure);
    let _e52 = (*fRgb_1);
    (*fRgb_1) = (_e52 / vec3(_e51));
    let _e55 = (*fExposure_1);
    let _e56 = (*fRgb_1);
    (*fRgb_1) = (_e56 * _e55);
    let _e58 = (*fRgb_1);
    (*fRgb_1) = clamp(_e58, vec3(0f), vec3(65504f));
    let _e62 = (*fRgb_1);
    return _e62;
}
fn LoadRCAS_Input_u0028_vi2_u003b(iPxPos_1: ptr<function, vec2<i32>>) -> vec4<f32> {
    let _e49 = (*iPxPos_1);
    let _e50 = textureLoad(r_rcas_input, _e49, 0i);
    return _e50;
}
fn FsrRcasLoadF_u0028_vi2_u003b(p: ptr<function, vec2<i32>>) -> vec4<f32> {
    var fColor_1: vec4<f32>;
    var param_4: vec2<i32>;
    var param_5: vec3<f32>;
    var param_6: f32;
    var param_7: f32;
    let _e54 = (*p);
    param_4 = _e54;
    let _e55 = LoadRCAS_Input_u0028_vi2_u003b((&param_4));
    fColor_1 = _e55;
    let _e56 = Exposure_u0028_();
    let _e57 = PreExposure_u0028_();
    let _e58 = fColor_1;
    param_5 = _e58.xyz;
    param_6 = _e56;
    param_7 = _e57;
    let _e60 = PrepareRgb_u0028_vf3_u003b_f1_u003b_f1_u003b((&param_5), (&param_6), (&param_7));
    fColor_1[0u] = _e60.x;
    fColor_1[1u] = _e60.y;
    fColor_1[2u] = _e60.z;
    let _e67 = fColor_1;
    return _e67;
}
fn FsrRcasF_u0028_f1_u003b_f1_u003b_f1_u003b_vu2_u003b_vu4_u003b(pixR: ptr<function, f32>, pixG: ptr<function, f32>, pixB: ptr<function, f32>, ip: ptr<function, vec2<u32>>, con: ptr<function, vec4<u32>>) {
    var sp: vec2<i32>;
    var b_2: vec3<f32>;
    var param_8: vec2<i32>;
    var d: vec3<f32>;
    var param_9: vec2<i32>;
    var e: vec3<f32>;
    var param_10: vec2<i32>;
    var f: vec3<f32>;
    var param_11: vec2<i32>;
    var h: vec3<f32>;
    var param_12: vec2<i32>;
    var bR: f32;
    var bG: f32;
    var bB: f32;
    var dR: f32;
    var dG: f32;
    var dB: f32;
    var eR: f32;
    var eG: f32;
    var eB: f32;
    var fR: f32;
    var fG: f32;
    var fB: f32;
    var hR: f32;
    var hG: f32;
    var hB: f32;
    var param_13: f32;
    var param_14: f32;
    var param_15: f32;
    var param_16: f32;
    var param_17: f32;
    var param_18: f32;
    var param_19: f32;
    var param_20: f32;
    var param_21: f32;
    var param_22: f32;
    var param_23: f32;
    var param_24: f32;
    var param_25: f32;
    var param_26: f32;
    var param_27: f32;
    var bL: f32;
    var dL: f32;
    var eL: f32;
    var fL: f32;
    var hL: f32;
    var nz: f32;
    var param_28: f32;
    var param_29: f32;
    var param_30: f32;
    var param_31: f32;
    var param_32: f32;
    var param_33: f32;
    var param_34: f32;
    var param_35: f32;
    var param_36: f32;
    var param_37: f32;
    var param_38: f32;
    var param_39: f32;
    var param_40: f32;
    var param_41: f32;
    var mn4R: f32;
    var param_42: f32;
    var param_43: f32;
    var param_44: f32;
    var param_45: f32;
    var param_46: f32;
    var mn4G: f32;
    var param_47: f32;
    var param_48: f32;
    var param_49: f32;
    var param_50: f32;
    var param_51: f32;
    var mn4B: f32;
    var param_52: f32;
    var param_53: f32;
    var param_54: f32;
    var param_55: f32;
    var param_56: f32;
    var mx4R: f32;
    var param_57: f32;
    var param_58: f32;
    var param_59: f32;
    var mx4G: f32;
    var param_60: f32;
    var param_61: f32;
    var param_62: f32;
    var mx4B: f32;
    var param_63: f32;
    var param_64: f32;
    var param_65: f32;
    var peakC: vec2<f32>;
    var hitMinR: f32;
    var param_66: f32;
    var hitMinG: f32;
    var param_67: f32;
    var hitMinB: f32;
    var param_68: f32;
    var hitMaxR: f32;
    var param_69: f32;
    var hitMaxG: f32;
    var param_70: f32;
    var hitMaxB: f32;
    var param_71: f32;
    var lobeR: f32;
    var lobeG: f32;
    var lobeB: f32;
    var lobe: f32;
    var param_72: f32;
    var param_73: f32;
    var param_74: f32;
    var param_75: f32;
    var param_76: f32;
    var param_77: u32;
    var rcpL: f32;
    var param_78: f32;
    let _e169 = (*ip);
    sp = bitcast<vec2<i32>>(_e169);
    let _e171 = sp;
    param_8 = (_e171 + vec2<i32>(0i, -1i));
    let _e173 = FsrRcasLoadF_u0028_vi2_u003b((&param_8));
    b_2 = _e173.xyz;
    let _e175 = sp;
    param_9 = (_e175 + vec2<i32>(-1i, 0i));
    let _e177 = FsrRcasLoadF_u0028_vi2_u003b((&param_9));
    d = _e177.xyz;
    let _e179 = sp;
    param_10 = _e179;
    let _e180 = FsrRcasLoadF_u0028_vi2_u003b((&param_10));
    e = _e180.xyz;
    let _e182 = sp;
    param_11 = (_e182 + vec2<i32>(1i, 0i));
    let _e184 = FsrRcasLoadF_u0028_vi2_u003b((&param_11));
    f = _e184.xyz;
    let _e186 = sp;
    param_12 = (_e186 + vec2<i32>(0i, 1i));
    let _e188 = FsrRcasLoadF_u0028_vi2_u003b((&param_12));
    h = _e188.xyz;
    let _e191 = b_2[0u];
    bR = _e191;
    let _e193 = b_2[1u];
    bG = _e193;
    let _e195 = b_2[2u];
    bB = _e195;
    let _e197 = d[0u];
    dR = _e197;
    let _e199 = d[1u];
    dG = _e199;
    let _e201 = d[2u];
    dB = _e201;
    let _e203 = e[0u];
    eR = _e203;
    let _e205 = e[1u];
    eG = _e205;
    let _e207 = e[2u];
    eB = _e207;
    let _e209 = f[0u];
    fR = _e209;
    let _e211 = f[1u];
    fG = _e211;
    let _e213 = f[2u];
    fB = _e213;
    let _e215 = h[0u];
    hR = _e215;
    let _e217 = h[1u];
    hG = _e217;
    let _e219 = h[2u];
    hB = _e219;
    let _e220 = bR;
    param_13 = _e220;
    let _e221 = bG;
    param_14 = _e221;
    let _e222 = bB;
    param_15 = _e222;
    FsrRcasInputF_u0028_f1_u003b_f1_u003b_f1_u003b((&param_13), (&param_14), (&param_15));
    let _e223 = param_13;
    bR = _e223;
    let _e224 = param_14;
    bG = _e224;
    let _e225 = param_15;
    bB = _e225;
    let _e226 = dR;
    param_16 = _e226;
    let _e227 = dG;
    param_17 = _e227;
    let _e228 = dB;
    param_18 = _e228;
    FsrRcasInputF_u0028_f1_u003b_f1_u003b_f1_u003b((&param_16), (&param_17), (&param_18));
    let _e229 = param_16;
    dR = _e229;
    let _e230 = param_17;
    dG = _e230;
    let _e231 = param_18;
    dB = _e231;
    let _e232 = eR;
    param_19 = _e232;
    let _e233 = eG;
    param_20 = _e233;
    let _e234 = eB;
    param_21 = _e234;
    FsrRcasInputF_u0028_f1_u003b_f1_u003b_f1_u003b((&param_19), (&param_20), (&param_21));
    let _e235 = param_19;
    eR = _e235;
    let _e236 = param_20;
    eG = _e236;
    let _e237 = param_21;
    eB = _e237;
    let _e238 = fR;
    param_22 = _e238;
    let _e239 = fG;
    param_23 = _e239;
    let _e240 = fB;
    param_24 = _e240;
    FsrRcasInputF_u0028_f1_u003b_f1_u003b_f1_u003b((&param_22), (&param_23), (&param_24));
    let _e241 = param_22;
    fR = _e241;
    let _e242 = param_23;
    fG = _e242;
    let _e243 = param_24;
    fB = _e243;
    let _e244 = hR;
    param_25 = _e244;
    let _e245 = hG;
    param_26 = _e245;
    let _e246 = hB;
    param_27 = _e246;
    FsrRcasInputF_u0028_f1_u003b_f1_u003b_f1_u003b((&param_25), (&param_26), (&param_27));
    let _e247 = param_25;
    hR = _e247;
    let _e248 = param_26;
    hG = _e248;
    let _e249 = param_27;
    hB = _e249;
    let _e250 = bB;
    let _e252 = bR;
    let _e254 = bG;
    bL = ((_e250 * 0.5f) + ((_e252 * 0.5f) + _e254));
    let _e257 = dB;
    let _e259 = dR;
    let _e261 = dG;
    dL = ((_e257 * 0.5f) + ((_e259 * 0.5f) + _e261));
    let _e264 = eB;
    let _e266 = eR;
    let _e268 = eG;
    eL = ((_e264 * 0.5f) + ((_e266 * 0.5f) + _e268));
    let _e271 = fB;
    let _e273 = fR;
    let _e275 = fG;
    fL = ((_e271 * 0.5f) + ((_e273 * 0.5f) + _e275));
    let _e278 = hB;
    let _e280 = hR;
    let _e282 = hG;
    hL = ((_e278 * 0.5f) + ((_e280 * 0.5f) + _e282));
    let _e285 = bL;
)FSR2",
R"FSR2(    let _e287 = dL;
    let _e290 = fL;
    let _e293 = hL;
    let _e296 = eL;
    nz = (((((0.25f * _e285) + (0.25f * _e287)) + (0.25f * _e290)) + (0.25f * _e293)) - _e296);
    let _e298 = nz;
    let _e300 = bL;
    param_28 = _e300;
    let _e301 = dL;
    param_29 = _e301;
    let _e302 = eL;
    param_30 = _e302;
    let _e303 = ffxMax3_u0028_f1_u003b_f1_u003b_f1_u003b((&param_28), (&param_29), (&param_30));
    param_31 = _e303;
    let _e304 = fL;
    param_32 = _e304;
    let _e305 = hL;
    param_33 = _e305;
    let _e306 = ffxMax3_u0028_f1_u003b_f1_u003b_f1_u003b((&param_31), (&param_32), (&param_33));
    let _e307 = bL;
    param_34 = _e307;
    let _e308 = dL;
    param_35 = _e308;
    let _e309 = eL;
    param_36 = _e309;
    let _e310 = ffxMin3_u0028_f1_u003b_f1_u003b_f1_u003b((&param_34), (&param_35), (&param_36));
    param_37 = _e310;
    let _e311 = fL;
    param_38 = _e311;
    let _e312 = hL;
    param_39 = _e312;
    let _e313 = ffxMin3_u0028_f1_u003b_f1_u003b_f1_u003b((&param_37), (&param_38), (&param_39));
    param_40 = (_e306 - _e313);
    let _e315 = ffxApproximateReciprocalMedium_u0028_f1_u003b((&param_40));
    param_41 = (abs(_e298) * _e315);
    let _e317 = ffxSaturate_u0028_f1_u003b((&param_41));
    nz = _e317;
    let _e318 = nz;
    nz = ((-0.5f * _e318) + 1f);
    let _e321 = bR;
    param_42 = _e321;
    let _e322 = dR;
    param_43 = _e322;
    let _e323 = fR;
    param_44 = _e323;
    let _e324 = ffxMin3_u0028_f1_u003b_f1_u003b_f1_u003b((&param_42), (&param_43), (&param_44));
    param_45 = _e324;
    let _e325 = hR;
    param_46 = _e325;
    let _e326 = ffxMin_u0028_f1_u003b_f1_u003b((&param_45), (&param_46));
    mn4R = _e326;
    let _e327 = bG;
    param_47 = _e327;
    let _e328 = dG;
    param_48 = _e328;
    let _e329 = fG;
    param_49 = _e329;
    let _e330 = ffxMin3_u0028_f1_u003b_f1_u003b_f1_u003b((&param_47), (&param_48), (&param_49));
    param_50 = _e330;
    let _e331 = hG;
    param_51 = _e331;
    let _e332 = ffxMin_u0028_f1_u003b_f1_u003b((&param_50), (&param_51));
    mn4G = _e332;
    let _e333 = bB;
    param_52 = _e333;
    let _e334 = dB;
    param_53 = _e334;
    let _e335 = fB;
    param_54 = _e335;
    let _e336 = ffxMin3_u0028_f1_u003b_f1_u003b_f1_u003b((&param_52), (&param_53), (&param_54));
    param_55 = _e336;
    let _e337 = hB;
    param_56 = _e337;
    let _e338 = ffxMin_u0028_f1_u003b_f1_u003b((&param_55), (&param_56));
    mn4B = _e338;
    let _e339 = bR;
    param_57 = _e339;
    let _e340 = dR;
    param_58 = _e340;
    let _e341 = fR;
    param_59 = _e341;
    let _e342 = ffxMax3_u0028_f1_u003b_f1_u003b_f1_u003b((&param_57), (&param_58), (&param_59));
    let _e343 = hR;
    mx4R = max(_e342, _e343);
    let _e345 = bG;
    param_60 = _e345;
    let _e346 = dG;
    param_61 = _e346;
    let _e347 = fG;
    param_62 = _e347;
    let _e348 = ffxMax3_u0028_f1_u003b_f1_u003b_f1_u003b((&param_60), (&param_61), (&param_62));
    let _e349 = hG;
    mx4G = max(_e348, _e349);
    let _e351 = bB;
    param_63 = _e351;
    let _e352 = dB;
    param_64 = _e352;
    let _e353 = fB;
    param_65 = _e353;
    let _e354 = ffxMax3_u0028_f1_u003b_f1_u003b_f1_u003b((&param_63), (&param_64), (&param_65));
    let _e355 = hB;
    mx4B = max(_e354, _e355);
    peakC = vec2<f32>(1f, -4f);
    let _e357 = mn4R;
    let _e358 = mx4R;
    param_66 = (4f * _e358);
    let _e360 = rcp_u0028_f1_u003b((&param_66));
    hitMinR = (_e357 * _e360);
    let _e362 = mn4G;
    let _e363 = mx4G;
    param_67 = (4f * _e363);
    let _e365 = rcp_u0028_f1_u003b((&param_67));
    hitMinG = (_e362 * _e365);
    let _e367 = mn4B;
    let _e368 = mx4B;
    param_68 = (4f * _e368);
    let _e370 = rcp_u0028_f1_u003b((&param_68));
    hitMinB = (_e367 * _e370);
    let _e373 = peakC[0u];
    let _e374 = mx4R;
    let _e376 = mn4R;
    let _e379 = peakC[1u];
    param_69 = ((4f * _e376) + _e379);
    let _e381 = rcp_u0028_f1_u003b((&param_69));
    hitMaxR = ((_e373 - _e374) * _e381);
    let _e384 = peakC[0u];
    let _e385 = mx4G;
    let _e387 = mn4G;
    let _e390 = peakC[1u];
    param_70 = ((4f * _e387) + _e390);
    let _e392 = rcp_u0028_f1_u003b((&param_70));
    hitMaxG = ((_e384 - _e385) * _e392);
    let _e395 = peakC[0u];
    let _e396 = mx4B;
    let _e398 = mn4B;
    let _e401 = peakC[1u];
    param_71 = ((4f * _e398) + _e401);
    let _e403 = rcp_u0028_f1_u003b((&param_71));
    hitMaxB = ((_e395 - _e396) * _e403);
    let _e405 = hitMinR;
    let _e407 = hitMaxR;
    lobeR = max(-(_e405), _e407);
    let _e409 = hitMinG;
    let _e411 = hitMaxG;
    lobeG = max(-(_e409), _e411);
    let _e413 = hitMinB;
    let _e415 = hitMaxB;
    lobeB = max(-(_e413), _e415);
    let _e417 = lobeR;
    param_72 = _e417;
    let _e418 = lobeG;
    param_73 = _e418;
    let _e419 = lobeB;
    param_74 = _e419;
    let _e420 = ffxMax3_u0028_f1_u003b_f1_u003b_f1_u003b((&param_72), (&param_73), (&param_74));
    param_75 = _e420;
    param_76 = 0f;
    let _e421 = ffxMin_u0028_f1_u003b_f1_u003b((&param_75), (&param_76));
    let _e424 = (*con)[0u];
    param_77 = _e424;
    let _e425 = ffxAsFloat_u0028_u1_u003b((&param_77));
    lobe = (max(-0.1875f, _e421) * _e425);
    let _e427 = nz;
    let _e428 = lobe;
    lobe = (_e428 * _e427);
    let _e430 = lobe;
    param_78 = ((4f * _e430) + 1f);
    let _e433 = ffxApproximateReciprocalMedium_u0028_f1_u003b((&param_78));
    rcpL = _e433;
    let _e434 = lobe;
    let _e435 = bR;
    let _e437 = lobe;
    let _e438 = dR;
    let _e441 = lobe;
    let _e442 = hR;
    let _e445 = lobe;
    let _e446 = fR;
    let _e449 = eR;
    let _e451 = rcpL;
    (*pixR) = ((((((_e434 * _e435) + (_e437 * _e438)) + (_e441 * _e442)) + (_e445 * _e446)) + _e449) * _e451);
    let _e453 = lobe;
    let _e454 = bG;
    let _e456 = lobe;
    let _e457 = dG;
    let _e460 = lobe;
    let _e461 = hG;
    let _e464 = lobe;
    let _e465 = fG;
    let _e468 = eG;
    let _e470 = rcpL;
    (*pixG) = ((((((_e453 * _e454) + (_e456 * _e457)) + (_e460 * _e461)) + (_e464 * _e465)) + _e468) * _e470);
    let _e472 = lobe;
    let _e473 = bB;
    let _e475 = lobe;
    let _e476 = dB;
    let _e479 = lobe;
    let _e480 = hB;
    let _e483 = lobe;
    let _e484 = fB;
    let _e487 = eB;
    let _e489 = rcpL;
    (*pixB) = ((((((_e472 * _e473) + (_e475 * _e476)) + (_e479 * _e480)) + (_e483 * _e484)) + _e487) * _e489);
    return;
}
fn RCASConfig_u0028_() -> vec4<u32> {
    let _e49 = cbRCAS.rcasConfig;
    return _e49;
}
fn CurrFilter_u0028_vu2_u003b(pos: ptr<function, vec2<u32>>) {
    var c: vec3<f32>;
    var param_79: f32;
    var param_80: f32;
    var param_81: f32;
    var param_82: vec2<u32>;
    var param_83: vec4<u32>;
    var param_84: vec3<f32>;
    var param_85: f32;
    var param_86: vec2<u32>;
    var param_87: vec3<f32>;
    let _e59 = RCASConfig_u0028_();
    let _e60 = (*pos);
    param_82 = _e60;
    param_83 = _e59;
    FsrRcasF_u0028_f1_u003b_f1_u003b_f1_u003b_vu2_u003b_vu4_u003b((&param_79), (&param_80), (&param_81), (&param_82), (&param_83));
    let _e61 = param_79;
    c[0u] = _e61;
    let _e63 = param_80;
    c[1u] = _e63;
    let _e65 = param_81;
    c[2u] = _e65;
    let _e67 = Exposure_u0028_();
    let _e68 = c;
    param_84 = _e68;
    param_85 = _e67;
    let _e69 = UnprepareRgb_u0028_vf3_u003b_f1_u003b((&param_84), (&param_85));
    c = _e69;
    let _e70 = (*pos);
    param_86 = _e70;
    let _e71 = c;
    param_87 = _e71;
    WriteUpscaledOutput_u0028_vu2_u003b_vf3_u003b((&param_86), (&param_87));
    return;
}
fn bitfieldInsertMask_u0028_u1_u003b_u1_u003b_u1_u003b(src: ptr<function, u32>, ins: ptr<function, u32>, bits: ptr<function, u32>) -> u32 {
    let _e51 = (*src);
    let _e52 = (*ins);
    let _e53 = (*bits);
    return insertBits(_e51, _e52, bitcast<u32>(0i), bitcast<u32>(bitcast<i32>(_e53)));
}
fn bitfieldExtract_u0028_u1_u003b_u1_u003b_u1_u003b(src_1: ptr<function, u32>, off: ptr<function, u32>, bits_1: ptr<function, u32>) -> u32 {
    let _e51 = (*src_1);
    let _e52 = (*off);
    let _e54 = (*bits_1);
    return extractBits(_e51, bitcast<u32>(bitcast<i32>(_e52)), bitcast<u32>(bitcast<i32>(_e54)));
}
fn ffxRemapForQuad_u0028_u1_u003b(a: ptr<function, u32>) -> vec2<u32> {
    var param_88: u32;
    var param_89: u32;
    var param_90: u32;
    var param_91: u32;
    var param_92: u32;
    var param_93: u32;
    var param_94: u32;
    var param_95: u32;
    var param_96: u32;
    let _e58 = (*a);
    param_88 = _e58;
    param_89 = 1u;
    param_90 = 3u;
    let _e59 = bitfieldExtract_u0028_u1_u003b_u1_u003b_u1_u003b((&param_88), (&param_89), (&param_90));
    let _e60 = (*a);
    param_91 = _e60;
    param_92 = 3u;
    param_93 = 3u;
    let _e61 = bitfieldExtract_u0028_u1_u003b_u1_u003b_u1_u003b((&param_91), (&param_92), (&param_93));
    param_94 = _e61;
    let _e62 = (*a);
    param_95 = _e62;
    param_96 = 1u;
    let _e63 = bitfieldInsertMask_u0028_u1_u003b_u1_u003b_u1_u003b((&param_94), (&param_95), (&param_96));
    return vec2<u32>(_e59, _e63);
}
fn RCAS_u0028_vu3_u003b_vu3_u003b_vu3_u003b(LocalThreadId: ptr<function, vec3<u32>>, WorkGroupId: ptr<function, vec3<u32>>, Dtid: ptr<function, vec3<u32>>) {
    var gxy: vec2<u32>;
    var param_97: u32;
    var param_98: vec2<u32>;
    var param_99: vec2<u32>;
    var param_100: vec2<u32>;
    var param_101: vec2<u32>;
    let _e58 = (*LocalThreadId)[0u];
    param_97 = _e58;
    let _e59 = ffxRemapForQuad_u0028_u1_u003b((&param_97));
    let _e61 = (*WorkGroupId)[0u];
    let _e65 = (*WorkGroupId)[1u];
    gxy = (_e59 + vec2<u32>((_e61 << bitcast<u32>(4u)), (_e65 << bitcast<u32>(4u))));
    let _e70 = gxy;
    param_98 = _e70;
    CurrFilter_u0028_vu2_u003b((&param_98));
    let _e72 = gxy[0u];
    gxy[0u] = (_e72 + 8u);
    let _e75 = gxy;
    param_99 = _e75;
    CurrFilter_u0028_vu2_u003b((&param_99));
    let _e77 = gxy[1u];
    gxy[1u] = (_e77 + 8u);
    let _e80 = gxy;
    param_100 = _e80;
    CurrFilter_u0028_vu2_u003b((&param_100));
    let _e82 = gxy[0u];
    gxy[0u] = (_e82 - 8u);
    let _e85 = gxy;
    param_101 = _e85;
    CurrFilter_u0028_vu2_u003b((&param_101));
    return;
}
fn main_1() {
    var param_102: vec3<u32>;
    var param_103: vec3<u32>;
    var param_104: vec3<u32>;
    let _e51 = gl_LocalInvocationID_1;
    param_102 = _e51;
    let _e52 = gl_WorkGroupID_1;
    param_103 = _e52;
    let _e53 = gl_GlobalInvocationID_1;
    param_104 = _e53;
    RCAS_u0028_vu3_u003b_vu3_u003b_vu3_u003b((&param_102), (&param_103), (&param_104));
    return;
}
@compute @workgroup_size(64, 1, 1)
fn main(@builtin(local_invocation_id) gl_LocalInvocationID: vec3<u32>, @builtin(workgroup_id) gl_WorkGroupID: vec3<u32>, @builtin(global_invocation_id) gl_GlobalInvocationID: vec3<u32>) {
    gl_LocalInvocationID_1 = gl_LocalInvocationID;
    gl_WorkGroupID_1 = gl_WorkGroupID;
    gl_GlobalInvocationID_1 = gl_GlobalInvocationID;
    main_1();
}

)FSR2",
};
inline const Binding rcas_bindings[] = {
    {3, kUniform, "cbFSR2_", "cbFSR2_", false},
    {2, kStorageTexture, "rw_upscaled_output", "rgba16float", false},
    {0, kTexture, "r_input_exposure", "", false},
    {4, kUniform, "cbRCAS", "cbRCAS", false},
    {1, kTexture, "r_rcas_input", "", false},
};
inline const Pass passes[] = {
    {"compute_luminance_pyramid", compute_luminance_pyramid_parts, (int)(sizeof compute_luminance_pyramid_parts / sizeof *compute_luminance_pyramid_parts), compute_luminance_pyramid_bindings, (int)(sizeof compute_luminance_pyramid_bindings / sizeof *compute_luminance_pyramid_bindings), {256, 1, 1}},
    {"reconstruct_previous_depth", reconstruct_previous_depth_parts, (int)(sizeof reconstruct_previous_depth_parts / sizeof *reconstruct_previous_depth_parts), reconstruct_previous_depth_bindings, (int)(sizeof reconstruct_previous_depth_bindings / sizeof *reconstruct_previous_depth_bindings), {8, 8, 1}},
    {"depth_clip", depth_clip_parts, (int)(sizeof depth_clip_parts / sizeof *depth_clip_parts), depth_clip_bindings, (int)(sizeof depth_clip_bindings / sizeof *depth_clip_bindings), {8, 8, 1}},
    {"lock", lock_parts, (int)(sizeof lock_parts / sizeof *lock_parts), lock_bindings, (int)(sizeof lock_bindings / sizeof *lock_bindings), {8, 8, 1}},
    {"accumulate", accumulate_parts, (int)(sizeof accumulate_parts / sizeof *accumulate_parts), accumulate_bindings, (int)(sizeof accumulate_bindings / sizeof *accumulate_bindings), {8, 8, 1}},
    {"rcas", rcas_parts, (int)(sizeof rcas_parts / sizeof *rcas_parts), rcas_bindings, (int)(sizeof rcas_bindings / sizeof *rcas_bindings), {64, 1, 1}},
};
}  // namespace rx::fsr2gen
