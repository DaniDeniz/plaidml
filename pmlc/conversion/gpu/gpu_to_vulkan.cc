// Copyright 2020, Intel Corporation

#include "mlir/Conversion/GPUToVulkan/ConvertGPUToVulkanPass.h"
#include "mlir/Dialect/GPU/GPUDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/SPIRV/SPIRVOps.h"
#include "mlir/Dialect/SPIRV/Serialization.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Function.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/Module.h"
#include "mlir/IR/StandardTypes.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Support/LogicalResult.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"

#include "pmlc/conversion/gpu/pass_detail.h"
#include "pmlc/util/logging.h"
#include "pmlc/util/tags.h"

#include "mlir/Support/DebugStringHelper.h"

namespace pmlc::conversion::gpu {
namespace gpu = mlir::gpu;
namespace spirv = mlir::spirv;
namespace LLVM = mlir::LLVM;
using mlir::AllocOp;
using mlir::ArrayRef;
using mlir::BlockArgument;
using mlir::failure;
using mlir::FunctionType;
using mlir::LoadOp;
using mlir::Location;
using mlir::LogicalResult;
using mlir::MemRefCastOp;
using mlir::MemRefType;
using mlir::ModuleOp;
using mlir::OpBuilder;
using mlir::Operation;
using mlir::OpOperand;
using mlir::SmallString;
using mlir::SmallVector;
using mlir::StoreOp;
using mlir::StringRef;
using mlir::success;
using mlir::Type;
using mlir::UnrankedMemRefType;
using mlir::Value;

static constexpr const char *kSPIRVBinary = "SPIRV_BIN";
static constexpr const char *kPrint_memref_f32 = "print_memref_f32";
static constexpr const char *kInitVulkan = "initVulkan";
static constexpr const char *kDeinitVulkan = "deinitVulkan";
static constexpr const char *kRun = "run";
static constexpr const char *kCreateVulkanLaunchKernelAction =
    "createVulkanLaunchKernelAction";
static constexpr const char *kSetVulkanLaunchKernelAction =
    "setVulkanLaunchKernelAction";
static constexpr const char *kCreateVulkanMemoryTransferAction =
    "createVulkanMemoryTransferAction";
static constexpr const char *kAddVulkanLaunchActionToSchedule =
    "addVulkanLaunchActionToSchedule";

static constexpr const char *kBindAllBuffers = "bindAllBuffers";

static constexpr const char *kBindBufferBFloat16 = "bindBufferBFloat16";
static constexpr const char *kBindBufferFloat16 = "bindBufferFloat16";
static constexpr const char *kBindBufferFloat32 = "bindBufferFloat32";
static constexpr const char *kBindBufferFloat64 = "bindBufferFloat64";

// These functions are signless, meaning they apply to both signed and unsigned
// integers
static constexpr const char *kBindBufferInteger8 = "bindBufferInteger8";
static constexpr const char *kBindBufferInteger16 = "bindBufferInteger16";
static constexpr const char *kBindBufferInteger32 = "bindBufferInteger32";
static constexpr const char *kBindBufferInteger64 = "bindBufferInteger64";

static constexpr const int kByteBits = 8;
static constexpr const int kBufferCopyModeInit = 0;
static constexpr const int kBufferCopyModeHostToDevice = 1 << 0;
static constexpr const int kBufferCopyModeDeviceToHost = 1 << 1;

/// A pass to convert gpu launch op to vulkan launch call op, by creating a
/// SPIR-V binary shader from `spirv::ModuleOp` using `spirv::serialize`
/// function and attaching binary data and entry point name as an attributes to
/// created vulkan launch call op.
class ConvertGpuLaunchFuncToVulkanCalls
    : public ConvertGpuLaunchFuncToVulkanCallsBase<
          ConvertGpuLaunchFuncToVulkanCalls> {
public:
  void runOnOperation();

private:
  /// Creates a SPIR-V binary shader from the given `module` using
  /// `spirv::serialize` function.
  LogicalResult createBinaryShader(ModuleOp module,
                                   std::vector<char> &binaryShader);

  /// Creates a LLVM global for the given `name`.
  Value createEntryPointNameConstant(StringRef name, uint64_t lauchFuncIndex,
                                     Location loc, OpBuilder &builder);

  /// bind gpu.launchOp buffers to Vulkan runtime.
  LogicalResult bindBuffers(mlir::CallOp &callOp);

  /// Check and transfer VkBuffers when necessary.
  LogicalResult transferBuffers(Location loc, OpBuilder &builder,
                                gpu::LaunchFuncOp launchOp);

  /// Print a single buffer.
  LogicalResult printBuffer(Location loc, OpBuilder &builder, Value &buffer);

  /// Converts the given `luanchOp` to vulkan launch call.
  void convertGpuLaunchFunc(gpu::LaunchFuncOp launchOp);

  /// Declares all needed runtime functions.
  void declareVulkanFunctions(Location loc);

  /// Check buffer type and only copy from/to device when necessary
  uint32_t getBufferCopyMode(mlir::CallOp &callOp, Value &buffer);

  bool isInternalOperation(Operation *op);

  llvm::SmallSet<Operation *, 4> getExternalDependentOperations(Value &value);

  void getCachedTypes() {
    llvmVoidType = LLVM::LLVMType::getVoidTy(&getContext());
    llvmPointerType = LLVM::LLVMType::getInt8PtrTy(&getContext());
    llvmInt32Type = LLVM::LLVMType::getInt32Ty(&getContext());
    llvmInt64Type = LLVM::LLVMType::getInt64Ty(&getContext());

    OpBuilder builder(getOperation());
    mlirFloat32Type = builder.getF32Type();
  }

  mlir::Type getUnrankedMemRefType(Type elementType) {
    return UnrankedMemRefType::get(elementType, /*memorySpace=*/0);
  }

  const char *getBufferBindingFunc(Type elementType) {
    if (elementType.isInteger(8)) {
      return kBindBufferInteger8;
    }
    if (elementType.isInteger(16)) {
      return kBindBufferInteger16;
    }
    if (elementType.isInteger(32)) {
      return kBindBufferInteger32;
    }
    if (elementType.isInteger(64)) {
      return kBindBufferInteger64;
    }
    if (elementType.isBF16()) {
      return kBindBufferBFloat16;
    }
    if (elementType.isF16()) {
      return kBindBufferFloat16;
    }
    if (elementType.isF32()) {
      return kBindBufferFloat32;
    }
    if (elementType.isF64()) {
      return kBindBufferFloat64;
    }
    return nullptr;
  }

  LLVM::LLVMType getLLVMVoidType() { return llvmVoidType; }
  LLVM::LLVMType getLLVMPointerType() { return llvmPointerType; }
  LLVM::LLVMType getLLVMInt32Type() { return llvmInt32Type; }
  LLVM::LLVMType getLLVMInt64Type() { return llvmInt64Type; }

  mlir::Type getMLIRFloat32Type() { return mlirFloat32Type; }

  LLVM::LLVMType llvmVoidType;
  LLVM::LLVMType llvmPointerType;
  LLVM::LLVMType llvmInt32Type;
  LLVM::LLVMType llvmInt64Type;

  mlir::Type mlirFloat32Type;

  uint64_t numKernel = 0;
  uint64_t lauchFuncIndex = 0;
  mlir::Value vulkanRuntime;
  LLVM::CallOp deinitVulkan;
  llvm::DenseMap<Value, llvm::SmallVector<uint64_t, 2>> bufferMap;

  struct mlirTypeComparator {
    bool operator()(mlir::Type x, mlir::Type y) const {
      return x.getTypeID().getAsOpaquePointer() >
             y.getTypeID().getAsOpaquePointer();
    }
  };

  llvm::SmallSet<mlir::Type, 4, mlirTypeComparator> bufferElementTypes;
  llvm::SmallSet<StringRef, 4> optionalSymbols;
};

void ConvertGpuLaunchFuncToVulkanCalls::runOnOperation() {
  auto loc = getOperation().getLoc();
  getCachedTypes();

  getOperation().walk([this](gpu::LaunchFuncOp op) { numKernel++; });

  getOperation().walk(
      [this](gpu::LaunchFuncOp op) { convertGpuLaunchFunc(op); });

  // Erase `gpu::GPUModuleOp` and `spirv::Module` operations.
  for (auto gpuModule :
       llvm::make_early_inc_range(getOperation().getOps<gpu::GPUModuleOp>()))
    gpuModule.erase();

  for (auto spirvModule :
       llvm::make_early_inc_range(getOperation().getOps<spirv::ModuleOp>()))
    spirvModule.erase();

  for (auto funcOp : getOperation().getOps<mlir::FuncOp>()) {
    funcOp.walk([this](mlir::CallOp callOp) {
      if (callOp.callee() == kBindAllBuffers) {
        bindBuffers(callOp);
        callOp.erase();
      }
    });
  }

  // Declare runtime functions.
  declareVulkanFunctions(loc);
}

LogicalResult ConvertGpuLaunchFuncToVulkanCalls::createBinaryShader(
    ModuleOp module, std::vector<char> &binaryShader) {
  SmallVector<uint32_t, 0> binary;
  uint64_t shader_index = 0;
  for (auto spirvModule : module.getOps<spirv::ModuleOp>()) {
    if (shader_index == lauchFuncIndex) {
      if (failed(spirv::serialize(spirvModule, binary))) {
        return failure();
      }
    }
    shader_index++;
  }
  binaryShader.resize(binary.size() * sizeof(uint32_t));
  std::memcpy(binaryShader.data(), reinterpret_cast<char *>(binary.data()),
              binaryShader.size());
  return success();
}

Value ConvertGpuLaunchFuncToVulkanCalls::createEntryPointNameConstant(
    StringRef name, uint64_t lauchFuncIndex, Location loc, OpBuilder &builder) {
  SmallString<16> shaderName(name.begin(), name.end());
  // Append `\0` to follow C style string given that
  // LLVM::createGlobalString() won't handle this directly for us.
  shaderName.push_back('\0');

  std::string entryPointGlobalName =
      (name + "_spv_entry_point_name" + std::to_string(lauchFuncIndex)).str();
  return LLVM::createGlobalString(loc, builder, entryPointGlobalName,
                                  shaderName, LLVM::Linkage::Internal);
}

llvm::SmallSet<Operation *, 4>
ConvertGpuLaunchFuncToVulkanCalls::getExternalDependentOperations(
    Value &value) {
  llvm::SmallSet<Operation *, 4> operations;
  llvm::SetVector<OpOperand *> uses;

  operations.insert(value.getDefiningOp());
  for (auto &use : value.getUses()) {
    uses.insert(&use);
  }
  while (!uses.empty()) {
    auto owner = uses.back()->getOwner();
    operations.insert(owner);
    uses.pop_back();
    for (auto opResult : owner->getOpResults()) {
      for (auto &use : opResult.getUses()) {
        uses.insert(&use);
      }
    }
  }

  for (auto operation : operations) {
    if (isInternalOperation(operation)) {
      operations.erase(operation);
    }
  }
  return operations;
}

bool ConvertGpuLaunchFuncToVulkanCalls::isInternalOperation(Operation *op) {
  // Buffer related calls are all mlir::CallOp, does not need to check
  // LLVM::CallOp
  auto callOp = llvm::dyn_cast<mlir::CallOp>(op);
  if (callOp && (callOp.callee() == kBindAllBuffers ||
                 optionalSymbols.count(callOp.callee()))) {
    return true;
  }

  auto allocOp = llvm::dyn_cast<AllocOp>(op);
  auto memRefCastOp = llvm::dyn_cast<MemRefCastOp>(op);
  if (allocOp || memRefCastOp) {
    return true;
  }
  return false;
}

uint32_t
ConvertGpuLaunchFuncToVulkanCalls::getBufferCopyMode(mlir::CallOp &callOp,
                                                     Value &buffer) {
  uint32_t copyMode = kBufferCopyModeInit;
  if (buffer.isa<BlockArgument>()) {
    copyMode |= (kBufferCopyModeHostToDevice | kBufferCopyModeDeviceToHost);
    return copyMode;
  }

  auto operationDeps = getExternalDependentOperations(buffer);
  auto currentBlock = callOp.getOperation()->getBlock();
  for (auto op : operationDeps) {
    if (op->getBlock() == currentBlock) {
      if (op->isBeforeInBlock(callOp.getOperation())) {
        copyMode |= kBufferCopyModeHostToDevice;
      } else if (deinitVulkan.getOperation()->isBeforeInBlock(op)) {
        copyMode |= kBufferCopyModeDeviceToHost;
      } else {
        callOp.emitWarning("A host side buffer is used after copied to "
                           "device and before device returns.");
      }
    } else {
      copyMode |= (kBufferCopyModeHostToDevice | kBufferCopyModeDeviceToHost);
    }
  }
  return copyMode;
}

LogicalResult
ConvertGpuLaunchFuncToVulkanCalls::bindBuffers(mlir::CallOp &callOp) {
  OpBuilder builder(callOp);
  Location loc = callOp.getLoc();
  auto buffers = callOp.operands();

  // Create LLVM constant for the descriptor set index.
  // Bind all memrefs to the `0` descriptor set, the same way as `GPUToSPIRV`
  // pass does.
  Value descriptorSet = builder.create<LLVM::ConstantOp>(
      loc, getLLVMInt32Type(), builder.getI32IntegerAttr(0));

  for (uint32_t bindIndex = 0; bindIndex < buffers.size(); bindIndex++) {
    auto buffer = buffers[bindIndex];
    // Create LLVM constant for the descriptor binding index.
    Value descriptorBinding = builder.create<LLVM::ConstantOp>(
        loc, getLLVMInt32Type(), builder.getI32IntegerAttr(bindIndex));
    if (auto memRefType = buffer.getType().dyn_cast_or_null<MemRefType>()) {
      auto shape = memRefType.getShape();
      uint32_t numElement = 1;
      for (auto dim : shape) {
        numElement *= dim;
      }
      auto elementType = memRefType.getElementType();
      bufferElementTypes.insert(elementType);
      uint32_t elementTypeSize =
          llvm::divideCeil(elementType.getIntOrFloatBitWidth(), kByteBits);
      Value bufferByteSize = builder.create<LLVM::ConstantOp>(
          loc, getLLVMInt32Type(),
          builder.getI32IntegerAttr(numElement * elementTypeSize));
      Value unrankedBuffer = builder.create<MemRefCastOp>(
          loc, buffer, getUnrankedMemRefType(elementType));
      Value bufferCopyMode = builder.create<LLVM::ConstantOp>(
          loc, getLLVMInt32Type(),
          builder.getI32IntegerAttr(getBufferCopyMode(callOp, buffer)));

      builder.create<mlir::CallOp>(
          loc, ArrayRef<Type>{},
          builder.getSymbolRefAttr(getBufferBindingFunc(elementType)),
          ArrayRef<Value>{vulkanRuntime, descriptorSet, descriptorBinding,
                          bufferByteSize, bufferCopyMode, unrankedBuffer});
      optionalSymbols.insert(getBufferBindingFunc(elementType));
    } else {
      return failure();
    }
  }
  return success();
}

LogicalResult ConvertGpuLaunchFuncToVulkanCalls::transferBuffers(
    Location loc, OpBuilder &builder, gpu::LaunchFuncOp launchOp) {
  auto buffers = launchOp.operands();
  for (size_t i = 0; i < buffers.size(); i++) {
    for (auto pair : bufferMap) {
      if (pair.first == buffers[i]) {
        Value dst_index = builder.create<LLVM::ConstantOp>(
            loc, getLLVMInt64Type(), builder.getI64IntegerAttr(lauchFuncIndex));
        Value dst_binding = builder.create<LLVM::ConstantOp>(
            loc, getLLVMInt64Type(), builder.getI64IntegerAttr(i));
        Value src_index = builder.create<LLVM::ConstantOp>(
            loc, getLLVMInt64Type(), builder.getI64IntegerAttr(pair.second[0]));
        Value src_binding = builder.create<LLVM::ConstantOp>(
            loc, getLLVMInt64Type(), builder.getI64IntegerAttr(pair.second[1]));

        builder.create<LLVM::CallOp>(
            loc, ArrayRef<Type>{},
            builder.getSymbolRefAttr(kCreateVulkanMemoryTransferAction),
            ArrayRef<Value>{vulkanRuntime, src_index, src_binding, dst_index,
                            dst_binding});
        optionalSymbols.insert(kCreateVulkanMemoryTransferAction);
      }
    }
    llvm::SmallVector<uint64_t, 2> second;
    second.append({lauchFuncIndex, i});
    bufferMap[buffers[i]] = second;
  }
  return success();
}

LogicalResult ConvertGpuLaunchFuncToVulkanCalls::printBuffer(Location loc,
                                                             OpBuilder &builder,
                                                             Value &buffer) {
  auto type = buffer.getType();
  if (auto memRefType = type.dyn_cast_or_null<MemRefType>()) {
    auto elementType = memRefType.getElementType();
    if (elementType.isF32()) {
      auto unrankedBuffer = builder.create<MemRefCastOp>(
          loc, buffer, getUnrankedMemRefType(elementType));
      builder.create<mlir::CallOp>(loc, ArrayRef<Type>{},
                                   builder.getSymbolRefAttr(kPrint_memref_f32),
                                   ArrayRef<Value>(unrankedBuffer));
      optionalSymbols.insert(kPrint_memref_f32);
    }
  }
  return success();
}

void ConvertGpuLaunchFuncToVulkanCalls::declareVulkanFunctions(Location loc) {
  auto &ctx = getContext();
  ModuleOp module = getOperation();
  OpBuilder builder(module.getBody()->getTerminator());

  builder.create<LLVM::LLVMFuncOp>(
      loc, kInitVulkan,
      LLVM::LLVMType::getFunctionTy(getLLVMPointerType(), {},
                                    /*isVarArg=*/false));

  builder.create<LLVM::LLVMFuncOp>(
      loc, kCreateVulkanLaunchKernelAction,
      LLVM::LLVMType::getFunctionTy(getLLVMVoidType(),
                                    {getLLVMPointerType(), getLLVMPointerType(),
                                     getLLVMInt32Type(), getLLVMPointerType(),
                                     getLLVMInt32Type(), getLLVMInt32Type(),
                                     getLLVMInt32Type()},
                                    /*isVarArg=*/false));

  builder.create<LLVM::LLVMFuncOp>(
      loc, kSetVulkanLaunchKernelAction,
      LLVM::LLVMType::getFunctionTy(getLLVMVoidType(),
                                    {getLLVMPointerType(), getLLVMInt32Type()},
                                    /*isVarArg=*/false));

  builder.create<LLVM::LLVMFuncOp>(
      loc, kAddVulkanLaunchActionToSchedule,
      LLVM::LLVMType::getFunctionTy(getLLVMVoidType(), {getLLVMPointerType()},
                                    /*isVarArg=*/false));

  builder.create<LLVM::LLVMFuncOp>(
      loc, kRun,
      LLVM::LLVMType::getFunctionTy(getLLVMVoidType(), {getLLVMPointerType()},
                                    /*isVarArg=*/false));

  builder.create<LLVM::LLVMFuncOp>(
      loc, kDeinitVulkan,
      LLVM::LLVMType::getFunctionTy(getLLVMVoidType(), {getLLVMPointerType()},
                                    /*isVarArg=*/false));

  if (optionalSymbols.count(kPrint_memref_f32)) {
    builder.create<mlir::FuncOp>(
        loc, kPrint_memref_f32,
        FunctionType::get(
            {ArrayRef<Type>{getUnrankedMemRefType(getMLIRFloat32Type())}}, {},
            &ctx),
        ArrayRef<std::pair<mlir::Identifier, mlir::Attribute>>());
  }

  if (optionalSymbols.count(kCreateVulkanMemoryTransferAction)) {
    builder.create<LLVM::LLVMFuncOp>(
        loc, kCreateVulkanMemoryTransferAction,
        LLVM::LLVMType::getFunctionTy(getLLVMVoidType(),
                                      {getLLVMPointerType(), getLLVMInt64Type(),
                                       getLLVMInt64Type(), getLLVMInt64Type(),
                                       getLLVMInt64Type()},
                                      /*isVarArg=*/false));
  }

  for (auto bufferElementType : bufferElementTypes) {
    auto func = getBufferBindingFunc(bufferElementType);
    if (optionalSymbols.count(func)) {
      builder.create<mlir::FuncOp>(
          loc, func,
          FunctionType::get(
              {ArrayRef<Type>{getLLVMPointerType(), getLLVMInt32Type(),
                              getLLVMInt32Type(), getLLVMInt32Type(),
                              getLLVMInt32Type(),
                              getUnrankedMemRefType(bufferElementType)}},
              {}, &ctx),
          ArrayRef<std::pair<mlir::Identifier, mlir::Attribute>>());
    }
  }
}

void ConvertGpuLaunchFuncToVulkanCalls::convertGpuLaunchFunc(
    gpu::LaunchFuncOp launchOp) {

  ModuleOp module = getOperation();
  OpBuilder builder(launchOp);
  Location loc = launchOp.getLoc();

  // Create call to `initVulkan` before the first GpuLauchFunc.
  if (lauchFuncIndex == 0) {
    auto initVulkanCall = builder.create<LLVM::CallOp>(
        loc, ArrayRef<Type>{getLLVMPointerType()},
        builder.getSymbolRefAttr(kInitVulkan), ArrayRef<Value>{});
    vulkanRuntime = initVulkanCall.getResult(0);
  }

  // Serialize `spirv::Module` into binary form.
  std::vector<char> binary;
  if (failed(createBinaryShader(module, binary)))
    return signalPassFailure();

  // Create LLVM global with SPIR-V binary data, so we can pass a pointer with
  // that data to runtime call.
  Value ptrToSPIRVBinary = LLVM::createGlobalString(
      loc, builder, kSPIRVBinary + std::to_string(lauchFuncIndex),
      {binary.data(), binary.size()}, LLVM::Linkage::Internal);

  // Create LLVM constant for the size of SPIR-V binary shader.
  Value binarySize = builder.create<LLVM::ConstantOp>(
      loc, getLLVMInt32Type(), builder.getI32IntegerAttr(binary.size()));

  // Create LLVM global with entry point name.
  Value entryPointName = createEntryPointNameConstant(
      launchOp.getKernelName(), lauchFuncIndex, loc, builder);

  auto gSize = launchOp.getGridSizeOperandValues();
  auto x = gSize.x.getDefiningOp()->getAttrOfType<mlir::IntegerAttr>("value");
  auto y = gSize.y.getDefiningOp()->getAttrOfType<mlir::IntegerAttr>("value");
  auto z = gSize.z.getDefiningOp()->getAttrOfType<mlir::IntegerAttr>("value");
  Value gx = builder.create<LLVM::ConstantOp>(loc, getLLVMInt32Type(), x);
  Value gy = builder.create<LLVM::ConstantOp>(loc, getLLVMInt32Type(), y);
  Value gz = builder.create<LLVM::ConstantOp>(loc, getLLVMInt32Type(), z);

  // Create createVulkanLaunchKernelAction.
  builder.create<LLVM::CallOp>(
      loc, ArrayRef<Type>{},
      builder.getSymbolRefAttr(kCreateVulkanLaunchKernelAction),
      ArrayRef<Value>{vulkanRuntime, ptrToSPIRVBinary, binarySize,
                      entryPointName, gx, gy, gz});

  /// bind gpu.launchOp buffers to Vulkan runtime.
  builder.create<mlir::CallOp>(loc, ArrayRef<Type>{},
                               builder.getSymbolRefAttr(kBindAllBuffers),
                               launchOp.operands());

  // Presume block.x is the subgroup size
  auto blockSize = launchOp.getBlockSizeOperandValues();
  int64_t subgroupSize = 1;
  mlir::IntegerAttr intAttr;
  if (matchPattern(blockSize.x, m_Constant(&intAttr))) {
    subgroupSize = intAttr.getInt();
  }
  if (subgroupSize != 1) {
    IVLOG(2, "Subgroup size = " << subgroupSize);
  }

  Value subgroupSizeVal = builder.create<LLVM::ConstantOp>(
      loc, getLLVMInt32Type(), builder.getI32IntegerAttr(subgroupSize));

  // Create call to `setLaunchKernelAction` runtime function.
  builder.create<LLVM::CallOp>(
      loc, ArrayRef<Type>{},
      builder.getSymbolRefAttr(kSetVulkanLaunchKernelAction),
      ArrayRef<Value>{vulkanRuntime, subgroupSizeVal});

  // Check and transfer VkBuffers when necessary.
  if (failed(transferBuffers(loc, builder, launchOp))) {
    return signalPassFailure();
  }

  // Create call to `AddVulkanLaunchActionToSchedule` runtime function.
  builder.create<LLVM::CallOp>(
      loc, ArrayRef<Type>{},
      builder.getSymbolRefAttr(kAddVulkanLaunchActionToSchedule),
      ArrayRef<Value>{vulkanRuntime});

  // Create call to 'run' and 'deinitVulkan' runtime function
  // after the last GpuLauchFunc.
  if (lauchFuncIndex == numKernel - 1) {
    builder.create<LLVM::CallOp>(loc, ArrayRef<Type>{},
                                 builder.getSymbolRefAttr(kRun),
                                 ArrayRef<Value>{vulkanRuntime});

    deinitVulkan = builder.create<LLVM::CallOp>(
        loc, ArrayRef<Type>{}, builder.getSymbolRefAttr(kDeinitVulkan),
        ArrayRef<Value>{vulkanRuntime});
  }

  // Print buffers
  if (VLOG_IS_ON(4)) {
    auto buffers = launchOp.operands();
    for (auto buffer : buffers) {
      if (failed(printBuffer(loc, builder, buffer))) {
        return signalPassFailure();
      }
    }
  }
  launchOp.erase();
  lauchFuncIndex++;
}

std::unique_ptr<mlir::Pass> createConvertGpuLaunchFuncToVulkanCallsPass() {
  return std::make_unique<ConvertGpuLaunchFuncToVulkanCalls>();
}

} // namespace pmlc::conversion::gpu
