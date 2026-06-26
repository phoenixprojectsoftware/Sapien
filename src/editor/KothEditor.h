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

#pragma once

#include <string>
#include <vector>

#include "vectors.h"

class Renderer;
class Bsp;

struct KothZone
{
	std::string name;
	vec3 mins;
	vec3 maxs;

	vec3 origin() const
	{
		return (mins + maxs) * 0.5f;
	}

	vec3 size() const
	{
		return maxs - mins;
	}
};

class KothEditor
{
public:
	KothEditor(Renderer* renderer);

	void OnMapChanged(Bsp* map);

	void Draw3D();
	void DrawGui();
	void Controls();

	bool IsEnabled() const { return m_enabled; }
	void SetEnabled(bool enabled) { m_enabled = enabled; }
	void ToggleEnabled() { m_enabled = !m_enabled; }

private:
	std::string GetDefaultKothPath(Bsp* map) const;

	bool Load(const std::string& path);
	bool Save(const std::string& path);

	void AddZoneAtCamera();
	void DeleteSelectedZone();

	void NormalizeZone(KothZone& zone);
	void SelectZoneUnderCursor();

	bool TraceCursorToWorld(vec3& outPos);
	void BeginDrawZone();
	void UpdateDrawZone();
	void FinishDrawZone();
	void CancelDrawZone();
	KothZone MakeDrawPreviewZone() const;

	std::string MakeUniqueZoneName(const char* baseName) const;

	void MoveSelectedZone(const vec3& delta);
	void ResizeSelectedZone(const vec3& delta);
	void SnapZone(KothZone& zone);
	vec3 SnapVec(const vec3& v) const;
	float SnapFloat(float value) const;

	void DrawLabels();

private:
	Renderer* m_renderer = nullptr;

	bool m_enabled = false;
	bool m_loadedForMap = false;

	std::string m_currentPath;

	std::vector<KothZone> m_zones;
	int m_selectedZone = -1;

	char m_newZoneName[64] = "Hill";
	vec3 m_newZoneSize = vec3(256.0f, 256.0f, 256.0f);

	bool m_isDrawingZone = false;
	vec3 m_drawStart = vec3();
	vec3 m_drawEnd = vec3();
	float m_drawHeight = 128.0f;

	bool m_snapEnabled = true;
	float m_snapGrid = 16.0f;
	float m_moveStep = 16.0f;
	float m_resizeStep = 16.0f;
	bool m_showLabels = true;
};
