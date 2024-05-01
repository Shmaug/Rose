#pragma once

#include <optional>

#include "Mesh.hpp"
#include "Material.h"
#include <Render/Transform/Transform.h>

namespace RoseEngine {

class SceneNode : public std::enable_shared_from_this<SceneNode> {
private:
	std::string                 name = "";
	weak_ref<SceneNode>         parent = {};
	std::vector<ref<SceneNode>> children = {};

	inline SceneNode() {}

public:
	SceneNode(const SceneNode&) = default;
	SceneNode(SceneNode&&) = default;
	SceneNode& operator=(const SceneNode&) = default;
	SceneNode& operator=(SceneNode&&) = default;

	inline static ref<SceneNode> Create(const std::string& name) {
		SceneNode n = SceneNode();
		n.name = name;
		return make_ref<SceneNode>(n);
	}

	inline const std::string& Name() const { return name; }

	inline ref<SceneNode> GetParent() {
		return parent.lock();
	}
	inline void SetParent(const ref<SceneNode>& newParent) {
		const ref<SceneNode> oldParent = GetParent();
		if (oldParent == newParent) return;
		if (newParent) newParent->children.emplace_back(shared_from_this());
		if (oldParent) oldParent->children.erase(std::ranges::find(oldParent->children, this, &ref<SceneNode>::get));
		parent = newParent;
	}

	inline auto begin() { return children.begin(); }
	inline auto begin() const { return children.begin(); }
	inline auto end() const { return children.end(); }

	std::optional<Transform> transform = std::nullopt;
	ref<Mesh> mesh = {};
	ref<Material<ImageView>> material = {};
};

}