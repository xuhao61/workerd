// Copyright (c) 2017-2022 Cloudflare, Inc.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "gpu-device.h"
#include "gpu-bindgroup-layout.h"
#include "gpu-bindgroup.h"
#include "gpu-buffer.h"
#include "gpu-command-encoder.h"
#include "gpu-compute-pipeline.h"
#include "gpu-errors.h"
#include "gpu-query-set.h"
#include "gpu-queue.h"
#include "gpu-sampler.h"
#include "gpu-texture.h"
#include "gpu-utils.h"
#include "workerd/jsg/exception.h"
#include "workerd/jsg/jsg.h"

namespace workerd::api::gpu {

jsg::Ref<GPUBuffer> GPUDevice::createBuffer(jsg::Lock& js, GPUBufferDescriptor descriptor) {
  wgpu::BufferDescriptor desc{};
  desc.label = descriptor.label.cStr();
  desc.mappedAtCreation = descriptor.mappedAtCreation;
  desc.size = descriptor.size;
  desc.usage = static_cast<wgpu::BufferUsage>(descriptor.usage);
  auto buffer = device_.CreateBuffer(&desc);
  return jsg::alloc<GPUBuffer>(js, kj::mv(buffer), kj::mv(desc), device_, kj::addRef(*async_));
}

jsg::Ref<GPUTexture> GPUDevice::createTexture(jsg::Lock& js, GPUTextureDescriptor descriptor) {
  wgpu::TextureDescriptor desc{};
  desc.label = descriptor.label.cStr();

  KJ_SWITCH_ONEOF(descriptor.size) {
    KJ_CASE_ONEOF(coords, jsg::Sequence<GPUIntegerCoordinate>) {
      // if we have a sequence of coordinates we assume that the order is: width, heigth, depth, if
      // available, and ignore all the rest.
      switch (coords.size()) {
      default:
      case 3:
        desc.size.depthOrArrayLayers = coords[2];
        KJ_FALLTHROUGH;
      case 2:
        desc.size.height = coords[1];
        KJ_FALLTHROUGH;
      case 1:
        desc.size.width = coords[0];
        break;
      case 0:
        JSG_FAIL_REQUIRE(TypeError, "invalid value for GPUExtent3D");
      }
    }
    KJ_CASE_ONEOF(size, GPUExtent3DDict) {
      KJ_IF_SOME(depthOrArrayLayers, size.depthOrArrayLayers) {
        desc.size.depthOrArrayLayers = depthOrArrayLayers;
      }
      KJ_IF_SOME(height, size.height) {
        desc.size.height = height;
      }
      desc.size.width = size.width;
    }
  }

  KJ_IF_SOME(mipLevelCount, descriptor.mipLevelCount) {
    desc.mipLevelCount = mipLevelCount;
  }
  KJ_IF_SOME(sampleCount, descriptor.sampleCount) {
    desc.sampleCount = sampleCount;
  }
  KJ_IF_SOME(dimension, descriptor.dimension) {
    desc.dimension = parseTextureDimension(dimension);
  }
  desc.format = parseTextureFormat(descriptor.format);
  desc.usage = static_cast<wgpu::TextureUsage>(descriptor.usage);
  KJ_IF_SOME(viewFormatsSeq, descriptor.viewFormats) {
    auto viewFormats = KJ_MAP(format, viewFormatsSeq)->wgpu::TextureFormat {
      return parseTextureFormat(format);
    };
    desc.viewFormats = viewFormats.begin();
    desc.viewFormatCount = viewFormats.size();
  }

  auto texture = device_.CreateTexture(&desc);
  return jsg::alloc<GPUTexture>(kj::mv(texture));
}

wgpu::CompareFunction parseCompareFunction(kj::StringPtr compare) {
  if (compare == "never") {
    return wgpu::CompareFunction::Never;
  }

  if (compare == "less") {
    return wgpu::CompareFunction::Less;
  }

  if (compare == "equal") {
    return wgpu::CompareFunction::Equal;
  }

  if (compare == "less-equal") {
    return wgpu::CompareFunction::LessEqual;
  }

  if (compare == "greater") {
    return wgpu::CompareFunction::Greater;
  }

  if (compare == "not-equal") {
    return wgpu::CompareFunction::NotEqual;
  }

  if (compare == "greater-equal") {
    return wgpu::CompareFunction::GreaterEqual;
  }

  if (compare == "always") {
    return wgpu::CompareFunction::Always;
  }

  JSG_FAIL_REQUIRE(TypeError, "unknown compare function", compare);
}

wgpu::AddressMode parseAddressMode(kj::StringPtr mode) {

  if (mode == "clamp-to-edge") {
    return wgpu::AddressMode::ClampToEdge;
  }

  if (mode == "repeat") {
    return wgpu::AddressMode::Repeat;
  }

  if (mode == "mirror-repeat") {
    return wgpu::AddressMode::MirrorRepeat;
  }

  JSG_FAIL_REQUIRE(TypeError, "unknown address mode", mode);
}

wgpu::FilterMode parseFilterMode(kj::StringPtr mode) {

  if (mode == "nearest") {
    return wgpu::FilterMode::Nearest;
  }

  if (mode == "linear") {
    return wgpu::FilterMode::Linear;
  }

  JSG_FAIL_REQUIRE(TypeError, "unknown filter mode", mode);
}

wgpu::MipmapFilterMode parseMipmapFilterMode(kj::StringPtr mode) {

  if (mode == "nearest") {
    return wgpu::MipmapFilterMode::Nearest;
  }

  if (mode == "linear") {
    return wgpu::MipmapFilterMode::Linear;
  }

  JSG_FAIL_REQUIRE(TypeError, "unknown mipmap filter mode", mode);
}

jsg::Ref<GPUSampler> GPUDevice::createSampler(GPUSamplerDescriptor descriptor) {
  wgpu::SamplerDescriptor desc{};

  KJ_IF_SOME(addressModeU, descriptor.addressModeU) {
    desc.addressModeU = parseAddressMode(addressModeU);
  }
  KJ_IF_SOME(addressModeV, descriptor.addressModeV) {
    desc.addressModeV = parseAddressMode(addressModeV);
  }
  KJ_IF_SOME(addressModeW, descriptor.addressModeW) {
    desc.addressModeW = parseAddressMode(addressModeW);
  }
  KJ_IF_SOME(magFilter, descriptor.magFilter) {
    desc.magFilter = parseFilterMode(magFilter);
  }
  KJ_IF_SOME(minFilter, descriptor.minFilter) {
    desc.minFilter = parseFilterMode(minFilter);
  }
  KJ_IF_SOME(mipmapFilter, descriptor.mipmapFilter) {
    desc.mipmapFilter = parseMipmapFilterMode(mipmapFilter);
  }
  KJ_IF_SOME(lodMinClamp, descriptor.lodMinClamp) {
    desc.lodMinClamp = lodMinClamp;
  }
  KJ_IF_SOME(lodMaxClamp, descriptor.lodMaxClamp) {
    desc.lodMaxClamp = lodMaxClamp;
  }
  desc.compare = parseCompareFunction(descriptor.compare);
  KJ_IF_SOME(maxAnisotropy, descriptor.maxAnisotropy) {
    desc.maxAnisotropy = maxAnisotropy;
  }

  KJ_IF_SOME(label, descriptor.label) {
    desc.label = label.cStr();
  }

  auto sampler = device_.CreateSampler(&desc);
  return jsg::alloc<GPUSampler>(kj::mv(sampler));
}

jsg::Ref<GPUBindGroupLayout>
GPUDevice::createBindGroupLayout(GPUBindGroupLayoutDescriptor descriptor) {
  wgpu::BindGroupLayoutDescriptor desc{};

  KJ_IF_SOME(label, descriptor.label) {
    desc.label = label.cStr();
  }

  kj::Vector<wgpu::BindGroupLayoutEntry> layoutEntries;
  for (auto& e : descriptor.entries) {
    layoutEntries.add(parseBindGroupLayoutEntry(e));
  }
  desc.entries = layoutEntries.begin();
  desc.entryCount = layoutEntries.size();

  auto bindGroupLayout = device_.CreateBindGroupLayout(&desc);
  return jsg::alloc<GPUBindGroupLayout>(kj::mv(bindGroupLayout));
}

jsg::Ref<GPUBindGroup> GPUDevice::createBindGroup(GPUBindGroupDescriptor descriptor) {
  wgpu::BindGroupDescriptor desc{};

  KJ_IF_SOME(label, descriptor.label) {
    desc.label = label.cStr();
  }

  desc.layout = *descriptor.layout;

  kj::Vector<wgpu::BindGroupEntry> bindGroupEntries;
  for (auto& e : descriptor.entries) {
    bindGroupEntries.add(parseBindGroupEntry(e));
  }

  desc.entries = bindGroupEntries.begin();
  desc.entryCount = bindGroupEntries.size();

  auto bindGroup = device_.CreateBindGroup(&desc);
  return jsg::alloc<GPUBindGroup>(kj::mv(bindGroup));
}

jsg::Ref<GPUShaderModule> GPUDevice::createShaderModule(GPUShaderModuleDescriptor descriptor) {
  wgpu::ShaderModuleDescriptor desc{};
  wgpu::ShaderModuleWGSLDescriptor wgsl_desc{};
  desc.nextInChain = &wgsl_desc;

  KJ_IF_SOME(label, descriptor.label) {
    desc.label = label.cStr();
  }

  wgsl_desc.code = descriptor.code.cStr();

  auto shader = device_.CreateShaderModule(&desc);
  return jsg::alloc<GPUShaderModule>(kj::mv(shader), kj::addRef(*async_));
}

void parseStencilFaceState(wgpu::StencilFaceState& out, jsg::Optional<GPUStencilFaceState>& in) {
  KJ_IF_SOME(stencilFront, in) {
    KJ_IF_SOME(compare, stencilFront.compare) {
      out.compare = parseCompareFunction(compare);
    }
    KJ_IF_SOME(failOp, stencilFront.failOp) {
      out.failOp = parseStencilOperation(failOp);
    }
    KJ_IF_SOME(depthFailOp, stencilFront.depthFailOp) {
      out.depthFailOp = parseStencilOperation(depthFailOp);
    }
    KJ_IF_SOME(passOp, stencilFront.passOp) {
      out.passOp = parseStencilOperation(passOp);
    }
  }
}

// We use this struct to maintain ownership of allocated objects to build the descriptor.
// After the dawn call that uses this descriptor these can be cleaned up, and will be
// when this object goes out of scope.
struct ParsedRenderPipelineDescriptor {
  wgpu::RenderPipelineDescriptor desc;
  kj::Own<wgpu::PrimitiveDepthClipControl> depthClip;
  kj::Own<wgpu::DepthStencilState> stencilState;
  kj::Own<wgpu::FragmentState> fragment;
  kj::Vector<kj::Array<wgpu::ConstantEntry>> constantLists;
  kj::Array<wgpu::VertexBufferLayout> buffers;
  kj::Vector<kj::Array<wgpu::VertexAttribute>> attributeLists;
  kj::Array<wgpu::ColorTargetState> targets;
  kj::Vector<wgpu::BlendState> blends;
};

ParsedRenderPipelineDescriptor
parseRenderPipelineDescriptor(GPURenderPipelineDescriptor& descriptor) {
  ParsedRenderPipelineDescriptor parsedDesc{};

  KJ_IF_SOME(label, descriptor.label) {
    parsedDesc.desc.label = label.cStr();
  }

  parsedDesc.desc.vertex.module = *descriptor.vertex.module;
  parsedDesc.desc.vertex.entryPoint = descriptor.vertex.entryPoint.cStr();

  KJ_IF_SOME(cDict, descriptor.vertex.constants) {
    kj::Vector<wgpu::ConstantEntry> constants;

    for (auto& f : cDict.fields) {
      wgpu::ConstantEntry e;
      e.key = f.name.cStr();
      e.value = f.value;
      constants.add(kj::mv(e));
    }
    auto constantsArray = constants.releaseAsArray();
    parsedDesc.desc.vertex.constants = constantsArray.begin();
    parsedDesc.desc.vertex.constantCount = constantsArray.size();
    parsedDesc.constantLists.add(kj::mv(constantsArray));
  }

  KJ_IF_SOME(bList, descriptor.vertex.buffers) {
    kj::Vector<wgpu::VertexBufferLayout> buffers;

    for (auto& b : bList) {
      wgpu::VertexBufferLayout bLayout;
      bLayout.arrayStride = b.arrayStride;

      KJ_IF_SOME(stepMode, b.stepMode) {
        bLayout.stepMode = parseVertexStepMode(stepMode);
      }

      kj::Vector<wgpu::VertexAttribute> attributes;
      for (auto& a : b.attributes) {
        wgpu::VertexAttribute attr;
        attr.format = parseVertexFormat(a.format);
        attr.offset = a.offset;
        attr.shaderLocation = a.shaderLocation;
        attributes.add(kj::mv(attr));
      }
      auto attrArray = attributes.releaseAsArray();
      bLayout.attributes = attrArray.begin();
      bLayout.attributeCount = attrArray.size();
      parsedDesc.attributeLists.add(kj::mv(attrArray));

      buffers.add(kj::mv(bLayout));
    }
    parsedDesc.buffers = buffers.releaseAsArray();
    parsedDesc.desc.vertex.buffers = parsedDesc.buffers.begin();
    parsedDesc.desc.vertex.bufferCount = parsedDesc.buffers.size();
  }

  KJ_SWITCH_ONEOF(descriptor.layout) {
    KJ_CASE_ONEOF(autoLayoutMode, jsg::NonCoercible<kj::String>) {
      JSG_REQUIRE(autoLayoutMode.value == "auto", TypeError, "unknown auto layout mode",
                  autoLayoutMode.value);
      parsedDesc.desc.layout = nullptr;
    }
    KJ_CASE_ONEOF(layout, jsg::Ref<GPUPipelineLayout>) {
      parsedDesc.desc.layout = *layout;
    }
  }

  KJ_IF_SOME(primitive, descriptor.primitive) {
    if (primitive.unclippedDepth.orDefault(false)) {
      auto depthClip = kj::heap<wgpu::PrimitiveDepthClipControl>();
      depthClip->unclippedDepth = true;
      parsedDesc.depthClip = kj::mv(depthClip);
      parsedDesc.desc.nextInChain = parsedDesc.depthClip;
    }

    KJ_IF_SOME(topology, primitive.topology) {
      parsedDesc.desc.primitive.topology = parsePrimitiveTopology(topology);
    }
    KJ_IF_SOME(indexFormat, primitive.stripIndexFormat) {
      parsedDesc.desc.primitive.stripIndexFormat = parseIndexFormat(indexFormat);
    }
    KJ_IF_SOME(frontFace, primitive.frontFace) {
      parsedDesc.desc.primitive.frontFace = parseFrontFace(frontFace);
    }
    KJ_IF_SOME(cullMode, primitive.cullMode) {
      parsedDesc.desc.primitive.cullMode = parseCullMode(cullMode);
    }
  }

  KJ_IF_SOME(depthStencil, descriptor.depthStencil) {
    auto depthStencilState = kj::heap<wgpu::DepthStencilState>();
    depthStencilState->format = parseTextureFormat(depthStencil.format);
    depthStencilState->depthWriteEnabled = depthStencil.depthWriteEnabled;

    parseStencilFaceState(depthStencilState->stencilFront, depthStencil.stencilFront);
    parseStencilFaceState(depthStencilState->stencilBack, depthStencil.stencilBack);

    KJ_IF_SOME(stencilReadMask, depthStencil.stencilReadMask) {
      depthStencilState->stencilReadMask = stencilReadMask;
    }
    KJ_IF_SOME(stencilWriteMask, depthStencil.stencilWriteMask) {
      depthStencilState->stencilWriteMask = stencilWriteMask;
    }
    KJ_IF_SOME(depthBias, depthStencil.depthBias) {
      depthStencilState->depthBias = depthBias;
    }
    KJ_IF_SOME(depthBiasSlopeScale, depthStencil.depthBiasSlopeScale) {
      depthStencilState->depthBiasSlopeScale = depthBiasSlopeScale;
    }
    KJ_IF_SOME(depthBiasClamp, depthStencil.depthBiasClamp) {
      depthStencilState->depthBiasClamp = depthBiasClamp;
    }

    parsedDesc.stencilState = kj::mv(depthStencilState);
    parsedDesc.desc.depthStencil = parsedDesc.stencilState;
  }

  KJ_IF_SOME(multisample, descriptor.multisample) {
    KJ_IF_SOME(count, multisample.count) {
      parsedDesc.desc.multisample.count = count;
    }
    KJ_IF_SOME(mask, multisample.mask) {
      parsedDesc.desc.multisample.mask = mask;
    }
    KJ_IF_SOME(alphaToCoverageEnabled, multisample.alphaToCoverageEnabled) {
      parsedDesc.desc.multisample.alphaToCoverageEnabled = alphaToCoverageEnabled;
    }
  }

  KJ_IF_SOME(fragment, descriptor.fragment) {
    auto fragmentState = kj::heap<wgpu::FragmentState>();
    fragmentState->module = *fragment.module;
    fragmentState->entryPoint = fragment.entryPoint.cStr();

    KJ_IF_SOME(cDict, fragment.constants) {
      kj::Vector<wgpu::ConstantEntry> constants;

      for (auto& f : cDict.fields) {
        wgpu::ConstantEntry e;
        e.key = f.name.cStr();
        e.value = f.value;
        constants.add(kj::mv(e));
      }
      auto constantsArray = constants.releaseAsArray();
      fragmentState->constants = constantsArray.begin();
      fragmentState->constantCount = constantsArray.size();
      parsedDesc.constantLists.add(kj::mv(constantsArray));
    }

    kj::Vector<wgpu::ColorTargetState> targets;
    for (auto& t : fragment.targets) {
      wgpu::ColorTargetState target;

      wgpu::BlendState blend;
      KJ_IF_SOME(dstFactor, t.blend.alpha.dstFactor) {
        blend.alpha.dstFactor = parseBlendFactor(dstFactor);
      }
      KJ_IF_SOME(srcFactor, t.blend.alpha.srcFactor) {
        blend.alpha.srcFactor = parseBlendFactor(srcFactor);
      }
      KJ_IF_SOME(operation, t.blend.alpha.operation) {
        blend.alpha.operation = parseBlendOperation(operation);
      }
      KJ_IF_SOME(dstFactor, t.blend.color.dstFactor) {
        blend.color.dstFactor = parseBlendFactor(dstFactor);
      }
      KJ_IF_SOME(srcFactor, t.blend.color.srcFactor) {
        blend.color.srcFactor = parseBlendFactor(srcFactor);
      }
      KJ_IF_SOME(operation, t.blend.color.operation) {
        blend.color.operation = parseBlendOperation(operation);
      }
      parsedDesc.blends.add(kj::mv(blend));
      target.blend = &parsedDesc.blends.back();

      target.format = parseTextureFormat(t.format);
      KJ_IF_SOME(mask, t.writeMask) {
        target.writeMask = static_cast<wgpu::ColorWriteMask>(mask);
      }

      targets.add(kj::mv(target));
    }
    auto targetsArray = targets.releaseAsArray();
    fragmentState->targets = targetsArray.begin();
    fragmentState->targetCount = targetsArray.size();
    parsedDesc.targets = kj::mv(targetsArray);

    parsedDesc.fragment = kj::mv(fragmentState);
    parsedDesc.desc.fragment = parsedDesc.fragment;
  }

  return kj::mv(parsedDesc);
}

jsg::Ref<GPURenderPipeline>
GPUDevice::createRenderPipeline(GPURenderPipelineDescriptor descriptor) {
  auto parsedDesc = parseRenderPipelineDescriptor(descriptor);
  auto pipeline = device_.CreateRenderPipeline(&parsedDesc.desc);
  return jsg::alloc<GPURenderPipeline>(kj::mv(pipeline));
}

jsg::Ref<GPUPipelineLayout>
GPUDevice::createPipelineLayout(GPUPipelineLayoutDescriptor descriptor) {
  wgpu::PipelineLayoutDescriptor desc{};

  KJ_IF_SOME(label, descriptor.label) {
    desc.label = label.cStr();
  }

  kj::Vector<wgpu::BindGroupLayout> bindGroupLayouts;
  for (auto& l : descriptor.bindGroupLayouts) {
    bindGroupLayouts.add(*l);
  }

  desc.bindGroupLayouts = bindGroupLayouts.begin();
  desc.bindGroupLayoutCount = bindGroupLayouts.size();

  auto layout = device_.CreatePipelineLayout(&desc);
  return jsg::alloc<GPUPipelineLayout>(kj::mv(layout));
}

jsg::Ref<GPUCommandEncoder>
GPUDevice::createCommandEncoder(jsg::Optional<GPUCommandEncoderDescriptor> descriptor) {
  wgpu::CommandEncoderDescriptor desc{};

  kj::String label = kj::str("");
  KJ_IF_SOME(d, descriptor) {
    KJ_IF_SOME(l, d.label) {
      label = kj::mv(l);
      desc.label = label.cStr();
    }
  }

  auto encoder = device_.CreateCommandEncoder(&desc);
  return jsg::alloc<GPUCommandEncoder>(kj::mv(encoder), kj::mv(label));
}

// TODO(soon): checks the allocations that are done during this method for dangling refs.
// Will problably need to implement some allocator that will keep track of what needs
// to be deallocated as it's being done in parseRenderPipelineDescriptor(). The constants
// vector jumps to mind since we're just returning a pointer to a local variable.
wgpu::ComputePipelineDescriptor
parseComputePipelineDescriptor(GPUComputePipelineDescriptor& descriptor) {
  wgpu::ComputePipelineDescriptor desc{};

  KJ_IF_SOME(label, descriptor.label) {
    desc.label = label.cStr();
  }

  desc.compute.module = *descriptor.compute.module;
  desc.compute.entryPoint = descriptor.compute.entryPoint.cStr();

  kj::Vector<wgpu::ConstantEntry> constants;
  KJ_IF_SOME(cDict, descriptor.compute.constants) {
    for (auto& f : cDict.fields) {
      wgpu::ConstantEntry e;
      e.key = f.name.cStr();
      e.value = f.value;
      constants.add(kj::mv(e));
    }
  }

  desc.compute.constants = constants.begin();
  desc.compute.constantCount = constants.size();

  KJ_SWITCH_ONEOF(descriptor.layout) {
    KJ_CASE_ONEOF(autoLayoutMode, jsg::NonCoercible<kj::String>) {
      JSG_REQUIRE(autoLayoutMode.value == "auto", TypeError, "unknown auto layout mode",
                  autoLayoutMode.value);
      desc.layout = nullptr;
    }
    KJ_CASE_ONEOF(layout, jsg::Ref<GPUPipelineLayout>) {
      desc.layout = *layout;
    }
  }

  return kj::mv(desc);
}

jsg::Ref<GPUComputePipeline>
GPUDevice::createComputePipeline(GPUComputePipelineDescriptor descriptor) {
  wgpu::ComputePipelineDescriptor desc = parseComputePipelineDescriptor(descriptor);
  auto pipeline = device_.CreateComputePipeline(&desc);
  return jsg::alloc<GPUComputePipeline>(kj::mv(pipeline));
}

jsg::Promise<kj::Maybe<jsg::Ref<GPUError>>> GPUDevice::popErrorScope(jsg::Lock& js) {
  struct Context {
    kj::Own<kj::PromiseFulfiller<kj::Maybe<jsg::Ref<GPUError>>>> fulfiller;
    AsyncTask task;
  };

  auto paf = kj::newPromiseAndFulfiller<kj::Maybe<jsg::Ref<GPUError>>>();
  // This context object will hold information for the callback, including the
  // fullfiller to signal the caller with the result, and an async task that
  // will ensure the device's Tick() function is called periodically. It will be
  // deallocated at the end of the callback function.
  auto ctx = new Context{kj::mv(paf.fulfiller), AsyncTask(kj::addRef(*async_))};

  device_.PopErrorScope(
      [](WGPUErrorType type, char const* message, void* userdata) {
        auto c = std::unique_ptr<Context>(static_cast<Context*>(userdata));
        switch (type) {
        case WGPUErrorType::WGPUErrorType_NoError:
          c->fulfiller->fulfill(kj::none);
          break;
        case WGPUErrorType::WGPUErrorType_OutOfMemory: {
          jsg::Ref<GPUError> err = jsg::alloc<GPUOutOfMemoryError>(kj::str(message));
          c->fulfiller->fulfill(kj::mv(err));
          break;
        }
        case WGPUErrorType::WGPUErrorType_Validation: {
          jsg::Ref<GPUError> err = jsg::alloc<GPUValidationError>(kj::str(message));
          c->fulfiller->fulfill(kj::mv(err));
          break;
        }
        case WGPUErrorType::WGPUErrorType_Unknown:
        case WGPUErrorType::WGPUErrorType_DeviceLost:
          c->fulfiller->reject(JSG_KJ_EXCEPTION(FAILED, TypeError, message));
          break;
        default:
          c->fulfiller->reject(JSG_KJ_EXCEPTION(FAILED, TypeError, "unhandled error type"));
          break;
        }
      },
      ctx);

  auto& context = IoContext::current();
  return context.awaitIo(js, kj::mv(paf.promise));
}

jsg::Promise<jsg::Ref<GPUComputePipeline>>
GPUDevice::createComputePipelineAsync(jsg::Lock& js, GPUComputePipelineDescriptor descriptor) {
  wgpu::ComputePipelineDescriptor desc = parseComputePipelineDescriptor(descriptor);

  struct Context {
    kj::Own<kj::PromiseFulfiller<jsg::Ref<GPUComputePipeline>>> fulfiller;
    AsyncTask task;
  };
  auto paf = kj::newPromiseAndFulfiller<jsg::Ref<GPUComputePipeline>>();
  // This context object will hold information for the callback, including the
  // fullfiller to signal the caller with the result, and an async task that
  // will ensure the device's Tick() function is called periodically. It will be
  // deallocated at the end of the callback function.
  auto ctx = new Context{kj::mv(paf.fulfiller), AsyncTask(kj::addRef(*async_))};

  device_.CreateComputePipelineAsync(
      &desc,
      [](WGPUCreatePipelineAsyncStatus status, WGPUComputePipeline pipeline, char const* message,
         void* userdata) {
        // Note: this is invoked outside the JS isolate lock
        auto c = std::unique_ptr<Context>(static_cast<Context*>(userdata));

        switch (status) {
        case WGPUCreatePipelineAsyncStatus::WGPUCreatePipelineAsyncStatus_Success:
          c->fulfiller->fulfill(jsg::alloc<GPUComputePipeline>(kj::mv(pipeline)));
          break;
        default:
          c->fulfiller->reject(JSG_KJ_EXCEPTION(FAILED, TypeError, "unknown error"));
          break;
        }
      },
      ctx);

  auto& context = IoContext::current();
  return context.awaitIo(js, kj::mv(paf.promise));
}

jsg::Ref<GPUQueue> GPUDevice::getQueue() {
  auto queue = device_.GetQueue();
  return jsg::alloc<GPUQueue>(kj::mv(queue));
}

GPUDevice::~GPUDevice() {
  if (!destroyed_) {
    device_.Destroy();
    destroyed_ = true;
  }
}

void GPUDevice::destroy() {
  if (lost_promise_fulfiller_->isWaiting()) {
    auto lostInfo =
        jsg::alloc<GPUDeviceLostInfo>(kj::str("destroyed"), kj::str("device was destroyed"));
    lost_promise_fulfiller_->fulfill(kj::mv(lostInfo));
  }

  device_.Destroy();
  destroyed_ = true;
}

jsg::MemoizedIdentity<jsg::Promise<jsg::Ref<GPUDeviceLostInfo>>>& GPUDevice::getLost() {
  return lost_promise_;
}

kj::String parseDeviceLostReason(WGPUDeviceLostReason reason) {
  switch (reason) {
  case WGPUDeviceLostReason_Force32:
    KJ_UNREACHABLE
  case WGPUDeviceLostReason_Destroyed:
    return kj::str("destroyed");
  case WGPUDeviceLostReason_Undefined:
    return kj::str("undefined");
  }
}

GPUDevice::GPUDevice(jsg::Lock& js, wgpu::Device d)
    : device_(d), lost_promise_(nullptr), async_(kj::refcounted<AsyncRunner>(d)) {
  auto& context = IoContext::current();
  auto paf = kj::newPromiseAndFulfiller<jsg::Ref<GPUDeviceLostInfo>>();
  lost_promise_fulfiller_ = kj::mv(paf.fulfiller);
  lost_promise_ = context.awaitIo(js, kj::mv(paf.promise));

  device_.SetLoggingCallback(
      [](WGPULoggingType type, char const* message, void* userdata) {
        KJ_LOG(INFO, "WebGPU logging", kj::str(type), message);
      },
      this);

  device_.SetUncapturedErrorCallback(
      [](WGPUErrorType type, char const* message, void* userdata) {
        auto* self = static_cast<GPUDevice*>(userdata);
        if (self->getHandlerCount("uncapturederror") > 0) {
          jsg::Ref<GPUError> error = nullptr;
          switch (type) {
          case WGPUErrorType_Validation:
            error = jsg::alloc<GPUValidationError>(kj::str(message));
            break;
          case WGPUErrorType_NoError:
          case WGPUErrorType_Force32:
            KJ_UNREACHABLE;
          case WGPUErrorType_OutOfMemory:
            error = jsg::alloc<GPUOutOfMemoryError>(kj::str(message));
            break;
          case WGPUErrorType_Internal:
          case WGPUErrorType_DeviceLost:
          case WGPUErrorType_Unknown:
            error = jsg::alloc<GPUInternalError>(kj::str(message));
            break;
          }

          auto init = GPUUncapturedErrorEventInit{kj::mv(error)};
          auto ev = jsg::alloc<GPUUncapturedErrorEvent>("uncapturederror"_kj, kj::mv(init));
          self->dispatchEventImpl(IoContext::current().getCurrentLock(), kj::mv(ev));
          return;
        }

        // no "uncapturederror" handler
        KJ_LOG(INFO, "WebGPU uncaptured error", kj::str(type), message);
      },
      this);

  device_.SetDeviceLostCallback(
      [](WGPUDeviceLostReason reason, char const* message, void* userdata) {
        auto r = parseDeviceLostReason(reason);
        auto* self = static_cast<GPUDevice*>(userdata);
        if (self->lost_promise_fulfiller_->isWaiting()) {
          auto lostInfo = jsg::alloc<GPUDeviceLostInfo>(kj::mv(r), kj::str(message));
          self->lost_promise_fulfiller_->fulfill(kj::mv(lostInfo));
        }
      },
      this);
};

jsg::Ref<GPUQuerySet> GPUDevice::createQuerySet(GPUQuerySetDescriptor descriptor) {
  wgpu::QuerySetDescriptor desc{};

  KJ_IF_SOME(label, descriptor.label) {
    desc.label = label.cStr();
  }

  desc.count = descriptor.count;
  desc.type = parseQueryType(descriptor.type);

  auto querySet = device_.CreateQuerySet(&desc);
  return jsg::alloc<GPUQuerySet>(kj::mv(querySet));
}

wgpu::ErrorFilter parseErrorFilter(GPUErrorFilter& filter) {

  if (filter == "validation") {
    return wgpu::ErrorFilter::Validation;
  }

  if (filter == "out-of-memory") {
    return wgpu::ErrorFilter::OutOfMemory;
  }

  if (filter == "internal") {
    return wgpu::ErrorFilter::Internal;
  }

  JSG_FAIL_REQUIRE(TypeError, "unknown error filter", filter);
}

void GPUDevice::pushErrorScope(GPUErrorFilter filter) {
  wgpu::ErrorFilter f = parseErrorFilter(filter);
  device_.PushErrorScope(f);
}

jsg::Ref<GPUSupportedFeatures> GPUDevice::getFeatures() {
  wgpu::Device device(device_.Get());
  size_t count = device.EnumerateFeatures(nullptr);
  kj::Array<wgpu::FeatureName> features = kj::heapArray<wgpu::FeatureName>(count);
  if (count > 0) {
    device.EnumerateFeatures(&features[0]);
  }
  return jsg::alloc<GPUSupportedFeatures>(kj::mv(features));
}

jsg::Ref<GPUSupportedLimits> GPUDevice::getLimits() {
  wgpu::SupportedLimits limits{};
  JSG_REQUIRE(device_.GetLimits(&limits), TypeError, "failed to get device limits");
  return jsg::alloc<GPUSupportedLimits>(kj::mv(limits));
}

} // namespace workerd::api::gpu
