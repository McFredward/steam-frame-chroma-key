// Copy this file into an isolated Unity 2021.3.5 project's Assets/Editor only.
using System;
using System.IO;
using FrameAppPayloadProbe;
using UnityEditor;
using UnityEngine;

[InitializeOnLoad]
public static class UnityRenderControls
{
    [Serializable]
    private sealed class ColorPath
    {
        public string requested_rt_encoding;
        public bool actual_rt_srgb;
        public bool gl_srgb_write;
        public bool sample_texture_linear = false;
        public double mean_absolute_rgb_code_error;
        public int maximum_rgb_code_error;
        public string rgb_sha256;
    }

    [Serializable]
    private sealed class Result
    {
        public string schema = "frame-app-payload-unity-render-controls-v1";
        public bool passed;
        public int controls;
        public string unity_version;
        public string graphics_device;
        public string color_space;
        public string left_blit_rgb_sha256;
        public string right_blit_rgb_sha256;
        public ColorPath[] color_paths;
        public bool mono_callback_observed;
        public bool mono_callback_current_camera_matches;
        public bool xr_stereo_callbacks_proved = false;
        public bool native_carriage_proved = false;
        public string error;
    }

    private static int _controls;
    private static void Require(bool condition, string label)
    {
        _controls++;
        if (!condition) throw new Exception(label);
    }

    private static string Argument(string name)
    {
        string[] args = Environment.GetCommandLineArgs();
        for (int index = 0; index + 1 < args.Length; index++)
            if (args[index] == name) return Path.GetFullPath(args[index + 1]);
        throw new ArgumentException("Missing " + name);
    }

    static UnityRenderControls()
    {
        EditorApplication.playModeStateChanged += state =>
        {
            if (state == PlayModeStateChange.EnteredPlayMode
                && Array.IndexOf(Environment.GetCommandLineArgs(), "-probeSourceDirectory") >= 0)
                RunFixture();
        };
    }

    public static void Run()
    {
        if (!EditorApplication.isPlaying)
        {
            EditorApplication.EnterPlaymode();
            return;
        }
        RunFixture();
    }

    private static void RunFixture()
    {
        string output = Argument("-probeOutputDirectory");
        Directory.CreateDirectory(output);
        var result = new Result
        {
            unity_version = Application.unityVersion,
            graphics_device = SystemInfo.graphicsDeviceType + ":" + SystemInfo.graphicsDeviceName,
            color_space = QualitySettings.activeColorSpace.ToString()
        };
        ProbeTexture left = null, right = null;
        GameObject cameraObject = null;
        RenderTexture target = null, sentinel = null;
        RenderTexture oldActive = RenderTexture.active;
        bool oldSrgb = GL.sRGBWrite;
        try
        {
            string source = Argument("-probeSourceDirectory");
            left = ProbeTexture.Load(source, "left");
            right = ProbeTexture.Load(source, "right");
            ProbeManifest.CheckPair(left.Manifest, right.Manifest);
            Require(left.Texture.width == 688 && left.Texture.height == 548, "Actual fixture extent differs.");
            Require(!left.Texture.isReadable && !right.Texture.isReadable, "Prepared textures should be immutable/non-readable.");
            target = new RenderTexture(left.Texture.width, left.Texture.height, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.sRGB);
            sentinel = new RenderTexture(8, 8, 0);
            target.Create(); sentinel.Create();
            RenderTexture.active = sentinel;
            GL.sRGBWrite = false;
            result.left_blit_rgb_sha256 = BlitAndHash(left, target, sentinel, output, "left");
            result.right_blit_rgb_sha256 = BlitAndHash(right, target, sentinel, output, "right");
            if (QualitySettings.activeColorSpace == ColorSpace.Gamma)
            {
                Require(result.left_blit_rgb_sha256 == left.Manifest.CanvasSha256, "Gamma left GPU blit/orientation changed RGB codes.");
                Require(result.right_blit_rgb_sha256 == right.Manifest.CanvasSha256, "Gamma right GPU blit/orientation changed RGB codes.");
            }
            byte[] original = OriginalRgb(Path.Combine(source, "left.png"));
            result.color_paths = new ColorPath[4];
            int colorIndex = 0;
            foreach (RenderTextureReadWrite encoding in new[] { RenderTextureReadWrite.Linear, RenderTextureReadWrite.sRGB })
                foreach (bool write in new[] { false, true })
                    result.color_paths[colorIndex++] = MeasureColorPath(left, original, encoding, write, output);

            cameraObject = new GameObject(ProbePolicy.HeadCameraName);
            Camera camera = cameraObject.AddComponent<Camera>();
            camera.enabled = false;
            camera.cullingMask = 0;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = new Color(0.2f, 0.4f, 0.6f, 1);
            camera.targetTexture = target;
            camera.Render();
            Texture2D baselineTexture = ReadBack(target);
            string baselineHash;
            try { baselineHash = ProbeManifest.Sha256(ProbeTexture.TopDownRgb(baselineTexture.GetPixels32(), target.width, target.height)); }
            finally { UnityEngine.Object.DestroyImmediate(baselineTexture); }
            var policy = new ProbePolicy();
            Require(policy.Start(Time.realtimeSinceStartupAsDouble, true, true, true, true, true), "Fixture policy start failed.");
            var effect = cameraObject.AddComponent<PayloadOutput>();
            effect.Initialize(camera, policy, left, right);
            camera.Render();
            Require(effect.FallbackCallbacks == 1 && effect.LeftCallbacks == 0 && effect.RightCallbacks == 0,
                    "Real mono camera must execute one ordinary-source fallback, never an eye payload.");
            PayloadOutput.Observation observation;
            Require(effect.TakeObservation(0, out observation), "Actual OnRenderImage did not produce a bounded observation.");
            result.mono_callback_observed = true;
            result.mono_callback_current_camera_matches = observation.CurrentCameraMatches;
            Require(!observation.SubmittedPayload, "Mono callback incorrectly admitted a payload.");
            Require(effect.TakeFault() == null, "Actual callback threw an exception.");
            Texture2D readback = ReadBack(target);
            try
            {
                string fallbackHash = ProbeManifest.Sha256(ProbeTexture.TopDownRgb(readback.GetPixels32(), target.width, target.height));
                Require(fallbackHash == baselineHash, "Mono source fallback changed the camera's ordinary output.");
            }
            finally { UnityEngine.Object.DestroyImmediate(readback); }
            UnityEngine.Object.DestroyImmediate(effect);
            Require(policy.Phase == ProbePhase.Stopped, "Component destruction must permanently close the run.");
            result.passed = true;
        }
        catch (Exception error)
        {
            result.error = error.ToString();
            Debug.LogException(error);
        }
        finally
        {
            if (cameraObject != null) UnityEngine.Object.DestroyImmediate(cameraObject);
            if (left != null) left.Dispose();
            if (right != null) right.Dispose();
            GL.sRGBWrite = oldSrgb;
            RenderTexture.active = oldActive;
            if (target != null) { target.Release(); UnityEngine.Object.DestroyImmediate(target); }
            if (sentinel != null) { sentinel.Release(); UnityEngine.Object.DestroyImmediate(sentinel); }
            result.controls = _controls;
            File.WriteAllText(Path.Combine(output, "unity-result.json"), JsonUtility.ToJson(result, true) + "\n");
            EditorApplication.Exit(result.passed ? 0 : 1);
        }
    }

