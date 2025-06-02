#include <Rose/Core/WindowedApp.hpp>
#include <Rose/WorkGraph/CommandGraph.hpp>
#include <imnodes.h>

using namespace RoseEngine;

class NodeWidget {
private:
	ImNodesContext* nodeContext;

	std::unordered_map<int, WorkNodeId> nodeIdMap;
	std::unordered_map<int, std::pair<WorkAttributePointer, bool>> attributeIdMap;
	std::unordered_map<int, std::pair<WorkAttributePointer, WorkAttributePointer>> linkIdMap;

	CommandGraph graph;
	std::optional<WorkNodeId> hovered = std::nullopt;

public:
	NodeWidget(CommandContext& context) {
		nodeContext = ImNodes::CreateContext();
	}
	~NodeWidget() {
		//ImNodes::DestroyContext(nodeContext);
	}

	void RenderProperties(CommandContext& context) {

	}

	void RenderNodes(CommandContext& context) {
		ImNodes::SetCurrentContext(nodeContext);
		ImNodes::BeginNodeEditor();

		// Context menu
		/*if (ImNodes::IsEditorHovered())*/ {
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 8.f));
			if (ImGui::BeginPopupContextWindow()) {
				const ImVec2 clickPos = ImGui::GetMousePosOnOpeningCurrentPopup();

				if (hovered.has_value()) {
					// Node context menu
					if (ImGui::MenuItem("Delete")) graph.erase(*hovered);
				} else {
					// Background context menu
					if (ImGui::BeginMenu("Add node")) {
						WorkNodeId id = GetUniqueNodeId();
						bool c = false;
						if (ImGui::MenuItem("Create Resource")) { c = true; graph[id] = ResourceCreateNode{ .nodeId = id }; }
						if (ImGui::MenuItem("Copy Resource"))   { c = true; graph[id] = ResourceCopyNode{ .nodeId = id }; }
						if (ImGui::MenuItem("Compute program")) { c = true; graph[id] = ComputeProgramNode{ .nodeId = id }; }
						if (c) {
							int nodeId = GetImNodeId(id);
							nodeIdMap[nodeId] = id;
							ImNodes::SetNodeScreenSpacePos(nodeId, clickPos);
						}
						ImGui::EndMenu();
					}
				}

				ImGui::EndPopup();
			}
			ImGui::PopStyleVar();
		}

		// draw nodes
		for (auto&[id, node] : graph.nodes) {
			int nodeId = GetImNodeId(id);
			ImNodes::BeginNode(nodeId);
			std::visit([&](auto& v) {
				DrawNode(context, v);
				for (const WorkNodeAttribute& attribute : GetAttributes(v)) {
					attributeIdMap[GetImNodeId(id, attribute.name)] = {
						WorkAttributePointer{id, attribute.name},
						(uint32_t)(attribute.flags & WorkAttributeFlagBits::eInput) != 0 };
				}
			}, node);
			ImNodes::EndNode();
			nodeIdMap[nodeId] = id;
		}

		// draw links
		for (const auto&[dst, src] : graph.edges) {
			int linkId = GetImNodeId(src, dst);
			ImNodes::Link(linkId, GetImNodeId(src), GetImNodeId(dst));
			linkIdMap[linkId] = {src, dst};
		}

		ImNodes::EndNodeEditor();

		// handle new edges
		{
            int srcAttr, dstAttr;
            if (ImNodes::IsLinkCreated(&srcAttr, &dstAttr)) {
				auto [src, srcInput] = attributeIdMap.at(srcAttr);
				auto [dst, dstInput] = attributeIdMap.at(dstAttr);
				if (srcInput != dstInput) {
					if (dstInput) std::swap(dst, src);
					graph.edges.emplace(src, dst);
					linkIdMap[GetImNodeId(src, dst)] = {src, dst};
				}
            }
        }

		// handle delinked edges
        {
            int linkId;
            if (ImNodes::IsLinkDestroyed(&linkId)) {
				const auto&[src,dst] = linkIdMap.at(linkId);
                graph.edges.erase(graph.edges.find(dst));
			}
        }

		// handle edge/node deletion
		if (ImGui::IsKeyReleased(ImGuiKey_Delete) || ImGui::IsKeyReleased(ImGuiKey_X)) {
			// delete selected links
			{
				const int num_selected = ImNodes::NumSelectedLinks();
				if (num_selected > 0) {
					static std::vector<int> linkIds;
					linkIds.resize(static_cast<size_t>(num_selected));
					ImNodes::GetSelectedLinks(linkIds.data());
					for (const int linkId : linkIds) {
						const auto&[src,dst] = linkIdMap.at(linkId);
                		graph.edges.erase(graph.edges.find(dst));
					}
				}
			}

			// delete selected nodes
			{
				const int num_selected = ImNodes::NumSelectedNodes();
				if (num_selected > 0) {
					static std::vector<int> selected_nodes;
					selected_nodes.resize(static_cast<size_t>(num_selected));
					ImNodes::GetSelectedNodes(selected_nodes.data());
					for (const int nodeId : selected_nodes)
						graph.erase(nodeIdMap.at(nodeId));
				}
			}
		}

		/*
		int hovered_;
		if (ImNodes::IsNodeHovered(&hovered_))
			hovered = nodeIdMap.at(hovered_);
		else
			hovered.reset();
		*/
	}
};

int main(int argc, const char** argv) {
	WindowedApp app("Work graph test", { VK_KHR_SWAPCHAIN_EXTENSION_NAME });

	NodeWidget nodeEditor(*app.contexts[0]);

	app.AddWidget("Properties", [&]() { nodeEditor.RenderProperties(*app.contexts[app.swapchain->ImageIndex()]); }, true);
	app.AddWidget("Nodes",      [&]() { nodeEditor.RenderNodes(     *app.contexts[app.swapchain->ImageIndex()]); }, true);

	app.Run();

	app.device->Wait();

	return EXIT_SUCCESS;
}