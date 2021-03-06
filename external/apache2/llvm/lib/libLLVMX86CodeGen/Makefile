#	$NetBSD: Makefile,v 1.2 2021/05/30 01:56:55 joerg Exp $

LIB=	LLVMX86CodeGen

.include <bsd.init.mk>

.PATH: ${LLVM_SRCDIR}/lib/Target/X86

SRCS+=	X86AsmPrinter.cpp \
	X86AvoidStoreForwardingBlocks.cpp \
	X86AvoidTrailingCall.cpp \
	X86CallFrameOptimization.cpp \
	X86CallingConv.cpp \
	X86CallLowering.cpp \
	X86CmovConversion.cpp \
	X86DiscriminateMemOps.cpp \
	X86DomainReassignment.cpp \
	X86EvexToVex.cpp\
	X86ExpandPseudo.cpp \
	X86FastISel.cpp \
	X86FastTileConfig.cpp \
	X86FixupBWInsts.cpp \
	X86FixupLEAs.cpp \
	X86FixupSetCC.cpp \
	X86FlagsCopyLowering.cpp \
	X86FloatingPoint.cpp \
	X86FrameLowering.cpp \
	X86IndirectBranchTracking.cpp \
	X86IndirectThunks.cpp \
	X86InsertPrefetch.cpp \
	X86InsertWait.cpp \
	X86InstCombineIntrinsic.cpp \
	X86InstrFMA3Info.cpp \
	X86InstrFoldTables.cpp \
	X86InstrInfo.cpp \
	X86InstructionSelector.cpp \
	X86InterleavedAccess.cpp \
	X86ISelDAGToDAG.cpp \
	X86ISelLowering.cpp \
	X86LegalizerInfo.cpp \
	X86LoadValueInjectionLoadHardening.cpp \
	X86LoadValueInjectionRetHardening.cpp \
	X86LowerAMXIntrinsics.cpp \
	X86LowerAMXType.cpp \
	X86LowerTileCopy.cpp \
	X86MachineFunctionInfo.cpp \
	X86MacroFusion.cpp \
	X86MCInstLower.cpp \
	X86OptimizeLEAs.cpp \
	X86PadShortFunction.cpp \
	X86PartialReduction.cpp \
	X86PreAMXConfig.cpp \
	X86PreTileConfig.cpp \
	X86RegisterBankInfo.cpp \
	X86RegisterInfo.cpp \
	X86SelectionDAGInfo.cpp \
	X86ShuffleDecodeConstantPool.cpp \
	X86SpeculativeExecutionSideEffectSuppression.cpp \
	X86SpeculativeLoadHardening.cpp \
	X86Subtarget.cpp \
	X86TargetMachine.cpp \
	X86TargetObjectFile.cpp \
	X86TargetTransformInfo.cpp \
	X86TileConfig.cpp \
	X86VZeroUpper.cpp \
	X86WinAllocaExpander.cpp \
	X86WinEHState.cpp

TABLEGEN_SRC=		X86.td
TABLEGEN_INCLUDES=	-I${LLVM_SRCDIR}/lib/Target/X86
TABLEGEN_OUTPUT= \
	X86GenAsmMatcher.inc|-gen-asm-matcher \
	X86GenAsmWriter.inc|-gen-asm-writer \
	X86GenAsmWriter1.inc|-gen-asm-writer^-asmwriternum=1 \
	X86GenCallingConv.inc|-gen-callingconv \
	X86GenDAGISel.inc|-gen-dag-isel \
	X86GenDisassemblerTables.inc|-gen-disassembler \
	X86GenEVEX2VEXTables.inc|-gen-x86-EVEX2VEX-tables \
	X86GenFastISel.inc|-gen-fast-isel \
	X86GenGlobalISel.inc|-gen-global-isel \
	X86GenInstrInfo.inc|-gen-instr-info \
	X86GenRegisterBank.inc|-gen-register-bank \
	X86GenRegisterInfo.inc|-gen-register-info \
	X86GenSubtargetInfo.inc|-gen-subtarget

.include "${.PARSEDIR}/../../tablegen.mk"

.if defined(HOSTLIB)
.include <bsd.hostlib.mk>
.else
.include <bsd.lib.mk>
.endif
