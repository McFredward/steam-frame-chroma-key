"""Cold receipt/API controls; no real OpenVR library, Frame or service is used."""
import ctypes
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import steamvr_camera_alpha as tool


class FakeCamera:
    def __init__(self,initial=False,default=False):
        self.value=initial;self.default=default;self.calls=[];self.refuse=False
    def read(self):return self.value
    def write(self,value):
        self.calls.append(('write',value))
        if not self.refuse:self.value=value
    def remove_override(self):
        self.calls.append(('remove',))
        if not self.refuse:self.value=self.default


class Controls(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='frame-camera-alpha-')
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.receipt=self.root/'camera-before.json'
        self.settings=self.root/'steamvr.vrsettings'
        self.library=self.root/'libopenvr_api.so'
        self.sha='ab'*32
        self.config({})
    def config(self,camera,**other):
        self.settings.write_text(json.dumps({'camera':camera,**other}))
        self.settings.chmod(0o600)
    def apply(self,camera):return tool.apply(camera,self.receipt,self.library,self.sha,self.settings)

    def test_existing_false_and_true_restore_without_other_setting_writes(self):
        for initial in (False,True):
            with self.subTest(initial=initial):
                self.config({tool.KEY:initial,'other':41},steamvr={'unchanged':'yes'})
                original=self.settings.read_bytes();camera=FakeCamera(initial)
                self.apply(camera)
                self.assertIs(camera.value,True)
                record=tool.receipt(self.receipt)
                self.assertIs(record['previous_override'],initial)
                self.assertEqual(self.settings.read_bytes(),original)
                # SteamVR persistence is not simulated by this tool; only public
                # settings methods are called, never an entire JSON-file write.
                result=tool.restore(camera,self.receipt)
                self.assertTrue(result['restored']);self.assertIs(camera.value,initial)
                self.assertEqual(camera.calls,[] if initial else [('write',True),('write',False)])
                self.assertEqual(self.settings.read_bytes(),original)
                self.receipt.unlink()
    def test_absent_override_restores_absence_with_known_or_absent_default(self):
        for default in (False,None):
            with self.subTest(default=default):
                camera=FakeCamera(default,default)
                self.apply(camera)
                # Model the running service persisting only its one key.
                self.config({tool.KEY:True,'unrelated':17})
                result=tool.restore(camera,self.receipt)
                self.assertFalse(result['restored']);self.assertTrue(result['runtime_restored'])
                self.assertTrue(result['persistence_pending']);self.assertIs(result['explicit_override_observed'],True)
                self.assertEqual(camera.calls,[('write',True),('remove',)])
                self.config({'unrelated':17})
                self.assertTrue(tool.restore(camera,self.receipt)['restored'])
                self.assertIs(camera.value,default)
                self.receipt.unlink();self.config({})
    def test_receipt_precedes_write_and_failed_ack_stays_recoverable(self):
        camera=FakeCamera(False);camera.refuse=True
        camera.write=lambda value:self.assertTrue(self.receipt.exists())
        with self.assertRaisesRegex(tool.files.SafetyError,'not acknowledged'):self.apply(camera)
        self.assertIs(tool.receipt(self.receipt)['previous_override'],None)
        camera=FakeCamera(True,False);self.config({tool.KEY:True})
        result=tool.restore(camera,self.receipt)
        self.assertFalse(result['restored']);self.assertTrue(result['runtime_restored'])
        self.config({})
        self.assertTrue(tool.restore(camera,self.receipt)['restored'])
    def test_idempotent_enable_keeps_original_receipt_inode_and_state(self):
        camera=FakeCamera(False);self.apply(camera)
        inode=self.receipt.stat().st_ino;raw=self.receipt.read_bytes()
        self.apply(camera)
        self.assertEqual(camera.calls,[('write',True)])
        self.assertEqual(self.receipt.stat().st_ino,inode);self.assertEqual(self.receipt.read_bytes(),raw)
    def test_runtime_file_disagreement_and_foreign_receipts_never_write(self):
        self.config({tool.KEY:True});camera=FakeCamera(False)
        with self.assertRaisesRegex(tool.files.SafetyError,'disagree'):self.apply(camera)
        self.assertFalse(self.receipt.exists());self.assertEqual(camera.calls,[])
        self.config({});self.apply(camera)
        with self.assertRaisesRegex(tool.files.SafetyError,'foreign camera receipt'):
            tool.apply(camera,self.receipt,self.library,'cd'*32,self.settings)
        self.assertEqual(camera.calls,[('write',True)])
    def test_changed_wrong_typed_and_duplicate_settings_preserved(self):
        camera=FakeCamera(None,None);self.apply(camera)
        self.config({tool.KEY:False})
        with self.assertRaisesRegex(tool.files.SafetyError,'foreign file override'):tool.restore(camera,self.receipt)
        self.assertEqual(camera.calls,[('write',True)])
        self.config({tool.KEY:'false'})
        with self.assertRaisesRegex(tool.files.SafetyError,'nonboolean'):tool.restore(camera,self.receipt)
        self.settings.write_text('{"camera":{},"camera":{}}')
        with self.assertRaisesRegex(tool.files.SafetyError,'duplicate'):tool.override(self.settings)
    def test_foreign_runtime_change_and_api_failure_preserve_receipt(self):
        camera=FakeCamera(None,None);self.apply(camera);camera.value=False
        with self.assertRaisesRegex(tool.files.SafetyError,'changed runtime'):self.apply(camera)
        with self.assertRaisesRegex(tool.files.SafetyError,'foreign runtime'):tool.restore(camera,self.receipt)
        self.assertTrue(self.receipt.exists());self.assertEqual(camera.calls,[('write',True)])
        camera.value=True;camera.refuse=True
        with self.assertRaisesRegex(tool.files.SafetyError,'restoration not acknowledged'):
            tool.restore(camera,self.receipt)
        self.assertTrue(self.receipt.exists())
    def test_foreign_file_symlinks_and_wrong_receipt_types_refused(self):
        foreign=self.root/'foreign';foreign.write_text('unchanged');foreign.chmod(0o600)
        self.receipt.symlink_to(foreign)
        with self.assertRaises(OSError):self.apply(FakeCamera(False))
        self.assertEqual(foreign.read_text(),'unchanged');self.receipt.unlink()
        self.apply(FakeCamera(False));record=tool.receipt(self.receipt);record['previous_effective']=0
        self.receipt.write_text(json.dumps(record))
        with self.assertRaisesRegex(tool.files.SafetyError,'previous camera'):tool.receipt(self.receipt)
    def test_foreign_removed_override_not_overwritten(self):
        self.config({tool.KEY:False});camera=FakeCamera(False)
        self.apply(camera);self.config({})
        for operation in (lambda:self.apply(camera),lambda:tool.restore(camera,self.receipt)):
            with self.assertRaisesRegex(tool.files.SafetyError,'foreign file override'):operation()
        self.assertEqual(camera.calls,[('write',True)])
    def test_no_write_default_true_cannot_adopt_later_override(self):
        camera=FakeCamera(True,True);self.apply(camera)
        self.config({tool.KEY:True})
        for operation in (lambda:self.apply(camera),lambda:tool.restore(camera,self.receipt)):
            with self.assertRaisesRegex(tool.files.SafetyError,'foreign file override'):operation()
        self.assertEqual(camera.calls,[])
        self.config({});self.assertTrue(tool.restore(camera,self.receipt)['restored'])
    def test_fresh_baseline_drift_never_adopted_before_first_write(self):
        for drift in ('file','runtime'):
            with self.subTest(drift=drift):
                self.config({tool.KEY:False});camera=FakeCamera(False)
                original=tool.files.write_new
                def drift_after_receipt(path,data):
                    original(path,data)
                    if drift=='file':self.config({tool.KEY:True})
                    else:camera.value=True
                with mock.patch.object(tool.files,'write_new',side_effect=drift_after_receipt):
                    with self.assertRaisesRegex(tool.files.SafetyError,'baseline changed before first'):self.apply(camera)
                self.assertEqual(camera.calls,[]);self.assertTrue(self.receipt.exists())
                self.receipt.unlink()
    def test_public_003_ctypes_calls_handle_only_exact_unset_error(self):
        # Exercise actual CFUNCTYPE scalar/char-pointer calls, not the permissive
        # get_bool(None-on-any-error) method in the old PC convenience wrapper.
        fieldtypes=dict(tool._vr._FnTable._fields_);state={'error':0,'value':False};calls=[]
        @fieldtypes['GetBool']
        def get_bool(section,key,error):
            self.assertEqual((section,key),(b'camera',b'forceSceneAlphaBlending'))
            error[0]=state['error'];return state['value']
        @fieldtypes['SetBool']
        def set_bool(section,key,value,error):
            calls.append(('write',bool(value)));state['value']=bool(value);error[0]=0
        @fieldtypes['RemoveKeyInSection']
        def remove(section,key,error):
            calls.append(('remove',));state['value']=False;error[0]=0
        table=tool._vr._FnTable();table.GetBool=get_bool;table.SetBool=set_bool;table.RemoveKeyInSection=remove
        native=tool.NativeCamera(self.library,self.sha);native.fn=table
        self.assertIs(native.read(),False);native.write(True);self.assertIs(native.read(),True)
        native.remove_override();self.assertEqual(calls,[('write',True),('remove',)])
        state['error']=5;self.assertIsNone(native.read())
        for error in (1,2,3,4,6):
            state['error']=error
            with mock.patch.object(native,'_check',side_effect=tool._vr.VRSettingsError('refused')):
                with self.assertRaises(tool._vr.VRSettingsError):native.read()
    def test_non_frame_native_loading_refused_before_dlopen(self):
        with mock.patch.object(tool.os,'uname',return_value=mock.Mock(machine='x86_64')),mock.patch.object(tool.ctypes,'CDLL') as loader:
            with self.assertRaisesRegex(tool.files.SafetyError,'AArch64'):tool.NativeCamera(self.library,self.sha).__enter__()
        loader.assert_not_called()


if __name__=='__main__':unittest.main()
