/**
* Sapien:
* Copyright (c) 2026 The Phoenix Project Software SVG.
*
*               Component: AuraPointModeEditor
*				 Editor of Aura gamemode entities
*
*
*               History:
**/

#include "AuraPointModeEditor.h"

#include "Renderer.h"
#include "Bsp.h"
#include "BspRenderer.h"
#include "bsplimits.h"
#include "Settings.h"
#include "util.h"
#include "log.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include <cmath>
#include <cfloat>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <GLFW/glfw3.h>

static constexpr float M_PI = 3.14159265358979323846f;

static vec3 YawToForward(float yawDegrees)
{
	const float radians = yawDegrees * (M_PI / 180.f);

	return vec3(cosf(radians), sinf(radians), 0.0f);
}

static vec3 YawToRight(float yawDegrees)
{
	const float radians = (yawDegrees + 90.0f) * (M_PI / 180.0f);
	return vec3(cosf(radians), sinf(radians), 0.0f);
}

namespace fs = std::filesystem;

AuraPointModeEditor::AuraPointModeEditor(Renderer* renderer)
{
	m_renderer = renderer;
}

const char* AuraPointModeEditor::GetModeName() const
{
	switch (m_mode)
	{
	case AuraPointMode::CTF:
		return "CTF";

	case AuraPointMode::DOM:
		return "DOM";

	default:
		return "None";
	}
}

const char* AuraPointModeEditor::GetModeFolder() const
{
	switch (m_mode)
	{
	case AuraPointMode::CTF:
		return "ctf";

	case AuraPointMode::DOM:
		return "dom";

	default:
		return "";
	}
}

const char* AuraPointModeEditor::GetModeExtension() const
{
	switch (m_mode)
	{
	case AuraPointMode::CTF:
		return ".ctf";

	case AuraPointMode::DOM:
		return ".dom";

	default:
		return "";
	}
}

void AuraPointModeEditor::SetMode(AuraPointMode mode)
{
	if (mode == AuraPointMode::None)
	{
		m_enabled = false;
		m_mode = AuraPointMode::None;
		m_points.clear();
		m_selectedPoint = -1;
		m_currentPath.clear();
		m_loadedForMap = false;
		return;
	}

	m_enabled = true;
	m_mode = mode;
	m_loadedForMap = false;
	m_points.clear();
	m_selectedPoint = -1;
	m_currentPath.clear();

	Bsp* map = m_renderer ? m_renderer->getSelectedMap() : nullptr;
	OnMapChanged(map);
}

void AuraPointModeEditor::ToggleMode(AuraPointMode mode)
{
	if (m_enabled && m_mode == mode)
	{
		SetMode(AuraPointMode::None);
		return;
	}

	SetMode(mode);
}

void AuraPointModeEditor::OnMapChanged(Bsp* map)
{
	m_points.clear();
	m_selectedPoint = -1;
	m_loadedForMap = false;
	m_currentPath.clear();

	if (!m_enabled || m_mode == AuraPointMode::None)
		return;

	if (!map || !map->bsp_valid || map->is_mdl_model || map->is_bsp_model)
		return;

	m_currentPath = GetModePath(map);
	Load(m_currentPath);

	m_loadedForMap = true;
}

std::string AuraPointModeEditor::GetModePath(Bsp* map) const
{
	if (!map)
		return "";

	const char* folder = GetModeFolder();
	const char* ext = GetModeExtension();

	if (!folder[0] || !ext[0])
		return "";

	fs::path bspPath(map->bsp_path);
	fs::path parent = bspPath.parent_path();

	// Preferred:
	// game/maps/mapname.bsp -> game/ctf/mapname.ctf
	// game/maps/mapname.bsp -> game/dom/mapname.dom
	if (!parent.empty() && toLowerCase(parent.filename().string()) == "maps")
	{
		fs::path gameDir = parent.parent_path();
		return (gameDir / folder / (map->bsp_name + ext)).string();
	}

	// Fallback to configured game dir.
	if (!g_game_dir.empty() && g_game_dir != "/")
	{
		return (fs::path(g_game_dir) / folder / (map->bsp_name + ext)).string();
	}

	// Final fallback.
	return (fs::path(g_working_dir) / folder / (map->bsp_name + ext)).string();
}

