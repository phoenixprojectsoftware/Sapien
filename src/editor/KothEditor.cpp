/**
* Sapien:
* Copyright (c) 2026 The Phoenix Project Software SVG. 
*
*               Component: KothEditor
*				 Editor of zamnhlmp koth files
*
*
*               History:
**/

#include "KothEditor.h"

#include "Renderer.h"
#include "Bsp.h"
#include "Settings.h"
#include "util.h"
#include "log.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

KothEditor::KothEditor(Renderer* renderer)
{
	m_renderer = renderer;
}

void KothEditor::OnMapChanged(Bsp* map)
{
	m_zones.clear();
	m_selectedZone = -1;
	m_loadedForMap = false;
	m_currentPath.clear();

	if (!map || !map->bsp_valid || map->is_mdl_model || map->is_bsp_model)
		return;

	m_currentPath = GetDefaultKothPath(map);
	Load(m_currentPath);

	m_loadedForMap = true;
}

std::string KothEditor::GetDefaultKothPath(Bsp* map) const
{
	if (!map)
		return "";

	// Preferred: if the BSP is in ".../maps/mapname.bsp",
	// save beside it in ".../koth/mapname.koth".
	fs::path bspPath(map->bsp_path);
	fs::path parent = bspPath.parent_path();

	if (!parent.empty() && toLowerCase(parent.filename().string()) == "maps")
	{
		fs::path gameDir = parent.parent_path();
		return (gameDir / "koth" / (map->bsp_name + ".koth")).string();
	}

	// Fallback: use configured game dir.
	if (!g_game_dir.empty() && g_game_dir != "/")
	{
		return (fs::path(g_game_dir) / "koth" / (map->bsp_name + ".koth")).string();
	}

	// Final fallback: current working/export dir.
	return (fs::path(g_working_dir) / "koth" / (map->bsp_name + ".koth")).string();
}

bool KothEditor::Load(const std::string& path)
{
	m_zones.clear();
	m_selectedZone = -1;

	std::ifstream file(path);

	if (!file.is_open())
	{
		print_log("KOTH: No existing file found: {}\n", path);
		return false;
	}

	std::string line;
	int lineNum = 0;

	while (std::getline(file, line))
	{
		++lineNum;

		line = trimSpaces(line);

		if (line.empty())
			continue;

		if (line[0] == '#' || line[0] == '/')
			continue;

		KothZone zone;
		vec3 origin;
		vec3 size;

		std::istringstream iss(line);

		if (!(iss >> zone.name >> origin.x >> origin.y >> origin.z >> size.x >> size.y >> size.z))
		{
			print_log(PRINT_RED, "KOTH: Failed to parse line {}: {}\n", lineNum, line);
			continue;
		}

		zone.mins = origin - (size * 0.5f);
		zone.maxs = origin + (size * 0.5f);
		NormalizeZone(zone);

		m_zones.push_back(zone);
	}

	print_log("KOTH: Loaded {} zones from {}\n", m_zones.size(), path);
	return true;
}

bool KothEditor::Save(const std::string& path)
{
	fs::path outPath(path);

	if (!outPath.parent_path().empty())
		createDir(outPath.parent_path().string());

	std::ofstream file(path, std::ios::trunc);

	if (!file.is_open())
	{
		print_log(PRINT_RED, "KOTH: Failed to save {}\n", path);
		return false;
	}

	file << "# Sapien generated KOTH zones\n";
	file << "# Format: name originX originY originZ sizeX sizeY sizeZ\n\n";

	for (const KothZone& zone : m_zones)
	{
		vec3 origin = zone.origin();
		vec3 size = zone.size();

		file
			<< zone.name << " "
			<< origin.x << " " << origin.y << " " << origin.z << " "
			<< size.x << " " << size.y << " " << size.z << "\n";
	}

	print_log("KOTH: Saved {} zones to {}\n", m_zones.size(), path);
	return true;
}

void KothEditor::NormalizeZone(KothZone& zone)
{
	vec3 mins(
		std::min(zone.mins.x, zone.maxs.x),
		std::min(zone.mins.y, zone.maxs.y),
		std::min(zone.mins.z, zone.maxs.z)
	);

	vec3 maxs(
		std::max(zone.mins.x, zone.maxs.x),
		std::max(zone.mins.y, zone.maxs.y),
		std::max(zone.mins.z, zone.maxs.z)
	);

	zone.mins = mins;
	zone.maxs = maxs;
}

void KothEditor::AddZoneAtCamera()
{
	if (!m_renderer)
		return;

	KothZone zone;
	zone.name = m_newZoneName[0] ? m_newZoneName : "Hill";

	vec3 center = cameraOrigin + (m_renderer->cameraForward * 128.0f);

	zone.mins = center - (m_newZoneSize * 0.5f);
	zone.maxs = center + (m_newZoneSize * 0.5f);

	NormalizeZone(zone);

	m_zones.push_back(zone);
	m_selectedZone = (int)m_zones.size() - 1;
}

