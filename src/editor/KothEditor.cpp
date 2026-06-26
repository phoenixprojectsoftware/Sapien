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
#include <cmath>

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
	file << std::fixed << std::setprecision(1);

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
	zone.name = MakeUniqueZoneName(m_newZoneName);

	vec3 center = cameraOrigin + (m_renderer->cameraForward * 128.0f);

	zone.mins = center - (m_newZoneSize * 0.5f);
	zone.maxs = center + (m_newZoneSize * 0.5f);

	NormalizeZone(zone);
	SnapZone(zone);

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

	const bool ctrlDown =
		m_renderer->pressed[GLFW_KEY_LEFT_CONTROL] ||
		m_renderer->pressed[GLFW_KEY_RIGHT_CONTROL];

	const bool shiftDown =
		m_renderer->pressed[GLFW_KEY_LEFT_SHIFT] ||
		m_renderer->pressed[GLFW_KEY_RIGHT_SHIFT];

	const bool lmbPressed =
		m_renderer->curLeftMouse == GLFW_PRESS &&
		m_renderer->oldLeftMouse != GLFW_PRESS;

	const bool lmbHeld =
		m_renderer->curLeftMouse == GLFW_PRESS;

	const bool lmbReleased =
		m_renderer->curLeftMouse != GLFW_PRESS &&
		m_renderer->oldLeftMouse == GLFW_PRESS;

	if (ctrlDown && lmbPressed)
	{
		BeginDrawZone();
		return;
	}

	if (m_isDrawingZone && lmbHeld)
	{
		UpdateDrawZone();
		return;
	}

	if (m_isDrawingZone && lmbReleased)
	{
		FinishDrawZone();
		return;
	}

	if (m_renderer->pressed[GLFW_KEY_ESCAPE] && !m_renderer->oldPressed[GLFW_KEY_ESCAPE])
	{
		CancelDrawZone();
		return;
	}

	// Normal click select, but only when not holding Ctrl.
	if (!ctrlDown && lmbPressed)
	{
		SelectZoneUnderCursor();
		return;
	}

	if (m_renderer->pressed[GLFW_KEY_N] && !m_renderer->oldPressed[GLFW_KEY_N])
	{
		AddZoneAtCamera();
		return;
	}

	if (m_renderer->pressed[GLFW_KEY_DELETE] && !m_renderer->oldPressed[GLFW_KEY_DELETE])
	{
		DeleteSelectedZone();
		return;
	}

	if (m_selectedZone < 0 || m_selectedZone >= (int)m_zones.size())
		return;

	const float moveStep = m_snapEnabled ? m_snapGrid : m_moveStep;
	const float resizeStep = m_snapEnabled ? m_snapGrid : m_resizeStep;

	vec3 moveDelta;
	vec3 resizeDelta;

	// Arrow movement / resizing.
	if (m_renderer->pressed[GLFW_KEY_LEFT] && !m_renderer->oldPressed[GLFW_KEY_LEFT])
	{
		if (shiftDown)
			resizeDelta.x -= resizeStep;
		else
			moveDelta.x -= moveStep;
	}

	if (m_renderer->pressed[GLFW_KEY_RIGHT] && !m_renderer->oldPressed[GLFW_KEY_RIGHT])
	{
		if (shiftDown)
			resizeDelta.x += resizeStep;
		else
			moveDelta.x += moveStep;
	}

	if (m_renderer->pressed[GLFW_KEY_DOWN] && !m_renderer->oldPressed[GLFW_KEY_DOWN])
	{
		if (shiftDown)
			resizeDelta.y -= resizeStep;
		else
			moveDelta.y -= moveStep;
	}

	if (m_renderer->pressed[GLFW_KEY_UP] && !m_renderer->oldPressed[GLFW_KEY_UP])
	{
		if (shiftDown)
			resizeDelta.y += resizeStep;
		else
			moveDelta.y += moveStep;
	}

	if (m_renderer->pressed[GLFW_KEY_PAGE_DOWN] && !m_renderer->oldPressed[GLFW_KEY_PAGE_DOWN])
	{
		if (shiftDown)
			resizeDelta.z -= resizeStep;
		else
			moveDelta.z -= moveStep;
	}

	if (m_renderer->pressed[GLFW_KEY_PAGE_UP] && !m_renderer->oldPressed[GLFW_KEY_PAGE_UP])
	{
		if (shiftDown)
			resizeDelta.z += resizeStep;
		else
			moveDelta.z += moveStep;
	}

	if (moveDelta != vec3())
		MoveSelectedZone(moveDelta);

	if (resizeDelta != vec3())
		ResizeSelectedZone(resizeDelta);
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

	if (m_isDrawingZone)
	{
		KothZone preview = MakeDrawPreviewZone();
		NormalizeZone(preview);

		m_renderer->drawBox(
			preview.mins,
			preview.maxs,
			COLOR4(0, 255, 120, 80)
		);
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

	DrawLabels();

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

	ImGui::Checkbox("Snap to grid", &m_snapEnabled);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100.0f);
	ImGui::DragFloat("Grid", &m_snapGrid, 1.0f, 1.0f, 1024.0f);

	ImGui::Checkbox("Show labels", &m_showLabels);

	ImGui::SetNextItemWidth(100.0f);
	ImGui::DragFloat("Move step", &m_moveStep, 1.0f, 1.0f, 1024.0f);

	ImGui::SetNextItemWidth(100.0f);
	ImGui::DragFloat("Resize step", &m_resizeStep, 1.0f, 1.0f, 1024.0f);

	ImGui::Separator();

	ImGui::DragFloat("DRAW HEIGHT", &m_drawHeight, 1.0f, 8.0f, 2048.0f);
	ImGui::InputText("New zone name base", m_newZoneName, sizeof(m_newZoneName));
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
		if (ImGui::Button("Snap selected"))
			SnapZone(zone);

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
	ImGui::TextUnformatted("Ctrl + LMB drag: draw new zone");
	ImGui::TextUnformatted("LMB: select zone");
	ImGui::TextUnformatted("N: add zone in front of camera");
	ImGui::TextUnformatted("Arrow keys: move selected X/Y");
	ImGui::TextUnformatted("Page Up / Page Down: move selected Z");
	ImGui::TextUnformatted("Shift + Arrow keys: resize selected X/Y");
	ImGui::TextUnformatted("Shift + Page Up / Down: resize selected Z");
	ImGui::TextUnformatted("Delete: delete selected zone");
	ImGui::TextUnformatted("Escape: cancel drawing");

	ImGui::End();
}

