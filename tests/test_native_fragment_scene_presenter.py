"""Focused scene-contribution contract tests; original modules stay private."""
import contextlib
import copy
import hashlib
import importlib.util
import io
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import native_fragment_scene_presenter as scene


def f32(x):
    return struct.unpack('<f',struct.pack('<f',x))[0]


def defs(instructions):
    return {scene.result_id(i):i for i in instructions if scene.result_id(i) is not None}


def evaluate(value,definitions,seeded):
    """Own scalar/vector evaluator for coordinates/predicates, no texture code."""
    if value in seeded:return seeded[value]
    item=definitions[value];op,a=item.opcode,item.operands
    get=lambda v:evaluate(v,definitions,seeded)
    if op==43:return struct.unpack('<f',struct.pack('<I',a[2]))[0]
    if op in (44,80):return [get(v) for v in a[2:]]
    if op==81:return get(a[2])[a[3]]
    if op==83:return get(a[2])
    if op==169:return get(a[3]) if get(a[2]) else get(a[4])
    if op==12:
        if a[3]!=43:raise AssertionError('only FClamp extension allowed')
        x,lo,hi=[get(v) for v in a[4:]]
        return [f32(min(hi[i],max(lo[i],x[i]))) for i in range(len(x))]
    left,right=[get(v) for v in a[2:]]
    if op==129:return f32(left+right)
    if op==131:return f32(left-right)
    if op==133:return f32(left*right)
    if op==136:return f32(left/right)
    if op==167:return left and right
    if op==186:return left>right
    if op==188:return left<=right
    if op==190:return left>=right
    raise AssertionError('unexpected coordinate/predicate opcode '+str(op))


class ProfileTests(unittest.TestCase):
    def test_frozen_geometry_and_source_convention(self):
        p=scene.load_profile()
        self.assertEqual(hashlib.sha256(json.dumps(p,sort_keys=True,separators=(',',':')).encode()).hexdigest(),scene.PROFILE_PIN)
        self.assertEqual(p['canvas'],[2112,2752]);self.assertEqual(p['scene'],[32,352,2048,2048])
        self.assertEqual(p['guard']['origin'],[64,2400]);self.assertEqual(p['guard']['stride'],[160,0])
        self.assertEqual(p['coverage']['data_origins'],[[16,48],[1072,48],[16,2448],[1072,2448]])
        self.assertEqual(p['guard']['codes'],list(hashlib.sha256(p['guard']['signature_preimage'].encode()).digest()[:12]))

    def test_no_profile_widening_and_caller_mutation(self):
        p=scene.load_profile();accepted=scene.profile_contract(p);p['scene'][0]=0
        self.assertEqual(accepted['scene'][0],32)
        mutations=[('scene',[0,352,2048,2048]),('canvas',[2112,2753]),('extra',True)]
        for key,value in mutations:
            bad=scene.load_profile();bad[key]=value
            with self.assertRaises(scene.presenter.atlas.probe.ProbeError):scene.profile_contract(bad)
        for key,value in [('origin',[64,2401]),('codes',[0]*12),('code_tolerance',3)]:
            bad=scene.load_profile();bad['guard'][key]=value
            with self.assertRaises(scene.presenter.atlas.probe.ProbeError):scene.profile_contract(bad)
        for value in [float('nan'),float('inf')]:
            bad=scene.load_profile();bad['scene'][0]=value
            with self.assertRaises(scene.presenter.atlas.probe.ProbeError):scene.profile_contract(bad)


class PrivateShaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        name=os.environ.get('FRAME_PRIVATE_ORIGINAL_PS')
        if not name:raise unittest.SkipTest('explicit private original shader absent')
        cls.original=scene.recovery.read_owned(Path(name),scene.ssa.MAX_BYTES)
        cls.binary,cls.record=scene.transform(cls.original,scene.load_profile())
        cls.header,cls.instructions=scene.parse_module(cls.binary);cls.definitions=defs(cls.instructions)

    def validate(self,binary):
        with tempfile.TemporaryDirectory(prefix='scene-val-') as directory:
            path=Path(directory)/'private.spv';path.write_bytes(binary)
            r=subprocess.run(['spirv-val','--target-env','vulkan1.1',str(path)],capture_output=True,text=True,timeout=15)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_original_prefix_descriptors_derivatives_and_fallback_exact(self):
        h,native=scene.ssa.parse_module(self.original);_,store=scene.ssa.profile_structure(h,native)
        start=next(i for i,x in enumerate(native) if x.opcode==54)
        newstart=next(i for i,x in enumerate(self.instructions) if x.opcode==54)
        self.assertEqual(self.instructions[newstart:newstart+store-start],native[start:store])
        for op in (61,65,87):self.assertEqual([x for x in self.instructions if x.opcode==op],[x for x in native if x.opcode==op])
        self.assertEqual(sum(x.opcode==87 for x in self.instructions),2)
        self.assertEqual(sum(x.opcode==88 for x in self.instructions),16)
        self.assertEqual(sum(x==scene.ssa.Instruction(62,(504,994)) for x in self.instructions),3)
        self.assertEqual(sum(x.opcode==62 for x in self.instructions),4)
        self.assertEqual([x for x in self.instructions if x.opcode==71 and x.operands[1] in (33,34)],
                         [x for x in native if x.opcode==71 and x.operands[1] in (33,34)])
        self.validate(self.binary);self.validate(scene.transform(self.original,scene.load_profile(),descriptor_count=3)[0])

    def test_contribution_never_multiplied_by_coverage(self):
        ids=self.record['new_ssa_ids'];d=self.definitions
        self.assertEqual(d[ids['scene_contribution_rgb']],scene.ssa.Instruction(83,(30,ids['scene_contribution_rgb'],ids['combined_color_rgb'])))
        self.assertEqual(d[ids['output_alpha']],scene.ssa.Instruction(133,(8,ids['output_alpha'],ids['clamped_coverage'],ids['original_aperture'])))
        self.assertFalse(self.record['zero_coverage_rgb_is_zero']);self.assertEqual(self.record['source_rgb_convention'],'scene_contribution')
        self.assertNotIn('mapping',self.record);self.assertNotIn('coverage',self.record)
        for coverage in [0.,.25,1.]:
            seeded={ids['combined_color_rgb']:[.8,.2,.3],ids['clamped_coverage']:coverage,ids['original_aperture']:.6}
            self.assertEqual(evaluate(ids['scene_contribution_rgb'],d,seeded),[.8,.2,.3])
            self.assertAlmostEqual(evaluate(ids['output_alpha'],d,seeded),coverage*.6,places=7)

    def test_global_mask_tiles_halos_and_scene_center_clamp(self):
        ids=self.record['new_ssa_ids'];d=self.definitions
        coordinates=[0.,.5/2048,.1,.249,.25,.251,.499,.5,.501,.749,.75,.751,.9,1-.5/2048,1.]
        for x in coordinates:
            for y in coordinates:
                q=[f32(x),f32(y)];clamped=[min(f32(1-.5/2048),max(f32(.5/2048),v)) for v in q]
                got=evaluate(ids['mapped_qa'],d,{883:q})
                px,py=[f32(f32(v*1024)-.5) for v in clamped];tile=min(3,math.floor(max(py,0)/256))
                ox,oy=[[16,48],[1072,48],[16,2448],[1072,2448]][tile]
                expected=[f32(f32(f32(ox+px)+.5)/2112),f32(f32(f32(oy+f32(py-256*tile))+.5)/2752)]
                self.assertEqual(got,expected)
                self.assertEqual(evaluate(ids['mask_tile_base'],d,{883:q}),tile*256)
                self.assertEqual(evaluate(ids['scene_global_q'],d,{883:q}),clamped)
        # Immediately either side of each global tile threshold selects the
        # continued halo image, not an independently clamped 256-row tile.
        for threshold in [256.,512.,768.]:
            for delta in [-.25,0.,.25]:
                y=f32((threshold+.5+delta)/1024)
                self.assertEqual(evaluate(ids['mask_tile_base'],d,{883:[.4,y]}),min(3,math.floor(max(f32(f32(y*1024)-.5),0)/256))*256)

    def test_geometry_guard_first_cell_and_all_inclusive_windows(self):
        ids=self.record['guard_ssa'];d=self.definitions
        spec=importlib.util.spec_from_file_location('owned_guard_structure',ROOT/'tests/test_native_fragment_guarded_presenter.py')
        helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
        helper.assert_guard_structure(self,self.instructions,self.record)
        self.assertTrue(evaluate(ids['geometry_valid'],d,{871:[0.,.25,1.,.5],538:[.5,.375]}))
        for r1,u in [([0.,.25,1.,.5],[-.1,.3]),([0.,.5,1.,.5],[.5,.5]),([0.,float('nan'),1.,.5],[.5,.3])]:
            self.assertFalse(evaluate(ids['geometry_valid'],d,{871:r1,538:u}))
        good={c['g']:f32(c['code']/255) for c in ids['cells']}
        for cell in ids['cells']:
            for delta in [-3,-2,2,3]:
                seeded={**good,cell['g']:f32((cell['code']+delta)/255)}
                self.assertEqual(evaluate(ids['signature_valid'],d,seeded),abs(delta)<=2)
        self.assertEqual(self.record['first_cell_mismatch_guard_samples'],1)
        self.assertTrue(self.record['guard_branch_before_panels'])

    def test_exact_original_off_mutated_pin_and_bounded_types(self):
        for count in [1,3]:
            off,r=scene.transform(self.original,scene.load_profile(),False,count)
            self.assertEqual(off,self.original);self.assertEqual(r['guard_ssa'],{})
            self.assertFalse(r['scene_format_guard_implemented'])
        bad=bytearray(self.original);bad[-1]^=1
        with self.assertRaises(scene.presenter.atlas.probe.ProbeError):scene.transform(bytes(bad),scene.load_profile())
        for kwargs in [{'enabled':1},{'descriptor_count':True},{'descriptor_count':2}]:
            with self.assertRaises(scene.presenter.atlas.probe.ProbeError):scene.transform(self.original,scene.load_profile(),**kwargs)
        self.assertEqual(self.record['output_sha256'],hashlib.sha256(self.binary).hexdigest())
        self.assertFalse(self.record['runtime_or_pipeline_activation']);self.assertFalse(self.record['physical_eye_proven'])

    def test_cli_validation_before_exclusive_publication(self):
        with tempfile.TemporaryDirectory(prefix='scene-cli-') as directory,contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
            p=Path(directory);original=p/'original.spv';output=p/'out.spv';receipt=p/'record.json';original.write_bytes(self.original)
            args=['--original',str(original),'--output',str(output),'--record',str(receipt)]
            with mock.patch.object(scene.subprocess,'run') as val:
                val.return_value.returncode=1;self.assertEqual(scene.main(args),2)
                self.assertFalse(output.exists());self.assertFalse(receipt.exists())
            self.assertEqual(scene.main(args),0);saved=(output.read_bytes(),receipt.read_bytes())
            self.assertEqual(scene.main(args),2);self.assertEqual(saved,(output.read_bytes(),receipt.read_bytes()))
            output.unlink();receipt.unlink();self.assertEqual(scene.main(args+['--disabled']),0);self.assertEqual(output.read_bytes(),self.original)


if __name__=='__main__':unittest.main()