bool AuraPointModeEditor::IsAllowedClassname(const std::string& classname) const
{
	if (m_mode == AuraPointMode::CTF)
	{
		return classname == "item_flag_team1" ||
			classname == "item_flag_team2" ||
			classname == "info_player_team1" ||
			classname == "info_player_team2";
	}

	if (m_mode == AuraPointMode::DOM)
	{
		return classname == "item_dom_controlpoint";
	}

	return false;
}

const char* AuraPointModeEditor::GetDefaultClassname() const
{
	if (m_mode == AuraPointMode::CTF)
		return "item_flag_team1";

	if (m_mode == AuraPointMode::DOM)
		return "item_dom_controlpoint";

	return "";
}

void AuraPointModeEditor::SanitizePoint(AuraModePoint& point)
{
	if (!IsAllowedClassname(point.classname))
		point.classname = GetDefaultClassname();

	if (m_mode == AuraPointMode::DOM)
	{
		if (point.data1.empty())
			point.data1 = "ControlPoint";

		for (char& c : point.data1)
		{
			if (c == ' ' || c == '\t')
				c = '_';
		}
	}
	else
	{
		point.data1.clear();
	}
}

bool AuraPointModeEditor::Load(const std::string& path)
{
	m_points.clear();
	m_selectedPoint = -1;

	std::ifstream file(path);

	if (!file.is_open())
	{
		print_log("Aura {} editor: no existing file found: {}\n", GetModeName(), path);
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

		AuraModePoint point;

		std::istringstream iss(line);

		if (m_mode == AuraPointMode::CTF)
		{
			if (!(iss >> point.classname
				>> point.origin.x >> point.origin.y >> point.origin.z
				>> point.angles.x >> point.angles.y >> point.angles.z))
			{
				print_log(PRINT_RED, "Aura CTF editor: failed to parse line {}: {}\n", lineNum, line);
				continue;
			}
		}
		else if (m_mode == AuraPointMode::DOM)
		{
			if (!(iss >> point.classname
				>> point.origin.x >> point.origin.y >> point.origin.z
				>> point.angles.x >> point.angles.y >> point.angles.z
				>> point.data1))
			{
				print_log(PRINT_RED, "Aura DOM editor: failed to parse line {}: {}\n", lineNum, line);
				continue;
			}
		}
		else
		{
			continue;
		}

		if (!IsAllowedClassname(point.classname))
		{
			print_log(
				PRINT_RED,
				"Aura {} editor: ignored unsupported classname '{}' on line {}\n",
				GetModeName(),
				point.classname,
				lineNum
			);

			continue;
		}

		SanitizePoint(point);
		m_points.push_back(point);
	}

	print_log("Aura {} editor: loaded {} points from {}\n", GetModeName(), m_points.size(), path);
	return true;
}

