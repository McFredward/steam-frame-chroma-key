#!/usr/bin/env python3
"""Offline exact-format guard with a source-local subgroup fast path.

Requires Fragment BASIC/BALLOT/SHUFFLE and subgroup-uniform-control-flow, not merely
Vulkan 1.1. No runtime hook, device capability grant, frame cache or activation
is provided. Helper invocations and unequal R1 use the complete old guard.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import native_fragment_guarded_presenter as guard

require, ssa = guard.require, guard.ssa
MARKER = "MRMASK/4 inert-subgroup-synthetic-v2-uniform-flow-only"
EXTRA_OPS = guard.EXTRA_OPS | {41, 42, 168, 180, 339, 343, 345}
REQUIREMENTS = {
    "spirv_version": "1.3", "minimum_vulkan_api": "1.1",
    "subgroup_stage": "FRAGMENT", "subgroup_operations": ["BASIC", "BALLOT", "SHUFFLE"],
    "device_extension": "VK_KHR_shader_subgroup_uniform_control_flow",
    "device_feature": "shaderSubgroupUniformControlFlow",
    "spirv_extension": "SPV_KHR_subgroup_uniform_control_flow",
    "execution_mode": "SubgroupUniformControlFlowKHR",
}


def literal(value):
    raw = value.encode("ascii") + b"\0"
    raw += b"\0" * (-len(raw) % 4)
    return struct.unpack("<%dI" % (len(raw)//4), raw)


def parse_module(binary):
    require(type(binary) is bytes and 20 <= len(binary) <= ssa.MAX_BYTES and len(binary)%4 == 0,
            "bounded subgroup module required")
    words = struct.unpack("<%dI" % (len(binary)//4), binary)
    require(words[0] == 0x07230203 and words[1] == 0x10300 and words[4] == 0,
            "exact SPIR-V1.3 subgroup profile required")
    result, at = [], 5
    while at < len(words):
        size, opcode = words[at] >> 16, words[at] & 65535
        require(0 < size <= len(words)-at and opcode in ssa.ALLOWED | EXTRA_OPS | {10},
                "bounded subgroup instruction framing required")
        result.append(ssa.Instruction(opcode, words[at+1:at+size])); at += size
    return list(words[:5]), result


def transform(original, profile, enabled=True, descriptor_count=1):
    candidate, inherited = guard.transform(original, profile, enabled, descriptor_count)
    record = {**inherited, "schema": "native-fragment-subgroup-presenter-v1",
              "scope": "INERT_OFFLINE_SUBGROUP_SYNTHETIC_FORMAT_ONLY",
              "requirements": REQUIREMENTS, "subgroup_fast_path": enabled,
              "mixed_r1_fallback": "complete twelve-code per-invocation guard",
              "helper_fallback": "complete twelve-code per-invocation guard",
              "historical_source_cache": False, "native_activation": False}
    if not enabled:
        return candidate, record
    header, instructions = guard.parse_module(candidate)
    original_header, native = ssa.parse_module(original)
    _, store = ssa.profile_structure(original_header, native)
    native_function = next(i for i,x in enumerate(native) if x.opcode == 54)
    function = next(i for i,x in enumerate(instructions) if x.opcode == 54)
    prefix = native[native_function:store]
    splice = function + len(prefix)
    require(instructions[function:splice] == prefix, "exact whole original function prefix required")
    # All original branches are selections ending before the splice. No original
    # termination, barrier or loop may silently acquire the new execution mode.
    require(not any(x.opcode in (246,252,254,255) for x in prefix), "nonterminating original selection CFG required")
    require(sum(x.opcode == 247 for x in prefix) == 3, "exact pinned original three selections required")
    old = inherited["guard_ssa"]
    panel_start = next(i for i in range(splice,len(instructions))
                       if instructions[i] == ssa.Instruction(248,(old["panel_header"],))) + 1
    panel_end = next(i for i in range(panel_start,len(instructions))
                     if instructions[i] == ssa.Instruction(249,(old["signature_merge"],)))
    panel = instructions[panel_start:panel_end]
    require(panel[-1] == ssa.Instruction(62,(504,inherited["new_ssa_ids"]["output"])), "one exact panel output")
    bound = header[3]
    def fresh():
        nonlocal bound
        value=bound; bound+=1; return value
    make = lambda op,*args: ssa.Instruction(op,tuple(args))
    declarations=[]
    constants = {x.operands[2]:x.operands[1] for x in instructions[:function]
                 if x.opcode == 43 and x.operands[0] == 8}
    def constant(value):
        bits=struct.unpack("<I",struct.pack("<f",value))[0]
        if bits not in constants:
            constants[bits]=fresh(); declarations.append(make(43,8,constants[bits],bits))
        return constants[bits]
    def emit(target,op,kind,*args):
        value=fresh(); target.append(make(op,kind,value,*args)); return value
    true, false, scope = fresh(),fresh(),fresh()
    declarations += [make(41,96,true),make(42,96,false),make(43,6,scope,3)]
    # HelperInvocation is an Input bool and explicitly participates in the
    # entry-point interface. Helpers never consume subgroup results.
    helper_type, helper_variable, lane_type, lane_variable = [fresh() for _ in range(4)]
    ballot_type=next(x.operands[0] for x in instructions if x.opcode==23 and x.operands[1:]==(6,4))
    declarations += [make(32,helper_type,1,96),make(59,helper_type,helper_variable,1),
                     make(32,lane_type,1,6),make(59,lane_type,lane_variable,1)]
    body=[]
    def conjunction(target,values):
        result=values[0]
        for value in values[1:]: result=emit(target,167,96,result,value)
        return result
    zero,one=constant(0.),constant(1.)
    minimum=[emit(body,81,8,871,i) for i in (0,1)]
    maximum=[emit(body,81,8,871,i) for i in (2,3)]
    span=[emit(body,131,8,hi,lo) for lo,hi in zip(minimum,maximum)]
    coordinates=[emit(body,81,8,538,i) for i in (0,1)]
    bounds=[]; local=[]
    for lo,hi,size,u in zip(minimum,maximum,span,coordinates):
        bounds += [emit(body,190,96,lo,zero),emit(body,188,96,hi,one),emit(body,186,96,size,zero)]
        local += [emit(body,190,96,u,lo),emit(body,188,96,u,hi)]
    bounds_valid=conjunction(body,bounds)
    geometry_valid=conjunction(body,[bounds_valid,*local])

    def signature(target):
        """Emit the identical ordered12 windows, including first-cell fast OFF."""
        windows=[]; samples=[]
        width,height=inherited["profile"]["canvas"]
        g=inherited["profile"]["guard"]
        def cells(indices):
            sampled=emit(target,86,297,599,600)
            for index in indices:
                code=g["codes"][index]
                centers=[g["origin"][a]+g["stride"][a]*index+g["size"][a]/2 for a in (0,1)]
                norm=[centers[0]/width,centers[1]/height]
                xy=[emit(target,129,8,lo,emit(target,133,8,size,constant(v)))
                    for lo,size,v in zip(minimum,span,norm)]
                xy_id=emit(target,80,39,*xy)
                sample=emit(target,88,9,sampled,xy_id,2,zero);samples.append(sample)
                raw=emit(target,81,8,sample,1)
                windows.extend((emit(target,190,96,raw,constant((code-g["code_tolerance"])/255.)),
                                emit(target,188,96,raw,constant((code+g["code_tolerance"])/255.))))
        cells(range(1)); first=conjunction(target,windows)
        remaining,bad,merge=[fresh() for _ in range(3)]
        target += [make(247,merge,0),make(250,first,remaining,bad),make(248,remaining)]
        cells(range(1,12)); all_valid=conjunction(target,windows)
        target += [make(249,merge),make(248,bad),make(249,merge),make(248,merge)]
        value=emit(target,245,96,all_valid,remaining,false,bad)
        return value,merge,samples

    # Stay in common control flow through the election/leader merge. Excluding
    # helpers with an outer divergent branch would weaken the reconvergence
    # premise. Ballot chooses a real nonhelper, and Shuffle names that exact
    # stable lane even if a driver treats helpers as inactive at group ops.
    helper=emit(body,61,96,helper_variable)
    nonhelper=emit(body,168,96,helper)
    lane=emit(body,61,6,lane_variable)
    real_lanes=emit(body,339,ballot_type,scope,nonhelper)
    leader_id=emit(body,343,6,scope,real_lanes)
    leader_r1=emit(body,345,9,scope,871,leader_id)
    leader=emit(body,167,96,nonhelper,emit(body,170,96,lane,leader_id))
    leader_block,other_block,leader_merge=[fresh() for _ in range(3)]
    body += [make(247,leader_merge,0),make(250,leader,leader_block,other_block),make(248,leader_block)]
    leader_bounds,leader_bad,leader_bounds_merge=[fresh() for _ in range(3)]
    body += [make(247,leader_bounds_merge,0),make(250,bounds_valid,leader_bounds,leader_bad),make(248,leader_bounds)]
    leader_valid,leader_sig_end,leader_samples=signature(body)
    body += [make(249,leader_bounds_merge),make(248,leader_bad),make(249,leader_bounds_merge),make(248,leader_bounds_merge)]
    selected=emit(body,245,96,leader_valid,leader_sig_end,false,leader_bad)
    body += [make(249,leader_merge),make(248,other_block),make(249,leader_merge),make(248,leader_merge)]
    elected_result=emit(body,245,96,selected,leader_bounds_merge,false,other_block)
    shared=emit(body,345,96,scope,elected_result,leader_id)
    equal=[]
    for i in range(4):
        own=emit(body,81,8,871,i); first=emit(body,81,8,leader_r1,i)
        equal.append(emit(body,180,96,own,first)) # Ordered equal: NaN never shares.
    same_r1=conjunction(body,equal)
    may_share=emit(body,167,96,nonhelper,same_r1)
    shared_valid=shared
    shared_block,fallback_block,validity_merge=[fresh() for _ in range(3)]
    body += [make(247,validity_merge,0),make(250,may_share,shared_block,fallback_block),make(248,shared_block)]
    own_valid=emit(body,167,96,geometry_valid,shared_valid)
    body += [make(249,validity_merge),make(248,fallback_block)]
    fallback_sample,fallback_bad,fallback_merge=[fresh() for _ in range(3)]
    body += [make(247,fallback_merge,0),make(250,geometry_valid,fallback_sample,fallback_bad),make(248,fallback_sample)]
    fallback_valid,fallback_sig_end,fallback_samples=signature(body)
    body += [make(249,fallback_merge),make(248,fallback_bad),make(249,fallback_merge),make(248,fallback_merge)]
    fallback_result=emit(body,245,96,fallback_valid,fallback_sig_end,false,fallback_bad)
    body += [make(249,validity_merge),make(248,validity_merge)]
    valid=emit(body,245,96,own_valid,shared_block,fallback_result,fallback_merge)
    panel_block,original_block,finish=[fresh() for _ in range(3)]
    body += [make(247,finish,0),make(250,valid,panel_block,original_block),make(248,panel_block)]
    body += panel + [make(249,finish),make(248,original_block),make(62,504,994),make(249,finish),
                     make(248,finish),make(253),make(56)]
    output=instructions[:splice]+body
    output[function:function]=declarations
    entry=next(i for i,x in enumerate(output) if x.opcode == 15)
    output[entry]=make(15,*output[entry].operands,helper_variable,lane_variable)
    decoration=next(i for i,x in enumerate(output) if x.opcode in (71,72))
    output[decoration:decoration]=[make(71,helper_variable,11,23),make(71,lane_variable,11,41),make(71,lane_variable,14)]
    extension=next(i for i,x in enumerate(output) if x.opcode != 17)
    output[extension:extension]=[make(17,61),make(17,64),make(17,65),make(10,*literal(REQUIREMENTS["spirv_extension"]))]
    mode=next(i for i,x in enumerate(output) if x.opcode == 16)
    output.insert(mode,make(16,4,4421))
    marker=next(i for i,x in enumerate(output) if x.opcode == 330)
    output[marker]=make(330,*literal(MARKER))
    header[1],header[3]=0x10300,bound
    binary=guard.pack_module(header,output)
    record.pop("guard_ssa",None) # The replaced old guard's SSA no longer exists.
    record.update(output_bytes=len(binary),output_sha256=hashlib.sha256(binary).hexdigest(),candidate_marker=MARKER,
                  original_function_prefix_unchanged=True,subgroup_ssa={"helper":helper,"leader":leader,
                  "leader_r1":leader_r1,"leader_id":leader_id,"real_lanes":real_lanes,
                  "shared_guard":shared,"same_r1":same_r1,"may_share":may_share,
                  "valid":valid,"leader_samples":leader_samples,"fallback_samples":fallback_samples},
                  subgroup_sample_budget="12 per elected nonhelper +12 per unequal-R1/helper, fast OFF1",
                  static_explicit_lod0_guard_samples=24,
                  original_implicit_samples_unchanged=True)
    return binary,record


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    for name in ("original","output","record"):p.add_argument("--"+name,type=Path,required=True)
    p.add_argument("--disabled",action="store_true")
    args=p.parse_args(argv)
    require(len({p.absolute() for p in (args.original,args.output,args.record)})==3,"distinct paths required")
    require(not any(p.exists() or p.is_symlink() for p in (args.output,args.record)),"exclusive outputs required")
    binary,record=transform(guard.recovery.read_owned(args.original,ssa.MAX_BYTES),guard.load_profile(),not args.disabled)
    with tempfile.TemporaryDirectory(prefix="native-subgroup-val-") as directory:
        tmp=Path(directory)/"probe.spv";tmp.write_bytes(binary)
        v=subprocess.run(["spirv-val","--target-env","vulkan1.1",str(tmp)],capture_output=True,text=True,timeout=15)
        require(v.returncode==0,"SPIR-V validation refused: "+v.stdout+v.stderr)
    guard.recovery.write_owned(args.output,binary)
    guard.recovery.write_owned(args.record,(json.dumps(record,indent=2)+"\n").encode())


if __name__ == "__main__":
    main()
