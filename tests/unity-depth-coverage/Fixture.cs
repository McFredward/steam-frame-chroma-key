using System;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;

// Isolated editor camera only. No production camera, game data, carrier or XR.
[ExecuteInEditMode]
public sealed class DepthCoverageRead : MonoBehaviour
{
    public Color[] pixels;
    public string sourceFormat;
    public int sourceSamples;
    private void OnRenderImage(RenderTexture source, RenderTexture destination)
    {
        RenderTexture previous = RenderTexture.active;
        var read = new Texture2D(source.width, source.height, TextureFormat.RGBAFloat, false, true);
        try
        {
            sourceFormat = source.format.ToString(); sourceSamples = source.antiAliasing;
            RenderTexture.active = source;
            read.ReadPixels(new Rect(0, 0, source.width, source.height), 0, 0, false);
            read.Apply(false, false); pixels = read.GetPixels();
            Graphics.Blit(source, destination);
        }
        finally { RenderTexture.active = previous; DestroyImmediate(read); }
    }
}

public static class DepthCoverageFixture
{
    [Serializable] public sealed class Row
    {
        public string name, format, sourceFormat;
        public int samples, sourceSamples, partialPixels;
        public float maxRgbDifference, maxCoverageDifference, legacyFailure, originalRedSum, originalGreenSum;
        public bool excludedDepthlessDetected;
    }
    [Serializable] public sealed class Result
    {
        public string unity, backend, device, colorSpace, failure;
        public bool passed, reversedZ, xrExecuted, gameWideCoverageProven;
        public int checks, legacyControls, depthlessControls, wrongComparisonControls;
        public List<Row> rows = new List<Row>();
    }
    private static Result result = new Result();
    private static readonly List<UnityEngine.Object> owned = new List<UnityEngine.Object>();
    private static Camera camera;
    private static Mesh triangle, quad;
    private static Material reset, stamp;
    private static CommandBuffer buffer;
    private static HashSet<string> selected;
    private static void Check(bool value, string why)
    { result.checks++; if (!value) throw new Exception(why); }
    public static void Start()
    {
        string[] args = Environment.GetCommandLineArgs();
        string output = args[Array.IndexOf(args, "-depthCoverageResult") + 1];
        int cases=Array.IndexOf(args,"-depthCoverageCases");
        if(cases>=0)selected=new HashSet<string>(args[cases+1].Split(','));
        int code = 1;
        try
        {
            result.unity = Application.unityVersion;
            result.backend = SystemInfo.graphicsDeviceType.ToString();
            result.device = SystemInfo.graphicsDeviceName;
            result.colorSpace = QualitySettings.activeColorSpace.ToString();
            result.reversedZ = SystemInfo.usesReversedZBuffer;
            Check(result.backend != "Null", "Actual GPU required");
            var go = Keep(new GameObject("Owned coverage camera"));
            camera = go.AddComponent<Camera>(); camera.enabled = false;
            camera.orthographic = true; camera.orthographicSize = 1;
            camera.nearClipPlane = .1f; camera.farClipPlane = 20;
            camera.transform.position = new Vector3(0, 0, -5);
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = new Color(0,0,0,.31f);
            camera.allowHDR = false; camera.allowMSAA = true;
            camera.renderingPath = RenderingPath.Forward;
            camera.cullingMask = 1 << 30;
            go.AddComponent<DepthCoverageRead>();
            triangle = Keep(new Mesh());
            triangle.vertices = new[] {new Vector3(-.91f,-.83f,0),new Vector3(.87f,-.72f,0),new Vector3(-.63f,.89f,0)};
            triangle.uv = new[] {Vector2.zero, Vector2.right, Vector2.up};
            triangle.colors = new[] {Color.white,Color.white,Color.white};
            triangle.triangles = new[] {0,1,2}; triangle.RecalculateBounds();
            quad = Keep(new Mesh());
            quad.vertices = new[] {new Vector3(-1,-1,0),new Vector3(1,-1,0),new Vector3(1,1,0),new Vector3(-1,1,0)};
            quad.uv = new[] {Vector2.zero,Vector2.right,Vector2.one,Vector2.up};
            quad.colors = new[] {Color.white,Color.white,Color.white,Color.white};
            quad.triangles = new[] {0,1,2,0,2,3}; quad.RecalculateBounds();
            reset = MaterialFor("DepthCoverageFixture/Stamp"); reset.SetFloat("_Value",0);
            reset.SetInt("_Test", (int)CompareFunction.Always);
            stamp = MaterialFor("DepthCoverageFixture/Stamp"); stamp.SetFloat("_Value",1);
            // ShaderLab comparisons are logical camera-depth comparisons. Unity
            // reverses the native compare along with its depth convention; do
            // not reverse this property a second time on Vulkan/D3D.
            stamp.SetInt("_Test",(int)CompareFunction.Greater);
            buffer = new CommandBuffer {name="Offline same-target alpha reconstruction"};
            // Deliberately inherit the camera's actual color+depth/MSAA attachments.
            buffer.DrawProcedural(Matrix4x4.identity,reset,0,MeshTopology.Triangles,3);
            buffer.DrawProcedural(Matrix4x4.identity,stamp,0,MeshTopology.Triangles,3);
            foreach (RenderTextureFormat format in new[] {RenderTextureFormat.ARGB32,RenderTextureFormat.ARGBFloat})
            {
                if (!SystemInfo.SupportsRenderTextureFormat(format)) continue;
                foreach (int samples in new[] {1,2,4,8})
                {
                    var desc = new RenderTextureDescriptor(64,64,format,24) {msaaSamples=samples};
                    if (SystemInfo.GetRenderTextureSupportedMSAASampleCount(desc) != samples) continue;
                    Probe("empty",format,samples,false,false,false,false);
                    Probe("opaque-edges",format,samples,true,false,false,false);
                    Probe("cutout-edges",format,samples,true,true,false,false);
                    Probe("over-empty",format,samples,false,false,true,false);
                    Probe("over-opaque",format,samples,true,false,true,false);
                    Probe("emission",format,samples,false,false,false,true);
                    Probe("emission-over-opaque",format,samples,true,false,false,true);
                    Probe("excluded-depthless",format,samples,true,false,false,false,true);
                    Probe("alpha-to-coverage",format,samples,true,false,false,false,false,true);
                    Probe("stencil-clipped",format,samples,true,false,false,false,false,false,true);
                    Probe("stencil-only",format,samples,false,false,false,false,false,false,true);
                }
            }
            Check(result.rows.Count > 0,"Actual selected GPU samples must execute");
            if(selected==null)Check(result.legacyControls>0 && result.depthlessControls>0 && result.wrongComparisonControls>0,
                                  "Complete run must execute causal limitations");
            result.passed = true; code = 0;
        }
        catch(Exception error) {result.failure=error.ToString(); Debug.LogException(error);}
        finally
        {
            if(camera) camera.RemoveAllCommandBuffers();
            if(buffer != null) buffer.Release();
            for(int i=owned.Count-1;i>=0;i--) if(owned[i]) UnityEngine.Object.DestroyImmediate(owned[i]);
            File.WriteAllText(output,JsonUtility.ToJson(result,true)+"\n");
        }
        EditorApplication.Exit(code);
    }
    private static T Keep<T>(T value) where T : UnityEngine.Object {owned.Add(value);return value;}
    private static Material MaterialFor(string name)
    {
        Shader shader=Shader.Find(name);
        Check(shader && shader.isSupported && !ShaderUtil.ShaderHasError(shader),"Actual shader compiled: "+name);
        return Keep(new Material(shader));
    }
    private static Texture2D TextureFor(Color value,bool cutout=false)
    {
        var texture=Keep(new Texture2D(4,4,TextureFormat.RGBAFloat,false,true));
        texture.filterMode=FilterMode.Point;
        var colors=new Color[16];
        for(int i=0;i<colors.Length;i++) {colors[i]=value;if(cutout)colors[i].a=(i%4+i/4)%2 == 0 ? 1 : 0;}
        texture.SetPixels(colors);texture.Apply(false,false);return texture;
    }
    private static GameObject Surface(Mesh mesh,Material material,float z)
    {
        var go=Keep(new GameObject("Owned fixture surface"));go.layer=30;
        go.transform.position=new Vector3(0,0,z);
        go.AddComponent<MeshFilter>().sharedMesh=mesh;go.AddComponent<MeshRenderer>().sharedMaterial=material;
        return go;
    }
    private static Color[] Render(RenderTexture target,bool enabled)
    {
        if(enabled)camera.AddCommandBuffer(CameraEvent.AfterForwardOpaque,buffer);
        try
        {
            camera.targetTexture=target;camera.Render();
            var read=camera.GetComponent<DepthCoverageRead>();
            Check(read.pixels != null && read.pixels.Length == 4096,"Actual OnRenderImage source read");
            return read.pixels;
        }
        finally {if(enabled)camera.RemoveCommandBuffer(CameraEvent.AfterForwardOpaque,buffer);camera.targetTexture=null;}
    }
    private static void Probe(string name,RenderTextureFormat format,int samples,bool hasOpaque,bool cutout,
                              bool over,bool emission,bool depthless=false,bool alphaToCoverage=false,bool stencil=false)
    {
        if(selected!=null && !selected.Contains(name))return;
        int start=owned.Count;
        var rt=new RenderTexture(64,64,24,format,RenderTextureReadWrite.Linear){antiAliasing=samples};
        try
        {
            Check(rt.Create() && rt.antiAliasing == samples,"Actual target sample count");
            if(hasOpaque)
            {
                Material m=MaterialFor(cutout ? "DepthCoverageFixture/Cutout" : depthless ? "DepthCoverageFixture/Depthless"
                                      : alphaToCoverage ? "DepthCoverageFixture/AlphaToCoverage" : stencil ? "DepthCoverageFixture/StencilRead" : "GloomhavenVR/HeadUnlit");
                m.SetTexture("_MainTex",TextureFor(new Color(1,.375f,.625f,.17f),cutout));
                m.SetColor("_Color",Color.white);Surface(triangle,m,0);
            }
            if(stencil)
            {
                Material m=MaterialFor("DepthCoverageFixture/StencilWrite");
                m.renderQueue=1900;m.SetTexture("_MainTex",TextureFor(Color.white));m.SetColor("_Color",Color.white);
                var mask=Surface(quad,m,-.5f);mask.transform.localScale=new Vector3(.5f,1,1);mask.transform.position+=Vector3.left*.5f;
            }
            if(over || emission)
            {
                Material m=MaterialFor("DepthCoverageFixture/"+(over ? "EnvParticleAlpha" : "EnvParticleAdd"));
                m.SetTexture("_MainTex",TextureFor(new Color(0,.2f,.4f,.5f)));m.SetColor("_Tint",Color.white);
                Surface(quad,m,-1);
            }
            Color[] stock=Render(rt,false),covered=Render(rt,true);
            var row=new Row{name=name,format=format.ToString(),samples=samples};
            var read=camera.GetComponent<DepthCoverageRead>();row.sourceFormat=read.sourceFormat;row.sourceSamples=read.sourceSamples;
            float tolerance=format == RenderTextureFormat.ARGB32 ? .008f : .0015f;
            for(int i=0;i<stock.Length;i++)
            {
                row.originalRedSum+=stock[i].r;row.originalGreenSum+=stock[i].g;
                float delta=Mathf.Max(Mathf.Abs(stock[i].r-covered[i].r),Mathf.Abs(stock[i].g-covered[i].g),Mathf.Abs(stock[i].b-covered[i].b));
                row.maxRgbDifference=Mathf.Max(row.maxRgbDifference,delta);
                Check(delta == 0,"Alpha-only injection must keep actual RGB bits at "+name+"/"+i+": "+delta);
                float expected=over ? .5f+stock[i].r : stock[i].r;
                if(depthless)
                {
                    if(stock[i].r>.9f && covered[i].a<.01f)row.excludedDepthlessDetected=true;
                    continue;
                }
                float difference=Mathf.Abs(covered[i].a-expected);
                row.maxCoverageDifference=Mathf.Max(row.maxCoverageDifference,difference);
                Check(difference<tolerance,"Actual depth/resolve coverage mismatch "+name+"/"+i+": "+covered[i].a+" vs "+expected);
                if(!over && stock[i].r>.01f && stock[i].r<.99f)row.partialPixels++;
                row.legacyFailure=Mathf.Max(row.legacyFailure,Mathf.Abs(stock[i].a-expected));
            }
            if(depthless){Check(row.excludedDepthlessDetected,"Depthless color is deliberately NOT admitted");result.depthlessControls++;}
            if(name=="emission")Check(row.originalGreenSum>1f,"Original additive emission is nonzero at zero coverage");
            if(name=="cutout-edges")
            {
                Row opaque=result.rows.Find(value=>value.name=="opaque-edges" && value.format==row.format && value.samples==samples);
                Check(opaque!=null && row.originalRedSum>opaque.originalRedSum*.2f && row.originalRedSum<opaque.originalRedSum*.8f,
                      "Actual cutout has both surviving and discarded silhouette samples");
            }
            if(name=="opaque-edges")
            {
                Check(row.legacyFailure>.1f,"Unchanged source alpha must fail coverage");result.legacyControls++;
                stamp.SetInt("_Test",(int)CompareFunction.Less);
                Color[] wrong;
                try {wrong=Render(rt,true);}
                finally {stamp.SetInt("_Test",(int)CompareFunction.Greater);}
                bool rejected=false;
                for(int i=0;i<stock.Length;i++)if(stock[i].r>.99f && wrong[i].a<.01f)rejected=true;
                Check(rejected,"Reversing ShaderLab comparison a second time must be detected");
                result.wrongComparisonControls++;
            }
            result.rows.Add(row);
        }
        finally
        {
            rt.Release();UnityEngine.Object.DestroyImmediate(rt);
            for(int i=owned.Count-1;i>=start;i--){if(owned[i])UnityEngine.Object.DestroyImmediate(owned[i]);owned.RemoveAt(i);}
        }
    }
}
