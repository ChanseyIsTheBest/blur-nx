// blur-nx (2026-11-18): the game menu (game_menu.h).
#include "game_menu.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/runtime.h>
#include <rex/string.h>
#include <rex/system/achievement_manager.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/overlay/achievement_icon_cache.h>

#if REX_PLATFORM_SWITCH
#include <rex/input/switch/switch_input_driver.h>  // rex::input::nx::RebuildButtonMaps

// libnx's shared font service (switch.h is not included: its types clash with the SDK's). pl.h's PlFontData.
extern "C" {
struct BlurPlFontData {
  uint32_t type, offset, size;
  void* address;
};
uint32_t plInitialize(int service_type);
uint32_t plGetSharedFontByType(BlurPlFontData* font, int shared_font_type);
int RexSwitchApplyFourCoreMode(int on);  // threading_switch.cpp: the forwarder's NPDM written or removed
}
#endif

// blur_keys.cpp: the frame cap from the next frame, and the one in force.
void BlurSetFrameCapLive(int fps);
int BlurFrameCapNow();

namespace blur {

namespace {

// ---- Fonts ----------------------------------------------------------------------------------------------------------

ImFont* g_font_small = nullptr;       // for a 720-line display
ImFont* g_font_large = nullptr;       // for a 1080-line one
ImFont* g_font_small_text = nullptr;  // their smaller text (help, descriptions, the footer): baked at its own size, as
ImFont* g_font_large_text = nullptr;  // the Switch's drawer has a fixed atlas (another size would be scaled, blurred)

// Only a font of this context's atlas (the PC draws the app's dialogs with Renut's own ImGui context).
ImFont* InAtlas(const ImGuiIO& io, ImFont* font) {
  return font && io.Fonts && io.Fonts->Fonts.contains(font) ? font : nullptr;
}
ImFont* MenuFont(const ImGuiIO& io) { return InAtlas(io, io.DisplaySize.y > 900.0f ? g_font_large : g_font_small); }

// The smaller text's font and size this frame (OnDraw sets them): its own font, else the current one at 0.8.
ImFont* g_text_font = nullptr;
float g_text_size = 0.0f;
void PickTextFont(const ImGuiIO& io) {
  ImFont* own = InAtlas(io, io.DisplaySize.y > 900.0f ? g_font_large_text : g_font_small_text);
  g_text_font = own ? own : ImGui::GetFont();
  g_text_size = own ? own->LegacySize : ImGui::GetFontSize() * 0.8f;
}
ImVec2 TextSize(const char* text, float wrap = 0.0f) {
  return g_text_font->CalcTextSizeA(g_text_size, FLT_MAX, wrap, text);
}

// ---- Colours: green and black ---------------------------------------------------------------------------------------

const ImVec4 kBlack(0.012f, 0.020f, 0.014f, 0.97f);
const ImVec4 kPanel(0.030f, 0.060f, 0.038f, 1.00f);
const ImVec4 kGreen(0.200f, 0.930f, 0.420f, 1.00f);
const ImVec4 kGreenDim(0.090f, 0.420f, 0.190f, 1.00f);
const ImVec4 kGreenDark(0.050f, 0.230f, 0.105f, 1.00f);
const ImVec4 kText(0.880f, 1.000f, 0.900f, 1.00f);
const ImVec4 kTextDim(0.500f, 0.660f, 0.540f, 1.00f);
const ImVec4 kPending(0.780f, 0.960f, 0.350f, 1.00f);  // a * setting's value for the next start
const ImVec4 kWarn(1.000f, 0.720f, 0.180f, 1.00f);     // the four-core option's warning, and its Off

ImU32 U32(const ImVec4& c, float alpha = 1.0f) { return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha)); }

// ---- The player's file, key by key ----------------------------------------------------------------------------------

struct FileEdit {
  std::string key;
  bool remove = false;
  std::string toml;
};

std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t");
  size_t b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool LineIsKey(const std::string& line, const std::string& key) {
  const std::string t = Trim(line);
  if (t.size() <= key.size() || t.compare(0, key.size(), key) != 0)
    return false;
  size_t i = key.size();
  while (i < t.size() && (t[i] == ' ' || t[i] == '\t'))
    ++i;
  return i < t.size() && t[i] == '=';
}

bool IsTableLine(const std::string& line, std::string* name) {
  const std::string t = Trim(line);
  if (t.size() < 3 || t.front() != '[' || t.back() != ']')
    return false;
  if (name)
    *name = t.substr(1, t.size() - 2);
  return true;
}

