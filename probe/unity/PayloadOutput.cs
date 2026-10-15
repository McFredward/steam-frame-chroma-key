using System;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.XR;

namespace FrameAppPayloadProbe
{
    // Attached only after explicit F8 start. Unknown callbacks always retain the source blit.
    [DisallowMultipleComponent]
    [RequireComponent(typeof(Camera))]
    public sealed class PayloadOutput : MonoBehaviour
    {
        public struct Observation
        {
            public ProbeEye Eye;
            public Camera.MonoOrStereoscopicEye ActualEye;
            public XRSettings.StereoRenderingMode StereoMode;
            public bool XrEnabled, XrDeviceActive, CameraStereoEnabled, BuiltInPipeline, BothEyes, SrgbWrite;
            public int SourceWidth, SourceHeight, DestinationWidth, DestinationHeight;
            public RenderTextureFormat SourceFormat, DestinationFormat;
            public TextureDimension SourceDimension, DestinationDimension;
            public VRTextureUsage SourceVrUsage, DestinationVrUsage;
            public bool SourceSrgb, DestinationSrgb, HasDestination, CurrentCameraMatches, SubmittedPayload;
            public ColorSpace ColorSpace;
        }

        private Camera _camera;
        private ProbePolicy _policy;
        private ProbeTexture _left, _right;
        private readonly Observation[] _observations = new Observation[3];
        private readonly bool[] _pending = new bool[3];
        private readonly bool[] _seen = new bool[3];
        private string _fault;
        private bool _initialized;
        public uint LeftCallbacks { get; private set; }
        public uint RightCallbacks { get; private set; }
        public uint FallbackCallbacks { get; private set; }

        public void Initialize(Camera camera, ProbePolicy policy, ProbeTexture left, ProbeTexture right)
        {
            if (_initialized || camera == null || camera.gameObject != gameObject
                || camera.name != ProbePolicy.HeadCameraName || policy == null || left == null || right == null)
                throw new InvalidOperationException("Payload output initialization is invalid.");
            _camera = camera;
            _policy = policy;
            _left = left;
            _right = right;
            _initialized = true;
        }

        public bool TakeObservation(int slot, out Observation observation)
        {
            observation = _observations[slot];
            if (!_pending[slot])
                return false;
            _pending[slot] = false;
            return true;
        }

        public string TakeFault()
        {
            string value = _fault;
            _fault = null;
            return value;
        }

        private void OnRenderImage(RenderTexture source, RenderTexture destination)
        {
            bool submitted = false;
            ProbeEye selected = ProbeEye.Unknown;
            try
            {
                if (_initialized && _camera != null)
                {
                    Camera.MonoOrStereoscopicEye actualEye = _camera.stereoActiveEye;
                    ProbeEye eye = actualEye == Camera.MonoOrStereoscopicEye.Left ? ProbeEye.Left
                        : actualEye == Camera.MonoOrStereoscopicEye.Right ? ProbeEye.Right : ProbeEye.Unknown;
                    selected = _policy.Select(Time.realtimeSinceStartupAsDouble, _camera.gameObject == gameObject
                        && source != null && source.dimension == TextureDimension.Tex2D
                        && (destination == null || destination.dimension == TextureDimension.Tex2D),
                        Camera.current == _camera, GraphicsSettings.currentRenderPipeline == null,
                        XRSettings.enabled && XRSettings.isDeviceActive && _camera.stereoEnabled,
                        XRSettings.stereoRenderingMode == XRSettings.StereoRenderingMode.MultiPass,
                        _camera.stereoTargetEye == StereoTargetEyeMask.Both, eye);
                    if (selected != ProbeEye.Unknown)
                    {
                        // Existing public Unity blit, no custom shader, camera state or native hook.
                        BlitPreservingState(selected == ProbeEye.Left ? _left.Texture : _right.Texture, destination);
                        submitted = true;
                    }
                }
            }
            catch (Exception error)
            {
                RecordFault(error);
            }
            if (!submitted)
            {
                try { BlitPreservingState(source, destination); }
                catch (Exception error) { RecordFault(error); }
            }
            if (submitted && selected == ProbeEye.Left)
            {
                if (LeftCallbacks != uint.MaxValue) LeftCallbacks++;
            }
            else if (submitted && selected == ProbeEye.Right)
            {
                if (RightCallbacks != uint.MaxValue) RightCallbacks++;
            }
            else if (FallbackCallbacks != uint.MaxValue) FallbackCallbacks++;
            int slot = submitted ? (selected == ProbeEye.Left ? 1 : 2) : 0;
            if (!_seen[slot])
            {
                _seen[slot] = true;
                _pending[slot] = true;
                _observations[slot] = new Observation
                {
                    Eye = selected, SubmittedPayload = submitted,
                    ActualEye = _camera == null ? Camera.MonoOrStereoscopicEye.Mono : _camera.stereoActiveEye,
                    StereoMode = XRSettings.stereoRenderingMode,
                    XrEnabled = XRSettings.enabled, XrDeviceActive = XRSettings.isDeviceActive,
                    CameraStereoEnabled = _camera != null && _camera.stereoEnabled,
                    BuiltInPipeline = GraphicsSettings.currentRenderPipeline == null,
                    BothEyes = _camera != null && _camera.stereoTargetEye == StereoTargetEyeMask.Both,
                    SrgbWrite = GL.sRGBWrite,
                    SourceWidth = source == null ? 0 : source.width,
                    SourceHeight = source == null ? 0 : source.height,
                    SourceFormat = source == null ? 0 : source.format,
                    SourceDimension = source == null ? 0 : source.dimension,
                    SourceVrUsage = source == null ? 0 : source.vrUsage,
                    SourceSrgb = source != null && source.sRGB,
                    HasDestination = destination != null,
                    DestinationWidth = destination == null ? 0 : destination.width,
                    DestinationHeight = destination == null ? 0 : destination.height,
                    DestinationFormat = destination == null ? 0 : destination.format,
                    DestinationDimension = destination == null ? 0 : destination.dimension,
                    DestinationVrUsage = destination == null ? 0 : destination.vrUsage,
                    DestinationSrgb = destination != null && destination.sRGB,
                    CurrentCameraMatches = Camera.current == _camera,
                    ColorSpace = QualitySettings.activeColorSpace
                };
            }
        }

        private void RecordFault(Exception error)
        {
            if (_fault == null)
            {
                string message = error.GetType().Name + ": " + error.Message;
                _fault = message.Length > 512 ? message.Substring(0, 512) : message;
            }
            if (_policy != null)
                _policy.Stop();
        }

        public static void BlitPreservingState(Texture source, RenderTexture destination)
        {
            RenderTexture previous = RenderTexture.active;
            bool previousSrgbWrite = GL.sRGBWrite;
            try { Graphics.Blit(source, destination); }
            finally
            {
                GL.sRGBWrite = previousSrgbWrite;
                RenderTexture.active = previous;
            }
        }

        private void OnDestroy()
        {
            if (_policy != null)
                _policy.Stop();
        }
    }
}