    private static string BlitAndHash(ProbeTexture payload, RenderTexture target, RenderTexture sentinel,
                                      string output, string eye)
    {
        PayloadOutput.BlitPreservingState(payload.Texture, target);
        Require(RenderTexture.active == sentinel, "Blit did not restore RenderTexture.active.");
        Require(!GL.sRGBWrite, "Blit did not restore GL.sRGBWrite.");
        Texture2D readback = ReadBack(target);
        try
        {
            File.WriteAllBytes(Path.Combine(output, eye + "-blit.png"), ImageConversion.EncodeToPNG(readback));
            return ProbeManifest.Sha256(ProbeTexture.TopDownRgb(readback.GetPixels32(), readback.width, readback.height));
        }
        finally { UnityEngine.Object.DestroyImmediate(readback); }
    }

    private static byte[] OriginalRgb(string path)
    {
        var image = new Texture2D(2, 2, TextureFormat.RGB24, false, false);
        try
        {
            Require(ImageConversion.LoadImage(image, File.ReadAllBytes(path), false), "Original PNG did not load.");
            return ProbeTexture.TopDownRgb(image.GetPixels32(), image.width, image.height);
        }
        finally { UnityEngine.Object.DestroyImmediate(image); }
    }

    private static ColorPath MeasureColorPath(ProbeTexture payload, byte[] original, RenderTextureReadWrite encoding,
                                             bool write, string output)
    {
        var target = new RenderTexture(payload.Texture.width, payload.Texture.height, 0, RenderTextureFormat.ARGB32, encoding);
        RenderTexture previous = RenderTexture.active;
        bool previousWrite = GL.sRGBWrite;
        target.Create();
        try
        {
            GL.sRGBWrite = write;
            PayloadOutput.BlitPreservingState(payload.Texture, target);
            Require(GL.sRGBWrite == write && RenderTexture.active == previous, "Color path failed state restoration.");
            Texture2D readback = ReadBack(target);
            try
            {
                byte[] actual = ProbeTexture.TopDownRgb(readback.GetPixels32(), readback.width, readback.height);
                long total = 0;
                int maximum = 0;
                for (int index = 0; index < original.Length; index++)
                {
                    int error = Math.Abs(original[index] - actual[index]);
                    total += error;
                    maximum = Math.Max(maximum, error);
                }
                File.WriteAllBytes(Path.Combine(output, "left-" + encoding + "-gl" + (write ? "1" : "0") + ".png"),
                                  ImageConversion.EncodeToPNG(readback));
                return new ColorPath { requested_rt_encoding = encoding.ToString(), actual_rt_srgb = target.sRGB,
                    gl_srgb_write = write, rgb_sha256 = ProbeManifest.Sha256(actual),
                    mean_absolute_rgb_code_error = (double)total / original.Length, maximum_rgb_code_error = maximum };
            }
            finally { UnityEngine.Object.DestroyImmediate(readback); }
        }
        finally
        {
            GL.sRGBWrite = previousWrite;
            RenderTexture.active = previous;
            target.Release();
            UnityEngine.Object.DestroyImmediate(target);
        }
    }

    private static Texture2D ReadBack(RenderTexture target)
    {
        RenderTexture previous = RenderTexture.active;
        try
        {
            RenderTexture.active = target;
            var texture = new Texture2D(target.width, target.height, TextureFormat.RGB24, false, false);
            texture.ReadPixels(new Rect(0, 0, target.width, target.height), 0, 0);
            texture.Apply(false);
            return texture;
        }
        finally { RenderTexture.active = previous; }
    }
}
