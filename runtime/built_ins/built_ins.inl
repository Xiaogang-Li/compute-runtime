/*
 * Copyright (c) 2018, Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "runtime/built_ins/aux_translation_builtin.h"
#include "runtime/mem_obj/buffer.h"
#include "runtime/kernel/kernel.h"

namespace OCLRT {
template <typename... KernelsDescArgsT>
void BuiltinDispatchInfoBuilder::populate(Context &context, Device &device, EBuiltInOps op, const char *options, KernelsDescArgsT &&... desc) {
    auto src = kernelsLib.getBuiltinsLib().getBuiltinCode(op, BuiltinCode::ECodeType::Any, device);
    prog.reset(BuiltinsLib::createProgramFromCode(src, context, device).release());
    prog->build(0, nullptr, options, nullptr, nullptr, kernelsLib.isCacheingEnabled());
    grabKernels(std::forward<KernelsDescArgsT>(desc)...);
}

template <typename HWFamily>
BuiltInOp<HWFamily, EBuiltInOps::AuxTranslation>::BuiltInOp(BuiltIns &kernelsLib, Context &context, Device &device)
    : BuiltinDispatchInfoBuilder(kernelsLib) {
    BuiltinDispatchInfoBuilder::populate(context, device, EBuiltInOps::AuxTranslation, "", "fullCopy", baseKernel);
    resizeKernelInstances(5);
}

template <typename HWFamily>
bool BuiltInOp<HWFamily, EBuiltInOps::AuxTranslation>::buildDispatchInfos(MultiDispatchInfo &multiDispatchInfo, const BuiltinOpParams &operationParams) const {
    size_t kernelInstanceNumber = 0;
    resizeKernelInstances(operationParams.buffersForAuxTranslation->size());

    for (auto &buffer : *operationParams.buffersForAuxTranslation) {
        DispatchInfoBuilder<SplitDispatch::Dim::d1D, SplitDispatch::SplitMode::NoSplit> builder;
        auto graphicsAllocation = buffer->getGraphicsAllocation();
        size_t allocationSize = graphicsAllocation->getUnderlyingBufferSize();

        if (AuxTranslationDirection::AuxToNonAux == operationParams.auxTranslationDirection) {
            builder.setKernel(convertToNonAuxKernel.at(kernelInstanceNumber++).get());
            builder.setArg(0, buffer);
            builder.setArgSvm(1, allocationSize, reinterpret_cast<void *>(graphicsAllocation->getGpuAddress()));
        } else {
            UNRECOVERABLE_IF(AuxTranslationDirection::NonAuxToAux != operationParams.auxTranslationDirection);
            builder.setKernel(convertToAuxKernel.at(kernelInstanceNumber++).get());
            builder.setArgSvm(0, allocationSize, reinterpret_cast<void *>(graphicsAllocation->getGpuAddress()));
            builder.setArg(1, buffer);
        }

        size_t elementSize = sizeof(uint32_t) * 4;
        DEBUG_BREAK_IF(allocationSize < elementSize || !isAligned<4>(allocationSize));
        size_t xGws = allocationSize / elementSize;

        builder.setDispatchGeometry(Vec3<size_t>{xGws, 0, 0}, Vec3<size_t>{0, 0, 0}, Vec3<size_t>{0, 0, 0});
        builder.bake(multiDispatchInfo);
    }

    return true;
}

template <typename HWFamily>
void BuiltInOp<HWFamily, EBuiltInOps::AuxTranslation>::resizeKernelInstances(size_t size) const {
    convertToNonAuxKernel.reserve(size);
    convertToAuxKernel.reserve(size);

    for (size_t i = convertToNonAuxKernel.size(); i < size; i++) {
        auto clonedKernel1 = Kernel::create(baseKernel->getProgram(), baseKernel->getKernelInfo(), nullptr);
        auto clonedKernel2 = Kernel::create(baseKernel->getProgram(), baseKernel->getKernelInfo(), nullptr);
        clonedKernel1->cloneKernel(baseKernel);
        clonedKernel2->cloneKernel(baseKernel);

        convertToNonAuxKernel.emplace_back(clonedKernel1);
        convertToAuxKernel.emplace_back(clonedKernel2);
    }
}

} // namespace OCLRT
