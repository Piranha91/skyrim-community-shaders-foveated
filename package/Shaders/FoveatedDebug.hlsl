// Constant buffers
cbuffer FoveatedRegion : register(b10)
{
	float2 GazePoint;    // Normalized screen position (0-1)
	float InnerRadius;   // Inner full-detail radius
	float OuterRadius;   // Outer falloff radius
	float EdgeSoftness;  // Transition smoothness
	uint IsTracking;     // Eye tracking active flag
	float Confidence;    // Tracking confidence (0-1)
	uint2 pad0;
}

cbuffer Settings : register(b11)
{
	bool EnableDebug;
	float3 DebugHue;
	float DebugAlpha;
	bool ShowGazePoint;
	float GazePointSize;
	bool ShowMetrics;
	uint pad1;
}

// Structures
struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD0;
};

// Vertex Shader - Fullscreen triangle
VS_OUTPUT main_vs(uint id : SV_VERTEXID)
{
	VS_OUTPUT output;
	// Generate fullscreen triangle
	output.texcoord = float2((id << 1) & 2, id & 2);
	output.position = float4(output.texcoord * float2(2, -2) + float2(-1, 1), 0, 1);
	return output;
}

// Helper function: Draw a circle
float Circle(float2 uv, float2 center, float radius, float softness)
{
	float dist = distance(uv, center);
	return 1.0 - smoothstep(radius - softness, radius + softness, dist);
}

// Helper function: Draw a crosshair
float Crosshair(float2 uv, float2 center, float size, float thickness)
{
	float2 delta = abs(uv - center);
	float horizontal = step(delta.y, thickness) * step(delta.x, size);
	float vertical = step(delta.x, thickness) * step(delta.y, size);
	return max(horizontal, vertical);
}

// Pixel Shader - Debug visualization
float4 main_ps(VS_OUTPUT input) :
	SV_TARGET
{
	if (!EnableDebug)
		discard;

	float2 uv = input.texcoord;
	float dist = distance(uv, GazePoint);

	// Create foveated region visualization
	// Inner circle: High opacity (foveal region)
	// Transition ring: Gradient opacity (parafoveal region)
	// Outer area: Low opacity (peripheral region)

	float innerMask = 1.0 - smoothstep(InnerRadius - EdgeSoftness, InnerRadius, dist);
	float outerMask = smoothstep(OuterRadius - EdgeSoftness, OuterRadius, dist);

	// Combine masks: bright in center, fade in transition, transparent outside
	float regionMask = innerMask * 0.8 + (1.0 - innerMask) * (1.0 - outerMask) * 0.4;

	// Base color
	float3 color = DebugHue;

	// Modulate color based on tracking state
	if (!IsTracking) {
		// Gray and dim when not tracking
		color = float3(0.5, 0.5, 0.5);
		regionMask *= 0.5;
	} else {
		// Pulsing effect when tracking
		float pulse = sin(input.position.x * 0.01) * 0.1 + 0.9;
		color *= pulse;

		// Color intensity based on confidence
		color *= lerp(0.5, 1.0, Confidence);
	}

	// Add gaze point crosshair
	float crosshair = 0.0;
	if (ShowGazePoint && IsTracking) {
		crosshair = Crosshair(uv, GazePoint, GazePointSize, GazePointSize * 0.2);

		// Make crosshair more visible
		if (crosshair > 0.0) {
			color = float3(1.0, 1.0, 0.0);  // Yellow crosshair
			regionMask = 1.0;
		}
	}

	// Add ring markers for inner and outer boundaries
	float innerRing = Circle(uv, GazePoint, InnerRadius, 0.002);
	float outerRing = Circle(uv, GazePoint, OuterRadius, 0.002);

	if (innerRing > 0.5 || outerRing > 0.5) {
		color = float3(1.0, 1.0, 1.0);  // White rings
		regionMask = max(regionMask, 0.6);
	}

	// Final output
	float alpha = DebugAlpha * regionMask;
	return float4(color, alpha);
}