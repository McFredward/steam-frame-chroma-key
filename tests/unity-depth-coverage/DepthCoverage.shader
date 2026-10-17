// Offline candidate only: retain native RGB and use this eye's attached depth.
Shader "DepthCoverageFixture/Stamp"
{
    Properties { _Value ("Coverage", Float) = 0 _Test ("Depth comparison", Float) = 8 }
    SubShader
    {
        Cull Off ZWrite Off ZTest [_Test] ColorMask A Blend Off
        Pass
        {
            CGPROGRAM
            #pragma target 3.5
            #pragma vertex vert
            #pragma fragment frag
            #include "UnityCG.cginc"
            float _Value;
            float4 vert(uint id : SV_VertexID) : SV_POSITION
            {
                float2 uv = float2((id << 1) & 2, id & 2);
                #if defined(UNITY_REVERSED_Z)
                const float farDepth = 0;
                #else
                const float farDepth = 1;
                #endif
                return float4(uv * 2 - 1, farDepth, 1);
            }
            float4 frag() : SV_Target { return float4(0, 0, 0, _Value); }
            ENDCG
        }
    }
    Fallback Off
}