// Each key set (its line replaced, or added at the end of its part) or removed; the rest of the file as it was. A map key
// (input_p1_a) goes in its table ([input_p1] a = ...) when the file has that table, as the shipped files did.
bool UpdateConfigFile(const std::filesystem::path& path, const std::vector<FileEdit>& edits) {
  std::string text;
  {
    std::ifstream in(path, std::ios::binary);
    if (in)
      text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  const bool crlf = text.find("\r\n") != std::string::npos;
  std::vector<std::string> lines;
  {
    size_t start = 0;
    while (start < text.size()) {
      size_t end = text.find('\n', start);
      if (end == std::string::npos)
        end = text.size();
      std::string line = text.substr(start, end - start);
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      lines.push_back(std::move(line));
      start = end + 1;
    }
  }
  for (const FileEdit& e : edits) {
    std::string table, sub = e.key;
    if (e.key.rfind("input_p", 0) == 0 && e.key.size() > 9 && e.key[8] == '_') {
      const std::string want = e.key.substr(0, 8);
      for (const std::string& l : lines) {
        std::string name;
        if (IsTableLine(l, &name) && name == want) {
          table = want;
          sub = e.key.substr(9);
          break;
        }
      }
    }
    // The part: the top (before the first table), or the table's lines.
    size_t begin = 0, end = lines.size();
    if (table.empty()) {
      for (size_t i = 0; i < lines.size(); ++i)
        if (IsTableLine(lines[i], nullptr)) {
          end = i;
          break;
        }
    } else {
      for (size_t i = 0; i < lines.size(); ++i) {
        std::string name;
        if (IsTableLine(lines[i], &name) && name == table) {
          begin = i + 1;
          end = lines.size();
          for (size_t j = begin; j < lines.size(); ++j)
            if (IsTableLine(lines[j], nullptr)) {
              end = j;
              break;
            }
          break;
        }
      }
    }
    bool found = false;
    for (size_t i = begin; i < end; ++i) {
      if (!LineIsKey(lines[i], sub))
        continue;
      found = true;
      if (e.remove)
        lines.erase(lines.begin() + long(i));
      else
        lines[i] = sub + " = " + e.toml;
      break;
    }
    if (!found && !e.remove) {
      size_t at = end;
      while (at > begin && Trim(lines[at - 1]).empty())
        --at;
      lines.insert(lines.begin() + long(at), sub + " = " + e.toml);
    }
  }
  std::string out;
  for (const std::string& l : lines) {
    out += l;
    out += crlf ? "\r\n" : "\n";
  }
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream o(temporary, std::ios::binary | std::ios::trunc);
    if (!o)
      return false;
    o.write(out.data(), std::streamsize(out.size()));
    if (!o.good())
      return false;
  }
  std::error_code ec;
  std::filesystem::remove(path, ec);  // the Switch's file system renames onto no existing file
  std::filesystem::rename(temporary, path, ec);
  return !ec;
}

// ---- The settings ---------------------------------------------------------------------------------------------------

using Setting = GameMenuDialog::Setting;
using Choice = GameMenuDialog::Choice;

Choice C(std::string label, std::string raw, std::string toml) { return {std::move(label), std::move(raw), std::move(toml)}; }
Choice Q(std::string label, std::string raw) {  // a string key: quoted in the file
  std::string toml = "\"" + raw + "\"";
  return {std::move(label), std::move(raw), std::move(toml)};
}
std::vector<Choice> OnOff() { return {C("On", "true", "true"), C("Off", "false", "false")}; }

std::vector<Setting> MakeSettings() {
  std::vector<Setting> s;
  auto add = [&](std::string label, std::string key, bool restart, std::vector<Choice> choices, int def,
                 std::string help, bool always_in_file = true) {
    Setting x;
    x.label = std::move(label);
    x.key = std::move(key);
    x.restart = restart;
    x.choices = std::move(choices);
    x.default_choice = def;
    x.help = std::move(help);
    x.always_in_file = always_in_file;
    s.push_back(std::move(x));
  };
  add("Language", "language", true,
      {Q("System", "System"), Q("English", "English"), Q("French", "French"), Q("German", "German"),
       Q("Italian", "Italian"), Q("Spanish", "Spanish"), Q("Polish", "Polish"), Q("Russian", "Russian"),
       Q("Japanese", "Japanese"), Q("Korean", "Korean"), Q("Portuguese", "Portuguese"),
       Q("Chinese (Simplified)", "Chinese (Simplified)"), Q("Chinese (Traditional)", "Chinese (Traditional)")},
      0, "The game's language. System follows the console's language; a language the game does not have shows English.");
  add("Framerate cap", "frame_cap", false, {Q("30 fps", "30"), Q("60 fps", "60"), Q("Off", "Off")}, 0,
      "30 fps keeps a steady pace. 60 fps lets the game go faster where it can (uneven on busy tracks). Off: no cap.");
  add("Handheld resolution", "banjo_renut_render_height_handheld", true,
      {C("960x540", "540", "540"), C("1024x576", "576", "576"), C("1088x612", "612", "612"),
       C("1152x648", "648", "648"), C("1216x684", "684", "684"), C("1280x720", "720", "720")},
      5, "The size of the 3D image in handheld mode. Lower is faster; the HUD and text stay sharp.");
  add("Docked resolution", "banjo_renut_render_height_docked", true,
      {C("1280x720", "720", "720"), C("1408x792", "792", "792"), C("1536x864", "864", "864"),
       C("1600x900", "900", "900"), C("1664x936", "936", "936"), C("1792x1008", "1008", "1008"),
       C("1920x1080", "1080", "1080")},
      0, "The size of the 3D image on the TV. Lower is faster; the HUD and text are drawn at 1080p.");
  add("Rumble", "input_rumble", false, OnOff(), 0, "Controller vibration.", /*always_in_file=*/false);
  add("SMAA", "banjo_renut_smaa", false, OnOff(), 0,
      "Edge smoothing on the 3D image after the game's own picture (costs a little GPU time).");
  add("MSAA", "msaa_level", true, {C("Off", "1", "1"), C("2x", "2", "2"), C("4x", "4", "4")}, 0,
      "The game's own edge smoothing: smoother edges, a lower frame rate (4x: about 9 % lower than Off).");
  // blur-nx (2026-11-18, the user: "make 4 core mode an option ... on by default and a warning that it is highly
  // reccomended to be left on").
  add("4-core mode", "four_core_mode", true, OnOff(), 0,
      "Highly recommended to leave on: the game uses all four CPU cores (started from its Home menu icon). Off is "
      "much slower.");
  add("Xbox button layout", "input_xbox_layout", false, OnOff(), 0,
      "Face buttons by position, as on an Xbox 360 pad: the bottom button is A. Off: by letter (the Switch's A is A).");
  add("Joy-Con sideways", "input_joycon_sideways", false, OnOff(), 0,
      "Single Joy-Cons are held sideways, one per player.");
  add("Joy-Con sideways stick", "input_joycon_sideways_stick", false, OnOff(), 0,
      "A sideways Joy-Con's stick turned to match the grip.");
  return s;
}