bool AuraPointModeEditor::Save(const std::string& path)
{
	if (path.empty())
		return false;

	fs::path outPath(path);

	if (!outPath.parent_path().empty())
		createDir(outPath.parent_path().string());

	std::ofstream file(path, std::ios::trunc);

	if (!file.is_open())
	{
		print_log(PRINT_RED, "Aura {} editor: failed to save {}\n", GetModeName(), path);
		return false;
	}

	if (m_mode == AuraPointMode::CTF)
	{
		file << "# Sapien generated CTF entities\n";
		file << "# For Aura-SE Capture the Flag\n";
		file << "# Format: classname originX originY originZ angleX angleY angleZ\n\n";
	}
	else if (m_mode == AuraPointMode::DOM)
	{
		file << "# Sapien generated DOM entities\n";
		file << "# For Aura-SE Domination\n";
		file << "# Format: classname originX originY originZ angleX angleY angleZ data1\n\n";
	}

	file << std::fixed << std::setprecision(1);

	for (AuraModePoint point : m_points)
	{
		SanitizePoint(point);

		if (m_mode == AuraPointMode::CTF)
		{
			file
				<< point.classname << " "
				<< point.origin.x << " " << point.origin.y << " " << point.origin.z << " "
				<< point.angles.x << " " << point.angles.y << " " << point.angles.z << "\n";
		}
		else if (m_mode == AuraPointMode::DOM)
		{
			file
				<< point.classname << " "
				<< point.origin.x << " " << point.origin.y << " " << point.origin.z << " "
				<< point.angles.x << " " << point.angles.y << " " << point.angles.z << " "
				<< point.data1 << "\n";
		}
	}

	print_log("Aura {} editor: saved {} points to {}\n", GetModeName(), m_points.size(), path);
	return true;
}

void AuraPointModeEditor::AddDefaultPoint()
{
	if (m_mode == AuraPointMode::None)
		return;

	AuraModePoint point;

	if (m_mode == AuraPointMode::CTF)
	{
		point.classname = m_newClassname.empty() ? GetDefaultClassname() : m_newClassname;
	}
	else if (m_mode == AuraPointMode::DOM)
	{
		point.classname = "item_dom_controlpoint";
		point.data1 = m_newDomName.empty() ? "ControlPoint" : m_newDomName;
	}
	else
	{
		return;
	}

	if (m_renderer)
		point.origin = cameraOrigin + (m_renderer->cameraForward * 128.0f);
	else
		point.origin = vec3();

	point.angles = vec3();

	SanitizePoint(point);
	SnapPoint(point);

	m_points.push_back(point);
	m_selectedPoint = (int)m_points.size() - 1;
}

void AuraPointModeEditor::DeleteSelectedPoint()
{
	if (m_selectedPoint < 0 || m_selectedPoint >= (int)m_points.size())
		return;

	m_points.erase(m_points.begin() + m_selectedPoint);

	if (m_selectedPoint >= (int)m_points.size())
		m_selectedPoint = (int)m_points.size() - 1;
}

