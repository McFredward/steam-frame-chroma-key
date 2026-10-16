using System;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;

// Offline pixels only. No game callbacks, live material mutation, XR or carrier.
public static class CoverageFixture
{
    [Serializable] public sealed class Row
    {
        public string name;
        public int msaa;
        public float[] stock, coverage;
    }
    [Serializable] public sealed class Result
    {
        public string unity, backend, device, color_space;
        public int checks;
        public bool passed, legacy_alpha_rejected, xr_executed, live_alpha_authorized;
        public List<Row> rows = new List<Row>();
        public string failure;
    }
    private static Result result = new Result();
    private static Texture2D texture;
    private static Mesh mesh;
    private static void Check(bool value, string why)
    {
        result.checks++;
        if (!value) throw new Exception(why);
    }
    private static void Near(float a, float b, string why)
    {
        Check(!float.IsNaN(a) && !float.IsInfinity(a) && Mathf.Abs(a-b) < 0.001f, why + ": " + a + " != " + b);
    }
    public static void Start()
    {
        string[] args = Environment.GetCommandLineArgs();
        string output = args[Array.IndexOf(args, "-coverageResult") + 1];
        int exit = 1;
        try
        {
            result.unity = Application.unityVersion;
            result.backend = SystemInfo.graphicsDeviceType.ToString();
            result.device = SystemInfo.graphicsDeviceName;
            result.color_space = QualitySettings.activeColorSpace.ToString();
            Check(result.backend != "Null" && SystemInfo.SupportsRenderTextureFormat(RenderTextureFormat.ARGBFloat), "Real alpha-capable GPU required");
            texture = new Texture2D(2, 2, TextureFormat.RGBAFloat, false, true);
            texture.SetPixels(new[] {new Color(.2f,.4f,.6f,.5f),new Color(.2f,.4f,.6f,.5f),new Color(.2f,.4f,.6f,.5f),new Color(.2f,.4f,.6f,.5f)});
            texture.Apply(false, false);
            mesh = new Mesh();
            mesh.vertices = new[] {new Vector3(-1,-1,0),new Vector3(1,-1,0),new Vector3(1,1,0),new Vector3(-1,1,0)};
            mesh.uv = new[] {new Vector2(0,0),new Vector2(1,0),new Vector2(1,1),new Vector2(0,1)};
            mesh.colors = new[] {Color.white,Color.white,Color.white,Color.white};
            mesh.triangles = new[] {0,1,2,0,2,3};
            foreach (int samples in new[] {1,2,4,8})
            {
                var desc = new RenderTextureDescriptor(32,32,RenderTextureFormat.ARGBFloat,24) {msaaSamples=samples};
                if (SystemInfo.GetRenderTextureSupportedMSAASampleCount(desc) != samples) continue;
                Probe("clear", samples, "EnvParticleAlpha", 0, 0f);
                Probe("one-over", samples, "EnvParticleAlpha", 1, .5f);
                Probe("two-over", samples, "EnvParticleAlpha", 2, .75f);
                Probe("emission", samples, "EnvParticleAdd", 1, 0f);
                Probe("emission-over-glass", samples, "EnvParticleAlpha", 1, .5f, false, false, true);
                Probe("opaque", samples, "HeadUnlit", 1, 1f);
                Probe("ui-over", samples, "ActorBarSprite", 1, .5f);
                Probe("ui-clipped", samples, "ActorBarSprite", 1, 0f, true);
                Probe("ui-stencil-only", samples, "ActorBarSprite", 1, 0f, false, true);
            }
            Check(result.rows.Count >= 9, "At least one actual format/sample profile must execute");
            Check(result.legacy_alpha_rejected, "Legacy shared RGB/alpha factors must fail over-coverage");
            result.passed = true;
            exit = 0;
        }
        catch (Exception error) {result.failure = error.ToString(); Debug.LogException(error);}
        finally
        {
            if (mesh) UnityEngine.Object.DestroyImmediate(mesh);
            if (texture) UnityEngine.Object.DestroyImmediate(texture);
            File.WriteAllText(output, JsonUtility.ToJson(result, true)+"\n");
        }
        EditorApplication.Exit(exit);
    }
    private static void Probe(string name, int samples, string shader, int layers, float expected,
                              bool clip=false, bool stencil=false, bool additive=false)
    {
        Color stock = Draw(shader, false, samples, layers, clip, stencil, additive);
        Color covered = Draw(shader, true, samples, layers, clip, stencil, additive);
        Near(stock.r, covered.r, name+" original R");
        Near(stock.g, covered.g, name+" original G");
        Near(stock.b, covered.b, name+" original B");
        Near(covered.a, expected, name+" coverage");
        if (name == "one-over")
        {
            Near(stock.a, .25f, "Actual legacy shared-alpha failure");
            Check(Mathf.Abs(stock.a-expected)>.1f, "Legacy failure is causal");
            result.legacy_alpha_rejected = true;
        }
        if (name == "emission") Check(covered.r > .001f && covered.a == 0f, "Emission survives at zero coverage");
        if (name == "ui-clipped" || name == "ui-stencil-only") Near(covered.r,0f,"Invisible UI adds no RGB");
        result.rows.Add(new Row {name=name, msaa=samples,stock=Values(stock),coverage=Values(covered)});
    }
    private static float[] Values(Color c) {return new[] {c.r,c.g,c.b,c.a};}
    private static Color Draw(string family, bool covered, int samples, int layers, bool clip, bool stencil, bool additive)
    {
        RenderTexture old = RenderTexture.active;
        var rt = new RenderTexture(32,32,24,RenderTextureFormat.ARGBFloat,RenderTextureReadWrite.Linear) {antiAliasing=samples};
        var read = new Texture2D(32,32,TextureFormat.RGBAFloat,false,true);
        Material material = null, emission = null;
        var cb = new CommandBuffer {name="Owned inert coverage fixture"};
        try
        {
            Check(rt.Create() && rt.antiAliasing == samples, "Actual admitted RT/MSAA format");
            material = MaterialFor(family, covered);
            if (clip) {material.EnableKeyword("UNITY_UI_CLIP_RECT");material.SetVector("_ClipRect",new Vector4(-2,-2,0,2));}
            if (stencil) {material.SetFloat("_ColorMask",0);material.SetFloat("_StencilOp",2);material.SetFloat("_Stencil",1);}
            cb.SetRenderTarget(rt);
            cb.ClearRenderTarget(true,true,Color.clear);
            cb.SetViewProjectionMatrices(Matrix4x4.identity,Matrix4x4.identity);
            for (int layer=0;layer<layers;layer++) cb.DrawMesh(mesh,Matrix4x4.identity,material,0,0);
            if (additive) {emission=MaterialFor("EnvParticleAdd",covered);cb.DrawMesh(mesh,Matrix4x4.identity,emission,0,0);}
            Graphics.ExecuteCommandBuffer(cb);
            RenderTexture.active = rt;
            read.ReadPixels(new Rect(0,0,32,32),0,0,false);
            read.Apply(false,false);
            return read.GetPixel(24,16); // x>0 is outside the controlled UI clip.
        }
        finally
        {
            RenderTexture.active=old;
            cb.Release();
            if (material) UnityEngine.Object.DestroyImmediate(material);
            if (emission) UnityEngine.Object.DestroyImmediate(emission);
            UnityEngine.Object.DestroyImmediate(read);
            rt.Release();UnityEngine.Object.DestroyImmediate(rt);
        }
    }
    private static Material MaterialFor(string family, bool covered)
    {
        Shader shader=Shader.Find((covered ? "CoverageFixture/" : "GloomhavenVR/")+family);
        Check(shader && shader.isSupported && !ShaderUtil.ShaderHasError(shader),"Actual shader compiled: "+family);
        var material=new Material(shader);
        material.SetTexture("_MainTex",texture);
        if (material.HasProperty("_Tint")) material.SetColor("_Tint",Color.white);
        if (material.HasProperty("_Color")) material.SetColor("_Color",Color.white);
        return material;
    }
}
