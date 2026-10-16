"""Owned synthetic pixels, not the authentic C# generator or native evidence."""
import copy
import math
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch
import zlib

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import registration_canvas_locator as locator

probe, registration = locator.probe, locator.registration
SESSION = "0123456789abcdef0123456789abcdef"


def markers(canvas, width, height, cell, bits, protected=()):
    for y in range(0, height, cell):
        for x in range(0, width, cell):
            if x + bits * 2 > width or y + 2 > height: continue
            if any(x < a+w and x+bits*2 > a and y < b+h and y+2 > b for a,b,w,h in protected): continue
            # This deliberately differs from the production generator's art.
            value = ((x//cell)*29 + (y//cell)*47 + 7) % (1 << bits)
            for bit in range(bits):
                for side in (0, 1):
                    on = ((value >> (bits-1-bit)) & 1) ^ side
                    for dy in range(2):
                        at = ((y+dy)*width + x+bit*2+side)*3
                        canvas[at:at+3] = bytes((216 if on else 40,))*3


def fixture(pair=7, eye="left"):
    color = bytearray(bytes((96,))*3*(320*192))
    markers(color,320,192,16,8)
    color = bytes(color)
    coverage = bytes((0,64,128,191,255,192)[min(5,x*6//320)] for y in range(192) for x in range(320))
    rendered, manifest = probe.render(320,192,color,coverage,SESSION,pair,eye)
    canvas = bytearray(bytes((128,))*3*(688*548))
    protected = [[16,16+i*76,512,68] for i in range(3)] + [[16,244,320,192],[352,244,320,192]]
    markers(canvas,688,548,24,10,protected)
    for x,y,w,h in protected:
        for row in range(y,y+h):
            at=(row*688+x)*3
            canvas[at:at+w*3] = rendered.rgb[at:at+w*3]
    canvas = bytes(canvas)
    manifest = {key:manifest[key] for key in registration.MANIFEST_FIELDS}
    manifest["canvas_rgb_sha256"] = probe.digest(canvas)
    record = {"schema":registration.SCHEMA,"profile":registration.PROFILE,"profile_revision":1,
        "session":SESSION,"pair_id":pair,"eye":eye,"phase":(pair-1)%128,"engine_frame_cohort":7,
        "content_id_meaning":"app_owned_pair_id_not_native_video_frame","color_numeric_encoding":"uninterpreted_rgb8_codes",
        "coverage_numeric_encoding":"linear_coverage_r8","manifest":manifest,"native_frame_identity":None,
        "native_spatial_mapping":None,"physical_eye_proven":False,"source_frame_association_proven":False,
        "gpu_completion_proven":False,"opaque_fallback_required":True,"passthrough_authorized":False}
    return record,color,coverage,canvas


def observed(canvas, scale=(1.25,1.5), offset=(13,37), extent=None):
    """Independent inverse nearest rasterization, not locator.remap()."""
    sx,sy=scale;tx,ty=offset
    width,height=extent or (math.ceil(688*sx+tx+5),math.ceil(548*sy+ty+5))
    gray=bytearray(bytes((128,))*(width*height))
    xs=[math.floor((x+.5-tx)/sx) for x in range(width)]
    for y in range(height):
        yy=math.floor((y+.5-ty)/sy)
        if not 0<=yy<548:continue
        for x,xx in enumerate(xs):
            if 0<=xx<688:gray[y*width+x]=canvas[(yy*688+xx)*3+1]
    rgb=bytearray(width*height*3)
    for channel in range(3):rgb[channel::3]=gray
    return probe.Image(width,height,bytes(rgb))


def replace_header(canvas, packet):
    result=bytearray(canvas)
    for index in range(3):
        for bit in range(probe.HEADER_BYTES*8):
            on=(packet[bit//8]>>(7-bit%8))&1
            x,y=16+(bit%64)*8,16+index*76+(bit//64)*4
            for side in (0,1):probe.paint(result,688,x+4*side,y,4,4,(255*(on^side),)*3)
    return bytes(result)


class CanvasLocatorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source=fixture()
        cls.image=observed(cls.source[3])
        cls.located=locator.locate(cls.image,SESSION,"left")
        cls.verified=locator.verify(cls.located,cls.image,*cls.source)

    def rebound(self,image):
        located=copy.deepcopy(self.located)
        located["mapped_input_rgb_sha256"]=probe.digest(image.rgb)
        return located

    def test_scaled_translated_current_pixels_and_disjoint_verification(self):
        self.assertEqual(self.located["candidate_tuple"],{"session":SESSION,"pair_id":7,"eye":"left","phase":6})
        for actual,wanted in zip(self.located["parameters"],(1.25,1.5,13,37)):
            self.assertLess(abs(actual-wanted),.6)
        self.assertEqual([item["incorrect_bits"] for item in self.verified["held_out_markers"]],[0,0])
        self.assertTrue(all(item["maximum_code_error"]==0 for item in self.verified["coverage_interiors"]["levels"].values()))
        self.assertEqual(len(self.verified["Pc_packed"]),6)
        self.assertEqual(len(self.verified["Pa_packed"]),6)
        for key,value in registration.authorities().items():self.assertIs(self.verified[key],value)
        self.assertFalse(self.verified["generator_authenticity_proven"])
        self.assertFalse(self.verified["outer_band_proven"])
        self.assertIn("floor",self.verified["coordinates"])
        self.assertIn("sampled feasible grid",self.verified["uncertainty_scope"])

    def test_another_scale_translation_and_right_eye(self):
        source=fixture(2**64-1,"right")
        image=observed(source[3],(1.5,1.25),(19,53))
        located=locator.locate(image,SESSION,"right")
        self.assertEqual(located["candidate_tuple"]["pair_id"],2**64-1)
        report=locator.verify(located,image,*source)
        self.assertEqual(report["candidate_tuple"]["eye"],"right")

    def test_wrong_session_eye_and_cropped_complete_canvas_refuse(self):
        for session,eye in (("fedcba9876543210fedcba9876543210","left"),(SESSION,"right")):
            with self.subTest(session=session,eye=eye),self.assertRaises(probe.ProbeError):
                locator.locate(self.image,session,eye)
        # Complete headers survive, but lower source pixels are outside this image.
        image=probe.Image(self.image.width,600,self.image.rgb[:self.image.width*600*3])
        with self.assertRaises(probe.ProbeError):locator.locate(image,SESSION,"left")

    def test_duplicate_complete_copies_are_not_selected_by_size_or_first(self):
        image=observed(self.source[3],(1,1),(8,9),extent=(704,1115))
        data=bytearray(image.rgb)
        second=observed(self.source[3],(1,1),(8,560),extent=(704,1115))
        data[560*704*3:]=second.rgb[560*704*3:]
        with self.assertRaisesRegex(probe.ProbeError,"unique"):
            locator.locate(probe.Image(704,1115,bytes(data)),SESSION,"left")

    def test_budget_and_invalid_pixels_fail_before_unbounded_search(self):
        with patch.object(locator,"MAX_HITS",1),self.assertRaisesRegex(probe.ProbeError,"budget"):
            locator.locate(self.image,SESSION,"left")
        for image in (probe.Image(8193,1,b""),probe.Image(688,548,b""),probe.Image(1,1,b"\x00\x01\x00")):
            with self.subTest(extent=(image.width,image.height)),self.assertRaises(probe.ProbeError):locator.locate(image,SESSION,"left")

    def test_crc_magic_decoy_is_not_a_candidate(self):
        packet=bytearray(probe.header_for(320,192,self.source[1],self.source[2],SESSION,7,"left","straight"));packet[-1]^=1
        image=observed(replace_header(self.source[3],packet))
        with self.assertRaisesRegex(probe.ProbeError,"unique"):locator.locate(image,SESSION,"left")

    def test_different_image_extent_and_stale_plus128_pair_refuse(self):
        other=observed(fixture(135)[3])
        with self.assertRaisesRegex(probe.ProbeError,"different image"):
            locator.verify(self.located,other,*self.source)
        # A malicious caller rehashing the new image still cannot recycle header/tuple.
        with self.assertRaisesRegex(probe.ProbeError,"stale"):
            locator.verify(self.rebound(other),other,*self.source)
        changed=copy.deepcopy(self.located);changed["receiver_extent"][0]+=1
        with self.assertRaisesRegex(probe.ProbeError,"different image"):locator.verify(changed,self.image,*self.source)

    def test_declared_plane_digest_with_valid_all3_crc_refuses(self):
        packet=bytearray(probe.header_for(320,192,self.source[1],self.source[2],SESSION,7,"left","straight"))
        packet[-68]^=1;packet[-4:]=struct.pack(">I",zlib.crc32(packet[:-4]))
        changed=observed(replace_header(self.source[3],packet))
        located=self.rebound(changed);located["header_sha256"]=probe.digest(packet)
        with self.assertRaisesRegex(probe.ProbeError,"canonical source planes"):
            locator.verify(located,changed,*self.source)

    def test_body_marker_and_coverage_damage_cannot_pass_headers_alone(self):
        damaged=bytearray(self.source[3])
        # Independent color region first marker (not header/coverage).
        for y in (244,245):probe.paint(damaged,688,16,y,16,1,(128,)*3)
        image=observed(bytes(damaged))
        with self.assertRaisesRegex(probe.ProbeError,"marker"):
            locator.verify(self.rebound(image),image,*self.source)
        damaged=bytearray(self.source[3]);probe.paint(damaged,688,352,244,320,192,(0,)*3)
        image=observed(bytes(damaged))
        with self.assertRaisesRegex(probe.ProbeError,"coverage interior"):
            locator.verify(self.rebound(image),image,*self.source)

    def test_unsupported_record_and_authority_upgrades_refuse(self):
        for key,value in (("schema","future"),("scope","native"),("passthrough_authorized",True),
                          ("outer_band_proven",True),("native_frame_association_proven",True),("opaque_fallback_required",False)):
            changed=copy.deepcopy(self.located);changed[key]=value
            with self.subTest(key=key),self.assertRaises(probe.ProbeError):locator.verify(changed,self.image,*self.source)


if __name__=="__main__":unittest.main()
