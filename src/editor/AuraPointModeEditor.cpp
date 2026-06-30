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
#include "Settings.h"
#include "util.h"
#include "log.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

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
	point.classname = GetDefaultClassname();

	if (m_renderer)
	{
		point.origin = cameraOrigin + (m_renderer->cameraForward * 128.0f);
	}
	else
	{
		point.origin = vec3();
	}

	point.angles = vec3();

	if (m_mode == AuraPointMode::DOM)
		point.data1 = "ControlPoint";

	SanitizePoint(point);

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

	ImGui::End();
}