void AuraPointModeEditor::DrawGui()
{
	if (!m_enabled || m_mode == AuraPointMode::None)
		return;

	Bsp* map = m_renderer ? m_renderer->getSelectedMap() : nullptr;

	char title[64];
	snprintf(title, sizeof(title), "Aura %s Entity Editor", GetModeName());

	bool open = true;

	ImGui::Begin(title, &open);

	if (!open)
	{
		ImGui::End();
		SetMode(AuraPointMode::None);
		return;
	}

	if (!map || !map->bsp_valid || map->is_bsp_model || map->is_mdl_model)
	{
		ImGui::TextUnformatted("Open/select a BSP map to edit Aura mode entities.");
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

	ImGui::SameLine();

	if (ImGui::Button("Add"))
	{
		AddDefaultPoint();
	}

	ImGui::SameLine();

	if (ImGui::Button("Delete"))
	{
		DeleteSelectedPoint();
	}

	ImGui::Separator();

	ImGui::Separator();

	ImGui::Checkbox("Snap to grid", &m_snapEnabled);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100.0f);
	ImGui::DragFloat("Grid", &m_snapGrid, 1.0f, 1.0f, 1024.0f);

	ImGui::SetNextItemWidth(100.0f);
	ImGui::DragFloat("Move step", &m_moveStep, 1.0f, 1.0f, 1024.0f);

	ImGui::SetNextItemWidth(100.0f);
	ImGui::DragFloat("Rotate step", &m_rotateStep, 1.0f, 1.0f, 90.0f);

	ImGui::Separator();

	if (m_mode == AuraPointMode::CTF)
	{
		const char* classnames[] =
		{
			"item_flag_team1",
			"item_flag_team2",
			"info_player_team1",
			"info_player_team2"
		};

		int current = 0;

		for (int i = 0; i < 4; ++i)
		{
			if (m_newClassname == classnames[i])
			{
				current = i;
				break;
			}
		}

		if (ImGui::Combo("New point type", &current, classnames, 4))
			m_newClassname = classnames[current];
	}
	else if (m_mode == AuraPointMode::DOM)
	{
		ImGui::InputText("New control point name", &m_newDomName);
	}

	ImGui::Separator();

	if (m_points.empty())
	{
		ImGui::TextUnformatted("No points loaded. Press Add to create one.");
	}

	for (int i = 0; i < (int)m_points.size(); ++i)
	{
		ImGui::PushID(i);

		const AuraModePoint& point = m_points[i];

		std::string label = point.classname;

		if (m_mode == AuraPointMode::DOM && !point.data1.empty())
		{
			label += " - ";
			label += point.data1;
		}

		if (ImGui::Selectable(label.c_str(), i == m_selectedPoint))
		{
			m_selectedPoint = i;
		}

		ImGui::PopID();
	}

	if (m_selectedPoint >= 0 && m_selectedPoint < (int)m_points.size())
	{
		AuraModePoint& point = m_points[m_selectedPoint];

		ImGui::Separator();
		ImGui::TextUnformatted("Selected point");

		if (m_mode == AuraPointMode::CTF)
		{
			const char* classnames[] =
			{
				"item_flag_team1",
				"item_flag_team2",
				"info_player_team1",
				"info_player_team2"
			};

			int current = 0;

			for (int i = 0; i < 4; ++i)
			{
				if (point.classname == classnames[i])
				{
					current = i;
					break;
				}
			}

			if (ImGui::Combo("Classname", &current, classnames, 4))
				point.classname = classnames[current];
		}
		else if (m_mode == AuraPointMode::DOM)
		{
			point.classname = "item_dom_controlpoint";
			ImGui::InputText("Location name", &point.data1);
		}

		ImGui::DragFloat3("Origin", &point.origin.x, 1.0f);
		ImGui::DragFloat3("Angles", &point.angles.x, 1.0f);

		if (ImGui::Button("Sanitize"))
		{
			SanitizePoint(point);
		}

		vec3 origin = point.origin;
		vec3 angles = point.angles;

		ImGui::Text("Export origin: %.1f %.1f %.1f", origin.x, origin.y, origin.z);
		ImGui::Text("Export angles: %.1f %.1f %.1f", angles.x, angles.y, angles.z);

		if (m_mode == AuraPointMode::DOM)
			ImGui::Text("Export data1: %s", point.data1.c_str());
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Controls:");
	ImGui::TextUnformatted("Ctrl + LMB: place new point on map geometry");
	ImGui::TextUnformatted("LMB: select point marker");
	ImGui::TextUnformatted("Arrow keys: move selected X/Y");
	ImGui::TextUnformatted("Page Up / Page Down: move selected Z");
	ImGui::TextUnformatted("Q / E: rotate selected yaw");
	ImGui::TextUnformatted("Delete: delete selected point");

	ImGui::End();
}

COLOR4 AuraPointModeEditor::GetPointColor(const AuraModePoint& point, bool selected) const
{
	if (selected)
		return COLOR4(255, 255, 255, 220);

	if (point.classname == "item_flag_team1")
		return COLOR4(80, 160, 255, 180); // blue flag

	if (point.classname == "info_player_team1")
		return COLOR4(80, 200, 255, 140); // blue spawn

	if (point.classname == "item_flag_team2")
		return COLOR4(255, 80, 80, 180); // red flag

	if (point.classname == "info_player_team2")
		return COLOR4(255, 120, 80, 140); // red spawn

	if (point.classname == "item_dom_controlpoint")
		return COLOR4(255, 220, 64, 180); // DOM point

	return COLOR4(255, 255, 255, 160);
}

const char* AuraPointModeEditor::GetDisplayName(const AuraModePoint& point) const
{
	if (point.classname == "item_flag_team1")
		return "Blue Flag";

	if (point.classname == "item_flag_team2")
		return "Red Flag";

	if (point.classname == "info_player_team1")
		return "Blue Spawn";

	if (point.classname == "info_player_team2")
		return "Red Spawn";

	if (point.classname == "item_dom_controlpoint")
	{
		if (!point.data1.empty())
			return point.data1.c_str();

		return "Control Point";
	}

	return point.classname.c_str();
}

void AuraPointModeEditor::Draw3D()
{
	if (!m_enabled || m_mode == AuraPointMode::None)
		return;

	if (!m_renderer)
		return;

	glDisable(GL_CULL_FACE);

	for (int i = 0; i < (int)m_points.size(); ++i)
	{
		const AuraModePoint& point = m_points[i];

		const bool selected = i == m_selectedPoint;

		COLOR4 color = GetPointColor(point, selected);

		// Point entities are shown as player-ish marker boxes.
		// Keep them small enough not to obscure the map.
		vec3 mins = point.origin + vec3(-16.0f, -16.0f, 0.0f);
		vec3 maxs = point.origin + vec3(16.0f, 16.0f, 56.0f);

		m_renderer->drawBox(mins, maxs, color);

		// Add a small taller post for flags/control points so they stand out.
		if (point.classname == "item_flag_team1" ||
			point.classname == "item_flag_team2" ||
			point.classname == "item_dom_controlpoint")
		{
			vec3 postMins = point.origin + vec3(-4.0f, -4.0f, 0.0f);
			vec3 postMaxs = point.origin + vec3(4.0f, 4.0f, 96.0f);

			m_renderer->drawBox(postMins, postMaxs, color);
		}

		DrawFacingArrow(point, selected);
	}

	glEnable(GL_CULL_FACE);
}

void AuraPointModeEditor::DrawLabels()
{
	if (!m_enabled || m_mode == AuraPointMode::None)
		return;

	if (!m_renderer)
		return;

	ImDrawList* drawList = ImGui::GetForegroundDrawList();

	for (int i = 0; i < (int)m_points.size(); ++i)
	{
		const AuraModePoint& point = m_points[i];

		vec3 labelOrigin = point.origin;
		labelOrigin.z += 112.0f;

		vec2 screen;

		if (!m_renderer->worldToScreen(labelOrigin, screen))
			continue;

		const char* text = GetDisplayName(point);
		ImVec2 textSize = ImGui::CalcTextSize(text);

		ImVec2 pos(
			screen.x - textSize.x * 0.5f,
			screen.y - textSize.y * 0.5f
		);

		ImU32 color = i == m_selectedPoint
			? IM_COL32(255, 255, 255, 255)
			: IM_COL32(255, 220, 64, 255);

		if (point.classname == "item_flag_team1" || point.classname == "info_player_team1")
			color = i == m_selectedPoint ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 180, 255, 255);

		if (point.classname == "item_flag_team2" || point.classname == "info_player_team2")
			color = i == m_selectedPoint ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 100, 100, 255);

		drawList->AddText(
			ImVec2(pos.x + 1.0f, pos.y + 1.0f),
			IM_COL32(0, 0, 0, 220),
			text
		);

		drawList->AddText(
			pos,
			color,
			text
		);
	}
}