bool KothEditor::TraceCursorToWorld(vec3& outPos)
{
	if (!m_renderer)
		return false;

	Bsp* map = m_renderer->getSelectedMap();

	if (!map || !map->bsp_valid || !map->getBspRender())
		return false;

	vec3 start;
	vec3 dir;
	m_renderer->getPickRay(start, dir);

	PickInfo pickInfo;
	pickInfo.bestDist = g_limits.fltMaxCoord * 2.0f + 1.0f;

	Bsp* pickedMap = map;

	if (!map->getBspRender()->pickPoly(start, dir, -1, pickInfo, &pickedMap))
		return false;

	outPos = start + dir * pickInfo.bestDist;
	return true;
}

void KothEditor::BeginDrawZone()
{
	vec3 hit;

	if (!TraceCursorToWorld(hit))
		return;

	hit.z += 1.0f;

	if (m_snapEnabled)
		hit = SnapVec(hit);

	m_isDrawingZone = true;
	m_drawStart = hit;
	m_drawEnd = hit;
}

void KothEditor::UpdateDrawZone()
{
	if (!m_isDrawingZone)
		return;

	vec3 hit;

	if (!TraceCursorToWorld(hit))
		return;

	hit.z += 1.0f;

	if (m_snapEnabled)
		hit = SnapVec(hit);

	m_drawEnd = hit;
}

void KothEditor::FinishDrawZone()
{
	if (!m_isDrawingZone)
		return;

	KothZone zone = MakeDrawPreviewZone();
	NormalizeZone(zone);

	vec3 size = zone.size();

	// Avoid accidental tiny zones.
	if (size.x >= 8.0f && size.y >= 8.0f && size.z >= 8.0f)
	{
		zone.name = MakeUniqueZoneName(m_newZoneName);
		SnapZone(zone);

		m_zones.push_back(zone);
		m_selectedZone = (int)m_zones.size() - 1;
	}

	m_isDrawingZone = false;
}

void KothEditor::CancelDrawZone()
{
	m_isDrawingZone = false;
}

