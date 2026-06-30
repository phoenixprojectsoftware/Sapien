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

#pragma once

#include <string>
#include <vector>

#include "vectors.h"

class Renderer;
class Bsp;

enum class AuraPointMode // which mode are we in?
{
	None,
	CTF,
	DOM
};

struct AuraModePoint // which entity are we placing?
{
	std::string classname;
	vec3 origin;
	vec3 angles;
	std::string data1; // DOM control point name. not for ctf
};

class AuraPointModeEditor
{
public:
	AuraPointModeEditor(Renderer* renderer);

	void OnMapChanged(Bsp* map);

	void SetMode(AuraPointMode mode);
	void ToggleMode(AuraPointMode mode);

	bool IsEnabled() const { return m_enabled; }
	bool IsModeEnabled(AuraPointMode mode) const { return m_enabled && m_mode == mode; }
	AuraPointMode GetMode() const { return m_mode; }

	void DrawGui();

	void Draw3D();

	void Controls();

private:
	const char* GetModeName() const;
	const char* GetModeFolder() const;
	const char* GetModeExtension() const;

	std::string GetModePath(Bsp* map) const;

	bool Load(const std::string& path);
	bool Save(const std::string& path);

	bool IsAllowedClassname(const std::string& classname) const;
	const char* GetDefaultClassname() const;

	void AddDefaultPoint();
	void DeleteSelectedPoint();

	void SanitizePoint(AuraModePoint& point);

	void DrawLabels();
	const char* GetDisplayName(const AuraModePoint& point) const;
	COLOR4 GetPointColor(const AuraModePoint& point, bool selected) const;

	bool TraceCursorToWorld(vec3& outPos);
	void PlacePointAtCursor();
	void SelectPointUnderCursor();

	void MoveSelectedPoint(const vec3& delta);
	void RotateSelectedPoint(float yawDelta);

	float SnapFloat(float value) const;
	vec3 SnapVec(const vec3& v) const;
	void SnapPoint(AuraModePoint& point);

private:
	Renderer* m_renderer = nullptr;

	bool m_enabled = false;
	bool m_loadedForMap = false;

	AuraPointMode m_mode = AuraPointMode::None;

	std::string m_currentPath;

	std::vector<AuraModePoint> m_points;
	int m_selectedPoint = -1;

	std::string m_newClassname = "item_flag_team1";
	std::string m_newDomName = "ControlPoint";

	bool m_snapEnabled = true;
	float m_snapGrid = 16.0f;
	float m_moveStep = 16.0f;
	float m_rotateStep = 15.0f;
};
