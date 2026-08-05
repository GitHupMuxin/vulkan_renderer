#pragma once
/**
 * Vulkan glTF model and texture loading class based on tinyglTF (https://github.com/syoyo/tinygltf)
 *
 * Copyright (C) 2018-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <iostream>
#include <stdlib.h>
#include <string>
#include <fstream>
#include <vector>

#include "vulkan/vulkan.h"
#include "engine/core/device.h" 
#include "engine/core/buffer.h"
#include "engine/resource/texture.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <gli/gli.hpp>
#include <glm/gtx/string_cast.hpp>

// ERROR is already defined in wingdi.h and collides with a define in the Draco headers
#if defined(_WIN32) && defined(ERROR) && defined(TINYGLTF_ENABLE_DRACO) 
#undef ERROR
#pragma message ("ERROR constant already defined, undefining")
#endif

// #if defined(__ANDROID__)
// #define TINYGLTF_ANDROID_LOAD_FROM_ASSETS
// #include <android/asset_manager.h>
// #endif

// Changing this value here also requires changing it in the vertex shader
#define MAX_NUM_JOINTS 128u

namespace engine::resource
{
	struct Node;

	struct BoundingBox 
	{
		glm::vec3 		min;
		glm::vec3 		max;
		bool 			valid = false;
		BoundingBox();
		BoundingBox(glm::vec3 min, glm::vec3 max);
		BoundingBox 	GetAABB(glm::mat4 m);
	};

	struct Material 
	{		
		enum AlphaMode{ ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };
		AlphaMode 		alphaMode = ALPHAMODE_OPAQUE;
		float 			alphaCutoff = 1.0f;
		float 			metallicFactor = 1.0f;
		float 			roughnessFactor = 1.0f;
		glm::vec4 		baseColorFactor = glm::vec4(1.0f);
		glm::vec4 		emissiveFactor = glm::vec4(0.0f);
		Texture*		baseColorTexture;
		Texture*		metallicRoughnessTexture;
		Texture*		normalTexture;
		Texture*		occlusionTexture;
		Texture*		emissiveTexture;
		bool 			doubleSided = false;
		struct TexCoordSets 
		{
			uint8_t baseColor = 0;
			uint8_t metallicRoughness = 0;
			uint8_t specularGlossiness = 0;
			uint8_t normal = 0;
			uint8_t occlusion = 0;
			uint8_t emissive = 0;
		};
		TexCoordSets 	texCoordSets;

		struct Extension 
		{
			Texture *specularGlossinessTexture;
			Texture *diffuseTexture;
			glm::vec4 diffuseFactor = glm::vec4(1.0f);
			glm::vec3 specularFactor = glm::vec3(0.0f);
		};
		Extension 		extension;

		struct PbrWorkflows 
		{
			bool metallicRoughness = true;
			bool specularGlossiness = false;
		};
		PbrWorkflows 	pbrWorkflows;

		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		int 			index = 0;
		bool 			unlit = false;
		float 			emissiveStrength = 1.0f;
	};

	struct Primitive 
	{
		uint32_t 		firstIndex;
		uint32_t 		indexCount;
		uint32_t 		vertexCount;
		Material&		material;
		bool 			hasIndices;
		BoundingBox 	bb;
		Primitive(uint32_t firstIndex, uint32_t indexCount, uint32_t vertexCount, Material& material);
		void 			SetBoundingBox(glm::vec3 min, glm::vec3 max);
	};

	struct Mesh 
	{
		std::vector<Primitive*> primitives;
		BoundingBox 			bb;
		BoundingBox 			aabb;
		glm::mat4 				matrix;
		glm::mat4 				jointMatrix[MAX_NUM_JOINTS]{};
		uint32_t 				jointcount{ 0 };
		uint32_t 				index;
		Mesh(glm::mat4 matrix);
		~Mesh();
		void 					SetBoundingBox(glm::vec3 min, glm::vec3 max);
	};

	struct Skin 
	{
		std::string 			name;
		Node*					skeletonRoot = nullptr;
		std::vector<glm::mat4> 	inverseBindMatrices;
		std::vector<Node*> 		joints;
	};

	struct Node 
	{
		Node*				parent;
		uint32_t 			index;
		std::vector<Node*> 	children;
		glm::mat4 			matrix;
		std::string 		name;
		Mesh *mesh;
		Skin *skin;
		int32_t 			skinIndex = -1;
		glm::vec3 			translation{};
		glm::vec3 			scale{ 1.0f };
		glm::quat 			rotation{};
		BoundingBox 		bvh;
		BoundingBox 		aabb;
		bool 				useCachedMatrix{ false };
		glm::mat4 			cachedLocalMatrix{ glm::mat4(1.0f) };
		glm::mat4 			cachedMatrix{ glm::mat4(1.0f) };
		glm::mat4 			LocalMatrix();
		glm::mat4 			GetMatrix();
		void 				Update();
		~Node();
	};

	struct AnimationChannel 
	{
		enum PathType { TRANSLATION, ROTATION, SCALE };
		PathType 		path;
		Node*				node;
		uint32_t 			samplerIndex;
	};

	struct AnimationSampler 
	{
		enum InterpolationType { LINEAR, STEP, CUBICSPLINE };
		InterpolationType 		interpolation;
		std::vector<float> 		inputs;
		std::vector<glm::vec4> 	outputsVec4;
		std::vector<float> 		outputs;

		glm::vec4 				CubicSplineInterpolation(size_t index, float time, uint32_t stride);
		void 					Translate(size_t index, float time, Node* node);
		void 					Scale(size_t index, float time, Node* node);
		void 					Rotate(size_t index, float time, Node* node);
	};

	struct Animation 
	{
		std::string 					name;
		std::vector<AnimationSampler> 	samplers;
		std::vector<AnimationChannel> 	channels;
		float 							start = std::numeric_limits<float>::max();
		float 							end = std::numeric_limits<float>::min();
	};

	class Model
	{
		public:
			// Vertex layout shared by all model formats; referenced by the render passes
			struct Vertex 
			{
				glm::vec3 		pos;
				glm::vec3 		normal;
				glm::vec2 		uv0;
				glm::vec2 		uv1;
				glm::uvec4 		joint0;
				glm::vec4 		weight0;
				glm::vec4 		color;
			};

			virtual ~Model() = default;

			virtual uint32_t 							GetMaterialCount() = 0;
			virtual uint32_t 							GetMeshCount() = 0;
			virtual void 								LoadFromFile(std::string filename, float scale = 1.0) = 0;
			virtual std::unique_ptr<Model> 				Clone() = 0;
			virtual void 								Draw(VkCommandBuffer commandBuffer) = 0;
			virtual glm::mat4 							GetAABBBox() = 0;
			virtual std::vector<core::Buffer>& 			GetMeshShaderBuffer() = 0;
			virtual core::Buffer& 						GetMaterialShaderBuffer() = 0;
			virtual std::vector<resource::Material>& 	GetMaterialArray() = 0;
			virtual std::vector<VkDescriptorSet>& 		GetDescriptorSetsMeshData() = 0;
			// Descriptor set that exposes this model's material SSBO (set=3)
			virtual VkDescriptorSet& 					GetDescriptorSetMaterial() = 0;

			// Render-layer interface (VBO/IBO + scene graph + animation)
			virtual VkBuffer 							GetVertexBuffer() = 0;
			virtual VkBuffer 							GetIndexBuffer() = 0;
			virtual std::vector<Node*>& 				GetNodes() = 0;
			virtual std::vector<Node*>& 				GetLinearNodes() = 0;
			virtual std::vector<Animation>& 			GetAnimations() = 0;
			virtual void 								UpdateAnimation(uint32_t index, float time) = 0;
			virtual void 								UpdateMeshDataBuffer(uint32_t index) = 0;
			virtual void 								CreateMaterialBuffer() = 0;
			virtual void 								CreateMeshDataBuffer() = 0;
	};

	class GLTFModel : public Model 
	{
		public:
			// List of glTF extensions supported by this application
    		// Models with un-supported extensions may not work/look as expected
    		static const std::vector<std::string> 	supportedExtensions; 

    		enum PBRWorkflows{ PBR_WORKFLOW_METALLIC_ROUGHNESS = 0, PBR_WORKFLOW_SPECULAR_GLOSSINESS = 1 };

			struct alignas(16) ShaderMaterial 
			{
				glm::vec4 	baseColorFactor;
				glm::vec4 	emissiveFactor;
				glm::vec4 	diffuseFactor;
				glm::vec4 	specularFactor;
				float 		workflow;
				int 		colorTextureSet;
				int 		PhysicalDescriptorTextureSet;
				int 		normalTextureSet;
				int 		occlusionTextureSet;
				int 		emissiveTextureSet;
				float 		metallicFactor;
				float 		roughnessFactor;
				float 		alphaMask;
				float 		alphaMaskCutoff;
				float 		emissiveStrength;
			};
			engine::core::Buffer 					shaderMaterialBuffer_;

			struct alignas(16) ShaderMeshData 
			{
				glm::mat4 	matrix;
				glm::mat4 	jointMatrix[MAX_NUM_JOINTS]{};
				uint32_t 	jointcount{ 0 };
			};
			std::vector<engine::core::Buffer> 		shaderMeshDataBuffers_;
			std::vector<VkDescriptorSet> 			descriptorSetsMeshData_;
			VkDescriptorSet 						descriptorSetMaterial_ = VK_NULL_HANDLE;

			struct Vertices 
			{
				VkBuffer 		buffer = VK_NULL_HANDLE;
				VkDeviceMemory	memory;
			};
			Vertices 						vertices_;

			struct Indices 
			{
				VkBuffer 		buffer = VK_NULL_HANDLE;
				VkDeviceMemory 	memory;
			};
			Indices							indices_;

			glm::mat4 						aabb_;

			std::vector<Node*> 				nodes_;
			std::vector<Node*> 				linearNodes_;

			std::vector<Skin*> 				skins_;

			std::vector<Texture> 			textures_;
			std::vector<TextureSampler> 	textureSamplers_;
			std::vector<Material> 			materials_;
			std::vector<Animation> 			animations_;
			std::vector<std::string> 		extensions_;

			struct Dimensions 
			{
				glm::vec3 min = glm::vec3(FLT_MAX);
				glm::vec3 max = glm::vec3(-FLT_MAX);
			};
			Dimensions 						dimensions_;

			struct LoaderInfo 
			{
				uint32_t* 	indexBuffer;
				Vertex* 	vertexBuffer;
				size_t 		indexPos = 0;
				size_t 		vertexPos = 0;
			};

			std::string 					filePath_;

			~GLTFModel();
			void 								Destroy();
			void 								LoadNode(Node* parent, const tinygltf::Node& node, uint32_t nodeIndex, const tinygltf::Model& model, LoaderInfo& loaderInfo, float globalscale);
			void 								GetNodeProps(const tinygltf::Node& node, const tinygltf::Model& model, size_t& vertexCount, size_t& indexCount);
			void 								LoadSkins(tinygltf::Model& gltfModel);
			void 								LoadTextures(tinygltf::Model& gltfModel);
			VkSamplerAddressMode 				GetVkWrapMode(int32_t wrapMode);
			VkFilter 							GetVkFilterMode(int32_t filterMode);
			void 								LoadTextureSamplers(tinygltf::Model& gltfModel);
			void 								LoadMaterials(tinygltf::Model& gltfModel);
			void 								LoadAnimations(tinygltf::Model& gltfModel);
			void 								LoadFromFile(std::string filename, float scale = 1.0) override;
			void 								DrawNode(Node* node, VkCommandBuffer commandBuffer);
			void 								Draw(VkCommandBuffer commandBuffer) override;
			void 								CalculateBoundingBox(Node* node, Node* parent);
			void 								GetSceneDimensions();
			void 								UpdateAnimation(uint32_t index, float time) override;
			Node* 								FindNode(Node* parent, uint32_t index);
			Node* 								NodeFromIndex(uint32_t index);
			void 								CreateMaterialBuffer() override;
			void 								CreateMeshDataBuffer() override;
			void 								UpdateMeshDataBuffer(uint32_t index) override;
			std::unique_ptr<Model> 				Clone() override;
			glm::mat4 							GetAABBBox() override;
			uint32_t 							GetMaterialCount() override;
			uint32_t 							GetMeshCount() override;
			core::Buffer& 						GetMaterialShaderBuffer() override;
			std::vector<core::Buffer>& 			GetMeshShaderBuffer() override;
			std::vector<VkDescriptorSet>& 		GetDescriptorSetsMeshData() override;
			VkDescriptorSet& 					GetDescriptorSetMaterial() override;
			
			std::vector<resource::Material>& 	GetMaterialArray() override;

			VkBuffer 							GetVertexBuffer() override;
			VkBuffer 							GetIndexBuffer() override;
			std::vector<Node*>& 				GetNodes() override;
			std::vector<Node*>& 				GetLinearNodes() override;
			std::vector<Animation>& 			GetAnimations() override;
	};
}