KothZone KothEditor::MakeDrawPreviewZone() const
{
	KothZone zone;

	zone.name = m_newZoneName[0] ? m_newZoneName : "Hill";

	const float minX = std::min(m_drawStart.x, m_drawEnd.x);
	const float maxX = std::max(m_drawStart.x, m_drawEnd.x);

	const float minY = std::min(m_drawStart.y, m_drawEnd.y);
	const float maxY = std::max(m_drawStart.y, m_drawEnd.y);

	const float baseZ = std::min(m_drawStart.z, m_drawEnd.z);
	const float height = m_snapEnabled ? SnapFloat(m_drawHeight) : m_drawHeight;

	zone.mins = vec3(minX, minY, baseZ);
	zone.maxs = vec3(maxX, maxY, baseZ + height);

	return zone;
}

float KothEditor::SnapFloat(float value) const
{
	if (!m_snapEnabled || m_snapGrid <= 0.0f)
		return value;

	return roundf(value / m_snapGrid) * m_snapGrid;
}

vec3 KothEditor::SnapVec(const vec3& v) const
{
	return vec3(
		SnapFloat(v.x),
		SnapFloat(v.y),
		SnapFloat(v.z)
	);
}

void KothEditor::SnapZone(KothZone& zone)
{
	if (!m_snapEnabled)
		return;

	zone.mins = SnapVec(zone.mins);
	zone.maxs = SnapVec(zone.maxs);

	NormalizeZone(zone);
}

std::string KothEditor::MakeUniqueZoneName(const char* baseName) const
{
	std::string base = baseName && baseName[0] ? baseName : "Hill";

	// Aura-SE currently parses names with %s, so avoid spaces.
	for (char& c : base)
	{
		if (c == ' ' || c == '\t')
			c = '_';
	}

	for (int i = 1; i < 1000; ++i)
	{
		char candidate[128];
		snprintf(candidate, sizeof(candidate), "%s%02d", base.c_str(), i);

		bool used = false;

		for (const KothZone& zone : m_zones)
		{
			if (zone.name == candidate)
			{
				used = true;
				break;
			}
		}

		if (!used)
			return candidate;
	}

	return base;
}

void KothEditor::MoveSelectedZone(const vec3& delta)
{
	if (m_selectedZone < 0 || m_selectedZone >= (int)m_zones.size())
		return;

	KothZone& zone = m_zones[m_selectedZone];

	zone.mins += delta;
	zone.maxs += delta;

	SnapZone(zone);
}

void KothEditor::ResizeSelectedZone(const vec3& delta)
{
	if (m_selectedZone < 0 || m_selectedZone >= (int)m_zones.size())
		return;

	KothZone& zone = m_zones[m_selectedZone];

	zone.maxs += delta;

	NormalizeZone(zone);

	vec3 size = zone.size();

	// Prevent inverted or tiny zones.
	if (size.x < 8.0f)
		zone.maxs.x = zone.mins.x + 8.0f;

	if (size.y < 8.0f)
		zone.maxs.y = zone.mins.y + 8.0f;

	if (size.z < 8.0f)
		zone.maxs.z = zone.mins.z + 8.0f;

	SnapZone(zone);
}

void KothEditor::DrawLabels()
{
	if (!m_showLabels || !m_renderer)
		return;

	ImDrawList* drawList = ImGui::GetForegroundDrawList();

	for (int i = 0; i < (int)m_zones.size(); ++i)
	{
		const KothZone& zone = m_zones[i];

		vec3 topCenter = zone.origin();
		topCenter.z = zone.maxs.z + 16.0f;

		vec2 screen;

		if (!m_renderer->worldToScreen(topCenter, screen))
			continue;

		const char* text = zone.name.c_str();
		ImVec2 textSize = ImGui::CalcTextSize(text);

		ImVec2 pos(
			screen.x - textSize.x * 0.5f,
			screen.y - textSize.y * 0.5f
		);

		ImU32 textColor = i == m_selectedZone
			? IM_COL32(255, 230, 64, 255)
			: IM_COL32(80, 220, 255, 255);

		// Tiny dark backing so it is readable on bright maps.
		drawList->AddText(
			ImVec2(pos.x + 1.0f, pos.y + 1.0f),
			IM_COL32(0, 0, 0, 220),
			text
		);

		drawList->AddText(
			pos,
			textColor,
			text
		);
	}
}