struct XboxButton {
  const char* label;
  const char* suffix;     // input_pN_<suffix>
  const char* by_letter;  // the default Switch button (by letter / by position)
  const char* by_position;
};
const XboxButton kXboxButtons[] = {
    {"A", "a", "A", "B"},
    {"B", "b", "B", "A"},
    {"X", "x", "X", "Y"},
    {"Y", "y", "Y", "X"},
    {"LB", "lb", "L", "L"},
    {"RB", "rb", "R", "R"},
    {"LT", "lt", "ZL", "ZL"},
    {"RT", "rt", "ZR", "ZR"},
    {"Start", "start", "+", "+"},
    {"Back", "back", "-", "-"},
    {"Left stick click", "ls", "L stick", "L stick"},
    {"Right stick click", "rs", "R stick", "R stick"},
    {"D-pad Up", "up", "D-pad Up", "D-pad Up"},
    {"D-pad Down", "down", "D-pad Down", "D-pad Down"},
    {"D-pad Left", "left", "D-pad Left", "D-pad Left"},
    {"D-pad Right", "right", "D-pad Right", "D-pad Right"},
};
constexpr const char* kResetKey = "#reset";

std::vector<Setting> MakeControls(int player) {
  std::vector<Choice> buttons = {
      Q("Default", ""),           Q("A", "A"),           Q("B", "B"),           Q("X", "X"),
      Q("Y", "Y"),                Q("L", "L"),           Q("R", "R"),           Q("ZL", "ZL"),
      Q("ZR", "ZR"),              Q("+", "PLUS"),        Q("-", "MINUS"),       Q("L stick", "LSTICK"),
      Q("R stick", "RSTICK"),     Q("D-pad Up", "UP"),   Q("D-pad Down", "DOWN"), Q("D-pad Left", "LEFT"),
      Q("D-pad Right", "RIGHT"),  Q("SL", "SL"),         Q("SR", "SR"),         Q("None", "NONE"),
  };
  std::vector<Setting> s;
  for (const XboxButton& b : kXboxButtons) {
    Setting x;
    x.label = std::string("Xbox ") + b.label;
    x.key = "input_p" + std::to_string(player + 1) + "_" + b.suffix;
    x.restart = false;
    x.always_in_file = false;
    x.choices = buttons;
    x.default_choice = 0;
    x.help = "The Switch button that presses the Xbox 360 " + std::string(b.label) + " for player " +
             std::to_string(player + 1) + ".";
    s.push_back(std::move(x));
  }
  Setting reset;
  reset.label = "Reset to defaults";
  reset.key = kResetKey;
  reset.choices = {C("Press to reset", "", "")};
  reset.help = "Every button of player " + std::to_string(player + 1) + " back to its default.";
  s.push_back(std::move(reset));
  return s;
}

int FindChoice(const Setting& s, const std::string& raw) {
  for (size_t i = 0; i < s.choices.size(); ++i)
    if (rex::string::compare_case(s.choices[i].raw.c_str(), raw.c_str()) == 0)
      return int(i);
  return -1;
}

std::string CurrentRaw(const Setting& s) {
  if (s.key == "frame_cap") {
    const int fps = BlurFrameCapNow();
    return fps <= 0 ? "Off" : std::to_string(fps);
  }
  return rex::cvar::GetFlagByName(s.key);
}

const ImGuiKey kGamepadKeys[] = {
    ImGuiKey_GamepadFaceDown, ImGuiKey_GamepadFaceRight, ImGuiKey_GamepadFaceUp,   ImGuiKey_GamepadFaceLeft,
    ImGuiKey_GamepadDpadUp,   ImGuiKey_GamepadDpadDown,  ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight,
    ImGuiKey_GamepadL1,       ImGuiKey_GamepadR1,        ImGuiKey_GamepadL2,       ImGuiKey_GamepadR2,
    ImGuiKey_GamepadStart,    ImGuiKey_GamepadBack,      ImGuiKey_GamepadL3,       ImGuiKey_GamepadR3,
    ImGuiKey_Escape,          ImGuiKey_F8,               ImGuiKey_Enter,
};

