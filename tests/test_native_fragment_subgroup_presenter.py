"""Focused new subgroup profile structure; originals are explicit/private."""
import copy
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import native_fragment_subgroup_presenter as subgroup


class PublicContract(unittest.TestCase):
    def test_requirements_include_negotiated_feature(self):
        self.assertEqual(subgroup.REQUIREMENTS['subgroup_operations'],['BASIC','BALLOT','SHUFFLE'])
        self.assertEqual(subgroup.REQUIREMENTS['device_feature'],'shaderSubgroupUniformControlFlow')
        self.assertEqual(subgroup.REQUIREMENTS['spirv_version'],'1.3')

    def test_unpinned_source_refused(self):
        with self.assertRaises(ValueError):subgroup.transform(b'not a native shader',subgroup.guard.load_profile())

    def test_no_caller_fingerprint_change(self):
        profile=copy.deepcopy(subgroup.guard.load_profile());profile['guard']['codes'][11]^=1
        with self.assertRaises(ValueError):subgroup.transform(b'',profile)

    def test_framing_does_not_broaden_old_parser(self):
        self.assertNotIn(345,subgroup.guard.EXTRA_OPS)
        self.assertIn(345,subgroup.EXTRA_OPS)
        with self.assertRaises(ValueError):subgroup.parse_module(struct.pack('<5I',0x07230203,0x10100,0,1,0))


@unittest.skipUnless(os.environ.get('FRAME_PRIVATE_ORIGINAL_PS'),'explicit private original not supplied')
class PrivateProfile(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original=Path(os.environ['FRAME_PRIVATE_ORIGINAL_PS']).read_bytes()
        cls.binary,cls.record=subgroup.transform(cls.original,subgroup.guard.load_profile())
        cls.header,cls.instructions=subgroup.parse_module(cls.binary)

    def test_valid_both_descriptor_counts(self):
        with tempfile.TemporaryDirectory(prefix='subgroup-val-') as directory:
            for count in (1,3):
                binary,_=subgroup.transform(self.original,subgroup.guard.load_profile(),descriptor_count=count)
                path=Path(directory)/str(count);path.write_bytes(binary)
                p=subprocess.run(['spirv-val','--target-env','vulkan1.1',str(path)],capture_output=True,text=True,timeout=15)
                self.assertEqual(p.returncode,0,p.stdout+p.stderr)

    def test_off_exact_original(self):
        off,record=subgroup.transform(self.original,subgroup.guard.load_profile(),False)
        self.assertEqual(off,self.original);self.assertFalse(record['subgroup_fast_path'])

    def test_mutated_original_pin_refused(self):
        mutated=bytearray(self.original);mutated[-5]^=1
        with self.assertRaises(ValueError):subgroup.transform(bytes(mutated),subgroup.guard.load_profile())

    def test_whole_original_expression_and_implicit_samples(self):
        old_header,native=subgroup.ssa.parse_module(self.original)
        _,store=subgroup.ssa.profile_structure(old_header,native)
        first=next(i for i,x in enumerate(native) if x.opcode==54)
        generated=next(i for i,x in enumerate(self.instructions) if x.opcode==54)
        self.assertEqual(self.instructions[generated:generated+store-first],native[first:store])
        self.assertEqual([x for x in self.instructions if x.opcode==87],[x for x in native if x.opcode==87])
        self.assertEqual([x for x in self.instructions if x.opcode==62 and x.operands[1]==994],
                         [subgroup.ssa.Instruction(62,(504,994))])

    def test_caps_and_execution_mode(self):
        self.assertEqual(self.header[1],0x10300)
        for cap in (61,64,65):self.assertIn(subgroup.ssa.Instruction(17,(cap,)),self.instructions)
        self.assertIn(subgroup.ssa.Instruction(16,(4,4421)),self.instructions)
        self.assertIn(subgroup.ssa.Instruction(10,subgroup.literal('SPV_KHR_subgroup_uniform_control_flow')),self.instructions)

    def test_nonhelper_named_lane_not_first_or_elect(self):
        opcodes=[x.opcode for x in self.instructions]
        self.assertEqual(opcodes.count(339),1);self.assertEqual(opcodes.count(343),1)
        self.assertEqual(opcodes.count(345),2);self.assertNotIn(333,opcodes);self.assertNotIn(338,opcodes)
        ids=self.record['subgroup_ssa']
        shuffle=[x for x in self.instructions if x.opcode==345]
        self.assertEqual(shuffle[0].operands[3],871)
        self.assertTrue(all(x.operands[-1]==ids['leader_id'] for x in shuffle))

    def test_helpers_and_all_components_guard_shared_use(self):
        ids=self.record['subgroup_ssa']
        equal=[x for x in self.instructions if x.opcode==180]
        self.assertEqual(len(equal),4)
        may=next(x for x in self.instructions if x.opcode==167 and x.operands[1]==ids['may_share'])
        self.assertEqual(may.operands[-1],ids['same_r1'])
        self.assertEqual(len(ids['leader_samples']),12);self.assertEqual(len(ids['fallback_samples']),12)
        self.assertEqual(sum(x.opcode==88 for x in self.instructions),28) # 12+12+4 panels.

    def test_no_descriptor_load_added(self):
        _,native=subgroup.ssa.parse_module(self.original)
        # The only new loads read two built-ins; original descriptor load/access
        # streams and sampled image/sampler operands stay unchanged.
        old_access=[x for x in native if x.opcode==65]
        prefix=[x for x in self.instructions if x.opcode==65 and x.operands[1]<1010]
        self.assertEqual(prefix,old_access)
        self.assertTrue(self.record['historical_source_cache'] is False)


if __name__=='__main__':unittest.main()
