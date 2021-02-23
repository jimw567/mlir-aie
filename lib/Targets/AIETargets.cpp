// (c) Copyright 2019 Xilinx Inc. All Rights Reserved.

#include "AIETargets.h"

#include "mlir/Transforms/Passes.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/BlockAndValueMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/Location.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/Translation.h"
#include "mlir/Target/LLVMIR.h"

#include "llvm/IR/Module.h"
#include "llvm/Support/TargetSelect.h"

#include "AIEDialect.h"
#include "AIENetlistAnalysis.h"

using namespace mlir;
using namespace xilinx;
using namespace xilinx::AIE;

static llvm::cl::opt<int> tileCol("tilecol",
                                  llvm::cl::desc("column coordinate of core to translate"),
                                  llvm::cl::init(0));
static llvm::cl::opt<int> tileRow("tilerow",
                                  llvm::cl::desc("row coordinate of core to translate"),
                                  llvm::cl::init(0));

namespace xilinx {
namespace AIE {

// Output the buffer map for the given buffer operations, with the given offset.
// The offset is different depending on where the buffers are accessed from.
void writeBufferMap(raw_ostream &output, BufferOp buf,
                    int offset, NetlistAnalysis &NL) {
  std::string bufName(buf.name().getValue());
  int bufferBaseAddr = NL.getBufferBaseAddress(buf);
  int numBytes = buf.getAllocationSize();
  output << "_symbol " << bufName << " " <<
    "0x" << llvm::utohexstr(offset + bufferBaseAddr) << " " <<
    numBytes << '\n';
}
// Output the memorymap in BCF format for the given buffer operations, with the given offset.
// The offset is different depending on where the buffers are accessed from.
void writeBCFMap(raw_ostream &output, BufferOp buf,
                    int offset, NetlistAnalysis &NL) {
  std::string bufName(buf.name().getValue());
  int bufferBaseAddr = NL.getBufferBaseAddress(buf);
  int numBytes = buf.getAllocationSize();
  output << "_symbol " << bufName << " " <<
    "0x" << llvm::utohexstr(offset + bufferBaseAddr) << " " <<
    "0x" << llvm::utohexstr(numBytes) << '\n';
  output << "_extern " << bufName << "\n";
}
// Output the memorymap in gnu linker format for the given buffer operations, with the given offset.
// The offset is different depending on where the buffers are accessed from.
void writeLDScriptMap(raw_ostream &output, BufferOp buf,
                    int offset, NetlistAnalysis &NL) {
  std::string bufName(buf.name().getValue());
  int bufferBaseAddr = NL.getBufferBaseAddress(buf);
  int numBytes = buf.getAllocationSize();
  output << ". = 0x" << llvm::utohexstr(offset + bufferBaseAddr) << ";\n";
  output << bufName << " = .;\n";
  output << ". += 0x" << llvm::utohexstr(numBytes) << ";\n";
}

void registerAIETranslations() {
  TranslateFromMLIRRegistration
    registrationLLVM("aie-generate-llvmir",
    [](ModuleOp module, raw_ostream &output) {
      llvm::LLVMContext llvmContext;
      auto llvmModule = mlir::translateModuleToLLVMIR(module, llvmContext);
      if (!llvmModule) {
        llvm::errs() << "Failed to emit LLVM IR\n";
        return failure();
      }

      output << *llvmModule;
      return success();
    }, 
    [](DialectRegistry &registry) {
      registry.insert<xilinx::AIE::AIEDialect>();
      registry.insert<StandardOpsDialect>();
      registry.insert<LLVM::LLVMDialect>();
    });

  TranslateFromMLIRRegistration
    registrationMMap("aie-generate-mmap", [](ModuleOp module, raw_ostream &output) {
      DenseMap<std::pair<int, int>, Operation *> tiles;
      DenseMap<Operation *, CoreOp> cores;
      DenseMap<Operation *, MemOp> mems;
      DenseMap<std::pair<Operation *, int>, LockOp> locks;
      DenseMap<Operation *, SmallVector<BufferOp, 4>> buffers;
      DenseMap<Operation *, SwitchboxOp> switchboxes;

      NetlistAnalysis NL(module, tiles, cores, mems, locks, buffers, switchboxes);
      NL.collectTiles(tiles);
      NL.collectBuffers(buffers);

      for (auto tile : tiles) {
        Operation *srcTileOp = tile.second;
        std::pair<int, int> srcCoord = NL.getCoord(srcTileOp);
        int srcCol = srcCoord.first;
        int srcRow = srcCoord.second;

        output << "// Tile(" << srcCol << ", " << srcRow << ")\n";
        output << "// Memory map: name base_address num_bytes\n";

        auto doBuffer = [&](Optional<TileID> tile, int offset) {
          if(tiles.count(tile.getValue()))
            for (auto buf : buffers[tiles[tile.getValue()]])
              writeBufferMap(output, buf, offset, NL);
        };
        if(auto tile = getMemSouth(srcCoord)) doBuffer(tile, 0x00020000);
        if(auto tile = getMemWest(srcCoord))  doBuffer(tile, 0x00028000);
        if(auto tile = getMemNorth(srcCoord)) doBuffer(tile, 0x00030000);
        if(auto tile = getMemEast(srcCoord))  doBuffer(tile, 0x00038000);
      }
      return success();
    }, 
    [](DialectRegistry &registry) {
      registry.insert<xilinx::AIE::AIEDialect>();
      registry.insert<StandardOpsDialect>();
      registry.insert<LLVM::LLVMDialect>();
    });

///// ld.script format:
//
// MEMORY
// {
//    program (RX) : ORIGIN = 0, LENGTH = 0x0020000
//    data (!RX) : ORIGIN = 0x20000, LENGTH = 0x0020000
// }
// ENTRY(_main_init)
// SECTIONS
// {
//   . = 0x0;
//   .text : { 
//      // the _main_init symbol from me_basic.o has to come at address zero.
//      *me_basic.o(.text)
//      . = 0x200;
//      *(.text)
//   } > program
//   .data : { *(.data) } > data
//   . = 0x20000;
//   _sp_start_value_DM_stack = .;
//   . = 0x24000;
//   a = .;
//   . += 1024;
//   .bss : { *(.bss) } > data
// }

  TranslateFromMLIRRegistration
    registrationLDScript("aie-generate-ldscript", [](ModuleOp module, raw_ostream &output) {
      DenseMap<std::pair<int, int>, Operation *> tiles;
      DenseMap<Operation *, CoreOp> cores;
      DenseMap<Operation *, MemOp> mems;
      DenseMap<std::pair<Operation *, int>, LockOp> locks;
      DenseMap<Operation *, SmallVector<BufferOp, 4>> buffers;
      DenseMap<Operation *, SwitchboxOp> switchboxes;

      NetlistAnalysis NL(module, tiles, cores, mems, locks, buffers, switchboxes);
      NL.collectTiles(tiles);
      NL.collectBuffers(buffers);

      for (auto tile : module.getOps<TileOp>())
        if(tile.colIndex() == tileCol && tile.rowIndex() == tileRow) {
        //output << "// Tile(" << tileCol << ", " << tileRow << ")\n";
        //output << "// Memory map: name base_address num_bytes\n";
        output << R"THESCRIPT(
MEMORY
{
   program (RX) : ORIGIN = 0, LENGTH = 0x0020000
   data (!RX) : ORIGIN = 0x20000, LENGTH = 0x0020000
}
ENTRY(_main_init)
SECTIONS
{
  . = 0x0;
  .text : { 
     // the _main_init symbol from me_basic.o has to come at address zero.
     *me_basic.o(.text)
     . = 0x200;
     *(.text)
  } > program
  .data : { *(.data) } > data
  . = 0x20000;
  _sp_start_value_DM_stack = .;
  . = 0x24000;
)THESCRIPT";
        auto doBuffer = [&](Optional<TileID> tile, int offset) {
          if(tiles.count(tile.getValue()))
            for (auto buf : buffers[tiles[tile.getValue()]])
              writeLDScriptMap(output, buf, offset, NL);
        };
        auto srcCoord = std::make_pair(tile.colIndex(), tile.rowIndex());
        if(auto tile = getMemSouth(srcCoord)) doBuffer(tile, 0x00020000);
        if(auto tile = getMemWest(srcCoord))  doBuffer(tile, 0x00028000);
        if(auto tile = getMemNorth(srcCoord)) doBuffer(tile, 0x00030000);
        if(auto tile = getMemEast(srcCoord))  doBuffer(tile, 0x00038000);
        output << "}\n";
      }
      return success();
    },
    [](DialectRegistry &registry) {
      registry.insert<xilinx::AIE::AIEDialect>();
      registry.insert<StandardOpsDialect>();
      registry.insert<LLVM::LLVMDialect>();
    });

//   _entry_point _main_init
// _symbol      _main _after _main_init
// _symbol      _main_init 0
// _reserved DMb      0x00000 0x20000
// _symbol   a        0x38000 0x2000
// _extern   a
// _stack    DM_stack 0x20000  0x400 //stack for core
// _reserved DMb 0x40000 0xc0000 // And everything else the core can't see

  TranslateFromMLIRRegistration
    registrationBCF("aie-generate-bcf", [](ModuleOp module, raw_ostream &output) {
      DenseMap<std::pair<int, int>, Operation *> tiles;
      DenseMap<Operation *, CoreOp> cores;
      DenseMap<Operation *, MemOp> mems;
      DenseMap<std::pair<Operation *, int>, LockOp> locks;
      DenseMap<Operation *, SmallVector<BufferOp, 4>> buffers;
      DenseMap<Operation *, SwitchboxOp> switchboxes;

      NetlistAnalysis NL(module, tiles, cores, mems, locks, buffers, switchboxes);
      NL.collectTiles(tiles);
      NL.collectBuffers(buffers);
// _entry_point _main_init
// _symbol      _main _after _main_init
// _symbol      _main_init 0
// _reserved DMb      0x00000 0x20000
// _symbol   a        0x38000 0x2000
// _extern   a
// _stack    DM_stack 0x20000  0x400 //stack for core
// _reserved DMb 0x40000 0xc0000 // And everything else the core can't see
      for (auto tile : module.getOps<TileOp>())
        if(tile.colIndex() == tileCol && tile.rowIndex() == tileRow) {
          output << "_entry_point _main_init\n";
          output << "_symbol      _main _after _main_init\n";
          output << "_symbol      _main_init 0\n";
          output << "_reserved DMb      0x00000 0x20000 //Don't put data in code memory\n";

          auto doBuffer = [&](Optional<TileID> tile, int offset) {
            if(tiles.count(tile.getValue()))
              for (auto buf : buffers[tiles[tile.getValue()]])
                writeBCFMap(output, buf, offset, NL);
          };
          auto srcCoord = std::make_pair(tile.colIndex(), tile.rowIndex());
          if(auto tile = getMemSouth(srcCoord)) doBuffer(tile, 0x00020000);
          if(auto tile = getMemWest(srcCoord))  doBuffer(tile, 0x00028000);
          if(auto tile = getMemNorth(srcCoord)) doBuffer(tile, 0x00030000);
          if(auto tile = getMemEast(srcCoord))  doBuffer(tile, 0x00038000);
          output << "_stack    DM_stack 0x20000  0x400 //stack for core\n";
          output << "_reserved DMb 0x40000 0xc0000 // And everything else the core can't see\n";
        }
      return success();
    },
    [](DialectRegistry &registry) {
      registry.insert<xilinx::AIE::AIEDialect>();
      registry.insert<StandardOpsDialect>();
      registry.insert<LLVM::LLVMDialect>();
    });


  TranslateFromMLIRRegistration
    registrationCoreList("aie-generate-corelist", [](ModuleOp module, raw_ostream &output) {
        output << "[";
        for (auto tileOp : module.getOps<TileOp>()) {
          int col = tileOp.colIndex();
          int row = tileOp.rowIndex();
          if (auto coreOp = tileOp.getCoreOp()) {
            output << '(' << std::to_string(col) << ',' << std::to_string(row) << "),";
          }
        }
        output << "]\n";
        return success();
    },
    [](DialectRegistry &registry) {
      registry.insert<xilinx::AIE::AIEDialect>();
      registry.insert<StandardOpsDialect>();
      registry.insert<LLVM::LLVMDialect>();
    });
    TranslateFromMLIRRegistration
    registrationXAIE("aie-generate-xaie", [](ModuleOp module, raw_ostream &output) {
        return AIETranslateToXAIEV1(module, output);
    },
    [](DialectRegistry &registry) {
      registry.insert<xilinx::AIE::AIEDialect>();
      registry.insert<StandardOpsDialect>();
      registry.insert<LLVM::LLVMDialect>();
    });                
  }
}
}
