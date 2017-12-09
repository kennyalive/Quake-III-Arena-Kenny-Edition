struct Single_Texture_PS_Data {
	float4 position : SV_POSITION;
	float2 uv0 : TEXCOORD;
};

cbuffer Constants : register(b0) {
	float4x4 clip_space_xform;
};

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

Single_Texture_PS_Data single_texture_vs(float4 position : POSITION, float2 uv0 : TEXCOORD) {
	Single_Texture_PS_Data ps_data;
	ps_data.position = mul(clip_space_xform, position);
	ps_data.uv0 = uv0;
	return ps_data;
}

float4 single_texture_ps(Single_Texture_PS_Data data) : SV_TARGET {
	return texture0.Sample(sampler0, data.uv0);
}
