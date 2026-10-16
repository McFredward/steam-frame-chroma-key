"""Single-output controls; optional private shader never enters tracked files."""
import contextlib
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import native_fragment_packed_output as output


def mapping(convention="straight"):
    return {'schema': output.SCHEMA, 'scope': output.SCOPE, 'source_encoding': output.ENCODING,
        'rgb_convention': convention, 'B': [.9, -.22, .14, .18, .83, -.03],
        'color': [.4, 0., .03, 0., .6, .2], 'coverage': [.4, 0., .56, 0., .6, .2]}


class OwnedControls(unittest.TestCase):
    def test_only_explicit_owned_linear_source(self):
        for convention in ('straight', 'premultiplied'):
            packed = output.mapping_contract(mapping(convention))
            self.assertEqual(packed['schema'], output.atlas.PACKED_MAPPING_SCHEMA)
        for key, value in (('source_encoding', 'NATIVE_VIDEO_ALPHA'), ('rgb_convention', 'inferred'),
                           ('scope', 'LIVE_NATIVE_AUTHORITY'), ('B', [0.]*6), ('color', [True]*6),
                           ('coverage', [1., 0., float('nan'), 0., 1., 0.])):
            changed = mapping(); changed[key] = value
            with self.subTest(key=key), self.assertRaises(output.atlas.probe.ProbeError): output.mapping_contract(changed)
        for changed in (None, {}, dict(mapping(), extra=True)):
            with self.assertRaises(output.atlas.probe.ProbeError): output.mapping_contract(changed)
        with self.assertRaisesRegex(output.atlas.probe.ProbeError, 'explicit boolean'): output.transform(b'', mapping(), 1)


class PrivateControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        name = os.environ.get('FRAME_PRIVATE_ORIGINAL_PS')
        if not name: raise unittest.SkipTest('explicit private native shader absent')
        cls.original = output.recovery.read_owned(Path(name), output.ssa.MAX_BYTES)
        if output.atlas.probe.digest(cls.original) != output.ssa.ORIGINAL_SHA256: raise AssertionError('wrong original input')

    def test_single_output_and_preserved_native_reads(self):
        module, record = output.transform(self.original, mapping())
        original_header, native = output.ssa.parse_module(self.original)
        _, instructions = output.ssa.parse_module(module)
        outputs = [i.operands[1] for i in instructions if i.opcode == 59 and i.operands[2] == 3]
        self.assertEqual(outputs, [504])
        self.assertEqual([i for i in instructions if i.opcode == 62], [output.ssa.Instruction(62, (504, record['new_ssa_ids']['output']))])
        for opcode in (61, 65):
            self.assertEqual([i for i in instructions if i.opcode == opcode], [i for i in native if i.opcode == opcode])
        self.assertEqual(sum(i.opcode == 87 for i in instructions), 6)
        self.assertEqual(sum(i.opcode == 15 for i in instructions), 1)
        entry = next(i for i in instructions if i.opcode == 15)
        self.assertEqual(entry, next(i for i in native if i.opcode == 15))
        ids = record['new_ssa_ids']
        self.assertIn(output.ssa.Instruction(142, (30, ids['premultiplied_rgb'], ids['combined_color_rgb'], ids['clamped_coverage'])), instructions)
        self.assertIn(output.ssa.Instruction(133, (8, ids['output_alpha'], ids['clamped_coverage'], 899)), instructions)
        self.assertTrue(all(i.operands[1] >= original_header[3] for i in instructions
                            if i.opcode in (142, 133) and i.operands[1] in (ids['premultiplied_rgb'], ids['output_alpha'])))
        self.assertEqual(record['color_attachment_count'], 1)
        for key in ('runtime_or_pipeline_activation', 'native_spatial_mapping_proven', 'native_frame_association_proven',
                    'native_draw_coherence_proven', 'physical_eye_proven', 'native_sampler_equivalence_proven',
                    'linear_game_alpha_proven', 'alpha_composition_authorized', 'passthrough_authorized'):
            self.assertIs(record[key], False)

    def test_premultiplied_does_not_multiply_coverage_or_erase_alpha0_rgb(self):
        module, record = output.transform(self.original, mapping('premultiplied'))
        _, instructions = output.ssa.parse_module(module)
        ids = record['new_ssa_ids']
        rgb = next(i for i in instructions if i.opcode == 142
                   and i.operands[1] == ids['premultiplied_rgb'])
        self.assertEqual(rgb.operands[:3], (30, ids['premultiplied_rgb'], ids['combined_color_rgb']))
        self.assertIn(output.ssa.Instruction(43, (8, rgb.operands[3], 0x3f800000)), instructions)
        self.assertNotIn(output.ssa.Instruction(142, (30, ids['premultiplied_rgb'],
            ids['combined_color_rgb'], ids['clamped_coverage'])), instructions)
        self.assertIn(output.ssa.Instruction(133, (8, ids['output_alpha'], ids['clamped_coverage'], 899)), instructions)
        self.assertEqual(output.transform(self.original, mapping('premultiplied'), False)[0], self.original)
        specialized, specialized_record = output.transform(self.original, mapping('premultiplied'), descriptor_count=1)
        self.assertTrue(specialized_record['owned_count1_interface_specialization'])
        self.assertFalse(specialized_record['native_descriptor_layout_modified'])
        self.assertNotEqual(module, specialized)
        for key in ('runtime_or_pipeline_activation', 'native_spatial_mapping_proven',
                    'native_frame_association_proven', 'physical_eye_proven', 'passthrough_authorized'):
            self.assertIs(record[key], False)

    def test_disabled_exact_original_and_corrupt_profile_refused(self):
        module, record = output.transform(self.original, mapping(), False)
        self.assertEqual(module, self.original)
        self.assertEqual(record['output_sha256'], output.ssa.ORIGINAL_SHA256)
        self.assertFalse(record['enabled'])
        mutated = bytearray(self.original); mutated[8] ^= 1
        for source in (bytes(mutated), output.transform(self.original, mapping())[0], b''):
            with self.assertRaises(output.atlas.probe.ProbeError): output.transform(source, mapping())

    def test_explicit_count1_specialization_changes_only_owned_array_interface(self):
        regular, _ = output.transform(self.original, mapping())
        specialized, record = output.transform(self.original, mapping(), descriptor_count=1)
        _, old = output.ssa.parse_module(regular); _, new = output.ssa.parse_module(specialized)
        definitions = {output.atlas.result_id(i): i for i in new if output.atlas.result_id(i) is not None}
        variable = definitions[289]; pointer = definitions[variable.operands[0]]; array = definitions[pointer.operands[2]]
        self.assertEqual(variable.opcode, 59); self.assertEqual(pointer.opcode, 32); self.assertEqual(array.opcode, 28)
        self.assertEqual(array.operands[1], 286)
        self.assertEqual(definitions[array.operands[2]].operands[2], 1)
        fresh = {pointer.operands[0], array.operands[0], array.operands[2]}
        restored = [output.ssa.Instruction(59, (288, 289, 0)) if i == variable else i
                    for i in new if output.atlas.result_id(i) not in fresh]
        self.assertEqual(restored, old)
        self.assertEqual([i for i in new if i.opcode == 65], [i for i in old if i.opcode == 65])
        self.assertIs(record['owned_count1_interface_specialization'], True)
        self.assertIs(record['native_descriptor_layout_modified'], False)
        self.assertEqual(output.transform(self.original, mapping(), False, descriptor_count=1)[0], self.original)
        for count in (True, 0, 2, 4):
            with self.assertRaises(output.atlas.probe.ProbeError): output.transform(self.original, mapping(), descriptor_count=count)
        words = list(struct.unpack('<%dI' % (len(regular)//4), regular)); at = 5
        while at < len(words):
            count, opcode = words[at] >> 16, words[at] & 65535
            if opcode == 65 and words[at+3] == 289:
                words[at+4] = 102; break
            at += count
        corrupted = struct.pack('<%dI' % len(words), *words)
        with self.assertRaisesRegex(output.atlas.probe.ProbeError, 'descriptor access'):
            output.specialize_owned_binding9_count1(corrupted, self.original)

    def test_cli_validates_before_exclusive_publication(self):
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            root = Path(directory); source, definition, module, record = [root/name for name in ('private.spv', 'mapping.json', 'output.spv', 'record.json')]
            source.write_bytes(self.original); definition.write_text(json.dumps(mapping()))
            args = ['--original', str(source), '--mapping', str(definition), '--output', str(module), '--record', str(record)]
            with mock.patch.object(output.subprocess, 'run') as validator:
                validator.return_value.returncode = 1
                self.assertEqual(output.main(args), 2); self.assertFalse(module.exists()); self.assertFalse(record.exists())
            self.assertEqual(output.main(args), 0)
            saved = module.read_bytes(), record.read_bytes()
            self.assertTrue(json.loads(saved[1])['spirv_val_passed'])
            self.assertEqual(output.main(args), 2); self.assertEqual((module.read_bytes(), record.read_bytes()), saved)
            module.unlink(); record.unlink()
            self.assertEqual(output.main(args+['--disabled']), 0); self.assertEqual(module.read_bytes(), self.original)
            module.unlink(); record.unlink()
            record.write_text('existing evidence')
            self.assertEqual(output.main(args), 2); self.assertFalse(module.exists()); self.assertEqual(record.read_text(), 'existing evidence')
            record.unlink(); source.unlink(); source.symlink_to(definition)
            self.assertEqual(output.main(args), 2); self.assertFalse(module.exists())


if __name__ == '__main__': unittest.main()