bool Pressed(ImGuiKey a, ImGuiKey b = ImGuiKey_None, ImGuiKey c = ImGuiKey_None, bool repeat = true) {
  return ImGui::IsKeyPressed(a, repeat) || (b != ImGuiKey_None && ImGui::IsKeyPressed(b, repeat)) ||
         (c != ImGuiKey_None && ImGui::IsKeyPressed(c, repeat));
}

const char* const kTabNames[] = {"Settings", "Achievements", "P1 Controls", "P2 Controls", "P3 Controls", "P4 Controls"};

}  // namespace

// ---- Fonts ----------------------------------------------------------------------------------------------------------

void AddGameMenuFonts(ImFontAtlas* atlas) {
#if REX_PLATFORM_SWITCH
  if (!atlas || plInitialize(0 /* PlServiceType_User */) != 0)
    return;
  BlurPlFontData font{};
  if (plGetSharedFontByType(&font, 0 /* PlSharedFontType_Standard */) != 0 || !font.address || !font.size)
    return;
  static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0x2190, 0x2193, 0x25B2, 0x25C0, 0};
  ImFontConfig config;
  config.FontDataOwnedByAtlas = false;  // the system's shared memory
  config.OversampleH = 2;
  config.OversampleV = 1;
  g_font_small = atlas->AddFontFromMemoryTTF(font.address, int(font.size), 26.0f, &config, kRanges);
  g_font_large = atlas->AddFontFromMemoryTTF(font.address, int(font.size), 39.0f, &config, kRanges);
  g_font_small_text = atlas->AddFontFromMemoryTTF(font.address, int(font.size), 20.0f, &config, kRanges);
  g_font_large_text = atlas->AddFontFromMemoryTTF(font.address, int(font.size), 30.0f, &config, kRanges);
#else
  (void)atlas;
#endif
}

// ---- The dialog -----------------------------------------------------------------------------------------------------

GameMenuDialog::GameMenuDialog(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config_path, rex::Runtime* runtime,
                               rex::ui::ImmediateDrawer* immediate_drawer)
    : rex::ui::ImGuiDialog(drawer),
      config_path_(std::move(config_path)),
      runtime_(runtime),
      immediate_drawer_(immediate_drawer) {
  rex::ui::RegisterBind("bind_game_menu", "F8", "Open or close the game menu (A+B+X+Y+L+R on the Switch)",
                        [this] { Toggle(); });
}

GameMenuDialog::~GameMenuDialog() {
  rex::ui::UnregisterBind("bind_game_menu");
  if (state_ != State::kClosed)
    rex::ui::SetGameInputBlocked(false);
}

void GameMenuDialog::Toggle() {
  if (state_ == State::kClosed)
    Open();
  else if (state_ == State::kOpening || state_ == State::kOpen)
    BeginClose();
}

void GameMenuDialog::Open() {
  LoadValues();
  rex::ui::SetGameInputBlocked(true);
  state_ = State::kOpening;
  scroll_to_row_ = true;
  REXLOG_INFO("game menu: open");
}

void GameMenuDialog::BeginClose() {
  Save();
  state_ = State::kClosing;
  REXLOG_INFO("game menu: closed");
}

void GameMenuDialog::LoadValues() {
  if (!loaded_) {
    settings_ = MakeSettings();
    for (int p = 0; p < 4; ++p)
      controls_[p] = MakeControls(p);
  }
  auto load = [&](Setting& s) {
    if (s.key == kResetKey)
      return;
    // A * setting keeps what was chosen for the next start; the others show what the game runs with now.
    if (loaded_ && (s.restart || s.dirty))
      return;
    const std::string raw = CurrentRaw(s);
    s.value = FindChoice(s, raw);
    if (s.value < 0)
      s.custom = raw;
    if (!loaded_)
      s.running = s.value;
  };
  for (Setting& s : settings_)
    load(s);
  for (auto& player : controls_)
    for (Setting& s : player)
      load(s);
  loaded_ = true;
}

void GameMenuDialog::Change(Setting& s, int step) {
  if (s.key == kResetKey) {
    for (auto& player : controls_)
      if (&player.back() == &s)
        for (Setting& b : player)
          if (b.key != kResetKey && b.value != b.default_choice) {
            b.value = b.default_choice;
            b.dirty = true;
            Apply(b);
          }
    return;
  }
  const int n = int(s.choices.size());
  if (n == 0)
    return;
  s.value = s.value < 0 ? (step > 0 ? 0 : n - 1) : (s.value + step % n + n) % n;
  s.dirty = true;
  Apply(s);
}

void GameMenuDialog::Apply(Setting& s) {
  if (s.restart || s.value < 0)
    return;  // read at start: written for the next one
  const Choice& c = s.choices[size_t(s.value)];
  if (s.key == "frame_cap") {
    BlurSetFrameCapLive(c.raw == "Off" ? 0 : std::atoi(c.raw.c_str()));
    return;
  }
  if (!rex::cvar::SetFlagByName(s.key, c.raw))
    REXLOG_WARN("game menu: {} = {} not taken", s.key, c.raw);
#if REX_PLATFORM_SWITCH
  if (s.key.rfind("input_p", 0) == 0)
    rex::input::nx::RebuildButtonMaps();
#endif
}