float AuraPointModeEditor::SnapFloat(float value) const
{
	if (!m_snapEnabled || m_snapGrid <= 0.0f)
		return value;

	return roundf(value / m_snapGrid) * m_snapGrid;
}

vec3 AuraPointModeEditor::SnapVec(const vec3& v) const
{
	return vec3(SnapFloat(v.x), SnapFloat(v.y), SnapFloat(v.z));
}

void AuraPointModeEditor::SnapPoint(AuraModePoint& point)
{
	point.origin = SnapVec(point.origin);
}

bool AuraPointModeEditor::TraceCursorToWorld(vec3& outPos)
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

void AuraPointModeEditor::PlacePointAtCursor()
{
	if (!m_enabled || m_mode == AuraPointMode::None)
		return;

	vec3 hit;

	if (!TraceCursorToWorld(hit))
		return;

	hit.z += 1.0f;

	AuraModePoint point;

	if (m_mode == AuraPointMode::CTF)
	{
		point.classname = m_newClassname.empty() ? GetDefaultClassname() : m_newClassname;
	}
	else if (m_mode == AuraPointMode::DOM)
	{
		point.classname = "item_dom_controlpoint";
		point.data1 = m_newDomName.empty() ? "ControlPoint" : m_newDomName;
	}

	point.origin = hit;
	point.angles = vec3();

	SanitizePoint(point);
	SnapPoint(point);

	m_points.push_back(point);
	m_selectedPoint = (int)m_points.size() - 1;
}

