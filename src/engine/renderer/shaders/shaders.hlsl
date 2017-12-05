struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD;
};

cbuffer Constants : register(b0)
{
	float4x4 clip_space_xform;
};

Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

PSInput VSMain(float4 position : POSITION, float2 uv : TEXCOORD)
{
	PSInput result;

	result.position = mul(clip_space_xform, position);
	result.uv = uv;

	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return texture0.Sample(sampler0, input.uv);
}
