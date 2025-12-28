#version 450

// Push constants for mesh rendering
layout(push_constant) uniform PushConstants {
    mat4 mvp;         // Model-View-Projection matrix
    mat4 model;       // Model matrix (for normal transformation)
    vec4 lightDir;    // Light direction xyz, specular power in w
    vec4 matDiffuse;  // Material diffuse color RGBA
    vec4 matEmissive; // Material emissive color RGB
} pc;

// Vertex inputs (MeshVertex format)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Outputs to fragment shader
layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;

void main() {
    // Transform position to clip space
    gl_Position = pc.mvp * vec4(inPosition, 1.0);

    // Transform normal to world space (using upper-left 3x3 of model matrix)
    // Note: For non-uniform scaling, we'd need the inverse-transpose
    mat3 normalMatrix = mat3(pc.model);
    fragNormal = normalize(normalMatrix * inNormal);

    // Pass through texture coordinates
    fragTexCoord = inTexCoord;

    // World position for potential future use (specular lighting, etc.)
    fragWorldPos = (pc.model * vec4(inPosition, 1.0)).xyz;
}
