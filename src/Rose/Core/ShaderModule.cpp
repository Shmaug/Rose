#include "ShaderModule.hpp"
#include "Hash.hpp"

#include <slang.h>
#include <portable-file-dialogs.h>

#include <stdio.h>
#include <stdlib.h>

//#define LOG_SHADER_REFLECTION

#ifndef DEFAULT_SHADER_INCLUDE_PATHS
#define DEFAULT_SHADER_INCLUDE_PATHS
#endif

namespace {
	inline static const std::string kDefaultIncludePaths[] {
		// src/
		std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().string(),
		// thirdparty/
		(std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().parent_path() / "thirdparty").string(),

		DEFAULT_SHADER_INCLUDE_PATHS
	};
}

namespace RoseEngine {

inline static const std::unordered_map<SlangBindingType, vk::DescriptorType> gDescriptorTypeMap = {
	{ SLANG_BINDING_TYPE_SAMPLER, vk::DescriptorType::eSampler },
	{ SLANG_BINDING_TYPE_TEXTURE, vk::DescriptorType::eSampledImage},
	{ SLANG_BINDING_TYPE_CONSTANT_BUFFER, vk::DescriptorType::eUniformBuffer },
	{ SLANG_BINDING_TYPE_TYPED_BUFFER, vk::DescriptorType::eUniformTexelBuffer },
	{ SLANG_BINDING_TYPE_RAW_BUFFER, vk::DescriptorType::eStorageBuffer },
	{ SLANG_BINDING_TYPE_COMBINED_TEXTURE_SAMPLER, vk::DescriptorType::eCombinedImageSampler },
	{ SLANG_BINDING_TYPE_INPUT_RENDER_TARGET, vk::DescriptorType::eInputAttachment },
	{ SLANG_BINDING_TYPE_INLINE_UNIFORM_DATA, vk::DescriptorType::eInlineUniformBlock },
	{ SLANG_BINDING_TYPE_RAY_TRACING_ACCELERATION_STRUCTURE, vk::DescriptorType::eAccelerationStructureKHR },
	{ SLANG_BINDING_TYPE_MUTABLE_TETURE, vk::DescriptorType::eStorageImage },
	{ SLANG_BINDING_TYPE_MUTABLE_TYPED_BUFFER, vk::DescriptorType::eStorageTexelBuffer },
	{ SLANG_BINDING_TYPE_MUTABLE_RAW_BUFFER, vk::DescriptorType::eStorageBuffer },
};

inline const char* to_string(slang::TypeReflection::Kind kind) {
	switch (kind) {
		default:
		case slang::TypeReflection::Kind::None: return "None";
		case slang::TypeReflection::Kind::Struct: return "Struct";
		case slang::TypeReflection::Kind::Array: return "Array";
		case slang::TypeReflection::Kind::Matrix: return "Matrix";
		case slang::TypeReflection::Kind::Vector: return "Vector";
		case slang::TypeReflection::Kind::Scalar: return "Scalar";
		case slang::TypeReflection::Kind::ConstantBuffer: return "ConstantBuffer";
		case slang::TypeReflection::Kind::Resource: return "Resource";
		case slang::TypeReflection::Kind::SamplerState: return "SamplerState";
		case slang::TypeReflection::Kind::TextureBuffer: return "TextureBuffer";
		case slang::TypeReflection::Kind::ShaderStorageBuffer: return "ShaderStorageBuffer";
		case slang::TypeReflection::Kind::ParameterBlock: return "ParameterBlock";
		case slang::TypeReflection::Kind::GenericTypeParameter: return "GenericTypeParameter";
		case slang::TypeReflection::Kind::Interface: return "Interface";
		case slang::TypeReflection::Kind::OutputStream: return "OutputStream";
		case slang::TypeReflection::Kind::Specialized: return "Specialized";
		case slang::TypeReflection::Kind::Feedback: return "Feedback";
		case slang::TypeReflection::Kind::Pointer: return "Pointer";
	};
};

inline const char* to_string(slang::ParameterCategory category) {
	switch (category) {
		default:
		case slang::ParameterCategory::None: return "None";
		case slang::ParameterCategory::Mixed: return "Mixed";
		case slang::ParameterCategory::ConstantBuffer: return "ConstantBuffer";
		case slang::ParameterCategory::ShaderResource: return "ShaderResource";
		case slang::ParameterCategory::UnorderedAccess: return "UnorderedAccess";
		case slang::ParameterCategory::VaryingInput: return "VaryingInput";
		case slang::ParameterCategory::VaryingOutput: return "VaryingOutput";
		case slang::ParameterCategory::SamplerState: return "SamplerState";
		case slang::ParameterCategory::Uniform: return "Uniform";
		case slang::ParameterCategory::DescriptorTableSlot: return "DescriptorTableSlot";
		case slang::ParameterCategory::SpecializationConstant: return "SpecializationConstant";
		case slang::ParameterCategory::PushConstantBuffer: return "PushConstantBuffer";
		case slang::ParameterCategory::RegisterSpace: return "RegisterSpace";
		case slang::ParameterCategory::GenericResource: return "GenericResource";
		case slang::ParameterCategory::RayPayload: return "RayPayload";
		case slang::ParameterCategory::HitAttributes: return "HitAttributes";
		case slang::ParameterCategory::CallablePayload: return "CallablePayload";
		case slang::ParameterCategory::ShaderRecord: return "ShaderRecord";
		case slang::ParameterCategory::ExistentialTypeParam: return "ExistentialTypeParam";
		case slang::ParameterCategory::ExistentialObjectParam: return "ExistentialObjectParam";
		case slang::ParameterCategory::SubElementRegisterSpace: return "SubElementRegisterSpace";
	};
};

struct ParameterAccessPath {
	static const size_t kInvalidAccessPath = ~size_t(0);

