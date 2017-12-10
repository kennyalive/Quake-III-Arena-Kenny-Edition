struct Single_Texture_PS_Data {
    float4 position : SV_POSITION;
    float4 color  : COLOR;
    float2 uv0 : TEXCOORD;
};

struct Multi_Texture_PS_Data {
    float4 position : SV_POSITION;
    float4 color  : COLOR;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
};

cbuffer Constants : register(b0) {
    float4x4 clip_space_xform;
};

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

Texture2D texture1 : register(t1);
SamplerState sampler1 : register(s1);

Single_Texture_PS_Data single_texture_vs(
    float4 position : POSITION,
    float4 color    : COLOR,
    float2 uv0      : TEXCOORD)
{
    Single_Texture_PS_Data ps_data;
    ps_data.position = mul(clip_space_xform, position);
    ps_data.color = color;
    ps_data.uv0 = uv0;
    return ps_data;
}

Multi_Texture_PS_Data multi_texture_vs(
    float4 position : POSITION,
    float4 color    : COLOR,
    float2 uv0      : TEXCOORD0,
    float2 uv1      : TEXCOORD1)
{
    Multi_Texture_PS_Data ps_data;
    ps_data.position = mul(clip_space_xform, position);
    ps_data.color = color;
    ps_data.uv0 = uv0;
    ps_data.uv1 = uv1;
    return ps_data;
}

float4 single_texture_ps(Single_Texture_PS_Data data) : SV_TARGET {
    return data.color * texture0.Sample(sampler0, data.uv0);
}

float4 multi_texture_mul_ps(Multi_Texture_PS_Data data) : SV_TARGET {
    return data.color * texture0.Sample(sampler0, data.uv0) * texture1.Sample(sampler1, data.uv1);
}

float4 multi_texture_add_ps(Multi_Texture_PS_Data data) : SV_TARGET {
    float4 color_a = data.color * texture0.Sample(sampler0, data.uv0);
    float4 color_b = texture1.Sample(sampler1, data.uv1);
    return float4(
        color_a.r + color_b.r,
        color_a.g + color_b.g,
        color_a.b + color_b.b,
        color_a.a * color_b.a);
}
