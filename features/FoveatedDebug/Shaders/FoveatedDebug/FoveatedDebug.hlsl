// FoveatedDebug.hlsl - Debug visualization for foveated rendering regions

// Constant buffers
cbuffer FoveatedRegionData : register(b10)
{
	float2 GazePoint;    // Normalized screen coords (0-1)
	float InnerRadius;   // Inner full-res radius
	float OuterRadius;   // Outer transition radius
	float EdgeSoftness;  // Falloff between inner/outer
	uint IsTracking;     // Whether eye tracking is active
	float Confidence;    // Tracking confidence (0-1)
	uint2 pad0;
}

cbuffer Settings : register(b11)
{
	uint EnableDebug;
	float3 DebugHue;   // RGB color for overlay
	float DebugAlpha;  // Overlay transparency
	uint ShowGazePoint;
	float GazePointSize;
	uint ShowMetrics;
	uint pad1;
}

// Vertex shader output / Pixel shader input
struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

#ifdef VERTEX_SHADER

// Vertex Shader - Fullscreen triangle
VS_OUTPUT main(uint id : SV_VertexID)
{
	VS_OUTPUT output;

	// Generate fullscreen triangle
	output.TexCoord = float2((id << 1) & 2, id & 2);
	output.Position = float4(output.TexCoord * float2(2, -2) + float2(-1, 1), 0, 1);

	return output;
}

#endif  // VERTEX_SHADER

#ifdef PIXEL_SHADER

// Helper function: Calculate distance from point
float Circle(float2 uv, float2 center, float radius)
{
	return length(uv - center) - radius;
}

// Helper function: Draw crosshair
float Crosshair(float2 uv, float2 center, float size, float thickness)
{
	float2 d = abs(uv - center);
	float horizontal = step(d.y, thickness) * step(d.x, size);
	float vertical = step(d.x, thickness) * step(d.y, size);
	return max(horizontal, vertical);
}

// Pixel Shader - Visualize foveated region
float4 main(VS_OUTPUT input) :
	SV_Target
{
	if (!EnableDebug)
		discard;

	float2 uv = input.TexCoord;

	// Calculate distance from gaze point
	float dist = distance(uv, GazePoint);

	// Visualize foveated regions
	float alpha = 0.0;
	float3 color = DebugHue;

	// Inner radius (foveal region) - higher opacity
	if (dist < InnerRadius) {
		alpha = DebugAlpha * 0.6;
	}
	// Transition zone (parafoveal region) - gradient opacity
	else if (dist < OuterRadius) {
		float t = (dist - InnerRadius) / (OuterRadius - InnerRadius);
		alpha = DebugAlpha * lerp(0.6, 0.2, t);
	}
	// Outer region (peripheral) - low opacity
	else {
		alpha = DebugAlpha * 0.2;
	}

	// Draw boundary rings
	float innerRing = abs(Circle(uv, GazePoint, InnerRadius));
	float outerRing = abs(Circle(uv, GazePoint, OuterRadius));

	if (innerRing < 0.002) {
		color = float3(1, 1, 1);  // White ring
		alpha = DebugAlpha * 0.8;
	}
	if (outerRing < 0.002) {
		color = float3(1, 1, 1);  // White ring
		alpha = DebugAlpha * 0.8;
	}

	// Draw gaze point crosshair
	if (ShowGazePoint) {
		float crosshair = Crosshair(uv, GazePoint, GazePointSize, GazePointSize * 0.15);
		if (crosshair > 0.0) {
			if (IsTracking) {
				// Yellow when tracking
				color = float3(1, 1, 0);
				alpha = DebugAlpha;
			} else {
				// Gray when not tracking
				color = float3(0.5, 0.5, 0.5);
				alpha = DebugAlpha * 0.5;
			}
		}
	}

	return float4(color, alpha);
}

#endif  // PIXEL_SHADER