void AuraPointModeEditor::SelectPointUnderCursor()
{
	if (!m_renderer)
		return;

	vec3 start;
	vec3 dir;
	m_renderer->getPickRay(start, dir);

	float bestDist = FLT_MAX;
	int bestPoint = -1;

	for (int i = 0; i < (int)m_points.size(); ++i)
	{
		const AuraModePoint& point = m_points[i];

		vec3 mins = point.origin + vec3(-16.0f, -16.0f, 0.0f);
		vec3 maxs = point.origin + vec3(16.0f, 16.0f, 56.0f);

		// make flags easier to select
		if (point.classname == "item_flag_team1" || point.classname == "item_flag_team2" || point.classname == "item_dom_controlpoint")
			maxs.z = point.origin.z + 96.0f;

		float dist = bestDist;

		if (pickAABB(start, dir, mins, maxs, dist))
		{
			bestDist = dist;
			bestPoint = i;
		}
	}

	m_selectedPoint = bestPoint;
}

void AuraPointModeEditor::MoveSelectedPoint(const vec3& delta)
{
	if (m_selectedPoint < 0 || m_selectedPoint >= (int)m_points.size())
		return;

	AuraModePoint& point = m_points[m_selectedPoint];

	point.origin += delta;
	SnapPoint(point);
}

void AuraPointModeEditor::RotateSelectedPoint(float yawDelta)
{
	if (m_selectedPoint < 0 || m_selectedPoint >= (int)m_points.size())
		return;

	AuraModePoint& point = m_points[m_selectedPoint];

	// Aura-SE file format is angleX angleY angleZ.
	// Yaw is normally angleY in HL-style entity angles.
	point.angles.y += yawDelta;

	while (point.angles.y < 0.0f)
		point.angles.y += 360.0f;

	while (point.angles.y >= 360.0f)
		point.angles.y -= 360.0f;
}

