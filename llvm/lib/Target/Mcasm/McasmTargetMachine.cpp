//===-- McasmTargetMachine.cpp - Minimal Mcasm TargetMachine with stubs ---===//
//
// MCASM NOTE: This is a minimal stub implementation with necessary stubs
// to satisfy linker requirements without compiling problematic X86 files.
//
//===----------------------------------------------------------------------===//

#include "McasmTargetMachine.h"
#include "TargetInfo/McasmDebug.h"
#include "MCTargetDesc/McasmMCTargetDesc.h"
#include "MCTargetDesc/McasmFilteredStream.h"
#include "TargetInfo/McasmTargetInfo.h"
#include "Mcasm.h"
#include "McasmSubtarget.h"
#include "McasmISelDAGToDAG.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/RegisterBankInfo.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Errc.h"
#include "McasmTargetObjectFile.h"
#include "McasmMachineFunctionInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include <optional>

using namespace llvm;

namespace {
// Minimal TTI implementation for Mcasm
class McasmTTIImpl : public BasicTTIImplBase<McasmTTIImpl> {
  using BaseT = BasicTTIImplBase<McasmTTIImpl>;
  friend BaseT;

  const McasmSubtarget *ST;
  const McasmTargetLowering *TLI;

  const McasmSubtarget *getST() const { return ST; }
  const McasmTargetLowering *getTLI() const { return TLI; }

public:
  explicit McasmTTIImpl(const McasmTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}
};
} // end anonymous namespace

// MCASM NOTE: Minimal target initialization - only 32-bit target
extern "C" LLVM_C_ABI void LLVMInitializeMcasmTarget() {
  MCASM_DEBUG_LOG("DEBUG: LLVMInitializeMcasmTarget called\n");
  RegisterTargetMachine<McasmTargetMachine> X(getTheMcasm_32Target());
  MCASM_DEBUG_LOG("DEBUG: RegisterTargetMachine completed\n");
}

static void debugLog(const char *msg) {
  MCASM_DEBUG_LOG("DEBUG: %s\n", msg);
}

static std::unique_ptr<TargetLoweringObjectFile> createTLOF(const Triple &TT) {
  debugLog("createTLOF called");
  // Use custom McasmTargetObjectFile for symbol name formatting
  return std::make_unique<McasmTargetObjectFile>();
}

McasmTargetMachine::McasmTargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       std::optional<Reloc::Model> RM,
                                       std::optional<CodeModel::Model> CM,
                                       CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T,
                               "e-p:32:32-i8:8:8-i16:16:16-i32:32:32-i64:32-f32:32-f64:32-a:0:32-n32",
                               TT, CPU, FS, Options,
                               RM.value_or(Reloc::Static),
                               CM.value_or(CodeModel::Small), OL),
      TLOF(createTLOF(getTargetTriple())), IsJIT(JIT) {
  debugLog("McasmTargetMachine constructor body entered");

  // Mcasm uses SelectionDAG exclusively. FastISel is not supported.
  setFastISel(false);
  setO0WantsFastISel(false);

  // mcasm doesn't need .addrsig or .file directives
  this->Options.EmitAddrsig = false;

  debugLog("About to call initAsmInfo()");
  initAsmInfo();
  debugLog("initAsmInfo() completed");
}

McasmTargetMachine::~McasmTargetMachine() = default;

// Get or create subtarget for the given function
const McasmSubtarget *McasmTargetMachine::getSubtargetImpl(const Function &F) const {
  MCASM_DEBUG_LOG("DEBUG: McasmTargetMachine::getSubtargetImpl called for function '%s'\n",
          F.getName().str().c_str());

  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute TuneAttr = F.getFnAttribute("tune-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  StringRef CPU =
      CPUAttr.isValid() ? CPUAttr.getValueAsString() : (StringRef)getTargetCPU();
  StringRef TuneCPU = TuneAttr.isValid() ? TuneAttr.getValueAsString() : (StringRef)CPU;
  StringRef FS =
      FSAttr.isValid() ? FSAttr.getValueAsString() : (StringRef)getTargetFeatureString();

  MCASM_DEBUG_LOG("DEBUG:   CPU = '%s', TuneCPU = '%s', FS = '%s'\n",
          CPU.str().c_str(), TuneCPU.str().c_str(), FS.str().c_str());

  SmallString<512> Key;
  Key.reserve(CPU.size() + TuneCPU.size() + FS.size());
  Key += CPU;
  Key += TuneCPU;
  Key += FS;

  auto &I = SubtargetMap[Key];
  if (!I) {
    MCASM_DEBUG_LOG("DEBUG:   Creating new McasmSubtarget\n");
    I = std::make_unique<McasmSubtarget>(TargetTriple, CPU, TuneCPU, FS, *this,
                                         MaybeAlign(), 0, 0);
    MCASM_DEBUG_LOG("DEBUG:   McasmSubtarget created at %p\n", (void*)I.get());
  } else {
    MCASM_DEBUG_LOG("DEBUG:   Using cached McasmSubtarget at %p\n", (void*)I.get());
  }
  return I.get();
}

void McasmTargetMachine::reset() {
  // Stub
}

TargetTransformInfo McasmTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<McasmTTIImpl>(this, F));
}

