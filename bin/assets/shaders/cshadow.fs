#version 300 es
precision highp float;
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec4 FragPosViewSpace;

uniform sampler2D diffuseTexture;
uniform sampler2D shadowMap[4];

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform mat4 lightSpaceMatrices[4];
uniform float cascadePlaneDistances[4];
uniform int cascadeCount;
uniform bool showCascades;
uniform float farPlane;
uniform vec2 shadowMapSize;


//uniform float debugBaseBias;        // Controlar baseBias
//uniform float debugSlopeBias;       // Controlar slopeBias
//uniform float debugDiskRadius;      // Controlar Poisson disk radius
 

const vec2 poissonDisk[16] = vec2[](
   vec2(-0.94201624, -0.39906216),
   vec2(0.94558609, -0.76890725),
   vec2(-0.094184101, -0.92938870),
   vec2(0.34495938, 0.29387760),
   vec2(-0.91588581, 0.45771432),
   vec2(-0.81544232, -0.87912464),
   vec2(-0.38277543, 0.27676845),
   vec2(0.97484398, 0.75648379),
   vec2(0.44323325, -0.97511554),
   vec2(0.53742981, -0.47373420),
   vec2(-0.26496911, -0.41893023),
   vec2(0.79197514, 0.19090188),
   vec2(-0.24188840, 0.99706507),
   vec2(-0.81409955, 0.91437590),
   vec2(0.19984126, 0.78641367),
   vec2(0.14383161, -0.14100790)
);

float SampleShadowMap(int cascadeIndex, vec2 uv)
{
    if(cascadeIndex == 0)
        return texture(shadowMap[0], uv).r;
    else if(cascadeIndex == 1)
        return texture(shadowMap[1], uv).r;
    else
        if(cascadeIndex == 2)
            return texture(shadowMap[2], uv).r;
        else if(cascadeIndex == 3)
            return texture(shadowMap[3], uv).r;
    
    return texture(shadowMap[0], uv).r;
}