void GameMenuDialog::Save() {
  std::vector<FileEdit> edits;
  auto collect = [&](Setting& s) {
    if (!s.dirty || s.key == kResetKey || s.value < 0)
      return;
    s.dirty = false;
    FileEdit e;
    e.key = s.key;
    if (!s.always_in_file && s.value == s.default_choice)
      e.remove = true;
    else
      e.toml = s.choices[size_t(s.value)].toml;
    edits.push_back(std::move(e));
  };
  for (Setting& s : settings_)
    collect(s);
  for (auto& player : controls_)
    for (Setting& s : player)
      collect(s);
  if (edits.empty())
    return;
  const bool ok = UpdateConfigFile(config_path_, edits);
  REXLOG_INFO("game menu: {} setting(s) written to {}{}", edits.size(), config_path_.string(), ok ? "" : ": FAILED");
#if REX_PLATFORM_SWITCH
  // The four-core option's file at once (the forwarder's NPDM), so the next start has the cores it says.
  for (const FileEdit& e : edits)
    if (e.key == "four_core_mode")
      REXLOG_INFO("game menu: four-core mode {}: {}", e.toml, RexSwitchApplyFourCoreMode(e.toml == "true" ? 1 : 0));
#endif
}

rex::system::AchievementManager* GameMenuDialog::Achievements() const {
  return runtime_ && runtime_->kernel_state() ? &runtime_->kernel_state()->achievements() : nullptr;
}

bool GameMenuDialog::AnyButtonDown() const {
  for (ImGuiKey k : kGamepadKeys)
    if (ImGui::IsKeyDown(k))
      return true;
  return false;
}

int GameMenuDialog::RowCount() const {
  if (tab_ == kSettings)
    return int(settings_.size());
  if (tab_ == kAchievements) {
    rex::system::AchievementManager* manager = Achievements();
    return manager ? int(manager->ListAchievements().size()) : 0;
  }
  return int(controls_[tab_ - kP1].size());
}

void GameMenuDialog::OnDraw(ImGuiIO& io) {
  if (state_ == State::kClosed)
    return;
  if (state_ == State::kClosing) {
    // Nothing drawn; the game gets its controllers back once every button is up (the + that closed the menu too).
    if (!AnyButtonDown()) {
      rex::ui::SetGameInputBlocked(false);
      state_ = State::kClosed;
    }
    return;
  }
  if (state_ == State::kOpening && !AnyButtonDown())
    state_ = State::kOpen;  // the six buttons that opened it are up: from now on the presses are the menu's

  ImFont* font = MenuFont(io);
  if (font)
    ImGui::PushFont(font, font->LegacySize);
  const float scale = font ? 1.0f : std::max(1.0f, io.DisplaySize.y / 720.0f) * 1.8f;

  // ---- Input (the menu's own: the window takes no ImGui navigation) ----
  const int before_tab = tab_;
  if (state_ == State::kOpen) {
    if (Pressed(ImGuiKey_GamepadStart, ImGuiKey_Escape, ImGuiKey_None, false)) {
      if (font)
        ImGui::PopFont();
      BeginClose();
      return;
    }
    if (Pressed(ImGuiKey_GamepadL1, ImGuiKey_PageUp, ImGuiKey_None, false))
      tab_ = (tab_ + kTabCount - 1) % kTabCount;
    if (Pressed(ImGuiKey_GamepadR1, ImGuiKey_PageDown, ImGuiKey_None, false))
      tab_ = (tab_ + 1) % kTabCount;
    const int rows = RowCount();
    int& row = row_[tab_];
    if (rows > 0) {
      if (Pressed(ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp, ImGuiKey_UpArrow)) {
        row = (row + rows - 1) % rows;
        scroll_to_row_ = true;
      }
      if (Pressed(ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown, ImGuiKey_DownArrow)) {
        row = (row + 1) % rows;
        scroll_to_row_ = true;
      }
      row = std::clamp(row, 0, rows - 1);
      if (tab_ != kAchievements) {
        Setting& s = tab_ == kSettings ? settings_[size_t(row)] : controls_[tab_ - kP1][size_t(row)];
        // Confirm by the layout, as ImGui's own (ApplyEntry): the bottom button with the Xbox layout, else A.
        const ImGuiKey confirm = io.ConfigNavSwapGamepadButtons ? ImGuiKey_GamepadFaceRight : ImGuiKey_GamepadFaceDown;
        if (Pressed(ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft, ImGuiKey_LeftArrow))
          Change(s, -1);
        if (Pressed(ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight, ImGuiKey_RightArrow) ||
            Pressed(confirm, ImGuiKey_Enter, ImGuiKey_None, false))
          Change(s, +1);
      }
    }
  }
  if (tab_ != before_tab)
    scroll_to_row_ = true;

  // ---- The window ----
  const ImVec2 margin(io.DisplaySize.x * 0.06f, io.DisplaySize.y * 0.07f);
  ImGui::SetNextWindowPos(margin, ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - 2.0f * margin.x, io.DisplaySize.y - 2.0f * margin.y),
                           ImGuiCond_Always);
  ImGui::SetNextWindowFocus();
  ImGui::PushStyleColor(ImGuiCol_WindowBg, kBlack);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Border, kGreen);
  ImGui::PushStyleColor(ImGuiCol_Text, kText);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, kPanel);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, kGreenDim);
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kGreen);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, kGreenDark);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 16.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse;
  if (ImGui::Begin("##blur_game_menu", nullptr, flags)) {
    if (!font)
      ImGui::SetWindowFontScale(scale);
    row_height_ = ImGui::GetFontSize() * 1.9f;
    PickTextFont(io);
    const float width = ImGui::GetContentRegionAvail().x;
    DrawTabs(width);
    // The list fills what the footer leaves.
    const float footer = ImGui::GetFontSize() * 3.6f;
    const float list = std::max(row_height_, ImGui::GetContentRegionAvail().y - footer);
    ImGui::BeginChild("##list", ImVec2(0.0f, list), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar);
    if (!font)
      ImGui::SetWindowFontScale(scale);
    if (tab_ == kSettings)
      DrawSettingsRows(settings_, false, 0);
    else if (tab_ == kAchievements)
      DrawAchievements();
    else
      DrawSettingsRows(controls_[tab_ - kP1], true, tab_ - kP1);
    ImGui::EndChild();
    DrawFooter();
  }
  ImGui::End();
  ImGui::PopStyleVar(4);
  ImGui::PopStyleColor(8);
  if (font)
    ImGui::PopFont();
}

