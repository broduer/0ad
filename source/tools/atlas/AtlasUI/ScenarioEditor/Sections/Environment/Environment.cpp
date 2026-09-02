/* Copyright (C) 2026 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "precompiled.h"

#include "Environment.h"

#include "tools/atlas/AtlasUI/CustomControls/ColorDialog/ColorDialog.h"
#include "tools/atlas/AtlasUI/General/Observable.h"
#include "tools/atlas/AtlasUI/ScenarioEditor/ScenarioEditor.h"
#include "tools/atlas/AtlasUI/ScenarioEditor/Sections/Common/Sidebar.h"
#include "tools/atlas/AtlasUI/ScenarioEditor/Sections/Environment/LightControl.h"
#include "tools/atlas/AtlasUI/ScenarioEditor/StyleSheet.h"
#include "tools/atlas/AtlasUI/ScenarioEditor/Tools/Common/Tools.h"
#include "tools/atlas/GameInterface/Messages.h"
#include "tools/atlas/GameInterface/Shareable.h"
#include "tools/atlas/GameInterface/SharedTypes.h"

#include <cstddef>
#include <numbers>
#include <string>
#include <vector>
#include <wx/arrstr.h>
#include <wx/button.h>
#include <wx/chartype.h>
#include <wx/colour.h>
#include <wx/colourdata.h>
#include <wx/combobox.h>
#include <wx/gdicmn.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/statbox.h>
#include <wx/string.h>
#include <wx/toolbar.h>
#include <wx/translation.h>

class wxWindow;

using AtlasMessage::Shareable;

static Observable<AtlasMessage::sEnvironmentSettings> g_EnvironmentSettings;

//////////////////////////////////////////////////////////////////////////

class VariableSliderBox : public wxPanel
{
	static const int range = 1024;
public:
	VariableSliderBox(wxWindow* parent, const wxString& label, Shareable<float>& var, float min, float max)
		: wxPanel(parent),
		m_Var(var), m_Min(min), m_Max(max)
	{
		m_Conn = g_EnvironmentSettings.RegisterObserver(0, &VariableSliderBox::OnSettingsChange, this);

		m_Sizer = new wxStaticBoxSizer(wxVERTICAL, this, label);
		SetSizer(m_Sizer);

		m_Slider = new wxSlider(m_Sizer->GetStaticBox(), -1, 0, 0, range);
		m_Sizer->Add(m_Slider, wxSizerFlags().Expand().Border(wxALL, Atlas::Style::STATICBOX_PADDING));
	}

	void OnSettingsChange(const AtlasMessage::sEnvironmentSettings& WXUNUSED(env))
	{
		m_Slider->SetValue((m_Var - m_Min) * (range / (m_Max - m_Min)));
	}

	void OnScroll(wxScrollEvent& evt)
	{
		m_Var = m_Min + (m_Max - m_Min)*(evt.GetInt() / (float)range);

		g_EnvironmentSettings.NotifyObserversExcept(m_Conn);
	}

private:
	ObservableScopedConnection m_Conn;
	wxStaticBoxSizer* m_Sizer;
	wxSlider* m_Slider;
	Shareable<float>& m_Var;
	float m_Min, m_Max;

	wxDECLARE_EVENT_TABLE();
};

wxBEGIN_EVENT_TABLE(VariableSliderBox, wxPanel)
	EVT_SCROLL(VariableSliderBox::OnScroll)
wxEND_EVENT_TABLE()

//////////////////////////////////////////////////////////////////////////

class VariableListBox : public wxPanel
{
public:
	VariableListBox(wxWindow* parent, const wxString& label, Shareable<std::wstring>& var)
		: wxPanel(parent),
		m_Var(var)
	{
		m_Conn = g_EnvironmentSettings.RegisterObserver(
			0, &VariableListBox::OnSettingsChange, this);

		m_Sizer = new wxStaticBoxSizer(wxVERTICAL, this, label);
		SetSizer(m_Sizer);

		m_Combo = new wxComboBox(
			m_Sizer->GetStaticBox(), -1, wxEmptyString, wxDefaultPosition, wxDefaultSize,
			wxArrayString(), wxCB_READONLY),
		m_Sizer->Add(m_Combo, wxSizerFlags().Expand().Border(wxALL, Atlas::Style::STATICBOX_PADDING));
	}

	void SetChoices(const std::vector<std::wstring>& choices)
	{
		wxArrayString choices_arraystr;
		for (size_t i = 0; i < choices.size(); ++i)
			choices_arraystr.Add(choices[i].c_str());

		m_Combo->Clear();
		m_Combo->Append(choices_arraystr);

		m_Combo->SetValue(m_Var.c_str());
	}

	void OnSettingsChange(const AtlasMessage::sEnvironmentSettings& WXUNUSED(env))
	{
		m_Combo->SetValue(m_Var.c_str());
	}

	void OnSelect(wxCommandEvent& WXUNUSED(evt))
	{
		m_Var = std::wstring(m_Combo->GetValue().c_str());

		g_EnvironmentSettings.NotifyObserversExcept(m_Conn);
	}

private:
	ObservableScopedConnection m_Conn;
	wxStaticBoxSizer* m_Sizer;
	wxComboBox* m_Combo;
	Shareable<std::wstring>& m_Var;

	wxDECLARE_EVENT_TABLE();
};

wxBEGIN_EVENT_TABLE(VariableListBox, wxPanel)
	EVT_COMBOBOX(wxID_ANY, VariableListBox::OnSelect)
wxEND_EVENT_TABLE()

//////////////////////////////////////////////////////////////////////////

class VariableColorBox : public wxPanel
{
public:
	VariableColorBox(wxWindow* parent, const wxString& label, Shareable<AtlasMessage::Color>& color)
		: wxPanel(parent),
		m_Color(color)
	{
		m_Conn = g_EnvironmentSettings.RegisterObserver(0, &VariableColorBox::OnSettingsChange, this);

		m_Sizer = new wxStaticBoxSizer(wxVERTICAL, this, label);
		SetSizer(m_Sizer);

		m_Button = new wxButton(m_Sizer->GetStaticBox(), -1);
		m_Sizer->Add(m_Button, wxSizerFlags().Expand().Border(wxALL, Atlas::Style::STATICBOX_PADDING));
	}

	void OnSettingsChange(const AtlasMessage::sEnvironmentSettings& WXUNUSED(env))
	{
		UpdateButton();
	}

	void OnClick(wxCommandEvent& WXUNUSED(evt))
	{
		ColorDialog dlg (this, _T("Scenario Editor/LightingColor"),
			wxColor(m_Color->r, m_Color->g, m_Color->b));

		if (dlg.ShowModal() == wxID_OK)
		{
			wxColor& c = dlg.GetColourData().GetColour();
			m_Color = AtlasMessage::Color(c.Red(), c.Green(), c.Blue());
			UpdateButton();

			g_EnvironmentSettings.NotifyObserversExcept(m_Conn);
		}
	}

	void UpdateButton()
	{
		m_Button->SetBackgroundColour(wxColor(m_Color->r, m_Color->g, m_Color->b));
		m_Button->SetLabel(wxString::Format(_T("%02X %02X %02X"), m_Color->r, m_Color->g, m_Color->b));

		int y = 3*m_Color->r + 6*m_Color->g + 1*m_Color->b;
		if (y > 1280)
			m_Button->SetForegroundColour(wxColor(0, 0, 0));
		else
			m_Button->SetForegroundColour(wxColor(255, 255, 255));
	}


private:
	ObservableScopedConnection m_Conn;
	wxStaticBoxSizer* m_Sizer;
	wxButton* m_Button;
	Shareable<AtlasMessage::Color>& m_Color;

	wxDECLARE_EVENT_TABLE();
};

wxBEGIN_EVENT_TABLE(VariableColorBox, wxPanel)
	EVT_BUTTON(wxID_ANY, VariableColorBox::OnClick)
wxEND_EVENT_TABLE()

//////////////////////////////////////////////////////////////////////////

enum {
	ID_RecomputeWaterData,
	ID_PickWaterHeight
};
static void SendToGame(const AtlasMessage::sEnvironmentSettings& settings)
{
	POST_COMMAND(SetEnvironmentSettings, (settings));
}

EnvironmentSidebar::EnvironmentSidebar(
	ScenarioEditor& scenarioEditor, wxWindow* sidebarContainer, wxWindow* bottomBarContainer)
	: Sidebar(scenarioEditor, sidebarContainer, bottomBarContainer)
{
	wxStaticBoxSizer* waterBoxSizer = new wxStaticBoxSizer(wxVERTICAL, this, _T("Water settings"));
	wxStaticBox* waterBox = waterBoxSizer->GetStaticBox();
	m_MainSizer->Add(waterBoxSizer, wxSizerFlags().Expand());
	wxFlexGridSizer* waterSizer = new wxFlexGridSizer(1, 10, 10);
	waterSizer->AddGrowableCol(0);
	waterBoxSizer->Add(waterSizer, wxSizerFlags().Expand().Border(wxALL, Atlas::Style::STATICBOX_PADDING));

	waterSizer->Add(new wxButton(
		waterBox, ID_RecomputeWaterData, _("Reset Water Data")), wxSizerFlags().Expand());
	waterSizer->Add(m_WaterTypeList = new VariableListBox(
		waterBox, _("Water Type"), g_EnvironmentSettings.watertype), wxSizerFlags().Expand());
	waterSizer->Add(new VariableSliderBox(
		waterBox, _("Water height"), g_EnvironmentSettings.waterheight, 0.f, 1.2f), wxSizerFlags().Expand());
	waterSizer->Add(new wxButton(
		waterBox, ID_PickWaterHeight, _("Pick Water Height")), wxSizerFlags().Expand());
	waterSizer->Add(new VariableSliderBox(
		waterBox, _("Water waviness"), g_EnvironmentSettings.waterwaviness, 0.f, 10.f), wxSizerFlags().Expand());
	waterSizer->Add(new VariableSliderBox(
		waterBox, _("Water murkiness"), g_EnvironmentSettings.watermurkiness, 0.f, 1.f), wxSizerFlags().Expand());
	waterSizer->Add(new VariableSliderBox(
		waterBox, _("Wind angle"), g_EnvironmentSettings.windangle, -std::numbers::pi_v<float>, std::numbers::pi_v<float>), wxSizerFlags().Expand());
	waterSizer->Add(new VariableColorBox(
		waterBox, _("Water color"), g_EnvironmentSettings.watercolor), wxSizerFlags().Expand());
	waterSizer->Add(new VariableColorBox(
		waterBox, _("Water tint"), g_EnvironmentSettings.watertint), wxSizerFlags().Expand());

	std::vector<std::wstring> list;
	list.push_back(L"ocean"); list.push_back(L"lake"); list.push_back(L"clap");
	m_WaterTypeList->SetChoices(list);


	wxStaticBoxSizer* sunBoxSizer = new wxStaticBoxSizer(wxVERTICAL, this, _T("Sun / lighting settings"));
	wxStaticBox* sunBox = sunBoxSizer->GetStaticBox();
	m_MainSizer->Add(sunBoxSizer, wxSizerFlags().Expand());
	wxFlexGridSizer* sunSizer = new wxFlexGridSizer(1, 10, 10);
	sunSizer->AddGrowableCol(0);
	sunBoxSizer->Add(sunSizer, wxSizerFlags().Expand().Border(wxALL, Atlas::Style::STATICBOX_PADDING));

	sunSizer->Add(new VariableSliderBox(
		sunBox, _("Sun rotation"), g_EnvironmentSettings.sunrotation, -std::numbers::pi_v<float>, std::numbers::pi_v<float>), wxSizerFlags().Expand());
	sunSizer->Add(new VariableSliderBox(
		sunBox, _("Sun elevation"), g_EnvironmentSettings.sunelevation, -std::numbers::pi_v<float> / 2.0f, std::numbers::pi_v<float> / 2.0f), wxSizerFlags().Expand());
	sunSizer->Add(new VariableSliderBox(
		sunBox, _("Sun overbrightness"), g_EnvironmentSettings.sunoverbrightness, 1.0f, 3.0f), wxSizerFlags().Expand());
	sunSizer->Add(new LightControl(
		sunBox, wxSize(150, 150), g_EnvironmentSettings), wxSizerFlags().Align(wxALIGN_CENTER));
	sunSizer->Add(new VariableColorBox(
		sunBox, _("Sun color"), g_EnvironmentSettings.suncolor), wxSizerFlags().Expand());
	sunSizer->Add(m_SkyList = new VariableListBox(
		sunBox, _("Sky set"), g_EnvironmentSettings.skyset), wxSizerFlags().Expand());
	sunSizer->Add(new VariableSliderBox(
		sunBox, _("Fog Factor"), g_EnvironmentSettings.fogfactor, 0.0f, 0.01f), wxSizerFlags().Expand());
	sunSizer->Add(new VariableSliderBox(
		sunBox, _("Fog Thickness"), g_EnvironmentSettings.fogmax, 0.5f, 0.0f), wxSizerFlags().Expand());
	sunSizer->Add(new VariableColorBox(
		sunBox, _("Fog color"), g_EnvironmentSettings.fogcolor), wxSizerFlags().Expand());
	sunSizer->Add(new VariableColorBox(
		sunBox, _("Ambient color"), g_EnvironmentSettings.ambientcolor), wxSizerFlags().Expand());

	wxStaticBoxSizer* postProcBoxSizer = new wxStaticBoxSizer(wxVERTICAL, this, _T("Post-processing settings"));
	wxStaticBox* postProcBox = postProcBoxSizer->GetStaticBox();
	m_MainSizer->Add(postProcBoxSizer, wxSizerFlags().Expand());
	wxFlexGridSizer* postProcSizer = new wxFlexGridSizer(1, 10, 10);
	postProcSizer->AddGrowableCol(0);
	postProcBoxSizer->Add(postProcSizer, wxSizerFlags().Expand().Border(wxALL, Atlas::Style::STATICBOX_PADDING));

	postProcSizer->Add(m_PostEffectList = new VariableListBox(
		postProcBox, _("Post Effect"), g_EnvironmentSettings.posteffect), wxSizerFlags().Expand());
	postProcSizer->Add(new VariableSliderBox(
		postProcBox, _("Brightness"), g_EnvironmentSettings.brightness, -0.5f, 0.5f), wxSizerFlags().Expand());
	postProcSizer->Add(new VariableSliderBox(
		postProcBox, _("Contrast (HDR)"), g_EnvironmentSettings.contrast, 0.5f, 1.5f), wxSizerFlags().Expand());
	postProcSizer->Add(new VariableSliderBox(
		postProcBox, _("Saturation"), g_EnvironmentSettings.saturation, 0.0f, 2.0f), wxSizerFlags().Expand());
	postProcSizer->Add(new VariableSliderBox(
		postProcBox, _("Bloom"), g_EnvironmentSettings.bloom, 0.2f, 0.0f), wxSizerFlags().Expand());

	m_Conn = g_EnvironmentSettings.RegisterObserver(0, &SendToGame);
}

void EnvironmentSidebar::OnFirstDisplay()
{
	// Load the list of skies. (Can only be done now rather than in the constructor,
	// after the game has been initialised.)
	AtlasMessage::qGetSkySets qry_skysets;
	qry_skysets.Post();
	m_SkyList->SetChoices(*qry_skysets.skysets);

	AtlasMessage::qGetPostEffects qry_effects;
	qry_effects.Post();
	m_PostEffectList->SetChoices(*qry_effects.posteffects);

	UpdateEnvironmentSettings(false);
}

void EnvironmentSidebar::OnMapReload()
{
	UpdateEnvironmentSettings();
}

void EnvironmentSidebar::RecomputeWaterData(wxCommandEvent& WXUNUSED(evt))
{
	POST_COMMAND(RecalculateWaterData, (0.0f));
}

void EnvironmentSidebar::UpdateEnvironmentSettings(bool sendToEngine)
{
	AtlasMessage::qGetEnvironmentSettings qry_env;
	qry_env.Post();
	g_EnvironmentSettings = qry_env.settings;

	if (sendToEngine)
		g_EnvironmentSettings.NotifyObservers();
	else
		g_EnvironmentSettings.NotifyObserversExcept(m_Conn);

}

void EnvironmentSidebar::OnPickWaterHeight(wxCommandEvent& WXUNUSED(evt))
{
	m_ScenarioEditor.GetToolManager().SetCurrentTool(_T("PickWaterHeight"), this);
}

wxBEGIN_EVENT_TABLE(EnvironmentSidebar, Sidebar)
	EVT_BUTTON(ID_RecomputeWaterData, EnvironmentSidebar::RecomputeWaterData)
	EVT_BUTTON(ID_PickWaterHeight, EnvironmentSidebar::OnPickWaterHeight)
wxEND_EVENT_TABLE();
