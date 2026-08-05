/**
 * Vulkan glTF model and texture loading class based on tinyglTF (https://github.com/syoyo/tinygltf)
 *
 * Copyright (C) 2018-2024 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#if defined(__ANDROID__)
#define TINYGLTF_ANDROID_LOAD_FROM_ASSETS
#endif
#define STBI_MSC_SECURE_CRT

#include "engine/utils/log.h"
#include "engine/resource/model.h"

namespace engine::resource
{
	// We use a custom image loading function with tinyglTF, so we can do custom stuff loading ktx textures
	bool loadImageDataFunc(tinygltf::Image* image, const int imageIndex, std::string* error, std::string* warning, int req_width, int req_height, const unsigned char* bytes, int size, void* userData)
	{
		// KTX files will be handled by our own code
		if (image->uri.find_last_of(".") != std::string::npos) {
			if (image->uri.substr(image->uri.find_last_of(".") + 1) == "ktx2") {
				return true;
			}
		}

		return tinygltf::LoadImageData(image, imageIndex, error, warning, req_width, req_height, bytes, size, userData);
	}

	// Bounding box

	BoundingBox::BoundingBox() {
	};

	BoundingBox::BoundingBox(glm::vec3 min, glm::vec3 max) : min(min), max(max) {
	};

	BoundingBox BoundingBox::GetAABB(glm::mat4 m) {
		glm::vec3 min = glm::vec3(m[3]);
		glm::vec3 max = min;
		glm::vec3 v0, v1;
			
		glm::vec3 right = glm::vec3(m[0]);
		v0 = right * this->min.x;
		v1 = right * this->max.x;
		min += glm::min(v0, v1);
		max += glm::max(v0, v1);

		glm::vec3 up = glm::vec3(m[1]);
		v0 = up * this->min.y;
		v1 = up * this->max.y;
		min += glm::min(v0, v1);
		max += glm::max(v0, v1);

		glm::vec3 back = glm::vec3(m[2]);
		v0 = back * this->min.z;
		v1 = back * this->max.z;
		min += glm::min(v0, v1);
		max += glm::max(v0, v1);

		return BoundingBox(min, max);
	}


	// Primitive
	Primitive::Primitive(uint32_t firstIndex, uint32_t indexCount, uint32_t vertexCount, Material &material) : firstIndex(firstIndex), indexCount(indexCount), vertexCount(vertexCount), material(material) {
		this->hasIndices = indexCount > 0;
	};

	void Primitive::SetBoundingBox(glm::vec3 min, glm::vec3 max) {
		this->bb.min = min;
		this->bb.max = max;
		this->bb.valid = true;
	}

	// Mesh
	// @todo: create large SSBO instead of many small uniform buffers
	Mesh::Mesh(glm::mat4 matrix) {
		this->matrix = matrix;
	};

	Mesh::~Mesh() {
		for (Primitive* p : primitives)
			delete p;
	}

	void Mesh::SetBoundingBox(glm::vec3 min, glm::vec3 max) {
		this->bb.min = min;
		this->bb.max = max;
		this->bb.valid = true;
	}

	// Node
	glm::mat4 Node::LocalMatrix() {
		if (!this->useCachedMatrix) {
			this->cachedLocalMatrix = glm::translate(glm::mat4(1.0f), this->translation) * glm::mat4(this->rotation) * glm::scale(glm::mat4(1.0f), this->scale) * this->matrix;
		};
		return this->cachedLocalMatrix;
	}

	glm::mat4 Node::GetMatrix() {
		// Use a simple caching algorithm to avoid having to recalculate matrices to often while traversing the node hierarchy
		if (!this->useCachedMatrix) {
			glm::mat4 m = LocalMatrix();
			Node* p = parent;
			while (p) {
				m = p->LocalMatrix() * m;
				p = p->parent;
			}
			this->cachedMatrix = m;
			this->useCachedMatrix = true;
			return m;
		} else {
			return this->cachedMatrix;
		}
	}

	void Node::Update() {
		this->useCachedMatrix = false;
		if (this->mesh) {
			glm::mat4 m = GetMatrix();
			if (this->skin) {
				this->mesh->matrix = m;
				// Update join matrices
				glm::mat4 inverseTransform = glm::inverse(m);
				size_t numJoints = std::min((uint32_t)this->skin->joints.size(), MAX_NUM_JOINTS);
				for (size_t i = 0; i < numJoints; i++) {
					Node *jointNode = this->skin->joints[i];
					glm::mat4 jointMat = jointNode->GetMatrix() * this->skin->inverseBindMatrices[i];
					jointMat = inverseTransform * jointMat;
					this->mesh->jointMatrix[i] = jointMat;
				}
				this->mesh->jointcount = static_cast<uint32_t>(numJoints);
				/*
				// @todo: Move buffer copy to main.cpp (decouple)
				memcpy(mesh->uniformBuffer.mapped, &mesh->uniformBlock, sizeof(mesh->uniformBlock));
				*/
			} else {
				this->mesh->matrix = m;
				// memcpy(mesh->uniformBuffer.mapped, &m, sizeof(glm::mat4));
			}
		}

		for (auto& child : this->children) {
			child->Update();
		}
	}

	Node::~Node() {
		if (this->mesh) {
			delete this->mesh;
		}
		for (auto& child : this->children) {
			delete child;
		}
	}

	// AnimationSampler
	
	// Cube spline interpolation function used for translate/scale/rotate with cubic spline animation samples
	// Details on how this works can be found in the specs https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#appendix-c-spline-interpolation
	glm::vec4 AnimationSampler::CubicSplineInterpolation(size_t index, float time, uint32_t stride) {
		float delta = this->inputs[index + 1] - this->inputs[index];
		float t = (time - this->inputs[index]) / delta;
		const size_t current = index * stride * 3;
		const size_t next = (index + 1) * stride * 3;
		const size_t A = 0;
		const size_t V = stride * 1;
		const size_t B = stride * 2;

		float t2 = powf(t, 2);
		float t3 = powf(t, 3);
		glm::vec4 pt{ 0.0f };
		for (uint32_t i = 0; i < stride; i++) {
			float p0 = this->outputs[current + i + V];			// starting point at t = 0
			float m0 = delta * this->outputs[current + i + A];	// scaled starting tangent at t = 0
			float p1 = this->outputs[next + i + V];				// ending point at t = 1
			float m1 = delta * this->outputs[next + i + B];		// scaled ending tangent at t = 1
			pt[i] = ((2.f * t3 - 3.f * t2 + 1.f) * p0) + ((t3 - 2.f * t2 + t) * m0) + ((-2.f * t3 + 3.f * t2) * p1) + ((t3 - t2) * m0);
		}
		return pt;
	}

	// Calculates the translation of this sampler for the given node at a given time point depending on the interpolation type
	void AnimationSampler::Translate(size_t index, float time, Node* node) {
		switch (this->interpolation) {
		case AnimationSampler::InterpolationType::LINEAR: {
			float u = std::max(0.0f, time - this->inputs[index]) / (this->inputs[index + 1] - this->inputs[index]);
			node->translation = glm::vec3(glm::mix(outputsVec4[index], outputsVec4[index + 1], u));
			break;
		}
		case AnimationSampler::InterpolationType::STEP: {
			node->translation = glm::vec3(this->outputsVec4[index]);
			break;
		}
		case AnimationSampler::InterpolationType::CUBICSPLINE: {
			node->translation = glm::vec3(CubicSplineInterpolation(index, time, 3));
			break;
		}
		}
	}

	// Calculates the scale of this sampler for the given node at a given time point depending on the interpolation type
	void AnimationSampler::Scale(size_t index, float time, Node* node) {
		switch (this->interpolation) {
		case AnimationSampler::InterpolationType::LINEAR: {
			float u = std::max(0.0f, time - this->inputs[index]) / (this->inputs[index + 1] - this->inputs[index]);
			node->scale = glm::vec3(glm::mix(this->outputsVec4[index], this->outputsVec4[index + 1], u));
			break;
		}
		case AnimationSampler::InterpolationType::STEP: {
			node->scale = glm::vec3(this->outputsVec4[index]);
			break;
		}
		case AnimationSampler::InterpolationType::CUBICSPLINE: {
			node->scale = glm::vec3(this->CubicSplineInterpolation(index, time, 3));
			break;
		}
		}
	}

	// Calculates the rotation of this sampler for the given node at a given time point depending on the interpolation type
	void AnimationSampler::Rotate(size_t index, float time, Node* node) {
		switch (this->interpolation) {
		case AnimationSampler::InterpolationType::LINEAR: {
			float u = std::max(0.0f, time - this->inputs[index]) / (this->inputs[index + 1] - this->inputs[index]);
			glm::quat q1;
			q1.x = this->outputsVec4[index].x;
			q1.y = this->outputsVec4[index].y;
			q1.z = this->outputsVec4[index].z;
			q1.w = this->outputsVec4[index].w;
			glm::quat q2;
			q2.x = this->outputsVec4[index + 1].x;
			q2.y = this->outputsVec4[index + 1].y;
			q2.z = this->outputsVec4[index + 1].z;
			q2.w = this->outputsVec4[index + 1].w;
			node->rotation = glm::normalize(glm::slerp(q1, q2, u));
			break;
		}
		case AnimationSampler::InterpolationType::STEP: {
			glm::quat q1;
			q1.x = this->outputsVec4[index].x;
			q1.y = this->outputsVec4[index].y;
			q1.z = this->outputsVec4[index].z;
			q1.w = this->outputsVec4[index].w;
			node->rotation = q1;
			break;
		}
		case AnimationSampler::InterpolationType::CUBICSPLINE: {
			glm::vec4 rot = this->CubicSplineInterpolation(index, time, 4);
			glm::quat q;
			q.x = rot.x;
			q.y = rot.y;
			q.z = rot.z;
			q.w = rot.w;
			node->rotation = glm::normalize(q);
			break;
		}
		}
	}

	const std::vector<std::string> GLTFModel::supportedExtensions = {
    		"KHR_texture_basisu",
    		"KHR_materials_pbrSpecularGlossiness",
    		"KHR_materials_unlit",
    		"KHR_materials_emissive_strength"
    };

	// Model
	GLTFModel::~GLTFModel()
	{
		this->Destroy();
	}

	void GLTFModel::Destroy()
	{
		auto& device = core::Device::Instance();
		if (this->vertices_.buffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(device.GetLogicalDeviceHandle(), this->vertices_.buffer, nullptr);
			vkFreeMemory(device.GetLogicalDeviceHandle(), this->vertices_.memory, nullptr);
			this->vertices_.buffer = VK_NULL_HANDLE;
		}
		if (this->indices_.buffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(device.GetLogicalDeviceHandle(), this->indices_.buffer, nullptr);
			vkFreeMemory(device.GetLogicalDeviceHandle(), this->indices_.memory, nullptr);
			this->indices_.buffer = VK_NULL_HANDLE;
		}
		for (auto& texture : this->textures_) {
			texture.Destroy();
		}
		this->textures_.resize(0);
		this->textureSamplers_.resize(0);
		for (auto node : this->nodes_) {
			delete node;
		}
		this->materials_.resize(0);
		this->animations_.resize(0);
		this->nodes_.resize(0);
		this->linearNodes_.resize(0);
		this->extensions_.resize(0);
		for (auto skin : this->skins_) {
			delete skin;
		}
		this->skins_.resize(0);
		this->shaderMaterialBuffer_.Destroy();
		for (auto& shaderMeshDataBuffer : this->shaderMeshDataBuffers_) {
			shaderMeshDataBuffer.Destroy();
		}
	};
	
	void GLTFModel::LoadNode(Node *parent, const tinygltf::Node &node, uint32_t nodeIndex, const tinygltf::Model &model, LoaderInfo& loaderInfo, float globalscale)
	{
		Node *newNode = new Node{};
		newNode->index = nodeIndex;
		newNode->parent = parent;
		newNode->name = node.name;
		newNode->skinIndex = node.skin;
		newNode->matrix = glm::mat4(1.0f);

		// Generate local node matrix
		glm::vec3 translation = glm::vec3(0.0f);
		if (node.translation.size() == 3) {
			translation = glm::make_vec3(node.translation.data());
			newNode->translation = translation;
		}
		glm::mat4 rotation = glm::mat4(1.0f);
		if (node.rotation.size() == 4) {
			glm::quat q = glm::quat(
				static_cast<float>(node.rotation[3]),
				static_cast<float>(node.rotation[0]),
				static_cast<float>(node.rotation[1]),
				static_cast<float>(node.rotation[2]));
			newNode->rotation = q;
		}
		glm::vec3 scale = glm::vec3(1.0f);
		if (node.scale.size() == 3) {
			scale = glm::make_vec3(node.scale.data());
			newNode->scale = scale;
		}
		if (node.matrix.size() == 16) {
			newNode->matrix = glm::make_mat4x4(node.matrix.data());
		};

		// Node with children
		if (node.children.size() > 0) {
			for (size_t i = 0; i < node.children.size(); i++) {
				LoadNode(newNode, model.nodes[node.children[i]], node.children[i], model, loaderInfo, globalscale);
			}
		}

		// Node contains mesh data
		if (node.mesh > -1) {
			const tinygltf::Mesh mesh = model.meshes[node.mesh];
			Mesh *newMesh = new Mesh(newNode->matrix);
			for (size_t j = 0; j < mesh.primitives.size(); j++) {
				const tinygltf::Primitive &primitive = mesh.primitives[j];
				uint32_t vertexStart = static_cast<uint32_t>(loaderInfo.vertexPos);
				uint32_t indexStart = static_cast<uint32_t>(loaderInfo.indexPos);
				uint32_t indexCount = 0;
				uint32_t vertexCount = 0;
				glm::vec3 posMin{};
				glm::vec3 posMax{};
				bool hasSkin = false;
				bool hasIndices = primitive.indices > -1;
				// Vertices
				{
					const float *bufferPos = nullptr;
					const float *bufferNormals = nullptr;
					const float *bufferTexCoordSet0 = nullptr;
					const float *bufferTexCoordSet1 = nullptr;
					const float* bufferColorSet0 = nullptr;
					const void *bufferJoints = nullptr;
					const float *bufferWeights = nullptr;

					int posByteStride;
					int normByteStride;
					int uv0ByteStride;
					int uv1ByteStride;
					int color0ByteStride;
					int jointByteStride;
					int weightByteStride;

					int jointComponentType;

					// Position attribute is required
					assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

					const tinygltf::Accessor &posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
					const tinygltf::BufferView &posView = model.bufferViews[posAccessor.bufferView];
					bufferPos = reinterpret_cast<const float *>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));
					posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2]);
					posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2]);
					vertexCount = static_cast<uint32_t>(posAccessor.count);
					posByteStride = posAccessor.ByteStride(posView) ? (posAccessor.ByteStride(posView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);

					if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
						const tinygltf::Accessor &normAccessor = model.accessors[primitive.attributes.find("NORMAL")->second];
						const tinygltf::BufferView &normView = model.bufferViews[normAccessor.bufferView];
						bufferNormals = reinterpret_cast<const float *>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
						normByteStride = normAccessor.ByteStride(normView) ? (normAccessor.ByteStride(normView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);
					}

					// UVs
					if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
						const tinygltf::Accessor &uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
						const tinygltf::BufferView &uvView = model.bufferViews[uvAccessor.bufferView];
						bufferTexCoordSet0 = reinterpret_cast<const float *>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
						uv0ByteStride = uvAccessor.ByteStride(uvView) ? (uvAccessor.ByteStride(uvView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
					}
					if (primitive.attributes.find("TEXCOORD_1") != primitive.attributes.end()) {
						const tinygltf::Accessor &uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_1")->second];
						const tinygltf::BufferView &uvView = model.bufferViews[uvAccessor.bufferView];
						bufferTexCoordSet1 = reinterpret_cast<const float *>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
						uv1ByteStride = uvAccessor.ByteStride(uvView) ? (uvAccessor.ByteStride(uvView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC2);
					}

					// Vertex colors
					if (primitive.attributes.find("COLOR_0") != primitive.attributes.end()) {
						const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.find("COLOR_0")->second];
						const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
						bufferColorSet0 = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
						color0ByteStride = accessor.ByteStride(view) ? (accessor.ByteStride(view) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC3);
					}

					// Skinning
					// Joints
					if (primitive.attributes.find("JOINTS_0") != primitive.attributes.end()) {
						const tinygltf::Accessor &jointAccessor = model.accessors[primitive.attributes.find("JOINTS_0")->second];
						const tinygltf::BufferView &jointView = model.bufferViews[jointAccessor.bufferView];
						bufferJoints = &(model.buffers[jointView.buffer].data[jointAccessor.byteOffset + jointView.byteOffset]);
						jointComponentType = jointAccessor.componentType;
						jointByteStride = jointAccessor.ByteStride(jointView) ? (jointAccessor.ByteStride(jointView) / tinygltf::GetComponentSizeInBytes(jointComponentType)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
					}

					if (primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end()) {
						const tinygltf::Accessor &weightAccessor = model.accessors[primitive.attributes.find("WEIGHTS_0")->second];
						const tinygltf::BufferView &weightView = model.bufferViews[weightAccessor.bufferView];
						bufferWeights = reinterpret_cast<const float *>(&(model.buffers[weightView.buffer].data[weightAccessor.byteOffset + weightView.byteOffset]));
						weightByteStride = weightAccessor.ByteStride(weightView) ? (weightAccessor.ByteStride(weightView) / sizeof(float)) : tinygltf::GetNumComponentsInType(TINYGLTF_TYPE_VEC4);
					}

					hasSkin = (bufferJoints && bufferWeights);

					for (size_t v = 0; v < posAccessor.count; v++) {
						Vertex& vert = loaderInfo.vertexBuffer[loaderInfo.vertexPos];
						vert.pos = glm::make_vec3(&bufferPos[v * posByteStride]);
						vert.normal = glm::normalize(glm::vec3(bufferNormals ? glm::make_vec3(&bufferNormals[v * normByteStride]) : glm::vec3(0.0f)));
						vert.uv0 = bufferTexCoordSet0 ? glm::make_vec2(&bufferTexCoordSet0[v * uv0ByteStride]) : glm::vec2(0.0f);
						vert.uv1 = bufferTexCoordSet1 ? glm::make_vec2(&bufferTexCoordSet1[v * uv1ByteStride]) : glm::vec2(0.0f);
						vert.color = bufferColorSet0 ? glm::make_vec4(&bufferColorSet0[v * color0ByteStride]) : glm::vec4(1.0f);

						if (hasSkin)
						{
							switch (jointComponentType) {
							case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
								const uint16_t *buf = static_cast<const uint16_t*>(bufferJoints);
								vert.joint0 = glm::uvec4(glm::make_vec4(&buf[v * jointByteStride]));
								break;
							}
							case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
								const uint8_t *buf = static_cast<const uint8_t*>(bufferJoints);
								vert.joint0 = glm::vec4(glm::make_vec4(&buf[v * jointByteStride]));
								break;
							}
							default:
								// Not supported by spec
								std::cerr << "Joint component type " << jointComponentType << " not supported!" << std::endl;
								break;
							}
						}
						else {
							vert.joint0 = glm::vec4(0.0f);
						}
						vert.weight0 = hasSkin ? glm::make_vec4(&bufferWeights[v * weightByteStride]) : glm::vec4(0.0f);
						// Fix for all zero weights
						if (glm::length(vert.weight0) == 0.0f) {
							vert.weight0 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
						}
						loaderInfo.vertexPos++;
					}
				}
				// Indices
				if (hasIndices)
				{
					const tinygltf::Accessor &accessor = model.accessors[primitive.indices > -1 ? primitive.indices : 0];
					const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
					const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

					indexCount = static_cast<uint32_t>(accessor.count);
					const void *dataPtr = &(buffer.data[accessor.byteOffset + bufferView.byteOffset]);

					switch (accessor.componentType) {
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
						const uint32_t *buf = static_cast<const uint32_t*>(dataPtr);
						for (size_t index = 0; index < accessor.count; index++) {
							loaderInfo.indexBuffer[loaderInfo.indexPos] = buf[index] + vertexStart;
							loaderInfo.indexPos++;
						}
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
						const uint16_t *buf = static_cast<const uint16_t*>(dataPtr);
						for (size_t index = 0; index < accessor.count; index++) {
							loaderInfo.indexBuffer[loaderInfo.indexPos] = buf[index] + vertexStart;
							loaderInfo.indexPos++;
						}
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
						const uint8_t *buf = static_cast<const uint8_t*>(dataPtr);
						for (size_t index = 0; index < accessor.count; index++) {
							loaderInfo.indexBuffer[loaderInfo.indexPos] = buf[index] + vertexStart;
							loaderInfo.indexPos++;
						}
						break;
					}
					default:
						std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
						return;
					}
				}					
				Primitive *newPrimitive = new Primitive(indexStart, indexCount, vertexCount, primitive.material > -1 ? this->materials_[primitive.material] : this->materials_.back());
				newPrimitive->SetBoundingBox(posMin, posMax);
				newMesh->primitives.push_back(newPrimitive);
			}
			// Mesh BB from BBs of primitives
			for (auto p : newMesh->primitives) {
				if (p->bb.valid && !newMesh->bb.valid) {
					newMesh->bb = p->bb;
					newMesh->bb.valid = true;
				}
				newMesh->bb.min = glm::min(newMesh->bb.min, p->bb.min);
				newMesh->bb.max = glm::max(newMesh->bb.max, p->bb.max);
			}
			newNode->mesh = newMesh;
		}
		if (parent) {
			parent->children.push_back(newNode);
		} else {
			this->nodes_.push_back(newNode);
		}
		this->linearNodes_.push_back(newNode);
	}

	void GLTFModel::GetNodeProps(const tinygltf::Node& node, const tinygltf::Model& model, size_t& vertexCount, size_t& indexCount)
	{
		if (node.children.size() > 0) {
			for (size_t i = 0; i < node.children.size(); i++) {
				GetNodeProps(model.nodes[node.children[i]], model, vertexCount, indexCount);
			}
		}
		if (node.mesh > -1) {
			const tinygltf::Mesh mesh = model.meshes[node.mesh];
			for (size_t i = 0; i < mesh.primitives.size(); i++) {
				auto& primitive = mesh.primitives[i];
				vertexCount += model.accessors[primitive.attributes.find("POSITION")->second].count;
				if (primitive.indices > -1) {
					indexCount += model.accessors[primitive.indices].count;
				}
			}
		}
	}

	void GLTFModel::LoadSkins(tinygltf::Model &gltfModel)
	{
		for (tinygltf::Skin &source : gltfModel.skins) {
			Skin *newSkin = new Skin{};
			newSkin->name = source.name;
				
			// Find skeleton root node
			if (source.skeleton > -1) {
				newSkin->skeletonRoot = NodeFromIndex(source.skeleton);
			}

			// Find joint nodes
			for (int jointIndex : source.joints) {
				Node* node = NodeFromIndex(jointIndex);
				if (node) {
					newSkin->joints.push_back(NodeFromIndex(jointIndex));
				}
			}

			// Get inverse bind matrices from buffer
			if (source.inverseBindMatrices > -1) {
				const tinygltf::Accessor &accessor = gltfModel.accessors[source.inverseBindMatrices];
				const tinygltf::BufferView &bufferView = gltfModel.bufferViews[accessor.bufferView];
				const tinygltf::Buffer &buffer = gltfModel.buffers[bufferView.buffer];
				newSkin->inverseBindMatrices.resize(accessor.count);
				memcpy(newSkin->inverseBindMatrices.data(), &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::mat4));
			}

			if (newSkin->joints.size() > MAX_NUM_JOINTS) {
				std::cerr << "[WARNING] Skin " << newSkin->name << " has " << newSkin->joints.size() << " joints, which is higher than the supported maximum of " << MAX_NUM_JOINTS << "\n";
				std::cerr << "[WARNING] glTF scene may display wrong/incomplete\n";
			}

			this->skins_.push_back(newSkin);
		}
	}

	void GLTFModel::LoadTextures(tinygltf::Model &gltfModel)
	{
		auto& device = core::Device::Instance();
		auto transferQueue = device.GetGraphicsQueue();

		for (tinygltf::Texture &tex : gltfModel.textures) {
			int source = tex.source;
			// If this texture uses the KHR_texture_basisu, we need to get the source index from the extension structure
			if (tex.extensions.find("KHR_texture_basisu") != tex.extensions.end()) {
				auto ext = tex.extensions.find("KHR_texture_basisu");
				auto value = ext->second.Get("source");
				source = value.Get<int>();
			}				
			tinygltf::Image image = gltfModel.images[source];
			TextureSampler textureSampler;
			if (tex.sampler == -1) {
				// No sampler specified, use a default one
				textureSampler.magFilter = VK_FILTER_LINEAR;
				textureSampler.minFilter = VK_FILTER_LINEAR;
				textureSampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				textureSampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				textureSampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			} else {
				textureSampler = this->textureSamplers_[tex.sampler];
			}
			Texture texture;
			texture.FromglTfImage(image, this->filePath_, textureSampler);
			this->textures_.push_back(std::move(texture));
		}
	}

	VkSamplerAddressMode GLTFModel::GetVkWrapMode(int32_t wrapMode)
	{
		switch (wrapMode) {
		case -1:
		case 10497:
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		case 33071:
			return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		case 33648:
			return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		}

		std::cerr << "Unknown wrap mode for getVkWrapMode: " << wrapMode << std::endl;
		return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}

	VkFilter GLTFModel::GetVkFilterMode(int32_t filterMode)
	{
		switch (filterMode) {
		case -1:
		case 9728:
			return VK_FILTER_NEAREST;
		case 9729:
			return VK_FILTER_LINEAR;
		case 9984:
			return VK_FILTER_NEAREST;
		case 9985:
			return VK_FILTER_NEAREST;
		case 9986:
			return VK_FILTER_LINEAR;
		case 9987:
			return VK_FILTER_LINEAR;
		}

		std::cerr << "Unknown filter mode for getVkFilterMode: " << filterMode << std::endl;
		return VK_FILTER_NEAREST;
	}

	void GLTFModel::LoadTextureSamplers(tinygltf::Model &gltfModel)
	{
		for (tinygltf::Sampler smpl : gltfModel.samplers) {
			TextureSampler sampler{};
			sampler.minFilter = GetVkFilterMode(smpl.minFilter);
			sampler.magFilter = GetVkFilterMode(smpl.magFilter);
			sampler.addressModeU = GetVkWrapMode(smpl.wrapS);
			sampler.addressModeV = GetVkWrapMode(smpl.wrapT);
			sampler.addressModeW = sampler.addressModeV;
			this->textureSamplers_.push_back(sampler);
		}
	}

	void GLTFModel::LoadMaterials(tinygltf::Model &gltfModel)
	{
		for (tinygltf::Material &mat : gltfModel.materials) {
			Material material{};
			material.doubleSided = mat.doubleSided;
			if (mat.values.find("baseColorTexture") != mat.values.end()) {
				material.baseColorTexture = &this->textures_[mat.values["baseColorTexture"].TextureIndex()];
				material.texCoordSets.baseColor = mat.values["baseColorTexture"].TextureTexCoord();
			}
			if (mat.values.find("metallicRoughnessTexture") != mat.values.end()) {
				material.metallicRoughnessTexture = &this->textures_[mat.values["metallicRoughnessTexture"].TextureIndex()];
				material.texCoordSets.metallicRoughness = mat.values["metallicRoughnessTexture"].TextureTexCoord();
			}
			if (mat.values.find("roughnessFactor") != mat.values.end()) {
				material.roughnessFactor = static_cast<float>(mat.values["roughnessFactor"].Factor());
			}
			if (mat.values.find("metallicFactor") != mat.values.end()) {
				material.metallicFactor = static_cast<float>(mat.values["metallicFactor"].Factor());
			}
			if (mat.values.find("baseColorFactor") != mat.values.end()) {
				material.baseColorFactor = glm::make_vec4(mat.values["baseColorFactor"].ColorFactor().data());
			}				
			if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end()) {
				material.normalTexture = &this->textures_[mat.additionalValues["normalTexture"].TextureIndex()];
				material.texCoordSets.normal = mat.additionalValues["normalTexture"].TextureTexCoord();
			}
			if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end()) {
				material.emissiveTexture = &this->textures_[mat.additionalValues["emissiveTexture"].TextureIndex()];
				material.texCoordSets.emissive = mat.additionalValues["emissiveTexture"].TextureTexCoord();
			}
			if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end()) {
				material.occlusionTexture = &this->textures_[mat.additionalValues["occlusionTexture"].TextureIndex()];
				material.texCoordSets.occlusion = mat.additionalValues["occlusionTexture"].TextureTexCoord();
			}
			if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end()) {
				tinygltf::Parameter param = mat.additionalValues["alphaMode"];
				if (param.string_value == "BLEND") {
					material.alphaMode = Material::ALPHAMODE_BLEND;
				}
				if (param.string_value == "MASK") {
					material.alphaCutoff = 0.5f;
					material.alphaMode = Material::ALPHAMODE_MASK;
				}
			}
			if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end()) {
				material.alphaCutoff = static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
			}
			if (mat.additionalValues.find("emissiveFactor") != mat.additionalValues.end()) {
				material.emissiveFactor = glm::vec4(glm::make_vec3(mat.additionalValues["emissiveFactor"].ColorFactor().data()), 1.0);
			}

			// Extensions
			if (mat.extensions.find("KHR_materials_pbrSpecularGlossiness") != mat.extensions.end()) {
				auto ext = mat.extensions.find("KHR_materials_pbrSpecularGlossiness");
				if (ext->second.Has("specularGlossinessTexture")) {
					auto index = ext->second.Get("specularGlossinessTexture").Get("index");
					material.extension.specularGlossinessTexture = &this->textures_[index.Get<int>()];
					auto texCoordSet = ext->second.Get("specularGlossinessTexture").Get("texCoord");
					material.texCoordSets.specularGlossiness = texCoordSet.Get<int>();
					material.pbrWorkflows.specularGlossiness = true;
					material.pbrWorkflows.metallicRoughness = false;
				}
				if (ext->second.Has("diffuseTexture")) {
					auto index = ext->second.Get("diffuseTexture").Get("index");
					material.extension.diffuseTexture = &this->textures_[index.Get<int>()];
				}
				if (ext->second.Has("diffuseFactor")) {
					auto factor = ext->second.Get("diffuseFactor");
					for (uint32_t i = 0; i < factor.ArrayLen(); i++) {
						auto val = factor.Get(i);
						material.extension.diffuseFactor[i] = val.IsNumber() ? (float)val.Get<double>() : (float)val.Get<int>();
					}
				}
				if (ext->second.Has("specularFactor")) {
					auto factor = ext->second.Get("specularFactor");
					for (uint32_t i = 0; i < factor.ArrayLen(); i++) {
						auto val = factor.Get(i);
						material.extension.specularFactor[i] = val.IsNumber() ? (float)val.Get<double>() : (float)val.Get<int>();
					}
				}
			}

			if (mat.extensions.find("KHR_materials_unlit") != mat.extensions.end()) {
				material.unlit = true;
			}

			if (mat.extensions.find("KHR_materials_emissive_strength") != mat.extensions.end()) {
				auto ext = mat.extensions.find("KHR_materials_emissive_strength");
				if (ext->second.Has("emissiveStrength")) {
					auto value = ext->second.Get("emissiveStrength");
					material.emissiveStrength = (float)value.Get<double>();
				}
			}

			material.index = static_cast<uint32_t>(this->materials_.size());
			this->materials_.push_back(material);
		}
		// Push a default material at the end of the list for meshes with no material assigned
		this->materials_.push_back(Material());
	}

	void GLTFModel::LoadAnimations(tinygltf::Model &gltfModel)
	{
		for (tinygltf::Animation &anim : gltfModel.animations) {
			Animation animation{};
			animation.name = anim.name;
			if (anim.name.empty()) {
				animation.name = std::to_string(this->animations_.size());
			}

			// Samplers
			for (auto &samp : anim.samplers) {
				AnimationSampler sampler{};

				if (samp.interpolation == "LINEAR") {
					sampler.interpolation = AnimationSampler::InterpolationType::LINEAR;
				}
				if (samp.interpolation == "STEP") {
					sampler.interpolation = AnimationSampler::InterpolationType::STEP;
				}
				if (samp.interpolation == "CUBICSPLINE") {
					sampler.interpolation = AnimationSampler::InterpolationType::CUBICSPLINE;
				}

				// Read sampler input time values
				{
					const tinygltf::Accessor &accessor = gltfModel.accessors[samp.input];
					const tinygltf::BufferView &bufferView = gltfModel.bufferViews[accessor.bufferView];
					const tinygltf::Buffer &buffer = gltfModel.buffers[bufferView.buffer];

					assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

					const void *dataPtr = &buffer.data[accessor.byteOffset + bufferView.byteOffset];
					const float *buf = static_cast<const float*>(dataPtr);
					for (size_t index = 0; index < accessor.count; index++) {
						sampler.inputs.push_back(buf[index]);
					}

					for (auto input : sampler.inputs) {
						if (input < animation.start) {
							animation.start = input;
						};
						if (input > animation.end) {
							animation.end = input;
						}
					}
				}

				// Read sampler output T/R/S values 
				{
					const tinygltf::Accessor &accessor = gltfModel.accessors[samp.output];
					const tinygltf::BufferView &bufferView = gltfModel.bufferViews[accessor.bufferView];
					const tinygltf::Buffer &buffer = gltfModel.buffers[bufferView.buffer];

					assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

					const void *dataPtr = &buffer.data[accessor.byteOffset + bufferView.byteOffset];

					switch (accessor.type) {
					case TINYGLTF_TYPE_VEC3: {
						const glm::vec3 *buf = static_cast<const glm::vec3*>(dataPtr);
						for (size_t index = 0; index < accessor.count; index++) {
							sampler.outputsVec4.push_back(glm::vec4(buf[index], 0.0f));
							sampler.outputs.push_back(buf[index][0]);
							sampler.outputs.push_back(buf[index][1]);
							sampler.outputs.push_back(buf[index][2]);
						}
						break;
					}
					case TINYGLTF_TYPE_VEC4: {
						const glm::vec4 *buf = static_cast<const glm::vec4*>(dataPtr);
						for (size_t index = 0; index < accessor.count; index++) {
							sampler.outputsVec4.push_back(buf[index]);
							sampler.outputs.push_back(buf[index][0]);
							sampler.outputs.push_back(buf[index][1]);
							sampler.outputs.push_back(buf[index][2]);
							sampler.outputs.push_back(buf[index][3]);
						}
						break;
					}
					default: {
						std::cout << "unknown type" << std::endl;
						break;
					}
					}
				}

				animation.samplers.push_back(sampler);
			}

			// Channels
			for (auto &source: anim.channels) {
				AnimationChannel channel{};

				if (source.target_path == "rotation") {
					channel.path = AnimationChannel::PathType::ROTATION;
				}
				if (source.target_path == "translation") {
					channel.path = AnimationChannel::PathType::TRANSLATION;
				}
				if (source.target_path == "scale") {
					channel.path = AnimationChannel::PathType::SCALE;
				}
				if (source.target_path == "weights") {
					std::cout << "weights not yet supported, skipping channel" << std::endl;
					continue;
				}
				channel.samplerIndex = source.sampler;
				channel.node = NodeFromIndex(source.target_node);
				if (!channel.node) {
					continue;
				}

				animation.channels.push_back(channel);
			}

			this->animations_.push_back(animation);
		}
	}

	void GLTFModel::LoadFromFile(std::string filename, float scale)
	{
		LOG_INFO("GLTFModel: Loading glTF file: " + filename);
		auto& device = core::Device::Instance();

		bool success = true;

		tinygltf::Model gltfModel;
		tinygltf::TinyGLTF gltfContext;

		std::string error;
		std::string warning;

		bool binary = false;
		size_t extpos = filename.rfind('.', filename.length());
		if (extpos != std::string::npos) {
			binary = (filename.substr(extpos + 1, filename.length() - extpos) == "glb");
		}

		size_t pos = filename.find_last_of('/');
		if (pos == std::string::npos) {
			pos = filename.find_last_of('\\');
		}
		this->filePath_ = filename.substr(0, pos);

		// @todo
		gltfContext.SetImageLoader(loadImageDataFunc, nullptr);

		bool fileLoaded = binary ? gltfContext.LoadBinaryFromFile(&gltfModel, &error, &warning, filename.c_str()) : gltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, filename.c_str());

		LoaderInfo loaderInfo{};
		size_t vertexCount = 0;
		size_t indexCount = 0;

		if (fileLoaded) {
			this->extensions_ = gltfModel.extensionsUsed;
			for (auto& extension : this->extensions_) {
				// If this model uses basis universal compressed textures, we need to transcode them
				// So we need to initialize that transcoder once
				if (extension == "KHR_texture_basisu") {
					std::cout << "Model uses KHR_texture_basisu, initializing basisu transcoder\n";
					basist::basisu_transcoder_init();
				}
			}

			// Check and list unsupported extensions
			for (auto& ext : this->extensions_) {
				if (std::find(supportedExtensions.begin(), supportedExtensions.end(), ext) == supportedExtensions.end()) {
					std::cout << "[WARN] Unsupported extension " << ext << " detected. Scene may not work or display as intended\n";
				}
			}

			LoadTextureSamplers(gltfModel);
			LoadTextures(gltfModel);
			LoadMaterials(gltfModel);

			const tinygltf::Scene& scene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];

			// Get vertex and index buffer sizes up-front
			for (size_t i = 0; i < scene.nodes.size(); i++) {
				GetNodeProps(gltfModel.nodes[scene.nodes[i]], gltfModel, vertexCount, indexCount);
			}
			loaderInfo.vertexBuffer = new Vertex[vertexCount];
			loaderInfo.indexBuffer = new uint32_t[indexCount];

			// TODO: scene handling with no default scene
			for (size_t i = 0; i < scene.nodes.size(); i++) {
				const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
				LoadNode(nullptr, node, scene.nodes[i], gltfModel, loaderInfo, scale);
			}
			if (gltfModel.animations.size() > 0) {
				LoadAnimations(gltfModel);
			}
			LoadSkins(gltfModel);

			uint32_t meshIndex = 0;
			for (auto node : this->linearNodes_) {
				// Assign skins
				if (node->skinIndex > -1) {
					node->skin = this->skins_[node->skinIndex];
				}
				// Initial pose
				if (node->mesh) {
					node->mesh->index = meshIndex++;
					node->Update();
				}
			}
		}
		else {
			// TODO: throw
			std::cerr << "Could not load gltf file: " << error << std::endl;
			return;
		}

		size_t vertexBufferSize = vertexCount * sizeof(Vertex);
		size_t indexBufferSize = indexCount * sizeof(uint32_t);

		assert(vertexBufferSize > 0);

		struct StagingBuffer {
			VkBuffer buffer;
			VkDeviceMemory memory;
		} vertexStaging, indexStaging;

		// Create staging buffers
		// Vertex data
		success = device.CreateBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			vertexBufferSize,
			&vertexStaging.buffer,
			&vertexStaging.memory,
			loaderInfo.vertexBuffer);
		SUCCESS_OR_LOG(success, "GLTFModel: Failed to create buffer.");


		// Index data
		if (indexBufferSize > 0) {
			success = device.CreateBuffer(
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				indexBufferSize,
				&indexStaging.buffer,
				&indexStaging.memory,
				loaderInfo.indexBuffer);

			SUCCESS_OR_LOG(success, "GLTFModel: Failed to create buffer.");
		}

		// Create device local buffers
		// Vertex buffer
		success = device.CreateBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			vertexBufferSize,
			&this->vertices_.buffer,
			&this->vertices_.memory);
		SUCCESS_OR_LOG(success, "GLTFModel: Failed to create buffer.");

		// Index buffer
		if (indexBufferSize > 0) {
			success = device.CreateBuffer(
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				indexBufferSize,
				&this->indices_.buffer,
				&this->indices_.memory);
			SUCCESS_OR_LOG(success, "GLTFModel: Failed to create buffer.");
		}

		// Copy from staging buffers
		VkCommandBuffer copyCmd = device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		VkBufferCopy copyRegion = {};

		copyRegion.size = vertexBufferSize;
		vkCmdCopyBuffer(copyCmd, vertexStaging.buffer, this->vertices_.buffer, 1, &copyRegion);

		if (indexBufferSize > 0) {
			copyRegion.size = indexBufferSize;
			vkCmdCopyBuffer(copyCmd, indexStaging.buffer, this->indices_.buffer, 1, &copyRegion);
		}

		device.FlushCommandBuffer(copyCmd, true);

		vkDestroyBuffer(device.GetLogicalDeviceHandle(), vertexStaging.buffer, nullptr);
		vkFreeMemory(device.GetLogicalDeviceHandle(), vertexStaging.memory, nullptr);
		if (indexBufferSize > 0) {
			vkDestroyBuffer(device.GetLogicalDeviceHandle(), indexStaging.buffer, nullptr);
			vkFreeMemory(device.GetLogicalDeviceHandle(), indexStaging.memory, nullptr);
		}

		delete[] loaderInfo.vertexBuffer;
		delete[] loaderInfo.indexBuffer;

		GetSceneDimensions();
	}

	void GLTFModel::DrawNode(Node *node, VkCommandBuffer commandBuffer)
	{
		if (node->mesh) {
			for (Primitive *primitive : node->mesh->primitives) {
				vkCmdDrawIndexed(commandBuffer, primitive->indexCount, 1, primitive->firstIndex, 0, 0);
			}
		}
		for (auto& child : node->children) {
			DrawNode(child, commandBuffer);
		}
	}

	void GLTFModel::Draw(VkCommandBuffer commandBuffer)
	{
		const VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &this->vertices_.buffer, offsets);
		vkCmdBindIndexBuffer(commandBuffer, this->indices_.buffer, 0, VK_INDEX_TYPE_UINT32);
		for (auto& node : this->nodes_) {
			DrawNode(node, commandBuffer);
		}
	}

	void GLTFModel::CalculateBoundingBox(Node *node, Node *parent) {
		BoundingBox parentBvh = parent ? parent->bvh : BoundingBox(this->dimensions_.min, this->dimensions_.max);

		if (node->mesh) {
			if (node->mesh->bb.valid) {
				node->aabb = node->mesh->bb.GetAABB(node->GetMatrix());
				if (node->children.size() == 0) {
					node->bvh.min = node->aabb.min;
					node->bvh.max = node->aabb.max;
					node->bvh.valid = true;
				}
			}
		}

		parentBvh.min = glm::min(parentBvh.min, node->bvh.min);
		parentBvh.max = glm::min(parentBvh.max, node->bvh.max);

		for (auto &child : node->children) {
			CalculateBoundingBox(child, node);
		}
	}

	void GLTFModel::GetSceneDimensions()
	{
		// Calculate binary volume hierarchy for all nodes in the scene
		for (auto node : this->linearNodes_) {
			CalculateBoundingBox(node, nullptr);
		}

		this->dimensions_.min = glm::vec3(FLT_MAX);
		this->dimensions_.max = glm::vec3(-FLT_MAX);

		for (auto node : this->linearNodes_) {
			if (node->bvh.valid) {
				this->dimensions_.min = glm::min(this->dimensions_.min, node->bvh.min);
				this->dimensions_.max = glm::max(this->dimensions_.max, node->bvh.max);
			}
		}

		// Calculate scene aabb
		this->aabb_ = glm::scale(glm::mat4(1.0f), glm::vec3(this->dimensions_.max[0] - this->dimensions_.min[0], this->dimensions_.max[1] - this->dimensions_.min[1], this->dimensions_.max[2] - this->dimensions_.min[2]));
		this->aabb_[3][0] = this->dimensions_.min[0];
		this->aabb_[3][1] = this->dimensions_.min[1];
		this->aabb_[3][2] = this->dimensions_.min[2];
	}

	void GLTFModel::UpdateAnimation(uint32_t index, float time)
	{
		if (this->animations_.empty()) {
			std::cout << ".glTF does not contain animation." << std::endl;
			return;
		}
		if (index > static_cast<uint32_t>(this->animations_.size()) - 1) {
			std::cout << "No animation with index " << index << std::endl;
			return;
		}
		Animation &animation = this->animations_[index];

		bool updated = false;
		for (auto& channel : animation.channels) {
			AnimationSampler &sampler = animation.samplers[channel.samplerIndex];
			if (sampler.inputs.size() > sampler.outputsVec4.size()) {
				continue;
			}

			for (size_t i = 0; i < sampler.inputs.size() - 1; i++) {
				if ((time >= sampler.inputs[i]) && (time <= sampler.inputs[i + 1])) {
					float u = std::max(0.0f, time - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);
					if (u <= 1.0f) {
						switch (channel.path) {
						case AnimationChannel::PathType::TRANSLATION:
							sampler.Translate(i, time, channel.node);
							break;
						case AnimationChannel::PathType::SCALE:
							sampler.Scale(i, time, channel.node);
							break;
						case AnimationChannel::PathType::ROTATION:
							sampler.Rotate(i, time, channel.node);
							break;
						}
						updated = true;
					}
				}
			}
		}
		if (updated) {
			for (auto &node : this->nodes_) {
				node->Update();
			}
		}
	}



	Node* GLTFModel::FindNode(Node *parent, uint32_t index) {
		Node* nodeFound = nullptr;
		if (parent->index == index) {
			return parent;
		}
		for (auto& child : parent->children) {
			nodeFound = FindNode(child, index);
			if (nodeFound) {
				break;
			}
		}
		return nodeFound;
	}

	Node* GLTFModel::NodeFromIndex(uint32_t index) {
		Node* nodeFound = nullptr;
		for (auto &node : this->nodes_) {
			nodeFound = FindNode(node, index);
			if (nodeFound) {
				break;
			}
		}
		return nodeFound;
	}

	void GLTFModel::CreateMaterialBuffer()
	{
		auto& device = core::Device::Instance();

		std::vector<ShaderMaterial> shaderMaterials{};
		for (auto& material : this->materials_) {
			ShaderMaterial shaderMaterial{};

			shaderMaterial.emissiveFactor = material.emissiveFactor;
			// To save space, availabilty and texture coordinate set are combined
			// -1 = texture not used for this material, >= 0 texture used and index of texture coordinate set
			shaderMaterial.colorTextureSet = material.baseColorTexture != nullptr ? material.texCoordSets.baseColor : -1;
			shaderMaterial.normalTextureSet = material.normalTexture != nullptr ? material.texCoordSets.normal : -1;
			shaderMaterial.occlusionTextureSet = material.occlusionTexture != nullptr ? material.texCoordSets.occlusion : -1;
			shaderMaterial.emissiveTextureSet = material.emissiveTexture != nullptr ? material.texCoordSets.emissive : -1;
			shaderMaterial.alphaMask = static_cast<float>(material.alphaMode == engine::resource::Material::ALPHAMODE_MASK);
			shaderMaterial.alphaMaskCutoff = material.alphaCutoff;
			shaderMaterial.emissiveStrength = material.emissiveStrength;

			if (material.pbrWorkflows.metallicRoughness) {
				// Metallic roughness workflow
				shaderMaterial.workflow = static_cast<float>(engine::resource::GLTFModel::PBR_WORKFLOW_METALLIC_ROUGHNESS);
				shaderMaterial.baseColorFactor = material.baseColorFactor;
				shaderMaterial.metallicFactor = material.metallicFactor;
				shaderMaterial.roughnessFactor = material.roughnessFactor;
				shaderMaterial.PhysicalDescriptorTextureSet = material.metallicRoughnessTexture != nullptr ? material.texCoordSets.metallicRoughness : -1;
				shaderMaterial.colorTextureSet = material.baseColorTexture != nullptr ? material.texCoordSets.baseColor : -1;
			} else {
				if (material.pbrWorkflows.specularGlossiness) {
					// Specular glossiness workflow
					shaderMaterial.workflow = static_cast<float>(PBR_WORKFLOW_SPECULAR_GLOSSINESS);
					shaderMaterial.PhysicalDescriptorTextureSet = material.extension.specularGlossinessTexture != nullptr ? material.texCoordSets.specularGlossiness : -1;
					shaderMaterial.colorTextureSet = material.extension.diffuseTexture != nullptr ? material.texCoordSets.baseColor : -1;
					shaderMaterial.diffuseFactor = material.extension.diffuseFactor;
					shaderMaterial.specularFactor = glm::vec4(material.extension.specularFactor, 1.0f);
				}
			}

			shaderMaterials.push_back(shaderMaterial);
		}

		if (this->shaderMaterialBuffer_.buffer != VK_NULL_HANDLE) {
			this->shaderMaterialBuffer_.Destroy();
		}
		VkDeviceSize bufferSize = shaderMaterials.size() * sizeof(ShaderMaterial);
		engine::core::Buffer stagingBuffer;

		SUCCESS_OR_LOG(
			device.CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			bufferSize, &stagingBuffer.buffer, &stagingBuffer.memory, shaderMaterials.data()),
			"GLTFModel: Failed to create staging buffer for material SSBO.");

		SUCCESS_OR_LOG(
			device.CreateBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			bufferSize, &this->shaderMaterialBuffer_.buffer, &this->shaderMaterialBuffer_.memory),
			"GLTFModel: Failed to create device-local buffer for material SSBO.");

		// Copy from staging buffer to device-local buffer
		VkCommandBuffer copyCmd = device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkBufferCopy copyRegion{};
		copyRegion.size = bufferSize;
		vkCmdCopyBuffer(copyCmd, stagingBuffer.buffer, this->shaderMaterialBuffer_.buffer, 1, &copyRegion);
		device.FlushCommandBuffer(copyCmd, true);
		stagingBuffer.device = device.GetLogicalDeviceHandle();
		stagingBuffer.Destroy();

		// Update descriptor
		this->shaderMaterialBuffer_.descriptor.buffer = this->shaderMaterialBuffer_.buffer;
		this->shaderMaterialBuffer_.descriptor.offset = 0;
		this->shaderMaterialBuffer_.descriptor.range = bufferSize;
		this->shaderMaterialBuffer_.device = device.GetLogicalDeviceHandle();
	}

	void GLTFModel::CreateMeshDataBuffer()
	{
		auto& device = core::Device::Instance();
		this->shaderMeshDataBuffers_.resize(device.GetSetting().frameCount_);
		this->descriptorSetsMeshData_.resize(device.GetSetting().frameCount_);
		std::vector<ShaderMeshData> shaderMeshData{};
		for (auto& node : this->linearNodes_) {
			ShaderMeshData meshData{};
			if (node->mesh) {
				memcpy(meshData.jointMatrix, node->mesh->jointMatrix, sizeof(glm::mat4) * MAX_NUM_JOINTS);
				meshData.jointcount = node->mesh->jointcount;
				meshData.matrix = node->mesh->matrix;
				shaderMeshData.push_back(meshData);
			}
		}

		for (auto& shaderMeshDataBuffer : this->shaderMeshDataBuffers_) {
			if (shaderMeshDataBuffer.buffer != VK_NULL_HANDLE) {
				shaderMeshDataBuffer.Destroy();
			}
			VkDeviceSize bufferSize = shaderMeshData.size() * sizeof(ShaderMeshData);
			if (!device.GetRequireStaging()) {
				// Prefer a host visible device buffer (ReBAR/SAM on discreate GPUs, always available on integrated GPUs)
				SUCCESS_OR_LOG(
					device.CreateBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bufferSize, &shaderMeshDataBuffer.buffer, &shaderMeshDataBuffer.memory), 
					"GLTFModel: Failed to create buffer."
				);
				shaderMeshDataBuffer.device = device.GetLogicalDeviceHandle();
				shaderMeshDataBuffer.Map();
				memcpy(shaderMeshDataBuffer.mapped, shaderMeshData.data(), bufferSize);
			} else {
				engine::core::Buffer stagingBuffer;
				SUCCESS_OR_LOG(
					device.CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bufferSize, &stagingBuffer.buffer, &stagingBuffer.memory, shaderMeshData.data()),
					"GLTFModel: Failed to create buffer."
					);

				SUCCESS_OR_LOG(
					device.CreateBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, bufferSize, &shaderMeshDataBuffer.buffer, &shaderMeshDataBuffer.memory) == VK_SUCCESS,
					"GLTFModel: Failed to create buffer."
				);

				// Copy from staging buffers
				VkCommandBuffer copyCmd = device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
				VkBufferCopy copyRegion{};
				copyRegion.size = bufferSize;
				vkCmdCopyBuffer(copyCmd, stagingBuffer.buffer, shaderMeshDataBuffer.buffer, 1, &copyRegion);
				device.FlushCommandBuffer(copyCmd, true);
				stagingBuffer.device = device.GetLogicalDeviceHandle();
				stagingBuffer.Destroy();
			}
			// Update descriptor
			shaderMeshDataBuffer.descriptor.buffer = shaderMeshDataBuffer.buffer;
			shaderMeshDataBuffer.descriptor.offset = 0;
			shaderMeshDataBuffer.descriptor.range = bufferSize;
			shaderMeshDataBuffer.device = device.GetLogicalDeviceHandle();
		}
	}

	void GLTFModel::UpdateMeshDataBuffer(uint32_t index)
	{
		auto& device = core::Device::Instance();
		// @todo: optimize (no push, use fixed size)
		std::vector<ShaderMeshData> shaderMeshData{};
		
		for (auto& node : this->linearNodes_) {
			ShaderMeshData meshData{};
			if (node->mesh) {
				memcpy(meshData.jointMatrix, node->mesh->jointMatrix, sizeof(glm::mat4) * MAX_NUM_JOINTS);
				meshData.jointcount = node->mesh->jointcount;
				meshData.matrix = node->mesh->matrix;
				shaderMeshData.push_back(meshData);
			}
		}

		VkDeviceSize bufferSize = shaderMeshData.size() * sizeof(ShaderMeshData);

		if (!device.GetRequireStaging()) {
			memcpy(this->shaderMeshDataBuffers_[index].mapped, shaderMeshData.data(), bufferSize);
		}
		else {
			core::Buffer stagingBuffer;

			SUCCESS_OR_LOG(
				device.CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bufferSize, &stagingBuffer.buffer, &stagingBuffer.memory, shaderMeshData.data()) == VK_SUCCESS,
				"GLTFModel: Failed to create buffer."
			);

			// Copy from staging buffers
			VkCommandBuffer copyCmd = device.CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
			VkBufferCopy copyRegion{};
			copyRegion.size = bufferSize;
			vkCmdCopyBuffer(copyCmd, stagingBuffer.buffer, this->shaderMeshDataBuffers_[index].buffer, 1, &copyRegion);
			device.FlushCommandBuffer(copyCmd, true);
			stagingBuffer.device = device.GetLogicalDeviceHandle();
			stagingBuffer.Destroy();
		}
	}

	std::unique_ptr<Model> GLTFModel::Clone()
	{
		return std::move(std::make_unique<GLTFModel>());
	}

	glm::mat4 GLTFModel::GetAABBBox()
	{
		return this->aabb_;
	}
	
	uint32_t GLTFModel::GetMaterialCount() 
	{
		return this->materials_.size();
	}

	uint32_t GLTFModel::GetMeshCount() 
	{
		return this->linearNodes_.size();
	}

	std::vector<core::Buffer>& GLTFModel::GetMeshShaderBuffer() 
	{
		return this->shaderMeshDataBuffers_;
	}

	core::Buffer& GLTFModel::GetMaterialShaderBuffer()
	{
		return this->shaderMaterialBuffer_;
	}

	std::vector<VkDescriptorSet>& GLTFModel::GetDescriptorSetsMeshData() 
	{
		return this->descriptorSetsMeshData_;
	}

	VkDescriptorSet& GLTFModel::GetDescriptorSetMaterial()
	{
		return this->descriptorSetMaterial_;
	}

	std::vector<Material>& GLTFModel::GetMaterialArray()
	{
		return this->materials_;
	}

	VkBuffer GLTFModel::GetVertexBuffer()
	{
		return this->vertices_.buffer;
	}

	VkBuffer GLTFModel::GetIndexBuffer()
	{
		return this->indices_.buffer;
	}

	std::vector<Node*>& GLTFModel::GetNodes()
	{
		return this->nodes_;
	}

	std::vector<Node*>& GLTFModel::GetLinearNodes()
	{
		return this->linearNodes_;
	}

	std::vector<Animation>& GLTFModel::GetAnimations()
	{
		return this->animations_;
	}

}
