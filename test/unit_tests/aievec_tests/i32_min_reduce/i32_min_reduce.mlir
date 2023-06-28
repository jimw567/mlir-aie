// RUN: aie-opt %s --convert-vector-to-aievec="aie-target=aieml" -lower-affine | aie-translate -aieml=true --aievec-to-cpp -o dut.cc
// RUN: xchesscc_wrapper aie2 -f -g +s +w work +o work -I%S -I. %S/testbench.cc dut.cc
// RUN: mkdir -p data
// RUN: xme_ca_udm_dbg -qf -T -P %aietools/data/aie_ml/lib/ -t "%S/../profiling.tcl ./work/a.out" >& xme_ca_udm_dbg.stdout
// RUN: FileCheck --input-file=./xme_ca_udm_dbg.stdout %s
// CHECK: TEST PASSED

module {
  func.func @dut(%arg0: memref<1024xi32>, %arg1: memref<i32>) {
    %c0_i32 = arith.constant 0 : i32
    %cst = arith.constant dense<2147483647> : vector<16xi32>
    %0 = affine.for %arg2 = 0 to 1024 step 16 iter_args(%arg3 = %cst) -> (vector<16xi32>) {
      %2 = vector.transfer_read %arg0[%arg2], %c0_i32 : memref<1024xi32>, vector<16xi32>
      %3 = arith.minsi %arg3, %2 : vector<16xi32>
      affine.yield %3 : vector<16xi32>
    }
    %1 = vector.reduction <minsi>, %0 : vector<16xi32> into i32
    affine.store %1, %arg1[] : memref<i32>
    return
  }
}