float ShadowCalculationDiskPCF(int cascadeIndex, vec4 fragPosLightSpace)
{

    //melhores valores
    float debugBaseBias = 0.0002f;
    float debugSlopeBias = 0.0001f;
   float debugDiskRadius = 0.68f;


    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    if(projCoords.z > 1.0)
        return 0.0;
    
    if(projCoords.x < 0.0 || projCoords.x > 1.0 || 
       projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;
    
    float currentDepth = projCoords.z;
    
    vec3 normal = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float NdotL = max(dot(normal, lightDir), 0.0);
    
    float baseBias = max(debugBaseBias, 0.000005); 
    float slopeBias = debugSlopeBias * (1.0 - NdotL);
    float cascadeScale = 1.0 + float(cascadeIndex) * 0.2;
    float bias = (baseBias + slopeBias) * cascadeScale;
 
    
   
    float shadow = 0.0;
    vec2 texelSize = 1.0 / shadowMapSize;
    float diskRadius = debugDiskRadius;
    
    for(int i = 0; i < 16; ++i)
    {
        vec2 offset = poissonDisk[i] * texelSize * diskRadius;
        float pcfDepth = SampleShadowMap(cascadeIndex, projCoords.xy + offset);
        shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;
    }
    shadow /= 16.0;
    
    return shadow;
}

float ShadowCalculationPCF(int cascadeIndex, vec4 fragPosLightSpace)
{

    //melhores valores
     float debugBaseBias = 0.0004f;
     float debugSlopeBias = 0.00015f;
    
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    if(projCoords.z > 1.0)
        return 0.0;
    
    if(projCoords.x < 0.0 || projCoords.x > 1.0 || 
       projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;
    
    float currentDepth = projCoords.z;
    
    vec3 normal = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float NdotL = max(dot(normal, lightDir), 0.0);
    
    // Bias otimizado para superfícies planas e inclinadas
    float baseBias = debugBaseBias;
    float slopeBias = debugSlopeBias * (1.0 - NdotL);
    
  
    float cascadeScale = 1.0 + float(cascadeIndex) * 0.2;  //  Era 0.3
    float bias = (baseBias + slopeBias) * cascadeScale;
    
 
    
    // PCF com kernel maior para suavizar
    float shadow = 0.0;
    vec2 texelSize = 1.0 / shadowMapSize;
    int pcfCount = 2;  //  Aumentar para 3 (kernel 7x7)

    

    int numSamples = (pcfCount * 2 + 1) * (pcfCount * 2 + 1);
    
    for(int x = -pcfCount; x <= pcfCount; ++x)
    {
        for(int y = -pcfCount; y <= pcfCount; ++y)
        {
            float pcfDepth = SampleShadowMap(cascadeIndex, projCoords.xy + vec2(x, y) * texelSize); 
            shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;        
        }    
    }
    shadow /= float(numSamples);
    
    return shadow;
}

float ShadowCalculation(int cascadeIndex, vec4 fragPosLightSpace)
{

    if(cascadeIndex <= 1) 
    {
        return ShadowCalculationDiskPCF(cascadeIndex, fragPosLightSpace);
    }
    // Cascatas distantes (2,3): PCF Grid (mais suave, menos se nota)
    else 
    {
        return ShadowCalculationPCF(cascadeIndex, fragPosLightSpace);
    }

   //return ShadowCalculationDiskPCF(cascadeIndex, fragPosLightSpace);
   // return ShadowCalculationPCF(cascadeIndex, fragPosLightSpace);
}

vec3 GetCascadeColor(int cascadeIndex)
{
    if(cascadeIndex == 0)
        return vec3(1.0, 0.0, 0.0); // Vermelho
    else if(cascadeIndex == 1)
        return vec3(0.0, 1.0, 0.0); // Verde
    else if(cascadeIndex == 2)
        return vec3(0.0, 0.0, 1.0); // Azul
    else
        if (cascadeIndex == 3)
            return vec3(1.0, 1.0, 0.0); // Amarelo
    return vec3(1.0, 1.0, 1.0);
}

void main()
{           
    vec3 color = texture(diffuseTexture, TexCoords).rgb;
    vec3 normal = normalize(Normal);
    vec3 lightColor = vec3(1.0);
    
    // Ambient
    vec3 ambient = 0.15 * lightColor;
    
    // Diffuse
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(lightDir, normal), 0.0);
    vec3 diffuse = diff * lightColor;
    
    // Specular
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);  
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 64.0);
    vec3 specular = spec * lightColor;    
    
    // Selecionar cascata baseado na profundidade
    float depthValue = abs(FragPosViewSpace.z);
    int cascadeIndex = -1;
    
    for(int i = 0; i < cascadeCount; ++i)
    {
        if(depthValue < cascadePlaneDistances[i])
        {
            cascadeIndex = i;
            break;
        }
    }

    if(cascadeIndex == -1)
    {
        cascadeIndex = cascadeCount - 1;
    }
        
    // Calcular sombra
   // vec3 offsetFragPos = FragPos + normal* 0.0015;  //  Offset pela normal
   // vec4 fragPosLightSpace = lightSpaceMatrices[cascadeIndex] * vec4(offsetFragPos, 1.0);
   // float shadow = ShadowCalculation(cascadeIndex, fragPosLightSpace);



    float shadow = 0.0;
    float blendThreshold = 0.85;  // Começar blend nos últimos 10%
    
    if(cascadeIndex < cascadeCount - 1)
    {
        float cascadeDistance = cascadePlaneDistances[cascadeIndex];
        float blendRatio = smoothstep(
            cascadeDistance * blendThreshold,
            cascadeDistance,
            depthValue
        );
        
        if(blendRatio > 0.001)  // Só calcular se blend necessário
        {
            vec3 offsetFragPos = FragPos + normalize(Normal) *  0.0015;
            vec4 fragPosLight1 = lightSpaceMatrices[cascadeIndex] * vec4(offsetFragPos, 1.0);
            float shadow1 = ShadowCalculation(cascadeIndex, fragPosLight1);
            
            vec4 fragPosLight2 = lightSpaceMatrices[cascadeIndex + 1] * vec4(offsetFragPos, 1.0);
            float shadow2 = ShadowCalculation(cascadeIndex + 1, fragPosLight2);
            
            // Interpolar
            shadow = mix(shadow1, shadow2, blendRatio);
        }
        else
        {
            vec3 offsetFragPos = FragPos + normalize(Normal) *  0.0015;
            vec4 fragPosLightSpace = lightSpaceMatrices[cascadeIndex] * vec4(offsetFragPos, 1.0);
            shadow = ShadowCalculation(cascadeIndex, fragPosLightSpace);
        }
    }
    else
    {
        // Última cascata - sem blend
        vec3 offsetFragPos = FragPos + normalize(Normal) *  0.0015;
        vec4 fragPosLightSpace = lightSpaceMatrices[cascadeIndex] * vec4(offsetFragPos, 1.0);
        shadow = ShadowCalculation(cascadeIndex, fragPosLightSpace);
    }

    
    vec3 lighting = (ambient + (1.0 - shadow) * (diffuse + specular)) * color;
    
    // Visualização de cascatas (debug)
    if(showCascades)
    {
        lighting *= GetCascadeColor(cascadeIndex);
    }
    
    FragColor = vec4(lighting, 1.0);
}