void GameMenuDialog::DrawTabs(float width) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float font = ImGui::GetFontSize();
  const float height = font * 1.9f;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  // L and R at the ends, the tabs between them, as the mockup: names split by bars.
  const char* left_hint = "L";
  const char* right_hint = "R";
  const float hint_w = font * 1.6f;
  auto hint = [&](float x, const char* t) {
    const ImVec2 a(x, origin.y + height * 0.14f), b(x + hint_w, origin.y + height * 0.86f);
    dl->AddRect(a, b, U32(kGreenDim), 5.0f, 0, 2.0f);
    const ImVec2 ts = ImGui::CalcTextSize(t);
    dl->AddText(ImVec2(a.x + (hint_w - ts.x) * 0.5f, origin.y + (height - ts.y) * 0.5f), U32(kTextDim), t);
  };
  hint(origin.x, left_hint);
  hint(origin.x + width - hint_w, right_hint);
  float total = 0.0f;
  const float sep = font * 1.4f;
  for (int i = 0; i < kTabCount; ++i)
    total += ImGui::CalcTextSize(kTabNames[i]).x + (i ? sep : 0.0f);
  const float pad = font * 0.5f;
  float x = origin.x + std::max(hint_w + font * 0.5f, (width - total) * 0.5f);
  for (int i = 0; i < kTabCount; ++i) {
    if (i) {
      const ImVec2 bar(x + sep * 0.5f, origin.y + height * 0.22f);
      dl->AddLine(bar, ImVec2(bar.x, origin.y + height * 0.78f), U32(kGreenDim), 2.0f);
      x += sep;
    }
    const ImVec2 ts = ImGui::CalcTextSize(kTabNames[i]);
    const ImVec2 a(x - pad, origin.y + height * 0.1f), b(x + ts.x + pad, origin.y + height * 0.9f);
    ImGui::SetCursorScreenPos(a);
    ImGui::PushID(i);
    ImGui::InvisibleButton("##tab", ImVec2(b.x - a.x, b.y - a.y));
    if (ImGui::IsItemClicked() && state_ == State::kOpen && tab_ != i) {
      tab_ = i;
      scroll_to_row_ = true;
    }
    ImGui::PopID();
    if (i == tab_)
      dl->AddRectFilled(a, b, U32(kGreen), 6.0f);
    dl->AddText(ImVec2(x, origin.y + (height - ts.y) * 0.5f), i == tab_ ? U32(kBlack, 1.0f / kBlack.w) : U32(kText),
                kTabNames[i]);
    x += ts.x;
  }
  // The thick line under the tabs.
  dl->AddRectFilled(ImVec2(origin.x - 4.0f, origin.y + height + 2.0f), ImVec2(origin.x + width + 4.0f, origin.y + height + 6.0f),
                    U32(kGreen));
  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height + 6.0f + font * 0.5f));
  ImGui::Dummy(ImVec2(width, 0.0f));
}