MachineFunctionInfo *McasmTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return McasmMachineFunctionInfo::create<McasmMachineFunctionInfo>(Allocator,
                                                                     F, STI);
}

yaml::MachineFunctionInfo *McasmTargetMachine::createDefaultFuncInfoYAML() const {
  return nullptr;
}

yaml::MachineFunctionInfo *McasmTargetMachine::convertFuncInfoToYAML(
    const MachineFunction &) const {
  return nullptr;
}

bool McasmTargetMachine::parseMachineFunctionInfo(
    const yaml::MachineFunctionInfo &, PerFunctionMIParsingState &,
    SMDiagnostic &, SMRange &) const {
  return false;
}

bool McasmTargetMachine::isNoopAddrSpaceCast(unsigned, unsigned) const {
  return false;
}

ScheduleDAGInstrs *McasmTargetMachine::createMachineScheduler(
    MachineSchedContext *) const {
  return nullptr;
}

ScheduleDAGInstrs *McasmTargetMachine::createPostMachineScheduler(
    MachineSchedContext *) const {
  return nullptr;
}

namespace {
class McasmPassConfig : public TargetPassConfig {
public:
  McasmPassConfig(McasmTargetMachine &TM, PassManagerBase &PM)
    : TargetPassConfig(TM, PM) {
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig constructor\n");
    disablePass(&BranchFolderPassID);
    MCASM_DEBUG_LOG("DEBUG: BranchFolderPass disabled\n");
  }

  McasmTargetMachine &getMcasmTargetMachine() const {
    return getTM<McasmTargetMachine>();
  }

  bool addInstSelector() override {
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addInstSelector called\n");
    // Add the instruction selector pass
    addPass(createMcasmISelDag(getMcasmTargetMachine(), getOptLevel()));
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addInstSelector completed\n");
    return false;  // false means we handled it
  }

  void addIRPasses() override {
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addIRPasses called\n");

    // Call base class to add standard IR passes
    TargetPassConfig::addIRPasses();

    // Lower bit-ops to __bit_* libcalls and link runtime IR.
    addPass(createMcasmLowerBitOpsPass());

    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addIRPasses completed\n");
  }

  void addISelPrepare() override {
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addISelPrepare called\n");

    // Keep default ISel preparation first.
    TargetPassConfig::addISelPrepare();

    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addISelPrepare completed\n");
    return;
  }

  bool addIRTranslator() override {
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addIRTranslator called\n");
    return TargetPassConfig::addIRTranslator();
  }

  void addCodeGenPrepare() override {
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addCodeGenPrepare called\n");

    // Call base class to add CodeGenPrepare pass
    TargetPassConfig::addCodeGenPrepare();

    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addCodeGenPrepare completed\n");
  }

  bool addLegalizeMachineIR() override {
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addLegalizeMachineIR called\n");
    return TargetPassConfig::addLegalizeMachineIR();
  }

  bool addRegBankSelect() override {
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addRegBankSelect called\n");
    return TargetPassConfig::addRegBankSelect();
  }

  bool addGlobalInstructionSelect() override {
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addGlobalInstructionSelect called\n");
    return TargetPassConfig::addGlobalInstructionSelect();
  }

  void addMachineSSAOptimization() override {
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addMachineSSAOptimization called\n");
    TargetPassConfig::addMachineSSAOptimization();
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addMachineSSAOptimization completed\n");
  }

  void addPreRegAlloc() override {
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addPreRegAlloc called\n");
    TargetPassConfig::addPreRegAlloc();
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addPreRegAlloc completed\n");
  }

  void addPostRegAlloc() override {
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addPostRegAlloc called\n");
    TargetPassConfig::addPostRegAlloc();
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addPostRegAlloc completed\n");
  }

  void addPreSched2() override {
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addPreSched2 called\n");
    TargetPassConfig::addPreSched2();
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addPreSched2 completed\n");
  }

  void addBlockPlacement() override {
    // Skip generic machine block placement for now.
    // The current mcasm branch lowering can produce CFG patterns that trigger
    // assertions in the generic placer.
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addBlockPlacement skipped\n");
  }

  void addPreEmitPass() override {
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addPreEmitPass called\n");
    TargetPassConfig::addPreEmitPass();
    MCASM_DEBUG_LOG("DEBUG: McasmPassConfig::addPreEmitPass completed\n");
  }
};
}

