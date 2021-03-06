// Copyright 2018 The NXT Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "backend/vulkan/BindGroupVk.h"

#include "BindGroupLayoutVk.h"
#include "BufferVk.h"
#include "FencedDeleter.h"
#include "VulkanBackend.h"

namespace backend { namespace vulkan {

    BindGroup::BindGroup(BindGroupBuilder* builder) : BindGroupBase(builder) {
        // Create a pool to hold our descriptor set.
        // TODO(cwallez@chromium.org): This horribly inefficient, find a way to be better, for
        // example by having one pool per bind group layout instead.
        uint32_t numPoolSizes = 0;
        auto poolSizes = ToBackend(GetLayout())->ComputePoolSizes(&numPoolSizes);

        VkDescriptorPoolCreateInfo createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        createInfo.pNext = nullptr;
        createInfo.flags = 0;
        createInfo.maxSets = 1;
        createInfo.poolSizeCount = numPoolSizes;
        createInfo.pPoolSizes = poolSizes.data();

        Device* device = ToBackend(GetDevice());
        if (device->fn.CreateDescriptorPool(device->GetVkDevice(), &createInfo, nullptr, &mPool) !=
            VK_SUCCESS) {
            ASSERT(false);
        }

        // Now do the allocation of one descriptor set, this is very suboptimal too.
        VkDescriptorSetLayout vkLayout = ToBackend(GetLayout())->GetHandle();

        VkDescriptorSetAllocateInfo allocateInfo;
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.pNext = nullptr;
        allocateInfo.descriptorPool = mPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &vkLayout;

        if (device->fn.AllocateDescriptorSets(device->GetVkDevice(), &allocateInfo, &mHandle) !=
            VK_SUCCESS) {
            ASSERT(false);
        }

        // Now do a write of a single descriptor set with all possible chained data allocated on the
        // stack.
        uint32_t numWrites = 0;
        std::array<VkWriteDescriptorSet, kMaxBindingsPerGroup> writes;
        std::array<VkDescriptorBufferInfo, kMaxBindingsPerGroup> writeBufferInfo;

        const auto& layoutInfo = GetLayout()->GetBindingInfo();
        for (uint32_t bindingIndex : IterateBitSet(layoutInfo.mask)) {
            auto& write = writes[numWrites];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.pNext = nullptr;
            write.dstSet = mHandle;
            write.dstBinding = bindingIndex;
            write.dstArrayElement = 0;
            write.descriptorCount = 1;
            write.descriptorType = VulkanDescriptorType(layoutInfo.types[bindingIndex]);

            switch (layoutInfo.types[bindingIndex]) {
                case nxt::BindingType::UniformBuffer:
                case nxt::BindingType::StorageBuffer: {
                    BufferViewBase* view = GetBindingAsBufferView(bindingIndex);
                    Buffer* buffer = ToBackend(view->GetBuffer());

                    writeBufferInfo[numWrites].buffer = buffer->GetHandle();
                    writeBufferInfo[numWrites].offset = view->GetOffset();
                    writeBufferInfo[numWrites].range = view->GetSize();

                    write.pBufferInfo = &writeBufferInfo[numWrites];
                } break;

                case nxt::BindingType::Sampler:
                case nxt::BindingType::SampledTexture:
                default:
                    UNREACHABLE();
            }

            numWrites++;
        }

        device->fn.UpdateDescriptorSets(device->GetVkDevice(), numWrites, writes.data(), 0,
                                        nullptr);
    }

    BindGroup::~BindGroup() {
        // The descriptor set doesn't need to be delete because it's done implicitly when the
        // descriptor pool is destroyed.
        mHandle = VK_NULL_HANDLE;

        if (mPool != VK_NULL_HANDLE) {
            ToBackend(GetDevice())->GetFencedDeleter()->DeleteWhenUnused(mPool);
            mPool = VK_NULL_HANDLE;
        }
    }

    VkDescriptorSet BindGroup::GetHandle() const {
        return mHandle;
    }

}}  // namespace backend::vulkan