void GameMenuDialog::DrawSettingsRows(std::vector<Setting>& rows, bool controls, int player) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float width = ImGui::GetContentRegionAvail().x;
  const float font = ImGui::GetFontSize();
  const float pad = font * 0.7f;
  const float value_x = width * 0.52f;
  const float value_w = width - value_x - pad;
  const float arrow_w = font * 1.6f;
  const bool by_position = rex::cvar::GetFlagByName("input_xbox_layout") == "true";
  int& selected = row_[tab_];
  for (size_t i = 0; i < rows.size(); ++i) {
    Setting& s = rows[i];
    const bool sel = int(i) == selected;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(int(i));
    ImGui::InvisibleButton("##row", ImVec2(width, row_height_));
    if (ImGui::IsItemClicked() && state_ == State::kOpen) {
      const float mx = ImGui::GetIO().MousePos.x - p.x;
      if (s.key != kResetKey && mx >= value_x && mx < value_x + arrow_w)
        Change(s, -1);
      else if (mx >= value_x + value_w - arrow_w || (sel && mx >= value_x) || s.key == kResetKey)
        Change(s, +1);
      selected = int(i);
    }
    if (sel && scroll_to_row_) {
      ImGui::SetScrollHereY(0.5f);
      scroll_to_row_ = false;
    }
    ImGui::PopID();
    // The row: a band and a bar for the chosen one, a thin line under the others.
    const ImVec2 a(p.x, p.y + 2.0f), b(p.x + width, p.y + row_height_ - 2.0f);
    if (sel) {
      dl->AddRectFilled(a, b, U32(kGreenDark), 6.0f);
      dl->AddRectFilled(a, ImVec2(a.x + 5.0f, b.y), U32(kGreen), 2.0f);
    } else {
      dl->AddLine(ImVec2(a.x + pad, b.y + 1.0f), ImVec2(b.x - pad, b.y + 1.0f), U32(kGreenDim, 0.35f), 1.0f);
    }
    const float ty = p.y + (row_height_ - font) * 0.5f;
    std::string label = s.label;
    dl->AddText(ImVec2(p.x + pad * 1.6f, ty), U32(sel ? kText : kTextDim), label.c_str());
    if (s.restart) {
      const float lw = ImGui::CalcTextSize(label.c_str()).x;
      dl->AddText(ImVec2(p.x + pad * 1.6f + lw + font * 0.15f, ty), U32(kGreen), "*");
    }
    // The value, between its arrows.
    std::string value;
    if (s.key == kResetKey) {
      value = s.choices[0].label;
    } else if (s.value < 0) {
      value = s.custom;
    } else if (controls && s.value == 0) {
      // "Default (B)": the Switch button the layout gives this Xbox button.
      const XboxButton& xb = kXboxButtons[i < std::size(kXboxButtons) ? i : 0];
      value = std::string("Default (") + (by_position ? xb.by_position : xb.by_letter) + ")";
    } else {
      value = s.choices[size_t(s.value)].label;
    }
    const bool pending = s.restart && s.value != s.running;
    const bool warn = s.key == "four_core_mode" && s.value != s.default_choice;  // Off: the warning's colour
    const ImVec2 vs = ImGui::CalcTextSize(value.c_str());
    const float vx = p.x + value_x + (value_w - vs.x) * 0.5f;
    dl->AddText(ImVec2(vx, ty), U32(warn ? kWarn : pending ? kPending : (sel ? kGreen : kText)), value.c_str());
    if (sel && s.key != kResetKey) {
      const float cy = p.y + row_height_ * 0.5f, h = font * 0.32f;
      const float lx = p.x + value_x + arrow_w * 0.35f, rx = p.x + value_x + value_w - arrow_w * 0.35f;
      dl->AddTriangleFilled(ImVec2(lx, cy), ImVec2(lx + h * 1.3f, cy - h), ImVec2(lx + h * 1.3f, cy + h), U32(kGreen));
      dl->AddTriangleFilled(ImVec2(rx, cy), ImVec2(rx - h * 1.3f, cy - h), ImVec2(rx - h * 1.3f, cy + h), U32(kGreen));
    }
    if (pending) {
      const char* tag = "next start";
      const ImVec2 tsz = TextSize(tag);
      const float tx = std::min(vx + vs.x + font * 0.6f, p.x + width - tsz.x - pad);
      dl->AddText(g_text_font, g_text_size, ImVec2(tx, ty + (font - g_text_size) * 0.7f), U32(kPending, 0.85f), tag);
    }
  }
  (void)player;
}

