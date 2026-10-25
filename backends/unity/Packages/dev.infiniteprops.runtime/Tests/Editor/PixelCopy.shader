Shader "Hidden/InfiniteProps/PixelCopy"
{
    Properties { _MainTex ("Source", 2D) = "black" {} }
    SubShader
    {
        Pass
        {
            ZTest Always Cull Off ZWrite Off Blend Off
            CGPROGRAM
            #pragma vertex vert_img
            #pragma fragment frag
            #include "UnityCG.cginc"
            sampler2D _MainTex;
            float4 frag(v2f_img input) : SV_Target { return tex2D(_MainTex, input.uv); }
            ENDCG
        }
    }
}
