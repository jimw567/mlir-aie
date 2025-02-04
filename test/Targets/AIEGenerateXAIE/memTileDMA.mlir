//===- memTileDMA.mlir ------------------------------------------*- MLIR -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// (c) Copyright 2023 Advanced Micro Devices, Inc.
//
//===----------------------------------------------------------------------===//

// RUN: aie-translate --aie-generate-xaie %s | FileCheck %s

// AIE.end is not the last block.

// CHECK: XAie_DmaDesc [[bd0:.*]];
// CHECK: __mlir_aie_try(XAie_DmaDescInit(&(ctx->DevInst), &([[bd0]]), XAie_TileLoc(2,1)));
// CHECK: __mlir_aie_try(XAie_DmaSetAddrLen(&([[bd0]]),  /* addrA */ 0x82000,  /* len */ 16 * 4));
// CHECK: __mlir_aie_try(XAie_DmaSetNextBd(&([[bd0]]), {{.*}} 0, {{.*}} 1));
// CHECK: __mlir_aie_try(XAie_DmaWriteBd(&(ctx->DevInst), &([[bd0]]), XAie_TileLoc(2,1), {{.*}} 0));

// CHECK: XAie_DmaDesc [[bd24:.*]];
// CHECK: __mlir_aie_try(XAie_DmaDescInit(&(ctx->DevInst), &([[bd24]]), XAie_TileLoc(2,1)));
// CHECK: __mlir_aie_try(XAie_DmaSetAddrLen(&([[bd24]]),  /* addrA */ 0x82000,  /* len */ 16 * 4));
// CHECK: __mlir_aie_try(XAie_DmaSetNextBd(&([[bd24]]), {{.*}} 24, {{.*}} 1));
// CHECK: __mlir_aie_try(XAie_DmaWriteBd(&(ctx->DevInst), &([[bd24]]), XAie_TileLoc(2,1), {{.*}} 24));

// CHECK: XAie_DmaDesc [[bd25:.*]];
// CHECK: __mlir_aie_try(XAie_DmaDescInit(&(ctx->DevInst), &([[bd25]]), XAie_TileLoc(2,1)));
// CHECK: __mlir_aie_try(XAie_DmaSetAddrLen(&([[bd25]]),  /* addrA */ 0x80720,  /* len */ 16 * 4));
// CHECK: __mlir_aie_try(XAie_DmaSetNextBd(&([[bd25]]), {{.*}} 25, {{.*}} 1));
// CHECK: __mlir_aie_try(XAie_DmaWriteBd(&(ctx->DevInst), &([[bd25]]), XAie_TileLoc(2,1), {{.*}} 25));

// CHECK: XAie_DmaDesc [[bd1:.*]];
// CHECK: __mlir_aie_try(XAie_DmaDescInit(&(ctx->DevInst), &([[bd1]]), XAie_TileLoc(2,1)));
// CHECK: __mlir_aie_try(XAie_DmaSetAddrLen(&([[bd1]]),  /* addrA */ 0x80720,  /* len */ 16 * 4));
// CHECK: __mlir_aie_try(XAie_DmaSetNextBd(&([[bd1]]), {{.*}} 1, {{.*}} 1));
// CHECK: __mlir_aie_try(XAie_DmaWriteBd(&(ctx->DevInst), &([[bd1]]), XAie_TileLoc(2,1), {{.*}} 1));


module @aie_module  {
 AIE.device(xcve2302) {
  %t01 = AIE.tile(2, 1)
  %buf01_0 = AIE.buffer(%t01) { address = 8192 : i32, sym_name = "in" } : memref<16xi32>
  %buf01_1 = AIE.buffer(%t01) { address = 1824 : i32, sym_name = "out" } : memref<16xi32>

  %l01_0 = AIE.lock(%t01, 0) { init = 1 : i32 }
  %l01_1 = AIE.lock(%t01, 1)
  %l01_2 = AIE.lock(%t01, 2) { init = 1 : i32 }
  %l01_3 = AIE.lock(%t01, 3)

  %m01 = AIE.memTileDMA(%t01) {
      %srcDma = AIE.dmaStart(S2MM, 0, ^bd0, ^dma0)
    ^dma0:
      %memSrcDma = AIE.dmaStart(MM2S, 1, ^bd1, ^dma1)
    ^dma1:
      %memDstDma = AIE.dmaStart(S2MM, 1, ^bd2, ^dma2)
    ^dma2:
      %dstDma = AIE.dmaStart(MM2S, 0, ^bd3, ^end)
    ^bd0:
      AIE.useLock(%l01_0, "AcquireGreaterEqual", 1)
      AIE.dmaBd(<%buf01_0 : memref<16xi32>, 0, 16>, 0)
      AIE.useLock(%l01_1, "Release", 1)
      AIE.nextBd ^bd0
    ^bd1:
      AIE.useLock(%l01_1, "AcquireGreaterEqual", 1)
      AIE.dmaBd(<%buf01_0 : memref<16xi32>, 0, 16>, 0)
      AIE.useLock(%l01_0, "Release", 1)
      AIE.nextBd ^bd1
    ^bd2:
      AIE.useLock(%l01_2, "AcquireGreaterEqual", 1)
      AIE.dmaBd(<%buf01_1 : memref<16xi32>, 0, 16>, 0)
      AIE.useLock(%l01_3, "Release", 1)
      AIE.nextBd ^bd2
    ^bd3:
      AIE.useLock(%l01_3, "AcquireGreaterEqual", 1)
      AIE.dmaBd(<%buf01_1 : memref<16xi32>, 0, 16>, 0)
      AIE.useLock(%l01_2, "Release", 1)
      AIE.nextBd ^bd3
    ^end:
      AIE.end
  }
 }
}