TargetPassConfig *McasmTargetMachine::createPassConfig(PassManagerBase &PM) {
  MCASM_DEBUG_LOG("DEBUG: McasmTargetMachine::createPassConfig called\n");
  auto *PC = new McasmPassConfig(*this, PM);
  MCASM_DEBUG_LOG("DEBUG: McasmTargetMachine::createPassConfig completed, PC=%p\n", (void*)PC);
  return PC;
}

bool McasmTargetMachine::addPassesToEmitFile(
    PassManagerBase &PM, raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
    CodeGenFileType FileType, bool DisableVerify,
    MachineModuleInfoWrapperPass *MMIWP) {
  MCASM_DEBUG_LOG("DEBUG: McasmTargetMachine::addPassesToEmitFile called\n");
  MCASM_DEBUG_LOG("DEBUG:   FileType=%d, DisableVerify=%d, MMIWP=%p\n",
          (int)FileType, (int)DisableVerify, (void*)MMIWP);

  MCASM_DEBUG_LOG("DEBUG: About to call base class addPassesToEmitFile\n");
  bool Result = CodeGenTargetMachineImpl::addPassesToEmitFile(
      PM, Out, DwoOut, FileType, DisableVerify, MMIWP);
  MCASM_DEBUG_LOG("DEBUG: Base class addPassesToEmitFile returned %d\n", (int)Result);

  return Result;
}

bool McasmTargetMachine::addAsmPrinter(PassManagerBase &PM,
                                       raw_pwrite_stream &Out,
                                       raw_pwrite_stream *DwoOut,
                                       CodeGenFileType FileType,
                                       MCContext &Context) {
  MCASM_DEBUG_LOG("DEBUG: McasmTargetMachine::addAsmPrinter called\n");
  MCASM_DEBUG_LOG("DEBUG:   FileType=%d, DwoOut=%p, Context=%p\n",
          (int)FileType, (void*)DwoOut, (void*)&Context);

  MCASM_DEBUG_LOG("DEBUG: About to call base class addAsmPrinter\n");
  bool Result = CodeGenTargetMachineImpl::addAsmPrinter(PM, Out, DwoOut, FileType, Context);
  MCASM_DEBUG_LOG("DEBUG: Base class addAsmPrinter returned %d\n", (int)Result);

  return Result;
}

