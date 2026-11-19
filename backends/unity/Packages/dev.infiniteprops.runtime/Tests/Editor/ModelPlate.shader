Shader "Hidden/InfiniteProps/ModelPlate"
{
    Properties { _NormalMap ("Surface normal", 2D) = "bump" {} }
    SubShader
    {
        Pass
        {
            Cull Back
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "UnityCG.cginc"
            sampler2D _NormalMap;
            struct Frame {
                float4 position : SV_POSITION;
                float2 uv : TEXCOORD0;
                float3 normal : TEXCOORD1;
                float3 tangent : TEXCOORD2;
                float3 bitangent : TEXCOORD3;
            };
            Frame vert(appdata_full input) {
                Frame o;
                o.position = UnityObjectToClipPos(input.vertex);
                o.uv = input.texcoord.xy;
                o.normal = UnityObjectToWorldNormal(input.normal);
                o.tangent = UnityObjectToWorldDir(input.tangent.xyz);
                o.bitangent = cross(o.normal, o.tangent) * input.tangent.w * unity_WorldTransformParams.w;
                return o;
            }
            float4 frag(Frame i) : SV_Target {
                float3 n = tex2D(_NormalMap, i.uv).xyz * 2 - 1;
                float3 world = normalize(i.tangent * n.x + i.bitangent * n.y + i.normal * n.z);
                float light = saturate(dot(world, normalize(float3(0, 2, -1))));
                return float4(light, light, light, 1);
            }
            ENDCG
        }
    }
}