	slang::VariableLayoutReflection* varLayout = nullptr;
	size_t leafNode = kInvalidAccessPath;
	size_t outer = kInvalidAccessPath;

	size_t deepestConstantBuffer = kInvalidAccessPath;
	size_t deepestParameterBlock = kInvalidAccessPath;
	size_t depth = 0;
	bool pushConstant = false;
};

struct ParameterEnumerator {
	std::vector<ParameterAccessPath> nodes;

	void GetCumulativeOffset(const ParameterAccessPath n, slang::ParameterCategory layoutUnit, uint32_t& offset, uint32_t& space) const {
		offset = 0;
		space = 0;
		switch (layoutUnit) {
			default:
				for(auto node = n.leafNode; node != ParameterAccessPath::kInvalidAccessPath; node = nodes[node].outer) {
					offset += nodes[node].varLayout->getOffset((SlangParameterCategory)layoutUnit);
					space  += nodes[node].varLayout->getBindingSpace((SlangParameterCategory)layoutUnit);
				}
			case slang::ParameterCategory::Uniform:
				for (auto node = n.leafNode; node != n.deepestConstantBuffer; node = nodes[node].outer) {
					offset += nodes[node].varLayout->getOffset((SlangParameterCategory)layoutUnit);
				}
				break;
			case slang::ParameterCategory::ConstantBuffer:
			case slang::ParameterCategory::ShaderResource:
			case slang::ParameterCategory::UnorderedAccess:
			case slang::ParameterCategory::SamplerState:
			case slang::ParameterCategory::DescriptorTableSlot:
				for (auto node = n.leafNode; node != n.deepestParameterBlock; node = nodes[node].outer) {
					offset += nodes[node].varLayout->getOffset((SlangParameterCategory)layoutUnit);
					space  += nodes[node].varLayout->getBindingSpace((SlangParameterCategory)layoutUnit);
				}
				for (auto node = n.deepestParameterBlock; node != ParameterAccessPath::kInvalidAccessPath; node = nodes[node].outer) {
					space += nodes[node].varLayout->getOffset((SlangParameterCategory)slang::ParameterCategory::SubElementRegisterSpace);
				}
				break;
		}
	}

