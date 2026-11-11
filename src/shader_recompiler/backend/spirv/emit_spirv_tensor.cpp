// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"
#include "common/common_types.h"
#include "shader_recompiler/frontend/ir/program.h"

namespace Shader::Backend::SPIRV {
namespace {
// Whether the entry point can request fp16 round-to-nearest-even and denorm preserve, so that an
// fp32 -> fp16 -> fp32 conversion rounds like the software rounding below. The two agree for
// every non-NaN fp32 input; a NaN stays a NaN.
bool CanRoundHalfNatively(const Profile& profile, const Info& info) {
    if (!profile.support_float_controls || profile.has_broken_fp16_float_controls ||
        !profile.support_fp16_rounding_rte || !profile.support_fp16_denorm_preserve ||
        !profile.fp16_float_controls_independent_of_fp32 || info.uses_fp16_denorms_flush) {
        return false;
    }
    if (profile.support_separate_denorm_behavior && profile.support_separate_rounding_mode) {
        return true;
    }
    // fp64 has to take the same modes, which match how the guest treats fp64 by default.
    return profile.fp16_float_controls_shared_with_fp64 && profile.support_fp64_rounding_rte &&
           profile.support_fp64_denorm_preserve;
}
} // Anonymous namespace

void DefineSoftwareMatrixFunctions(EmitContext& ctx, const IR::Program& program) {
    std::array<bool, 2> needed{};
    for (const auto* block : program.blocks) {
        for (const auto& inst : block->Instructions()) {
            if (inst.GetOpcode() != IR::Opcode::TensorMulAdd || inst.Flags<u32>() == 0) continue;
            const u32 depth = inst.Flags<u32>();
            if (depth != 8 && depth != 16) throw LogicError("Invalid software HMMA depth");
            needed[depth == 16] = true;
        }
    }
    for (u32 variant = 0; variant < 2; ++variant) {
        if (!needed[variant]) continue;
        ctx.AddCapability(spv::Capability::GroupNonUniform);
        ctx.AddCapability(spv::Capability::GroupNonUniformShuffle);
        if (!ctx.profile.SupportsSubgroupStage(program.stage) ||
            ctx.profile.has_broken_spirv_subgroup_shuffle)
            throw NotImplementedException("Software HMMA requires subgroup shuffle support");
        const Id u32ty = ctx.U32[1], f32ty = ctx.F32[1];
        ctx.native_half_rounding = CanRoundHalfNatively(ctx.profile, program.info);
        const Id f16ty = ctx.native_half_rounding ? ctx.TypeFloat(16) : Id{};
        if (ctx.native_half_rounding) {
            ctx.AddCapability(spv::Capability::Float16);
        }
        const auto imm = [&](u32 v) { return ctx.Const(v); };
        const auto add = [&](Id a, Id b) { return ctx.OpIAdd(u32ty, a, b); };
        const auto and_ = [&](Id a, u32 b) { return ctx.OpBitwiseAnd(u32ty, a, imm(b)); };
        const auto unpack = [&](Id v) { return ctx.OpUnpackHalf2x16(ctx.F32[2], v); };
        const auto round_half = [&](Id value) {
            if (ctx.native_half_rounding) {
                // Without NoContraction, RADV folds the conversion pair away and nothing rounds.
                const Id half = ctx.OpFConvert(f16ty, value);
                ctx.Decorate(half, spv::Decoration::NoContraction);
                const Id widened = ctx.OpFConvert(f32ty, half);
                ctx.Decorate(widened, spv::Decoration::NoContraction);
                return widened;
            }
            // Do not express the intermediate rounding as unpack(pack(value)): drivers may
            // eliminate that round trip. Round normal significands explicitly, ties to even;
            // half subnormals lie on the exactly representable 2^-24 grid.
            const Id bits = ctx.OpBitcast(u32ty, value);
            const Id magnitude = and_(bits, 0x7fffffffU);
            const Id tie = and_(ctx.OpShiftRightLogical(u32ty, magnitude, imm(13)), 1);
            const Id rounded = and_(add(add(magnitude, imm(0xfff)), tie), ~0x1fffU);
            const Id normal = ctx.OpSelect(u32ty,
                ctx.OpUGreaterThanEqual(ctx.U1, rounded, imm(0x47800000)), imm(0x7f800000), rounded);
            const Id scaled = ctx.OpFMul(f32ty, ctx.OpBitcast(f32ty, magnitude), ctx.Const(16777216.0f));
            const Id subnormal = ctx.OpBitcast(u32ty,
                ctx.OpFMul(f32ty, ctx.OpRoundEven(f32ty, scaled), ctx.Const(0x1p-24f)));
            const Id finite = ctx.OpSelect(u32ty,
                ctx.OpUGreaterThanEqual(ctx.U1, magnitude, imm(0x38800000)), normal, subnormal);
            const Id result = ctx.OpSelect(u32ty,
                ctx.OpUGreaterThanEqual(ctx.U1, magnitude, imm(0x7f800000)), magnitude, finite);
            return ctx.OpBitcast(f32ty, ctx.OpBitwiseOr(u32ty, result, and_(bits, 0x80000000)));
        };
        const Id type = ctx.TypeFunction(ctx.U32[2], ctx.U32[4], ctx.U32[2], ctx.U32[2]);
        const Id fn = ctx.OpFunction(ctx.U32[2], spv::FunctionControlMask::DontInline, type);
        ctx.software_matrix_functions[variant] = fn;
        const Id matrix_a = ctx.OpFunctionParameter(ctx.U32[4]);
        const Id matrix_b = ctx.OpFunctionParameter(ctx.U32[2]);
        const Id matrix_c = ctx.OpFunctionParameter(ctx.U32[2]);
        ctx.AddLabel();
        const Id counter = ctx.AddLocalVariable(ctx.TypePointer(spv::StorageClass::Function, u32ty),
                                           spv::StorageClass::Function);
        std::array<Id, 4> accumulators;
        for (auto& acc : accumulators)
            acc = ctx.AddLocalVariable(ctx.TypePointer(spv::StorageClass::Function, f32ty),
                                 spv::StorageClass::Function);
        std::array<Id, 8> inputs;
        for (u32 i = 0; i < 4; ++i)
            inputs[i] = ctx.OpCompositeExtract(u32ty, matrix_a, i);
        for (u32 i = 0; i < 2; ++i) {
            inputs[4 + i] = ctx.OpCompositeExtract(u32ty, matrix_b, i);
            inputs[6 + i] = ctx.OpCompositeExtract(u32ty, matrix_c, i);
        }
        for (u32 row = 0; row < 2; ++row) {
            const Id c = unpack(inputs[6 + row]);
            for (u32 col = 0; col < 2; ++col)
                ctx.OpStore(accumulators[row * 2 + col], ctx.OpCompositeExtract(f32ty, c, col));
        }
        ctx.OpStore(counter, imm(0));
        const Id lane = ctx.OpLoad(u32ty, ctx.subgroup_local_invocation_id);
        const Id warp_base = and_(lane, ~31U);
        const Id row_base = and_(lane, 28);
        const Id col_base = ctx.OpShiftLeftLogical(u32ty, and_(lane, 3), imm(3));
        const Id header = ctx.OpLabel(), body = ctx.OpLabel(), next = ctx.OpLabel(), end = ctx.OpLabel();
        ctx.OpBranch(header);
        ctx.AddLabel(header);
        const Id pair = ctx.OpLoad(u32ty, counter);
        const Id more = ctx.OpULessThan(ctx.U1, pair, imm(variant ? 8 : 4));
        ctx.OpLoopMerge(end, next, spv::LoopControlMask::DontUnroll);
        ctx.OpBranchConditional(more, body, end);
        ctx.AddLabel(body);
        const Id upper = ctx.OpUGreaterThanEqual(ctx.U1, pair, imm(4));
        const Id component = and_(pair, 3);
        const Id a_lane = add(warp_base, add(row_base, component));
        const Id b_lane = add(warp_base, add(col_base, component));
        const Id b = ctx.OpSelect(u32ty, upper, inputs[5], inputs[4]);
        std::array<Id, 2> bs;
        for (u32 col = 0; col < 2; ++col)
            bs[col] = unpack(ctx.OpGroupNonUniformShuffle(u32ty, imm(3), b,
                                                          add(b_lane, imm(col * 4))));
        const Id round_now = ctx.OpIEqual(ctx.U1, component, imm(3));
        for (u32 row = 0; row < 2; ++row) {
            const Id a = ctx.OpSelect(u32ty, upper, inputs[row + 2], inputs[row]);
            const Id as = unpack(ctx.OpGroupNonUniformShuffle(u32ty, imm(3), a, a_lane));
            for (u32 col = 0; col < 2; ++col) {
                Id value = ctx.OpLoad(f32ty, accumulators[row * 2 + col]);
                for (u32 half = 0; half < 2; ++half) {
                    value = ctx.OpFma(f32ty, ctx.OpCompositeExtract(f32ty, as, half),
                                      ctx.OpCompositeExtract(f32ty, bs[col], half), value);
                    ctx.Decorate(value, spv::Decoration::NoContraction);
                }
                ctx.OpStore(accumulators[row * 2 + col], value);
            }
        }
        // HMMA.F16 rounds after each K=8 group, including the midpoint of K=16.
        // OpSelect evaluates both operands: keep the expensive half conversion in
        // a branch so the other three pairs do no rounding work. The counter is
        // identical in every participating lane and all shuffles precede this branch.
        const Id round_block = ctx.OpLabel(), round_merge = ctx.OpLabel();
        ctx.OpSelectionMerge(round_merge, spv::SelectionControlMask::DontFlatten);
        ctx.OpBranchConditional(round_now, round_block, round_merge);
        ctx.AddLabel(round_block);
        for (const Id accumulator : accumulators) {
            ctx.OpStore(accumulator, round_half(ctx.OpLoad(f32ty, accumulator)));
        }
        ctx.OpBranch(round_merge);
        ctx.AddLabel(round_merge);
        ctx.OpBranch(next);
        ctx.AddLabel(next);
        ctx.OpStore(counter, add(pair, imm(1)));
        ctx.OpBranch(header);
        ctx.AddLabel(end);
        std::array<Id, 2> packed;
        for (u32 row = 0; row < 2; ++row)
            packed[row] = ctx.OpPackHalf2x16(u32ty, ctx.OpCompositeConstruct(ctx.F32[2],
                ctx.OpLoad(f32ty, accumulators[row * 2]), ctx.OpLoad(f32ty, accumulators[row * 2 + 1])));
        ctx.OpReturnValue(ctx.OpCompositeConstruct(ctx.U32[2], packed[0], packed[1]));
        ctx.OpFunctionEnd();
    }
}

Id EmitTensorMulAdd(EmitContext& ctx, IR::Inst* inst, Id a, Id b, Id c) {
    const u32 depth = inst->Flags<u32>();
    if (depth != 8 && depth != 16) {
        throw NotImplementedException("Unsupported matrix multiply depth");
    }
    return ctx.OpFunctionCall(ctx.U32[2], ctx.software_matrix_functions[depth == 16], a, b, c);
}

} // namespace Shader::Backend::SPIRV
