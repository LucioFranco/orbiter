#version 450

// Push constants (shared with vertex shader)
layout(push_constant) uniform PushConstants {
    mat4 mvp;         // Not used in fragment shader
    mat4 model;       // Not used in fragment shader
    vec4 lightDir;    // Light direction xyz, specular power in w
    vec4 matDiffuse;  // Material diffuse color RGBA
    vec4 matEmissive; // Material emissive RGB, hasTexture flag in w
} pc;

// Texture sampler (binding 0, set 0)
layout(set = 0, binding = 0) uniform sampler2D texSampler;

// Inputs from vertex shader
layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;

// Output color
layout(location = 0) out vec4 outColor;

void main() {
    // Normalize interpolated normal
    vec3 normal = normalize(fragNormal);

    // Light direction (should be normalized in push constant)
    vec3 lightDirection = normalize(pc.lightDir.xyz);

    // Simple diffuse lighting (Lambert)
    float diffuse = max(dot(normal, lightDirection), 0.0);

    // Ambient light to prevent completely dark areas
    float ambient = 0.2;

    // Total lighting
    float lighting = ambient + diffuse * 0.8;

    // Sample texture
    vec4 texColor = texture(texSampler, fragTexCoord);

    // Determine base color: use texture if hasTexture flag is set, otherwise material diffuse
    // hasTexture flag is stored in matEmissive.w (1.0 = has texture, 0.0 = no texture)
    vec3 baseColor;
    float alpha;
    if (pc.matEmissive.w > 0.5) {
        // Has texture - multiply texture by material diffuse (allows tinting)
        vec3 matColor = pc.matDiffuse.a > 0.0 ? pc.matDiffuse.rgb : vec3(1.0, 1.0, 1.0);
        baseColor = texColor.rgb * matColor;
        alpha = texColor.a * (pc.matDiffuse.a > 0.0 ? pc.matDiffuse.a : 1.0);
    } else {
        // No texture - use material diffuse color (or default gray if alpha is 0)
        baseColor = pc.matDiffuse.a > 0.0 ? pc.matDiffuse.rgb : vec3(0.7, 0.7, 0.7);
        alpha = pc.matDiffuse.a > 0.0 ? pc.matDiffuse.a : 1.0;
    }

    // Apply lighting to diffuse, add emissive
    vec3 litColor = baseColor * lighting + pc.matEmissive.rgb;

    // Final color
    outColor = vec4(litColor, alpha);
}