	bool IsPushConstant(const ParameterAccessPath n) const {
		for (auto node = n.leafNode; node != ParameterAccessPath::kInvalidAccessPath; node = nodes[node].outer) {
			if (nodes[node].pushConstant)
				return true;
			if (nodes[node].varLayout->getTypeLayout()->getParameterCategory() == slang::ParameterCategory::PushConstantBuffer)
				return true;
		}
		return false;
	}

	void EnumerateAccessPaths(slang::VariableLayoutReflection* parameter, ShaderParameterBinding& binding, ParameterAccessPath accessPath) {
		// create access path node for parameter
		accessPath.varLayout = parameter;
		accessPath.outer = accessPath.leafNode;
		accessPath.leafNode = nodes.size();
		accessPath.depth++;
		nodes.emplace_back(accessPath);

		slang::ParameterCategory category = parameter->getCategory();
		slang::TypeLayoutReflection* typeLayout = parameter->getTypeLayout();
		bool isArray = typeLayout->getKind() == slang::TypeReflection::Kind::Array;
		uint32_t arraySize = 1;
		uint32_t arrayDescriptorStride = 0;
		uint32_t arrayUniformStride = 0;
		if (isArray) {
			arraySize  = typeLayout->getElementCount();
			typeLayout = typeLayout->getElementTypeLayout();

			arrayDescriptorStride = typeLayout->getStride((SlangParameterCategory)slang::ParameterCategory::DescriptorTableSlot);
			arrayUniformStride    = typeLayout->getStride((SlangParameterCategory)slang::ParameterCategory::Uniform);
		}

		if (parameter->getName()) {
			uint32_t setIndex = 0;
			uint32_t bindingIndex = 0;
			uint32_t uniformOffset = 0;
			uint32_t uniformSize = 0;

			// compute offsets
			if (category == slang::ParameterCategory::Uniform) {
				uint32_t tmp;
				uniformSize = typeLayout->getSize();
				GetCumulativeOffset(accessPath, slang::ParameterCategory::Uniform, uniformOffset, tmp);
				if (accessPath.deepestConstantBuffer != ParameterAccessPath::kInvalidAccessPath)
					GetCumulativeOffset(nodes[accessPath.deepestConstantBuffer], slang::ParameterCategory::DescriptorTableSlot, bindingIndex, tmp);
				GetCumulativeOffset(accessPath, slang::ParameterCategory::DescriptorTableSlot, tmp, setIndex);
			} else {
				if (category == slang::ParameterCategory::DescriptorTableSlot) {
					GetCumulativeOffset(accessPath, slang::ParameterCategory::DescriptorTableSlot, bindingIndex, setIndex);
				}
			}

			#ifdef LOG_SHADER_REFLECTION
			std::stringstream ss;
			ss << std::string(accessPath.depth, ' ');
			ss << parameter->getName();
			if (isArray) ss << "[" << arraySize <<  "]";
			if (parameter->getSemanticName()) {
				ss << " : " << parameter->getSemanticName();
				ss << parameter->getSemanticIndex();
			}
			if (IsPushConstant(accessPath)) ss << " [PC]";
			std::cout << std::left << std::setw(30) << ss.view();
			std::cout << " | " << std::left << std::setw(16) << to_string(typeLayout->getKind());
			std::cout << " | " << std::left << std::setw(24) << to_string(category);
			if (category == slang::ParameterCategory::VaryingInput || category == slang::ParameterCategory::VaryingOutput) {
				std::cout << " | location = " << parameter->getBindingIndex();
			}
			else if (category == slang::ParameterCategory::Uniform || category == slang::ParameterCategory::DescriptorTableSlot)
				std::cout << " | " << std::left << std::setw(5) << (std::to_string(setIndex) + "." + std::to_string(bindingIndex));
			if (category == slang::ParameterCategory::Uniform) {
				std::cout << " | offset = " << std::left << std::setw(4) << uniformOffset;
				std::cout << " | size = "   << std::left << std::setw(4) << uniformSize;
			}
			if (arraySize > 1) {
				if (category != slang::ParameterCategory::Uniform)
					std::cout << " | descriptor stride = " << std::left << std::setw(4) << arrayDescriptorStride;
				std::cout << " | uniform stride = " << std::left << std::setw(4) << arrayUniformStride;

			}
			std::cout << std::endl;
			#endif

			// create bindings

			switch (category) {
				case slang::ParameterCategory::ConstantBuffer:
				case slang::ParameterCategory::Mixed:
					binding = ShaderStructBinding{
						.arraySize = arraySize,
						.descriptorStride = arrayDescriptorStride,
						.uniformStride = arrayUniformStride
					};
					break;
				case slang::ParameterCategory::VaryingInput:
				case slang::ParameterCategory::VaryingOutput:
					/*if (typeLayout->getKind() != slang::TypeReflection::Kind::Struct)*/ {
						binding = ShaderVertexAttributeBinding{
							.location = parameter->getBindingIndex(),
							.semantic      = (parameter->getSemanticName() == nullptr ? "" : parameter->getSemanticName()),
							.semanticIndex = (uint32_t)parameter->getSemanticIndex()
						};
					}
					break;
				case slang::ParameterCategory::DescriptorTableSlot:
					if (typeLayout->getKind() == slang::TypeReflection::Kind::Struct) {
						binding = ShaderStructBinding{
							.arraySize = arraySize,
							.descriptorStride = arrayDescriptorStride,
							.uniformStride = arrayUniformStride
						};
					} else {
						binding = ShaderDescriptorBinding{
							.descriptorType = gDescriptorTypeMap.at((SlangBindingType)typeLayout->getBindingRangeType(0)),
							.setIndex       = setIndex,
							.bindingIndex   = bindingIndex,
							.arraySize      = arraySize,
							.inputAttachmentIndex = {},
							.writable       = true
						};
					}
					break;
				case slang::ParameterCategory::Uniform:
					binding = ShaderConstantBinding{
						.offset = uniformOffset,
						.typeSize = uniformSize,
						.setIndex = setIndex,
						.bindingIndex = bindingIndex,
						.arraySize      = arraySize,
						.pushConstant = IsPushConstant(accessPath)
					};
					break;
			}
		}

		// traverse down tree

		switch (typeLayout->getKind()) {
			case slang::TypeReflection::Kind::Struct: {
				int fieldCount = typeLayout->getFieldCount();
				for (int f = 0; f < fieldCount; f++) {
					slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(f);
					if (field->getTypeLayout()->getKind() == slang::TypeReflection::Kind::ConstantBuffer && field->getTypeLayout()->getElementTypeLayout()->getName() == nullptr)
						EnumerateAccessPaths(field, binding, accessPath); // dont create sub-binding for unnamed cbuffers
					else
						EnumerateAccessPaths(field, binding[field->getName()], accessPath);
				}
				break;
			}
			case slang::TypeReflection::Kind::ConstantBuffer:
			case slang::TypeReflection::Kind::ParameterBlock:
			case slang::TypeReflection::Kind::TextureBuffer:
			case slang::TypeReflection::Kind::ShaderStorageBuffer:
				accessPath.deepestConstantBuffer = accessPath.leafNode;
				if (typeLayout->getSize((SlangParameterCategory)slang::ParameterCategory::SubElementRegisterSpace) != 0)
					accessPath.deepestParameterBlock = accessPath.leafNode;
				EnumerateAccessPaths(typeLayout->getElementVarLayout(), binding, accessPath);
				break;
		}
	}
};


ref<ShaderModule> ShaderModule::Create(
	const Device& device,
	const std::filesystem::path& sourceFile,
	const std::string& entryPoint,
	const std::string& profile,
	const ShaderDefines& defines,
	const std::vector<std::string>& compileArgs,
	const bool allowRetry) {

	if (!std::filesystem::exists(sourceFile))
		throw std::runtime_error(sourceFile.string() + " does not exist");

	static thread_local slang::IGlobalSession* session;
	slang::createGlobalSession(&session);

	slang::ICompileRequest* request;
	int targetIndex, entryPointIndex;
	do { // loop to allow user to retry compilation (e.g. after fixing an error)
		session->createCompileRequest(&request);

		// process compile args

		std::vector<const char*> args;
		for (const std::string& arg : compileArgs) args.emplace_back(arg.c_str());
		if (SLANG_FAILED(request->processCommandLineArguments(args.data(), args.size())))
			std::cerr << "Warning: Failed to process compile arguments while compiling " << sourceFile.stem() << "/" << entryPoint << std::endl;

		for (const auto& p : kDefaultIncludePaths)
			request->addSearchPath(p.c_str());

		// process defines

		targetIndex = request->addCodeGenTarget(SLANG_SPIRV);
		for (const auto&[n,d] : defines)
			request->addPreprocessorDefine(n.c_str(), d.c_str());

		const int translationUnitIndex = request->addTranslationUnit(SLANG_SOURCE_LANGUAGE_SLANG, nullptr);
		request->addTranslationUnitSourceFile(translationUnitIndex, sourceFile.string().c_str());

		entryPointIndex = request->addEntryPoint(translationUnitIndex, entryPoint.c_str(), SLANG_STAGE_NONE);
		request->setTargetProfile(targetIndex, session->findProfile(profile.c_str()));
		//request->setTargetFloatingPointMode(targetIndex, SLANG_FLOATING_POINT_MODE_FAST);
		request->setTargetMatrixLayoutMode(targetIndex, SLANG_MATRIX_LAYOUT_COLUMN_MAJOR);

		// compile

		SlangResult r = request->compile();
		std::cout << "Compiled " << sourceFile.string() << ":" << entryPoint << std::endl;
		const char* msg = request->getDiagnosticOutput();
		if (msg) std::cout << msg;
		if (SLANG_FAILED(r)) {
			if (allowRetry) {
				pfd::message n("Shader compilation failed", "Retry?", pfd::choice::yes_no);
				if (n.result() == pfd::button::yes)
					continue;
			}
			throw std::runtime_error(msg);
		}
		break;
	} while (true);

	auto shader = make_ref<ShaderModule>();
	shader->mEntryPointName = entryPoint;
	shader->mCompileTime = std::chrono::file_clock::now();

	// get spirv binary
	{
		slang::IBlob* blob;
		SlangResult r = request->getEntryPointCodeBlob(entryPointIndex, targetIndex, &blob);
		// error check is disabled because it seems to fail on warnings
		/*
		if (SLANG_FAILED(r)) {
			std::stringstream stream;
			stream << "facility 0x" << std::setfill('0') << std::setw(4) << std::hex << SLANG_GET_RESULT_FACILITY(r);
			stream << ", result 0x" << std::setfill('0') << std::setw(4) << std::hex << SLANG_GET_RESULT_CODE(r);
			const std::string msg = stream.str();
			cerr << "Error: Failed to get code blob for " << resourceName() << ": " << msg << endl;
			throw runtime_error(msg);
		}
		*/

		std::span spirv{(const uint32_t*)blob->getBufferPointer(), blob->getBufferSize()/sizeof(uint32_t)};

		shader->mSpirvHash = HashRange(spirv);
		shader->mModule = device->createShaderModule(vk::ShaderModuleCreateInfo{}.setCode(spirv));
		blob->Release();
	}

	device.SetDebugName(*shader->mModule, sourceFile.stem().string() + "/" + entryPoint);

	const int depCount = request->getDependencyFileCount();
	shader->mSourceFiles.reserve(depCount + 1);
	shader->mSourceFiles.emplace_back(sourceFile);
	for(int dep = 0; dep < depCount; dep++)
		shader->mSourceFiles.emplace_back(spGetDependencyFilePath(request, dep));

	// reflection
	{
		slang::ShaderReflection* shaderReflection = (slang::ShaderReflection*)request->getReflection();
		slang::EntryPointReflection* entryPointReflection = shaderReflection->getEntryPointByIndex(0);

		switch (entryPointReflection->getStage()) {
			case SLANG_STAGE_VERTEX:         shader->mStage = vk::ShaderStageFlagBits::eVertex; break;
			case SLANG_STAGE_HULL:           shader->mStage = vk::ShaderStageFlagBits::eTessellationControl; break;
			case SLANG_STAGE_DOMAIN:         shader->mStage = vk::ShaderStageFlagBits::eTessellationEvaluation; break;
			case SLANG_STAGE_GEOMETRY:       shader->mStage = vk::ShaderStageFlagBits::eGeometry; break;
			case SLANG_STAGE_FRAGMENT:       shader->mStage = vk::ShaderStageFlagBits::eFragment; break;
			case SLANG_STAGE_COMPUTE:        shader->mStage = vk::ShaderStageFlagBits::eCompute; break;
			case SLANG_STAGE_RAY_GENERATION: shader->mStage = vk::ShaderStageFlagBits::eRaygenKHR; break;
			case SLANG_STAGE_INTERSECTION:   shader->mStage = vk::ShaderStageFlagBits::eIntersectionKHR; break;
			case SLANG_STAGE_ANY_HIT:        shader->mStage = vk::ShaderStageFlagBits::eAnyHitKHR; break;
			case SLANG_STAGE_CLOSEST_HIT:    shader->mStage = vk::ShaderStageFlagBits::eClosestHitKHR; break;
			case SLANG_STAGE_MISS:           shader->mStage = vk::ShaderStageFlagBits::eMissKHR; break;
			case SLANG_STAGE_CALLABLE:       shader->mStage = vk::ShaderStageFlagBits::eCallableKHR; break;
			case SLANG_STAGE_MESH:           shader->mStage = vk::ShaderStageFlagBits::eMeshEXT; break;
			case SLANG_STAGE_AMPLIFICATION:  shader->mStage = vk::ShaderStageFlagBits::eTaskEXT; break;
			default: throw std::runtime_error("Unsupported shader stage");
		};

		/*if (shader->mStage == vk::ShaderStageFlagBits::eCompute)*/ {
			SlangUInt sz[3];
			entryPointReflection->getComputeThreadGroupSize(3, &sz[0]);
			shader->mWorkgroupSize = uint3( (uint32_t)sz[0], (uint32_t)sz[1], (uint32_t)sz[2] );
		}

		shader->mRootBinding = ShaderParameterBinding{};

		ParameterEnumerator e;
		{
			ParameterAccessPath accessPath = {};
			e.EnumerateAccessPaths(shaderReflection->getGlobalParamsVarLayout(), shader->mRootBinding, accessPath);
		}

		for (uint32_t i = 0; i < entryPointReflection->getParameterCount(); i++) {
			slang::VariableLayoutReflection* varLayout = entryPointReflection->getParameterByIndex(i);
			ParameterAccessPath accessPath = {};
			if (varLayout->getCategory() == slang::ParameterCategory::Uniform)
				accessPath.pushConstant = true;
			e.EnumerateAccessPaths(varLayout, shader->mRootBinding[varLayout->getName()], accessPath);
		}
	}

	request->Release();

	return shader;
}

}