void KothEditor::DeleteSelectedZone()
{
	if (m_selectedZone < 0 || m_selectedZone >= (int)m_zones.size())
		return;

	m_zones.erase(m_zones.begin() + m_selectedZone);

	if (m_selectedZone >= (int)m_zones.size())
		m_selectedZone = (int)m_zones.size() - 1;
}

void KothEditor::SelectZoneUnderCursor()
{
	if (!m_renderer)
		return;

	vec3 start;
	vec3 dir;
	m_renderer->getPickRay(start, dir);

	float bestDist = FLT_MAX;
	int bestZone = -1;

	for (int i = 0; i < (int)m_zones.size(); ++i)
	{
		float dist = bestDist;

		if (pickAABB(start, dir, m_zones[i].mins, m_zones[i].maxs, dist))
		{
			bestDist = dist;
			bestZone = i;
		}
	}

	m_selectedZone = bestZone;
}

void KothEditor::Controls()
{
	if (!m_enabled)
		return;

	if (!m_renderer || !m_renderer->canControl)
		return;

	if (m_renderer->curLeftMouse == GLFW_PRESS && m_renderer->oldLeftMouse != GLFW_PRESS)
	{
		SelectZoneUnderCursor();
	}

	if (m_renderer->pressed[GLFW_KEY_N] && !m_renderer->oldPressed[GLFW_KEY_N])
	{
		AddZoneAtCamera();
	}

	if (m_renderer->pressed[GLFW_KEY_DELETE] && !m_renderer->oldPressed[GLFW_KEY_DELETE])
	{
		DeleteSelectedZone();
	}
}

void KothEditor::Draw3D()
{
	if (!m_enabled)
		return;

	if (!m_renderer)
		return;

	glDisable(GL_CULL_FACE);

	for (int i = 0; i < (int)m_zones.size(); ++i)
	{
		const bool selected = i == m_selectedZone;

		COLOR4 fill = selected
			? COLOR4(255, 220, 0, 72)
			: COLOR4(0, 180, 255, 48);

		m_renderer->drawBox(m_zones[i].mins, m_zones[i].maxs, fill);
	}

	glEnable(GL_CULL_FACE);
}

void KothEditor::DrawGui()
{
	if (!m_enabled)
		return;

	Bsp* map = m_renderer ? m_renderer->getSelectedMap() : nullptr;

	ImGui::Begin("KOTH Zone Editor", &m_enabled);

	if (!map || !map->bsp_valid || map->is_bsp_model || map->is_mdl_model)
	{
		ImGui::TextUnformatted("Open/select a BSP map to edit KOTH zones.");
		ImGui::End();
		return;
	}

	if (!m_loadedForMap)
		OnMapChanged(map);

	ImGui::Text("File: %s", m_currentPath.c_str());

	if (ImGui::Button("Reload"))
	{
		Load(m_currentPath);
	}

	ImGui::SameLine();

	if (ImGui::Button("Save"))
	{
		Save(m_currentPath);
	}

	ImGui::Separator();

	ImGui::InputText("New zone name", m_newZoneName, sizeof(m_newZoneName));
	ImGui::DragFloat3("New zone size", &m_newZoneSize.x, 1.0f, 1.0f, 8192.0f);

	if (ImGui::Button("Add zone in front of camera"))
	{
		AddZoneAtCamera();
	}

	ImGui::Separator();

	for (int i = 0; i < (int)m_zones.size(); ++i)
	{
		ImGui::PushID(i);

		bool selected = i == m_selectedZone;

		if (ImGui::Selectable(m_zones[i].name.c_str(), selected))
		{
			m_selectedZone = i;
		}

		ImGui::PopID();
	}

	if (m_selectedZone >= 0 && m_selectedZone < (int)m_zones.size())
	{
		KothZone& zone = m_zones[m_selectedZone];

		ImGui::Separator();
		ImGui::Text("Selected zone");

		ImGui::InputText("Name", &zone.name);
		ImGui::DragFloat3("Mins", &zone.mins.x, 1.0f);
		ImGui::DragFloat3("Maxs", &zone.maxs.x, 1.0f);

		if (ImGui::Button("Normalize mins/maxs"))
		{
			NormalizeZone(zone);
		}

		ImGui::SameLine();

		if (ImGui::Button("Delete"))
		{
			DeleteSelectedZone();
		}

		vec3 origin = zone.origin();
		vec3 size = zone.size();

		ImGui::Text("Export origin: %.1f %.1f %.1f", origin.x, origin.y, origin.z);
		ImGui::Text("Export size: %.1f %.1f %.1f", size.x, size.y, size.z);
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Controls:");
	ImGui::TextUnformatted("LMB: select zone");
	ImGui::TextUnformatted("N: add zone in front of camera");
	ImGui::TextUnformatted("Delete: delete selected zone");

	ImGui::End();
}