void AuraPointModeEditor::Controls()
{
	if (!m_enabled || m_mode == AuraPointMode::None)
		return;

	if (!m_renderer || !m_renderer->canControl)
		return;

	const bool ctrlDown =
		m_renderer->pressed[GLFW_KEY_LEFT_CONTROL] ||
		m_renderer->pressed[GLFW_KEY_RIGHT_CONTROL];

	const bool lmbPressed =
		m_renderer->curLeftMouse == GLFW_PRESS &&
		m_renderer->oldLeftMouse != GLFW_PRESS;

	if (ctrlDown && lmbPressed)
	{
		PlacePointAtCursor();
		return;
	}

	if (!ctrlDown && lmbPressed)
	{
		SelectPointUnderCursor();
		return;
	}

	if (m_renderer->pressed[GLFW_KEY_DELETE] && !m_renderer->oldPressed[GLFW_KEY_DELETE])
	{
		DeleteSelectedPoint();
		return;
	}

	if (m_selectedPoint < 0 || m_selectedPoint >= (int)m_points.size())
		return;

	const float step = m_snapEnabled ? m_snapGrid : m_moveStep;

	vec3 moveDelta;

	if (m_renderer->pressed[GLFW_KEY_LEFT] && !m_renderer->oldPressed[GLFW_KEY_LEFT])
		moveDelta.x -= step;

	if (m_renderer->pressed[GLFW_KEY_RIGHT] && !m_renderer->oldPressed[GLFW_KEY_RIGHT])
		moveDelta.x += step;

	if (m_renderer->pressed[GLFW_KEY_DOWN] && !m_renderer->oldPressed[GLFW_KEY_DOWN])
		moveDelta.y -= step;

	if (m_renderer->pressed[GLFW_KEY_UP] && !m_renderer->oldPressed[GLFW_KEY_UP])
		moveDelta.y += step;

	if (m_renderer->pressed[GLFW_KEY_PAGE_DOWN] && !m_renderer->oldPressed[GLFW_KEY_PAGE_DOWN])
		moveDelta.z -= step;

	if (m_renderer->pressed[GLFW_KEY_PAGE_UP] && !m_renderer->oldPressed[GLFW_KEY_PAGE_UP])
		moveDelta.z += step;

	if (moveDelta.x != 0.0f || moveDelta.y != 0.0f || moveDelta.z != 0.0f)
	{
		MoveSelectedPoint(moveDelta);
		return;
	}

	if (m_renderer->pressed[GLFW_KEY_Q] && !m_renderer->oldPressed[GLFW_KEY_Q])
	{
		RotateSelectedPoint(-m_rotateStep);
		return;
	}

	if (m_renderer->pressed[GLFW_KEY_E] && !m_renderer->oldPressed[GLFW_KEY_E])
	{
		RotateSelectedPoint(m_rotateStep);
		return;
	}
}

void AuraPointModeEditor::DrawFacingArrow(const AuraModePoint& point, bool selected)
{
	if (!m_renderer)
		return;

	COLOR4 color = GetPointColor(point, selected);

	// Spawn points care most about yaw, but drawing this for every mode point
	// makes Q/E rotation visually obvious everywhere.
	const float yaw = point.angles.y;

	vec3 forward = YawToForward(yaw);
	vec3 right = YawToRight(yaw);

	const float baseHeight = 24.0f;
	const float arrowLength = 64.0f;
	const float arrowHalfWidth = 12.0f;

	vec3 start = point.origin + vec3(0.0f, 0.0f, baseHeight);
	vec3 tip = start + forward * arrowLength;
	vec3 left = tip - forward * 16.0f - right * arrowHalfWidth;
	vec3 rightWing = tip - forward * 16.0f + right * arrowHalfWidth;

	// Main shaft as a thin box.
	vec3 shaftMid = start + forward * (arrowLength * 0.5f);
	vec3 shaftMins = shaftMid + vec3(-3.0f, -3.0f, -3.0f);
	vec3 shaftMaxs = shaftMid + vec3(3.0f, 3.0f, 3.0f);

	// This is an approximate shaft box, not rotated, but it gives a visible
	// anchor even before the triangle arrowhead.
	m_renderer->drawBox(shaftMins, shaftMaxs, color);

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_CULL_FACE);
	glLineWidth(selected ? 3.0f : 2.0f);

	glColor4ub(color.r, color.g, color.b, selected ? 255 : 220);

	glBegin(GL_LINES);
	glVertex3f(start.x, start.z, -start.y);
	glVertex3f(tip.x, tip.z, -tip.y);

	glVertex3f(tip.x, tip.z, -tip.y);
	glVertex3f(left.x, left.z, -left.y);

	glVertex3f(tip.x, tip.z, -tip.y);
	glVertex3f(rightWing.x, rightWing.z, -rightWing.y);
	glEnd();

	glLineWidth(1.0f);
	glEnable(GL_CULL_FACE);
}