void GameMenuDialog::DrawAchievements() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float width = ImGui::GetContentRegionAvail().x;
  const float font = ImGui::GetFontSize();
  const float pad = font * 0.7f;
  rex::system::AchievementManager* manager = Achievements();
  if (!manager) {
    ImGui::TextColored(kTextDim, "No achievements: the game's data is not loaded yet.");
    return;
  }
  if (!icons_ && immediate_drawer_ && runtime_)
    icons_ = std::make_unique<rex::ui::AchievementIconCache>(immediate_drawer_, runtime_);
  const auto list = manager->ListAchievements();
  int unlocked = 0, earned = 0, total = 0;
  for (const auto& a : list) {
    total += int(a.gamerscore);
    if (manager->IsUnlocked(a.id)) {
      ++unlocked;
      earned += int(a.gamerscore);
    }
  }
  if (list.empty()) {
    ImGui::TextColored(kTextDim, "No achievements found in the game's data.");
    return;
  }
  // The summary and its bar.
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    char line[96];
    std::snprintf(line, sizeof(line), "%d of %d unlocked", unlocked, int(list.size()));
    dl->AddText(ImVec2(p.x + pad, p.y), U32(kText), line);
    std::snprintf(line, sizeof(line), "%dG / %dG", earned, total);
    const float gw = ImGui::CalcTextSize(line).x;
    dl->AddText(ImVec2(p.x + width - pad - gw, p.y), U32(kGreen), line);
    const float by = p.y + font * 1.35f, bh = font * 0.32f;
    dl->AddRectFilled(ImVec2(p.x + pad, by), ImVec2(p.x + width - pad, by + bh), U32(kGreenDark), bh * 0.5f);
    const float frac = float(unlocked) / float(list.size());
    if (frac > 0.0f)
      dl->AddRectFilled(ImVec2(p.x + pad, by), ImVec2(p.x + pad + (width - 2.0f * pad) * frac, by + bh), U32(kGreen),
                        bh * 0.5f);
    ImGui::Dummy(ImVec2(width, font * 2.1f));
  }
  const float row_h = font * 3.1f;
  const float icon = row_h - font * 0.6f;
  int& selected = row_[tab_];
  ImGui::BeginChild("##ach", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar);
  ImDrawList* cl = ImGui::GetWindowDrawList();
  const float cw = ImGui::GetContentRegionAvail().x;
  for (size_t i = 0; i < list.size(); ++i) {
    const auto& a = list[i];
    const bool got = manager->IsUnlocked(a.id);
    const bool sel = int(i) == selected;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(int(i));
    ImGui::InvisibleButton("##a", ImVec2(cw, row_h));
    if (ImGui::IsItemClicked() && state_ == State::kOpen)
      selected = int(i);
    if (sel && scroll_to_row_) {
      ImGui::SetScrollHereY(0.5f);
      scroll_to_row_ = false;
    }
    ImGui::PopID();
    const ImVec2 ra(p.x, p.y + 2.0f), rb(p.x + cw, p.y + row_h - 2.0f);
    if (sel) {
      cl->AddRectFilled(ra, rb, U32(kGreenDark), 6.0f);
      cl->AddRectFilled(ra, ImVec2(ra.x + 5.0f, rb.y), U32(kGreen), 2.0f);
    } else if (got) {
      cl->AddRectFilled(ra, rb, U32(kPanel), 6.0f);
    }
    // The game's own image (its default.xex's), dimmed while locked.
    const ImVec2 ia(p.x + pad * 1.4f, p.y + (row_h - icon) * 0.5f), ib(ia.x + icon, ia.y + icon);
    rex::ui::ImmediateTexture* tex = icons_ ? icons_->GetIcon(a) : nullptr;
    if (tex)
      cl->AddImage(reinterpret_cast<ImTextureID>(tex), ia, ib, ImVec2(0, 0), ImVec2(1, 1),
                   got ? IM_COL32_WHITE : IM_COL32(90, 110, 95, 255));
    else
      cl->AddRect(ia, ib, U32(kGreenDim), 4.0f, 0, 2.0f);
    cl->AddRect(ia, ib, U32(got ? kGreen : kGreenDim), 4.0f, 0, got ? 2.0f : 1.0f);
    const float tx = ib.x + pad;
    const float text_w = p.x + cw - pad - tx;
    char score[16];
    std::snprintf(score, sizeof(score), "%uG", a.gamerscore);
    const float sw = ImGui::CalcTextSize(score).x;
    cl->AddText(ImVec2(p.x + cw - pad - sw, p.y + font * 0.35f), U32(got ? kGreen : kTextDim), score);
    cl->AddText(ImVec2(tx, p.y + font * 0.35f), U32(got ? kText : kTextDim), a.label.c_str());
    const std::string& desc = got ? a.description : a.unachieved_description;
    const char* tag = got ? "Unlocked" : nullptr;
    const float tw = tag ? TextSize(tag).x + pad : 0.0f;
    cl->AddText(g_text_font, g_text_size, ImVec2(tx, p.y + font * 1.55f), U32(kTextDim, got ? 1.0f : 0.8f), desc.c_str(),
                nullptr, text_w - tw);
    if (tag)
      cl->AddText(g_text_font, g_text_size, ImVec2(p.x + cw - pad - TextSize(tag).x, p.y + font * 1.55f), U32(kGreen), tag);
  }
  ImGui::EndChild();
}

void GameMenuDialog::DrawFooter() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float width = ImGui::GetContentRegionAvail().x;
  const float font = ImGui::GetFontSize();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  dl->AddLine(ImVec2(p.x - 4.0f, p.y + font * 0.3f), ImVec2(p.x + width + 4.0f, p.y + font * 0.3f), U32(kGreenDim), 2.0f);
  // The chosen row's help.
  std::string help;
  bool warning = false;
  if (tab_ == kSettings && row_[tab_] < int(settings_.size())) {
    help = settings_[size_t(row_[tab_])].help;
    warning = settings_[size_t(row_[tab_])].key == "four_core_mode";
  }
  else if (tab_ >= kP1 && row_[tab_] < int(controls_[tab_ - kP1].size()))
    help = controls_[tab_ - kP1][size_t(row_[tab_])].help;
  if (!help.empty())
    dl->AddText(g_text_font, g_text_size, ImVec2(p.x + font * 0.3f, p.y + font * 0.65f),
                U32(warning ? kWarn : kTextDim), help.c_str(), nullptr, width - font * 0.6f);
  // The note for the * settings, and the buttons.
  const float y = p.y + font * 2.15f;
  if (tab_ == kSettings)
    dl->AddText(g_text_font, g_text_size, ImVec2(p.x + font * 0.3f, y), U32(kGreen),
                "* Must restart the game to change these settings");
  const char* keys = tab_ == kAchievements ? "Up/Down: scroll    L/R: tabs    +: close"
                                           : "Up/Down: choose    Left/Right: change    L/R: tabs    +: close";
  const float kw = TextSize(keys).x;
  dl->AddText(g_text_font, g_text_size, ImVec2(p.x + width - kw - font * 0.3f, y), U32(kTextDim), keys);
  ImGui::Dummy(ImVec2(width, font * 3.0f));
}

}  // namespace blur
