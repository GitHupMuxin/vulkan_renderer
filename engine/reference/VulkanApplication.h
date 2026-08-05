/*
 * Vulkan physical based rendering glTF 2.0 renderer
 *
 * Copyright (C) 2018-2025 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

// glTF format: https://github.com/KhronosGroup/glTF
// tinyglTF loader: https://github.com/syoyo/tinygltf

#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <chrono>
#include <map>
#include <unordered_map>
#include <gli/gli.hpp>
#include <gli/save_ktx.hpp>
#include "algorithm"

#include <vulkan/vulkan.h>
#include "engine/reference/VulkanExampleBase.h"
#include "engine/reference/VulkanTexture.hpp"
#include "engine/reference/VulkanglTFModel.h"
#include "engine/reference/VulkanUtils.hpp"
#include "engine/reference/macros.h"
#include "app/ui/ui.hpp"
#include "vulkan/vulkan_core.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace engine::core
{
    /*
	    PBR example main class
    */
    class VulkanApplication : public VulkanExampleBase
    {
    public:
	    struct Textures {
		    resource::VulkanTextureCubeMap environmentCube;
    		resource::VulkanTexture2D empty;
    		resource::VulkanTexture2D lutBrdf;
    		resource::VulkanTexture2D Eu;
    		resource::VulkanTexture2D Eavg;
		    resource::VulkanTextureCubeMap irradianceCube;
    		resource::VulkanTextureCubeMap prefilteredCube;
	    } textures;

    	struct Models {
		    resource::vkglTF::Model scene;
		    resource::vkglTF::Model skybox;
	    } models;

    	struct UniformBufferSet {
		    Buffer scene;
		    Buffer skybox;
		    Buffer params;
	    };

    	struct UBOMatrices {
		    glm::mat4 projection{ 1.0f };
		    glm::mat4 model{ 1.0f };
		    glm::mat4 view{ 1.0f };
		    glm::vec3 camPos{ 0.0f };
	    } shaderValuesScene, shaderValuesSkybox;

    	struct shaderValuesParams {
    		glm::vec4 lightDir;
    		float exposure = 4.5f;
    		float gamma = 2.2f;
    		float prefilteredCubeMipLevels;
    		float scaleIBLAmbient = 1.0f;
    		float debugViewInputs = 0;
    		float debugViewEquation = 0;
    		float debugBsdfType = 0;
    	} shaderValuesParams;

    	VkPipelineLayout pipelineLayout_{ VK_NULL_HANDLE };

    	std::unordered_map<std::string, VkPipeline> pipelines;
    	VkPipeline boundPipeline{ VK_NULL_HANDLE };

    	struct DescriptorSetLayouts {
    		VkDescriptorSetLayout scene{ VK_NULL_HANDLE };
    		VkDescriptorSetLayout material{ VK_NULL_HANDLE };
    		VkDescriptorSetLayout materialBuffer{ VK_NULL_HANDLE };
    		VkDescriptorSetLayout meshDataBuffer{ VK_NULL_HANDLE };
    	} descriptorSetLayouts;

    	struct DescriptorSets {
    		VkDescriptorSet scene;
    		VkDescriptorSet skybox;
    	};
	    std::vector<DescriptorSets> descriptorSets;
	
    	std::vector<VkCommandBuffer> commandBuffers;
    	std::vector<UniformBufferSet> uniformBuffers;

    	std::vector<VkFence> waitFences;
    	std::vector<VkSemaphore> renderCompleteSemaphores;
    	std::vector<VkSemaphore> presentCompleteSemaphores;

    	const uint32_t renderAhead = 2;
    	uint32_t frameIndex = 0;

    	int32_t animationIndex = 0;
    	float animationTimer = 0.0f;
    	bool animate = true;

    	bool displayBackground = true;
	
    	struct LightSource {
    		glm::vec3 color = glm::vec3(1.0f);
    		glm::vec3 rotation = glm::vec3(75.0f, -40.0f, 0.0f);
    	} lightSource;

    	UI* ui{ nullptr };

    #if defined(VK_USE_PLATFORM_ANDROID_KHR)
    	const std::string assetpath = "";
    #else
    	const std::string assetpath = VK_EXAMPLE_DATA_DIR;
    #endif

    	enum PBRWorkflows{ PBR_WORKFLOW_METALLIC_ROUGHNESS = 0, PBR_WORKFLOW_SPECULAR_GLOSSINESS = 1 };

    	// We use a material buffer to pass material data ind image indices to the shader
    	struct alignas(16) ShaderMaterial {
    		glm::vec4 baseColorFactor;
    		glm::vec4 emissiveFactor;
    		glm::vec4 diffuseFactor;
    		glm::vec4 specularFactor;
    		float workflow;
    		int colorTextureSet;
    		int PhysicalDescriptorTextureSet;
    		int normalTextureSet;
    		int occlusionTextureSet;
    		int emissiveTextureSet;
    		float metallicFactor;
        	float roughnessFactor;
    		float alphaMask;
     		float alphaMaskCutoff;
    		float emissiveStrength;
    	};
    	Buffer shaderMaterialBuffer;
    	VkDescriptorSet descriptorSetMaterials{ VK_NULL_HANDLE };

    	struct MeshPushConstantBlock {
    		int32_t meshIndex;
    		int32_t materialIndex;
    	};

    	// We use a large buffer to store all per mesh data that needs to be passed to the shader
    	struct alignas(16) ShaderMeshData {
    		glm::mat4 matrix;
    		glm::mat4 jointMatrix[MAX_NUM_JOINTS]{};
    		uint32_t jointcount{ 0 };
    	};
        std::vector<Buffer> shaderMeshDataBuffers;
    	std::vector<VkDescriptorSet> descriptorSetsMeshData;

    	std::map<std::string, std::string> environments;
    	std::string selectedEnvironment = "papermill";

    #if !defined(_WIN32)
    	std::map<std::string, std::string> scenes;
    	std::string selectedScene = "DamagedHelmet";
    #endif

    	int32_t debugViewInputs = 0;
    	int32_t debugViewEquation = 0;
    	int32_t debugBsdfType = 0;

    	// List of glTF extensions supported by this application
    	// Models with un-supported extensions may not work/look as expected
    	const std::vector<std::string> supportedExtensions = {
    		"KHR_texture_basisu",
    		"KHR_materials_pbrSpecularGlossiness",
    		"KHR_materials_unlit",
    		"KHR_materials_emissive_strength"
    	};

    	VulkanApplication(); 

    	virtual ~VulkanApplication();


        void resetCamera(); 

        void renderNode(resource::vkglTF::Node *node, uint32_t cbIndex, resource::vkglTF::Material::AlphaMode alphaMode);


        void recordCommandBuffer();

        // We place all materials for the current scene into a shader storage buffer stored on the GPU
        // This allows us to use arbitrary large material defintions
        // The fragment shader then get's the index into this material array from a push constant set per primitive
        void createMaterialBuffer();

        // We place all the shader data blocks for all meshes (node) into a single buffer 
        // This allows us to use one singular allocation instead of having to do lots of small allocations per mesh
        // The vertex shader then get's the index into this buffer from a push constant set per mesh
        // @todo: Needs to be adjusted to work with frames-in-flight (duplicate buffer)
        // @todo: Update
        void createMeshDataBuffer();
        

        void updateMeshDataBuffer(uint32_t index);

        void loadScene(std::string filename);
        

        void loadEnvironment(std::string filename);
        void loadAssets();
        void setupDescriptors();
            // Depending on material setting, we need different pipeline variants per set, e.g. one with back-face culling, one without and one with alpha-blending enabled. This function generates such a set.
        void addPipelineSet(const std::string prefix, const std::string vertexShader, const std::string fragmentShader);


        void preparePipelines();

        /*
            Generate a BRDF integration map storing roughness/NdotV as a look-up-table
        */
        void generateBRDFLUT();
        void generateE_uMap();
    	void generateE_avgMap();
	
    	/*
    		Offline generation for the cube maps used for PBR lighting		
    		- Irradiance cube map
    		- Pre-filterd environment cubemap
    	*/
    	void generateCubemaps();
    	/* 
    		Prepare and initialize uniform buffers containing shader parameters
    	*/
    	void prepareUniformBuffers();
	

    	void updateUniformData();
    	void updateParams();
	
	    void windowResized();
	    void prepare();
	
    	/*
    		Update ImGui user interface
    	*/
    	void updateOverlay();
	

    	virtual void render();


    	virtual void fileDropped(std::string filename);

    };
}