Expected<std::unique_ptr<MCStreamer>>
McasmTargetMachine::createMCStreamer(raw_pwrite_stream &Out,
                                     raw_pwrite_stream *DwoOut,
                                     CodeGenFileType FileType,
                                     MCContext &Context) {
  MCASM_DEBUG_LOG("DEBUG: McasmTargetMachine::createMCStreamer called\n");
  MCASM_DEBUG_LOG("DEBUG:   FileType=%d (0=AssemblyFile, 1=ObjectFile)\n", (int)FileType);

  // 鑾峰彇 MC 缁勪欢
  MCASM_DEBUG_LOG("DEBUG: Step 1 - getting MC components\n");
  const MCSubtargetInfo &STI = *getMCSubtargetInfo();
  MCASM_DEBUG_LOG("DEBUG:   STI obtained\n");
  const MCAsmInfo &MAI = *getMCAsmInfo();
  MCASM_DEBUG_LOG("DEBUG:   MAI obtained\n");
  const MCRegisterInfo &MRI = *getMCRegisterInfo();
  MCASM_DEBUG_LOG("DEBUG:   MRI obtained\n");
  const MCInstrInfo &MII = *getMCInstrInfo();
  MCASM_DEBUG_LOG("DEBUG:   MII obtained\n");

  std::unique_ptr<MCStreamer> AsmStreamer;

  switch (FileType) {
  case CodeGenFileType::AssemblyFile: {
    MCASM_DEBUG_LOG("DEBUG: Step 2 - creating MCInstPrinter\n");
    MCASM_DEBUG_LOG("DEBUG:   Triple = %s\n", getTargetTriple().str().c_str());
    MCASM_DEBUG_LOG("DEBUG:   Target name = %s\n", getTarget().getName());
    MCASM_DEBUG_LOG("DEBUG:   Assembler dialect = %u\n", MAI.getAssemblerDialect());

    MCASM_DEBUG_LOG("DEBUG:   About to call getTarget().createMCInstPrinter()\n");
    std::unique_ptr<MCInstPrinter> InstPrinter(getTarget().createMCInstPrinter(
        getTargetTriple(),
        Options.MCOptions.OutputAsmVariant.value_or(MAI.getAssemblerDialect()),
        MAI, MII, MRI));
    MCASM_DEBUG_LOG("DEBUG:   createMCInstPrinter returned, InstPrinter=%p\n", (void*)InstPrinter.get());

    if (!InstPrinter) {
      fprintf(stderr, "ERROR: createMCInstPrinter returned nullptr\n");
      return createStringError("Failed to create MCInstPrinter for mcasm");
    }

    MCASM_DEBUG_LOG("DEBUG: Step 3 - applying InstPrinter options\n");
    for (StringRef Opt : Options.MCOptions.InstPrinterOptions) {
      MCASM_DEBUG_LOG("DEBUG:   Applying option: %s\n", Opt.str().c_str());
      if (!InstPrinter->applyTargetSpecificCLOption(Opt))
        return createStringError("invalid InstPrinter option '" + Opt + "'");
    }
    MCASM_DEBUG_LOG("DEBUG:   All InstPrinter options applied\n");

    // 鍒涘缓 code emitter (濡傛灉闇€瑕佹樉绀虹紪鐮?
    std::unique_ptr<MCCodeEmitter> MCE;
    if (Options.MCOptions.ShowMCEncoding) {
      MCASM_DEBUG_LOG("DEBUG: Step 4 - creating MCCodeEmitter\n");
      MCE.reset(getTarget().createMCCodeEmitter(MII, Context));
      MCASM_DEBUG_LOG("DEBUG:   createMCCodeEmitter returned, MCE=%p\n", (void*)MCE.get());
    } else {
      MCASM_DEBUG_LOG("DEBUG: Step 4 - skipping MCCodeEmitter (ShowMCEncoding=false)\n");
    }

    MCASM_DEBUG_LOG("DEBUG: Step 5 - creating MCAsmBackend\n");
    std::unique_ptr<MCAsmBackend> MAB(
        getTarget().createMCAsmBackend(STI, MRI, Options.MCOptions));
    MCASM_DEBUG_LOG("DEBUG:   createMCAsmBackend returned, MAB=%p\n", (void*)MAB.get());

    if (!MAB) {
      fprintf(stderr, "ERROR: createMCAsmBackend returned nullptr\n");
      return createStringError("Failed to create MCAsmBackend for mcasm");
    }

    MCASM_DEBUG_LOG("DEBUG: Step 6 - creating McasmFilteredStream\n");
    // Create filtered stream to remove ELF directives
    auto FilteredOut = std::make_unique<McasmFilteredStream>(Out);
    MCASM_DEBUG_LOG("DEBUG:   McasmFilteredStream created\n");

    MCASM_DEBUG_LOG("DEBUG: Step 7 - creating formatted_raw_ostream\n");
    auto FOut = std::make_unique<formatted_raw_ostream>(*FilteredOut);
    MCASM_DEBUG_LOG("DEBUG:   formatted_raw_ostream created\n");

    MCASM_DEBUG_LOG("DEBUG: Step 8 - calling getTarget().createAsmStreamer()\n");
    MCStreamer *S = getTarget().createAsmStreamer(
        Context, std::move(FOut), std::move(InstPrinter), std::move(MCE),
        std::move(MAB));
    MCASM_DEBUG_LOG("DEBUG:   createAsmStreamer returned, S=%p\n", (void*)S);

    // Keep FilteredOut alive by storing it in AsmStreamer's context
    // (it will be destroyed when AsmStreamer is destroyed)
    // IMPORTANT: FilteredOut must outlive FOut, so we need to keep it alive
    // For now, we leak it intentionally - TODO: find a better way
    (void)FilteredOut.release();

    if (!S) {
      fprintf(stderr, "ERROR: createAsmStreamer returned nullptr\n");
      return createStringError("Failed to create AsmStreamer for mcasm");
    }

    AsmStreamer.reset(S);
    MCASM_DEBUG_LOG("DEBUG: Step 9 - AsmStreamer reset completed\n");
    break;
  }
  case CodeGenFileType::ObjectFile: {
    fprintf(stderr, "ERROR: ObjectFile generation not supported for mcasm\n");
    return createStringError("mcasm does not support object file generation");
  }
  default:
    fprintf(stderr, "ERROR: Unknown FileType=%d\n", (int)FileType);
    return createStringError("Unknown CodeGenFileType");
  }

  MCASM_DEBUG_LOG("DEBUG: createMCStreamer completed successfully\n");
  return std::move(AsmStreamer);
}

// Stubs for new Pass Manager functions
void McasmTargetMachine::registerPassBuilderCallbacks(PassBuilder &) {
  // Stub
}

Error McasmTargetMachine::buildCodeGenPipeline(
    PassManager<Module, AnalysisManager<Module>> &,
    raw_pwrite_stream &,
    raw_pwrite_stream *,
    CodeGenFileType,
    const CGPassBuilderOption &,
    MCContext &,
    PassInstrumentationCallbacks *) {
  return Error::success();
}

