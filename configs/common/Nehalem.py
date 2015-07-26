# Authors: Mohammad Alian
from m5.objects import *

class Port0_FU(FUDesc):
    opList = [ OpDesc(opClass="IntAlu", opLat=1),
               OpDesc(opClass="IntDiv", opLat=20, pipelined=True),
               OpDesc(opClass="FloatMult", opLat=5),
               OpDesc(opClass="FloatCvt", opLat=3),
               OpDesc(opClass="FloatDiv", opLat=10),
               OpDesc(opClass="FloatSqrt", opLat=10),
               OpDesc(opClass="SimdFloatMult", opLat=5),
               OpDesc(opClass="SimdFloatMultAcc", opLat=6),
               OpDesc(opClass="SimdFloatCvt", opLat=3),
               OpDesc(opClass="SimdFloatDiv", opLat=10),
               OpDesc(opClass="SimdFloatSqrt", opLat=10),
               OpDesc(opClass="SimdAddAcc", opLat=1),
               OpDesc(opClass="SimdAdd", opLat=1),
               OpDesc(opClass="SimdAlu", opLat=1),
               OpDesc(opClass="SimdShiftAcc", opLat=1),
               OpDesc(opClass="SimdShift", opLat=1) ]
    count = 1

class Port1_FU(FUDesc):
    opList = [ OpDesc(opClass="IntAlu", opLat=1),
               OpDesc(opClass="IntMult", opLat=3),
               OpDesc(opClass="IprAccess", opLat=3),
               OpDesc(opClass="FloatAdd", opLat=3),
               OpDesc(opClass="SimdFloatAlu", opLat=3),
               OpDesc(opClass="SimdFloatAdd", opLat=3),
               OpDesc(opClass="SimdMult", opLat=3),
               OpDesc(opClass="SimdMultAcc", opLat=4),
               OpDesc(opClass="SimdSqrt", opLat=4),
               OpDesc(opClass="SimdCvt", opLat=3) ]
    count = 1

class Port5_FU(FUDesc):
    opList = [ OpDesc(opClass="IntAlu", opLat=1),
               OpDesc(opClass="FloatCmp", opLat=1),
               OpDesc(opClass="SimdFloatCmp", opLat=3),
               OpDesc(opClass="SimdFloatMisc", opLat=3),
               OpDesc(opClass="SimdCmp", opLat=1),
               OpDesc(opClass="SimdMisc", opLat=3),
               OpDesc(opClass="SimdAdd", opLat=1),
               OpDesc(opClass="SimdAddAcc", opLat=1),
               OpDesc(opClass="SimdShiftAcc", opLat=1),
               OpDesc(opClass="SimdShift", opLat=1),
               OpDesc(opClass="SimdAlu", opLat=1) ]
    count = 1


# Load/Store Units
class Nehalem_Load(FUDesc):
    opList = [ OpDesc(opClass='MemRead',opLat=2) ]
    count = 4

class Nehalem_Store(FUDesc):
    opList = [OpDesc(opClass='MemWrite',opLat=2) ]
    count = 1

# Functional Units for this CPU
class Nehalem_FUP(FUPool):
    FUList = [Port0_FU(), Port1_FU(),
              Nehalem_Load(), Nehalem_Store(), Port5_FU()]

# Bi-Mode Branch Predictor
class Nehalem_BP(BiModeBP):
    globalPredictorSize = 8192
    globalCtrBits = 2
    choicePredictorSize = 8192
    choiceCtrBits = 2
    BTBEntries = 2048
    BTBTagSize = 18
    RASSize = 16
    instShiftAmt = 2

class Nehalem_3(DerivO3CPU):
    LQEntries = 72
    SQEntries = 42
    LSQDepCheckShift = 0
    LFSTSize = 1024
    SSITSize = 1024
    decodeToFetchDelay = 1
    renameToFetchDelay = 1
    iewToFetchDelay = 1
    commitToFetchDelay = 1
    renameToDecodeDelay = 1
    iewToDecodeDelay = 1
    commitToDecodeDelay = 1
    iewToRenameDelay = 1
    commitToRenameDelay = 1
    commitToIEWDelay = 1
    fetchWidth = 4
    fetchToDecodeDelay = 2
    decodeWidth = 4
    decodeToRenameDelay = 2
    renameWidth = 4
    renameToIEWDelay = 2
    issueToExecuteDelay = 1
    dispatchWidth = 4
    issueWidth = 5
    wbWidth = 5
    fuPool = Nehalem_FUP()
    iewToCommitDelay = 1
    renameToROBDelay = 1
    commitWidth = 4
    squashWidth = 16
    trapLatency = 13
    backComSize = 10
    forwardComSize = 5
    numPhysIntRegs = 256
    numPhysFloatRegs = 256
    numIQEntries = 36
    numROBEntries = 128
    switched_out = False
    branchPred = Nehalem_BP()

# Instruction Cache
class Nehalem_ICache(BaseCache):
    hit_latency = 1
    response_latency = 1
    mshrs = 4
    tgts_per_mshr = 16
    size = '32kB'
    assoc = 2
    is_top_level = True

# Data Cache
class Nehalem_DCache(BaseCache):
    hit_latency = 3
    response_latency = 2
    mshrs = 16
    tgts_per_mshr = 16
    size = '32kB'
    assoc = 4
    write_buffers = 16
    is_top_level = True

# TLB Cache
# Use a cache as a L2 TLB
class NehalemWalkCache(BaseCache):
    hit_latency = 4
    response_latency = 4
    mshrs = 6
    tgts_per_mshr = 8
    size = '512B'
    assoc = 4
    write_buffers = 16
    is_top_level = True

# L2 Cache
class NehalemL2(BaseCache):
    hit_latency = 6
    response_latency = 2
    mshrs = 16
    tgts_per_mshr = 16
    size = '256kB'
    assoc = 8
    write_buffers = 8

# L3 Cache
class NehalemL3(BaseCache):
    hit_latency = 14
    response_latency = 10
    mshrs = 16
    tgts_per_mshr = 16
    size = '4MB'
    assoc = 16
    write_buffers = 8

