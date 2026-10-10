#include "chat_app.hpp"

#include "theme.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <ctime>

using chat::Message;
using chat::MsgType;
using namespace ui;

namespace {

constexpr const char* kAppName = "elazharyLearn";
constexpr const char* kOrganization = "ENSEM University";
constexpr uint64_t kGroupWindowMs = 5 * 60 * 1000;
constexpr std::size_t kMaxLinesPerConversation = 2000;
constexpr double kTypingRefreshSec = 3.0;
constexpr double kTypingIdleSec = 5.0;
constexpr double kTypingExpirySec = 6.0;

// ---------------------------------------------------------------------------
// Small helpers

template <std::size_t N>
void copyToBuffer(char (&buffer)[N], const std::string& text) {
    const std::size_t n = std::min(text.size(), N - 1);
    std::memcpy(buffer, text.data(), n);
    buffer[n] = '\0';
}

bool lessIgnoreCase(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
}

std::tm localTime(uint64_t ms) {
    const auto seconds = static_cast<std::time_t>(ms / 1000);
    std::tm local{};
    localtime_s(&local, &seconds);
    return local;
}

std::string formatTime(uint64_t ms, const char* format) {
    const std::tm local = localTime(ms);
    char text[64];
    std::strftime(text, sizeof text, format, &local);
    return text;
}

int dayNumber(uint64_t ms) {
    const std::tm local = localTime(ms);
    return (local.tm_year + 1900) * 1000 + local.tm_yday;
}

std::string dayLabel(uint64_t ms) {
    const uint64_t now = chat::nowMs();
    if (dayNumber(ms) == dayNumber(now))
        return "Today";
    if (dayNumber(ms) == dayNumber(now - 24ull * 60 * 60 * 1000))
        return "Yesterday";
    return formatTime(ms, "%A, %d %B %Y");
}

int nameCharFilter(ImGuiInputTextCallbackData* data) {
    const ImWchar c = data->EventChar;
    const bool allowed = c < 128 && (std::isalnum(static_cast<int>(c)) || c == '_' || c == '-' || c == '.');
    return allowed ? 0 : 1;
}

ImVec2 textSize(ImFont* font, std::string_view text, float wrap = 0.0f) {
    const ImVec2 size =
        font->CalcTextSizeA(font->FontSize, FLT_MAX, wrap, text.data(), text.data() + text.size());
    return ImVec2(std::ceil(size.x), std::ceil(size.y));
}

void drawText(ImDrawList* dl, ImFont* font, ImVec2 pos, ImU32 color, std::string_view text, float wrap = 0.0f) {
    dl->AddText(font, font->FontSize, pos, color, text.data(), text.data() + text.size(), wrap);
}

// ---------------------------------------------------------------------------
// Custom-drawn widgets

void drawLogo(ImDrawList* dl, ImVec2 p, float size) {
    dl->AddRectFilled(p, p + ImVec2(size, size), col::accent, size * 0.3f);
    const ImVec2 b0(p.x + size * 0.22f, p.y + size * 0.25f);
    const ImVec2 b1(p.x + size * 0.78f, p.y + size * 0.65f);
    dl->AddRectFilled(b0, b1, col::white, size * 0.12f);
    dl->AddTriangleFilled(ImVec2(p.x + size * 0.32f, b1.y - 1.0f), ImVec2(p.x + size * 0.48f, b1.y - 1.0f),
                          ImVec2(p.x + size * 0.30f, p.y + size * 0.80f), col::white);
    const float cy = (b0.y + b1.y) * 0.5f;
    for (int i = -1; i <= 1; ++i)
        dl->AddCircleFilled(ImVec2(p.x + size * 0.5f + static_cast<float>(i) * size * 0.13f, cy), size * 0.045f,
                            col::accent);
}

void drawAvatar(ImDrawList* dl, ImVec2 center, float radius, const std::string& name) {
    dl->AddCircleFilled(center, radius, avatarColor(name));
    const char initial[2] = {
        static_cast<char>(std::toupper(static_cast<unsigned char>(name.empty() ? '?' : name[0]))), '\0'};
    const float size = radius * 1.05f;
    const ImVec2 ts = fonts.title->CalcTextSizeA(size, FLT_MAX, 0.0f, initial);
    dl->AddText(fonts.title, size, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), col::white, initial);
}

void drawPresence(ImDrawList* dl, ImVec2 center, float radius, bool online, ImU32 ring) {
    dl->AddCircleFilled(center, radius + px(2.0f), ring);
    dl->AddCircleFilled(center, radius, online ? col::online : col::textFaint);
}

void drawSpinner(ImDrawList* dl, ImVec2 center, float radius, ImU32 color, float thickness) {
    const float start = static_cast<float>(ImGui::GetTime()) * 7.0f;
    dl->PathClear();
    dl->PathArcTo(center, radius, start, start + 3.14159265f * 1.4f, 24);
    dl->PathStroke(color, ImDrawFlags_None, thickness);
}

// Soft radial glow faked with stacked translucent circles.
void drawGlow(ImDrawList* dl, ImVec2 center, float radius, ImU32 color) {
    constexpr int kSteps = 24;
    for (int i = 0; i < kSteps; ++i) {
        const float t = static_cast<float>(i) / kSteps;
        dl->AddCircleFilled(center, radius * (1.0f - t), withAlpha(color, 0.009f), 64);
    }
}

bool accentButton(const char* label, ImVec2 size, bool enabled = true) {
    ImGui::BeginDisabled(!enabled);
    ImGui::PushFont(fonts.bold);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const bool pressed = ImGui::Button(label, size);
    if (enabled && ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    ImGui::EndDisabled();
    return pressed && enabled;
}

bool ghostButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, col::rgba(255, 255, 255, 10));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col::rgba(255, 255, 255, 22));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col::rgba(255, 255, 255, 34));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const bool pressed = ImGui::Button(label, size);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return pressed;
}

void fieldLabel(const char* text) {
    ImGui::PushFont(fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, col::textDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void sectionLabel(const char* text, const std::string& right = {}) {
    ImGui::PushFont(fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, col::textFaint);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(10));
    ImGui::TextUnformatted(text);
    if (!right.empty()) {
        ImGui::SameLine();
        const float w = ImGui::CalcTextSize(right.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w - px(10));
        ImGui::TextUnformatted(right.c_str());
    }
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(2)));
}

void noticeBox(const std::string& text, ImU32 background, ImU32 foreground) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 pad(px(14), px(10));
    const ImVec2 ts = textSize(fonts.regular, text, width - pad.x * 2);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size(width, ts.y + pad.y * 2);
    dl->AddRectFilled(p, p + size, background, px(10));
    drawText(dl, fonts.regular, p + pad, foreground, text, width - pad.x * 2);
    ImGui::Dummy(size);
}

}  // namespace

// ===========================================================================

ChatApp::ChatApp(HWND hwnd) : hwnd_(hwnd) {
    settings_.load();
    copyToBuffer(nameBuf_, settings_.name);
    copyToBuffer(hostBuf_, settings_.host);
    copyToBuffer(portBuf_, std::to_string(settings_.port));
    focusLogin_ = settings_.name.empty();
}

void ChatApp::frame() {
    processEvents();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    constexpr ImGuiWindowFlags kRootFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoSavedSettings |
                                            ImGuiWindowFlags_NoBringToFrontOnFocus |
                                            ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##root", nullptr, kRootFlags);
    ImGui::PopStyleVar();

    if (screen_ == Screen::Login)
        drawLogin();
    else
        drawChat();

    ImGui::End();

    // Deferred so we never mutate state that is being iterated while drawing.
    if (pendingSwitch_) {
        switchTo(*pendingSwitch_);
        pendingSwitch_.reset();
    }
    if (logoutRequested_) {
        logoutRequested_ = false;
        logout();
    }
    updateWindowTitle();
}

// ---------------------------------------------------------------------------
// Network events -> UI state

void ChatApp::processEvents() {
    for (chat::ClientEvent& event : client_.pollEvents()) {
        switch (event.kind) {
        case chat::ClientEvent::Kind::ConnectFailed:
            if (screen_ == Screen::Login) {
                conn_ = Conn::Idle;
                loginError_ = "Couldn't reach " + host_ + ":" + std::to_string(port_) + ". " + event.error;
            } else {
                conn_ = Conn::Offline;
                offlineReason_ = event.error;
            }
            break;
        case chat::ClientEvent::Kind::Disconnected:
            if (screen_ == Screen::Login) {
                conn_ = Conn::Idle;
                loginError_ = event.error;
            } else {
                onConnectionLost(event.error);
            }
            break;
        case chat::ClientEvent::Kind::Message:
            onMessage(event.message);
            break;
        }
    }
}

void ChatApp::onMessage(const Message& m) {
    switch (m.type) {
    case MsgType::Welcome: {
        const bool firstLogin = screen_ == Screen::Login;
        me_ = m.to;
        users_ = m.users;
        std::sort(users_.begin(), users_.end(), lessIgnoreCase);
        conn_ = Conn::Online;
        loginError_.clear();
        offlineReason_.clear();

        // The server replays recent #general history after every Welcome.
        Conversation& general = convs_[""];
        general.lines.clear();
        general.version++;
        general.forceScroll = true;

        if (firstLogin) {
            screen_ = Screen::Chat;
            active_.clear();
            input_[0] = '\0';
        }
        focusInput_ = true;

        settings_.name = me_;
        settings_.host = host_;
        settings_.port = port_;
        settings_.save();
        break;
    }

    case MsgType::Rejected:
        if (screen_ == Screen::Login) {
            conn_ = Conn::Idle;
            loginError_ = m.text;
        } else {
            conn_ = Conn::Offline;
            offlineReason_ = m.text;
        }
        break;

    case MsgType::UserJoined:
        if (!isOnline(m.from)) {
            users_.push_back(m.from);
            std::sort(users_.begin(), users_.end(), lessIgnoreCase);
        }
        addLine("", Line{Line::Kind::System, {}, m.from + " joined the chat", m.timestamp, false}, false);
        if (convs_.count(m.from))
            addLine(m.from, Line{Line::Kind::System, {}, m.from + " is back online", m.timestamp, false}, false);
        break;

    case MsgType::UserLeft:
        users_.erase(std::remove(users_.begin(), users_.end(), m.from), users_.end());
        for (auto& entry : typing_)
            entry.second.erase(m.from);
        addLine("", Line{Line::Kind::System, {}, m.from + " left the chat", m.timestamp, false}, false);
        if (convs_.count(m.from))
            addLine(m.from, Line{Line::Kind::System, {}, m.from + " went offline", m.timestamp, false}, false);
        break;

    case MsgType::Chat: {
        const bool mine = m.from == me_;
        typing_[""].erase(m.from);
        addLine("", Line{Line::Kind::Text, m.from, m.text, m.timestamp, mine}, !mine && !m.flag);
        if (!mine && !m.flag)
            notify();
        break;
    }

    case MsgType::Private: {
        const bool mine = m.from == me_;
        const std::string key = mine ? m.to : m.from;
        typing_[key].erase(m.from);
        addLine(key, Line{Line::Kind::Text, m.from, m.text, m.timestamp, mine}, !mine);
        if (!mine)
            notify();
        break;
    }

    case MsgType::Typing: {
        if (m.from == me_)
            break;
        const std::string key = m.to.empty() ? std::string() : m.from;
        if (m.flag)
            typing_[key][m.from] = ImGui::GetTime() + kTypingExpirySec;
        else
            typing_[key].erase(m.from);
        break;
    }

    case MsgType::System:
        addLine("", Line{Line::Kind::System, {}, m.text, m.timestamp, false}, false);
        break;

    case MsgType::Error:
        addLine(active_, Line{Line::Kind::Error, {}, m.text, chat::nowMs(), false}, false);
        break;

    default:
        break;
    }
}

void ChatApp::onConnectionLost(const std::string& reason) {
    conn_ = Conn::Offline;
    offlineReason_ = reason;
    users_.clear();
    typing_.clear();
    typingSent_ = false;
    addLine("", Line{Line::Kind::Error, {}, "Disconnected: " + reason, chat::nowMs(), false}, false);
}

void ChatApp::addLine(const std::string& key, Line line, bool countUnread) {
    Conversation& c = convs_[key];
    if (line.mine)
        c.forceScroll = true;  // always follow your own messages
    c.lines.push_back(std::move(line));
    if (c.lines.size() > kMaxLinesPerConversation)
        c.lines.erase(c.lines.begin(), c.lines.begin() + (c.lines.size() - kMaxLinesPerConversation));
    c.version++;
    if (countUnread && key != active_)
        c.unread++;
}

void ChatApp::notify() {
    if (GetForegroundWindow() == hwnd_)
        return;
    FLASHWINFO info{};
    info.cbSize = sizeof info;
    info.hwnd = hwnd_;
    info.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;  // flash the taskbar button until focused
    FlashWindowEx(&info);
}

// ---------------------------------------------------------------------------
// Actions

void ChatApp::startConnect() {
    const std::string name = nameBuf_;
    std::string host = chat::sanitizeText(hostBuf_);
    if (host.empty())
        host = "localhost";
    const int port = portBuf_[0] ? std::atoi(portBuf_) : chat::kDefaultPort;

    if (!chat::isValidUsername(name)) {
        loginError_ = "Names must be 1-20 characters: letters, numbers, '_', '-' or '.'.";
        return;
    }
    if (port < 1 || port > 65535) {
        loginError_ = "The port must be a number between 1 and 65535.";
        return;
    }

    loginError_.clear();
    host_ = host;
    port_ = static_cast<uint16_t>(port);
    conn_ = Conn::Connecting;
    client_.connect(host_, port_, name);
}

void ChatApp::reconnect() {
    offlineReason_.clear();
    conn_ = Conn::Connecting;
    client_.connect(host_, port_, me_);
}

void ChatApp::logout() {
    client_.disconnect();
    screen_ = Screen::Login;
    conn_ = Conn::Idle;
    convs_.clear();
    users_.clear();
    typing_.clear();
    active_.clear();
    input_[0] = '\0';
    typingSent_ = false;
    offlineReason_.clear();
    loginError_.clear();
    focusLogin_ = true;
}

void ChatApp::switchTo(const std::string& key) {
    if (key != active_) {
        convs_[active_].draft = input_;
        active_ = key;
        copyToBuffer(input_, convs_[key].draft);
    }
    Conversation& c = convs_[key];
    c.unread = 0;
    c.forceScroll = true;
    focusInput_ = true;
}

void ChatApp::sendCurrent(const std::string& text) {
    const bool sent = active_.empty() ? client_.sendChat(text) : client_.sendPrivate(active_, text);
    if (!sent)
        return;
    input_[0] = '\0';
    if (typingSent_) {
        client_.sendTyping(typingTarget_, false);
        typingSent_ = false;
    }
}

// Sends "typing" at most every few seconds while the user edits, and "stopped"
// when the box is cleared, the user goes idle, or switches conversation.
void ChatApp::updateTyping(bool edited) {
    if (conn_ != Conn::Online) {
        typingSent_ = false;
        return;
    }
    const double now = ImGui::GetTime();
    const bool hasText = input_[0] != '\0';
    if (edited)
        lastEditAt_ = now;

    if (typingSent_ && (typingTarget_ != active_ || !hasText || now - lastEditAt_ > kTypingIdleSec)) {
        client_.sendTyping(typingTarget_, false);
        typingSent_ = false;
    }
    if (edited && hasText && (!typingSent_ || now - typingSentAt_ > kTypingRefreshSec)) {
        client_.sendTyping(active_, true);
        typingSent_ = true;
        typingSentAt_ = now;
        typingTarget_ = active_;
    }
}

// ---------------------------------------------------------------------------
// Queries

bool ChatApp::isOnline(const std::string& user) const {
    return std::find(users_.begin(), users_.end(), user) != users_.end();
}

std::vector<std::string> ChatApp::directMessageList() const {
    std::vector<std::string> names;
    for (const std::string& user : users_)
        if (user != me_)
            names.push_back(user);
    for (const auto& entry : convs_)
        if (!entry.first.empty() && !isOnline(entry.first))
            names.push_back(entry.first);
    std::sort(names.begin(), names.end(), [this](const std::string& a, const std::string& b) {
        const bool onlineA = isOnline(a), onlineB = isOnline(b);
        if (onlineA != onlineB)
            return onlineA;
        return lessIgnoreCase(a, b);
    });
    return names;
}

std::vector<std::string> ChatApp::typingNames(const std::string& key) const {
    std::vector<std::string> names;
    const auto it = typing_.find(key);
    if (it == typing_.end())
        return names;
    const double now = ImGui::GetTime();
    for (const auto& [name, expires] : it->second)
        if (expires > now && name != me_)
            names.push_back(name);
    return names;
}

// ---------------------------------------------------------------------------
// Login screen

void ChatApp::drawLogin() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 ws = ImGui::GetWindowSize();
    drawGlow(dl, ImVec2(wp.x + ws.x * 0.16f, wp.y + ws.y * 0.20f), px(360), col::accent);
    drawGlow(dl, ImVec2(wp.x + ws.x * 0.88f, wp.y + ws.y * 0.86f), px(400), col::rgba(236, 72, 153));

    const float cardW = std::min(px(440), ws.x - px(40));
    const float cardH = loginCardHeight_ > 0.0f ? loginCardHeight_ : px(520);
    ImGui::SetCursorPos(ImVec2((ws.x - cardW) * 0.5f, std::max(px(20), (ws.y - cardH) * 0.5f)));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(40), px(36)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(20));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col::panel);
    ImGui::BeginChild("##login", ImVec2(cardW, 0),
                      ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    ImGui::GetWindowDrawList()->AddRect(ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize(),
                                        col::border, px(20));
    drawLoginForm();

    ImGui::EndChild();
    loginCardHeight_ = ImGui::GetItemRectSize().y;
}

void ChatApp::drawLoginForm() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float width = ImGui::GetContentRegionAvail().x;
    const bool connecting = conn_ == Conn::Connecting;

    const float logo = px(52);
    drawLogo(dl, ImGui::GetCursorScreenPos(), logo);
    ImGui::Dummy(ImVec2(logo, logo + px(6)));

    ImGui::PushFont(fonts.title);
    ImGui::TextUnformatted(kAppName);
    ImGui::PopFont();
    ImGui::PushFont(fonts.bold);
    ImGui::PushStyleColor(ImGuiCol_Text, col::accentText);
    ImGui::TextUnformatted(kOrganization);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, col::textDim);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted("Pick a display name and join a chat server.");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, px(10)));

    bool submit = false;
    ImGui::BeginDisabled(connecting);

    fieldLabel("Display name");
    ImGui::SetNextItemWidth(width);
    if (focusLogin_) {
        ImGui::SetKeyboardFocusHere();
        focusLogin_ = false;
    }
    submit |= ImGui::InputTextWithHint("##name", "e.g. alice", nameBuf_, sizeof nameBuf_,
                                       ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter,
                                       nameCharFilter);
    ImGui::PushFont(fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, col::textFaint);
    ImGui::TextUnformatted("Up to 20 letters, numbers, '_', '-' or '.'");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(4)));

    const float portW = px(100);
    const float hostW = width - portW - ImGui::GetStyle().ItemSpacing.x;
    const float portX = ImGui::GetCursorPosX() + hostW + ImGui::GetStyle().ItemSpacing.x;
    fieldLabel("Server");
    ImGui::SameLine(portX);
    fieldLabel("Port");
    ImGui::SetNextItemWidth(hostW);
    submit |= ImGui::InputTextWithHint("##host", "localhost", hostBuf_, sizeof hostBuf_,
                                       ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine(portX);
    ImGui::SetNextItemWidth(portW);
    submit |= ImGui::InputTextWithHint("##port", "5555", portBuf_, sizeof portBuf_,
                                       ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal);

    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, px(10)));

    if (!loginError_.empty()) {
        noticeBox(loginError_, col::dangerSoft, col::dangerText);
        ImGui::Dummy(ImVec2(0, px(4)));
    }

    const ImVec2 button(width, px(48));
    if (connecting) {
        // Busy state: spinner inside the button, plus a way out.
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(button);
        dl->AddRectFilled(p, p + button, col::accentActive, ImGui::GetStyle().FrameRounding);
        const char* label = "Connecting\xE2\x80\xA6";
        const ImVec2 ts = textSize(fonts.bold, label);
        const float total = px(18) + px(10) + ts.x;
        const float x = p.x + (button.x - total) * 0.5f;
        drawSpinner(dl, ImVec2(x + px(9), p.y + button.y * 0.5f), px(8), col::white, px(2.5f));
        drawText(dl, fonts.bold, ImVec2(x + px(28), p.y + (button.y - ts.y) * 0.5f), col::white, label);
        if (ghostButton("Cancel", ImVec2(width, px(40)))) {
            client_.disconnect();
            conn_ = Conn::Idle;
        }
    } else {
        const bool ready = nameBuf_[0] != '\0';
        if (accentButton("Connect", button, ready) || (submit && ready))
            startConnect();
    }

    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::PushFont(fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, col::textFaint);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted("No server yet? Run chat_server.exe on any PC in your network, then connect to its "
                           "address.");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// ---------------------------------------------------------------------------
// Chat screen

void ChatApp::drawChat() {
    constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, col::sidebar);
    ImGui::BeginChild("##sidebar", ImVec2(px(272), 0), ImGuiChildFlags_None, kPanelFlags);
    ImGui::PopStyleColor();
    drawSidebar();
    ImGui::EndChild();

    ImGui::SameLine(0, 0);
    ImGui::BeginChild("##main", ImVec2(0, 0), ImGuiChildFlags_None, kPanelFlags);
    drawMain();
    ImGui::EndChild();

    ImGui::PopStyleVar();
}

void ChatApp::drawSidebar() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const float headerH = px(72);
    const float footerH = px(72);

    // Header: logo, app name and connection status.
    const float logo = px(36);
    const ImVec2 logoPos(pos.x + px(18), pos.y + (headerH - logo) * 0.5f);
    drawLogo(dl, logoPos, logo);
    const float tx = logoPos.x + logo + px(12);
    drawText(dl, fonts.bold, ImVec2(tx, logoPos.y - px(2)), col::text, kAppName);

    std::string status;
    ImU32 statusColor = col::online;
    switch (conn_) {
    case Conn::Online: status = host_ + ":" + std::to_string(port_); break;
    case Conn::Connecting: status = "Reconnecting\xE2\x80\xA6"; statusColor = col::warning; break;
    default: status = "Offline"; statusColor = col::danger; break;
    }
    const float statusY = logoPos.y + logo - fonts.caption->FontSize + px(1);
    dl->AddCircleFilled(ImVec2(tx + px(4), statusY + fonts.caption->FontSize * 0.5f), px(4), statusColor);
    drawText(dl, fonts.caption, ImVec2(tx + px(14), statusY), col::textDim, status);

    dl->AddLine(ImVec2(pos.x, pos.y + headerH), ImVec2(pos.x + size.x, pos.y + headerH), col::border);
    dl->AddLine(ImVec2(pos.x + size.x - 1, pos.y), ImVec2(pos.x + size.x - 1, pos.y + size.y), col::border);

    // Channels and direct messages.
    ImGui::SetCursorPos(ImVec2(0, headerH + 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(10), px(14)));
    ImGui::BeginChild("##lists", ImVec2(size.x - 1, size.y - headerH - footerH - 1),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, px(2)));

    sectionLabel("CHANNELS");
    sidebarRow("");
    ImGui::Dummy(ImVec2(0, px(14)));

    const std::vector<std::string> people = directMessageList();
    const std::size_t others = users_.empty() ? 0 : users_.size() - (isOnline(me_) ? 1 : 0);
    sectionLabel("DIRECT MESSAGES", conn_ == Conn::Online ? std::to_string(others) + " online" : "");
    for (const std::string& name : people)
        sidebarRow(name);
    if (people.empty()) {
        ImGui::PushFont(fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, col::textFaint);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(10));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(("Nobody else is here yet. Ask a friend to connect to " + host_ + ":" +
                                std::to_string(port_) + ".")
                                   .c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();

    // Footer: who I am, and a way out.
    const float fy = pos.y + size.y - footerH;
    dl->AddLine(ImVec2(pos.x, fy), ImVec2(pos.x + size.x, fy), col::border);
    const float r = px(18);
    const ImVec2 c(pos.x + px(18) + r, fy + footerH * 0.5f);
    drawAvatar(dl, c, r, me_);
    drawPresence(dl, ImVec2(c.x + r * 0.72f, c.y + r * 0.72f), px(5), conn_ == Conn::Online, col::sidebar);
    drawText(dl, fonts.bold, ImVec2(c.x + r + px(12), c.y - fonts.bold->FontSize + px(2)), col::text, me_);
    drawText(dl, fonts.caption, ImVec2(c.x + r + px(12), c.y + px(2)), col::textDim,
             conn_ == Conn::Online ? "Online" : "Offline");

    const ImVec2 button(px(84), px(34));
    ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x - button.x - px(16), fy + (footerH - button.y) * 0.5f));
    if (ghostButton("Sign out", button))
        logoutRequested_ = true;
}

void ChatApp::sidebarRow(const std::string& key) {
    const bool channel = key.empty();
    const bool active = key == active_;
    const auto conv = convs_.find(key);
    const int unread = conv != convs_.end() ? conv->second.unread : 0;
    const bool typing = !typingNames(key).empty();
    const bool online = channel || isOnline(key);

    ImGui::PushID(channel ? "#general" : key.c_str());
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, px(46));
    const bool clicked = ImGui::InvisibleButton("##row", size);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active)
        dl->AddRectFilled(p, p + size, col::accentSoft, px(10));
    else if (hovered)
        dl->AddRectFilled(p, p + size, col::hover, px(10));

    const float r = px(15);
    const ImVec2 c(p.x + px(10) + r, p.y + size.y * 0.5f);
    if (channel) {
        dl->AddRectFilled(c - ImVec2(r, r), c + ImVec2(r, r), active ? col::accent : col::elevated, px(9));
        const ImVec2 hs = textSize(fonts.bold, "#");
        drawText(dl, fonts.bold, c - hs * 0.5f, col::text, "#");
    } else {
        drawAvatar(dl, c, r, key);
        if (!online)
            dl->AddCircleFilled(c, r, col::rgba(16, 18, 25, 130));
        drawPresence(dl, ImVec2(c.x + r * 0.72f, c.y + r * 0.72f), px(4.5f), online, col::sidebar);
    }

    ImFont* nameFont = unread > 0 ? fonts.bold : fonts.regular;
    const ImU32 nameColor = (active || unread > 0) ? col::text : (online ? col::textDim : col::textFaint);
    const char* label = channel ? "general" : key.c_str();
    const float tx = c.x + r + px(12);
    if (typing) {
        drawText(dl, nameFont, ImVec2(tx, c.y - nameFont->FontSize + px(3)), nameColor, label);
        drawText(dl, fonts.caption, ImVec2(tx, c.y + px(1)), col::accentText, "typing\xE2\x80\xA6");
    } else {
        drawText(dl, nameFont, ImVec2(tx, c.y - nameFont->FontSize * 0.5f), nameColor, label);
    }

    if (unread > 0) {
        const std::string count = unread > 99 ? "99+" : std::to_string(unread);
        const ImVec2 ts = textSize(fonts.caption, count);
        const float h = px(20);
        const float w = std::max(h, ts.x + px(12));
        const ImVec2 b0(p.x + size.x - w - px(10), c.y - h * 0.5f);
        dl->AddRectFilled(b0, b0 + ImVec2(w, h), col::accent, h * 0.5f);
        drawText(dl, fonts.caption, ImVec2(b0.x + (w - ts.x) * 0.5f, b0.y + (h - ts.y) * 0.5f), col::white, count);
    }

    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (clicked)
        pendingSwitch_ = key;
}

void ChatApp::drawMain() {
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    Conversation& conv = convs_[active_];

    const float headerH = px(72);
    drawHeader(pos, size.x, headerH);
    float top = headerH;
    if (conn_ != Conn::Online)
        top += drawConnectionBanner(pos, size.x, top);

    const float composerH = px(88);
    const float typingH = px(26);
    const float messagesH = std::max(px(80), size.y - top - composerH - typingH);

    ImGui::SetCursorPos(ImVec2(0, top));
    ImGui::PushID(active_.empty() ? "#general" : active_.c_str());  // separate scroll state per conversation
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(28), px(18)));
    ImGui::BeginChild("##messages", ImVec2(size.x, messagesH), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    drawMessages(conv);
    ImGui::EndChild();
    ImGui::PopID();

    drawTypingLine(ImVec2(pos.x + px(30), pos.y + top + messagesH + px(2)));

    ImGui::SetCursorScreenPos(ImVec2(pos.x + px(24), pos.y + size.y - composerH + px(4)));
    drawComposer(size.x - px(48));
}

void ChatApp::drawHeader(ImVec2 pos, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = px(18);
    const ImVec2 c(pos.x + px(28) + r, pos.y + height * 0.5f);

    std::string title, subtitle;
    if (active_.empty()) {
        dl->AddRectFilled(c - ImVec2(r, r), c + ImVec2(r, r), col::accent, px(10));
        const ImVec2 hs = textSize(fonts.bold, "#");
        drawText(dl, fonts.bold, c - hs * 0.5f, col::white, "#");
        title = "general";
        if (conn_ != Conn::Online)
            subtitle = "Offline";
        else if (users_.size() <= 1)
            subtitle = "Just you so far";
        else
            subtitle = std::to_string(users_.size()) + " members online";
    } else {
        const bool online = isOnline(active_);
        drawAvatar(dl, c, r, active_);
        drawPresence(dl, ImVec2(c.x + r * 0.72f, c.y + r * 0.72f), px(5), online, col::appBg);
        title = active_;
        subtitle = online ? "Online \xC2\xB7 Direct message" : "Offline \xC2\xB7 Direct message";
    }

    const float tx = c.x + r + px(14);
    drawText(dl, fonts.bold, ImVec2(tx, c.y - fonts.bold->FontSize + px(2)), col::text, title);
    drawText(dl, fonts.caption, ImVec2(tx, c.y + px(3)), col::textDim, subtitle);
    dl->AddLine(ImVec2(pos.x, pos.y + height - 1), ImVec2(pos.x + width, pos.y + height - 1), col::border);
}

float ChatApp::drawConnectionBanner(ImVec2 pos, float width, float y) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = px(52);
    const ImVec2 p0(pos.x, pos.y + y);
    const ImVec2 p1(pos.x + width, pos.y + y + h);
    const bool connecting = conn_ == Conn::Connecting;
    const float cy = p0.y + h * 0.5f;

    dl->AddRectFilled(p0, p1, connecting ? col::warningSoft : col::dangerSoft);
    std::string message;
    if (connecting) {
        drawSpinner(dl, ImVec2(p0.x + px(32), cy), px(8), col::warning, px(2.5f));
        message = "Reconnecting to " + host_ + ":" + std::to_string(port_) + "\xE2\x80\xA6";
    } else {
        dl->AddCircleFilled(ImVec2(p0.x + px(32), cy), px(5), col::danger);
        message = offlineReason_.empty() ? "You're offline." : "You're offline. " + offlineReason_;
    }

    const float buttonW = px(116);
    dl->PushClipRect(p0, ImVec2(p1.x - buttonW - px(24), p1.y), true);
    drawText(dl, fonts.regular, ImVec2(p0.x + px(50), cy - fonts.regular->FontSize * 0.5f), col::text, message);
    dl->PopClipRect();

    if (!connecting) {
        const ImVec2 button(buttonW, px(34));
        ImGui::SetCursorScreenPos(ImVec2(p1.x - button.x - px(16), cy - button.y * 0.5f));
        if (accentButton("Reconnect", button))
            reconnect();
    }
    return h;
}

void ChatApp::drawIntro(float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = px(30);
    const ImVec2 c(p.x + r, p.y + r);

    std::string title, body;
    if (active_.empty()) {
        dl->AddRectFilled(p, p + ImVec2(r * 2, r * 2), col::accent, px(16));
        const ImVec2 hs = textSize(fonts.title, "#");
        drawText(dl, fonts.title, c - hs * 0.5f, col::white, "#");
        title = "Welcome to #general";
        body = "This is the start of the channel. Everyone connected to the server can read what you post here.";
    } else {
        drawAvatar(dl, c, r, active_);
        title = active_;
        body = "This is the beginning of your direct messages with " + active_ +
               ". Only the two of you can see them.";
    }

    float y = p.y + r * 2 + px(12);
    drawText(dl, fonts.title, ImVec2(p.x, y), col::text, title);
    y += fonts.title->FontSize + px(4);
    const float wrap = std::min(width, px(560));
    const ImVec2 bs = textSize(fonts.regular, body, wrap);
    drawText(dl, fonts.regular, ImVec2(p.x, y), col::textDim, body, wrap);
    y += bs.y + px(18);
    dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + width, y), col::border);
    ImGui::Dummy(ImVec2(width, y - p.y + px(8)));
}

void ChatApp::drawMessages(Conversation& c) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float width = ImGui::GetContentRegionAvail().x;

    // Auto-scroll: follow new content only if the user was already at the
    // bottom; otherwise offer a "new messages" pill instead of yanking the view.
    const bool contentChanged = c.version != c.renderedVersion;
    const bool layoutChanged = contentChanged || std::fabs(width - c.renderedWidth) > 0.5f;
    const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - px(4);
    if (!layoutChanged) {
        c.stickToBottom = atBottom;
        if (atBottom)
            c.newBelow = false;
    } else if (contentChanged && !c.stickToBottom) {
        c.newBelow = true;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, px(4)));
    drawIntro(width);

    ImFont* font = fonts.regular;
    const float avatarR = px(18);
    const float gutter = avatarR * 2 + px(14);
    const float maxBubble = std::max(px(180), std::min(px(640), (width - gutter) * 0.75f));
    const ImVec2 pad(px(14), px(9));
    const float wrap = maxBubble - pad.x * 2;

    const Line* prev = nullptr;
    int prevDay = -1;
    for (std::size_t i = 0; i < c.lines.size(); ++i) {
        const Line& line = c.lines[i];
        const int day = dayNumber(line.timestamp);
        if (day != prevDay) {
            drawDaySeparator(line.timestamp, width);
            prevDay = day;
            prev = nullptr;
        }
        if (line.kind != Line::Kind::Text) {
            drawSystemLine(line, width);
            prev = nullptr;
            continue;
        }

        const bool grouped = prev && prev->from == line.from && line.timestamp >= prev->timestamp &&
                             line.timestamp - prev->timestamp < kGroupWindowMs;
        if (!grouped && prev)
            ImGui::Dummy(ImVec2(0, px(8)));

        const ImVec2 ts = textSize(font, line.text, wrap);
        const ImVec2 bubble(ts.x + pad.x * 2, ts.y + pad.y * 2);
        ImVec2 cursor = ImGui::GetCursorScreenPos();

        if (!grouped) {
            const float headerH = fonts.bold->FontSize + px(2);
            if (ImGui::IsRectVisible(cursor, cursor + ImVec2(width, headerH + avatarR * 2))) {
                const std::string clock = formatTime(line.timestamp, "%H:%M");
                if (line.mine) {
                    const std::string label = "You  \xC2\xB7  " + clock;
                    const ImVec2 ls = textSize(fonts.caption, label);
                    drawText(dl, fonts.caption, ImVec2(cursor.x + width - ls.x - px(4), cursor.y + headerH - ls.y),
                             col::textFaint, label);
                } else {
                    drawAvatar(dl, ImVec2(cursor.x + avatarR, cursor.y + avatarR), avatarR, line.from);
                    drawText(dl, fonts.bold, ImVec2(cursor.x + gutter, cursor.y), avatarColor(line.from), line.from);
                    const float nameW = textSize(fonts.bold, line.from).x;
                    drawText(dl, fonts.caption,
                             ImVec2(cursor.x + gutter + nameW + px(8),
                                    cursor.y + fonts.bold->FontSize - fonts.caption->FontSize - px(1)),
                             col::textFaint, clock);
                }
            }
            ImGui::Dummy(ImVec2(width, headerH));
            cursor = ImGui::GetCursorScreenPos();
        }

        const float x = line.mine ? cursor.x + width - bubble.x : cursor.x + gutter;
        ImGui::SetCursorScreenPos(ImVec2(x, cursor.y));
        ImGui::PushID(static_cast<int>(i));
        ImGui::InvisibleButton("##message", bubble);
        if (ImGui::IsItemVisible()) {
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 b0 = ImGui::GetItemRectMin();
            const ImVec2 b1 = ImGui::GetItemRectMax();
            const ImU32 background = line.mine ? (hovered ? col::accentHover : col::accent)
                                               : (hovered ? col::bubbleHover : col::bubble);
            dl->AddRectFilled(b0, b1, background, px(16));
            drawText(dl, font, b0 + pad, line.mine ? col::white : col::text, line.text, wrap);

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s", formatTime(line.timestamp, "%A, %d %B %Y at %H:%M").c_str());
        }
        if (ImGui::BeginPopupContextItem("##menu")) {
            if (ImGui::MenuItem("Copy text"))
                ImGui::SetClipboardText(line.text.c_str());
            if (!line.mine && active_.empty() && ImGui::MenuItem(("Message " + line.from + " privately").c_str()))
                pendingSwitch_ = line.from;
            ImGui::EndPopup();
        }
        ImGui::PopID();
        prev = &line;
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::PopStyleVar();

    if (c.forceScroll || (layoutChanged && c.stickToBottom)) {
        ImGui::SetScrollHereY(1.0f);
        c.forceScroll = false;
        c.stickToBottom = true;
        c.newBelow = false;
    }
    c.renderedVersion = c.version;
    c.renderedWidth = width;

    if (c.newBelow)
        drawNewMessagesPill(c);
}

void ChatApp::drawSystemLine(const Line& line, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = fonts.caption;
    const float wrap = width - px(80);
    const ImVec2 ts = textSize(font, line.text, wrap);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ts.y + px(12);
    ImGui::Dummy(ImVec2(width, h));
    if (!ImGui::IsItemVisible())
        return;

    const float x = p.x + (width - ts.x) * 0.5f;
    const float y = p.y + px(6);
    if (line.kind == Line::Kind::Error) {
        dl->AddRectFilled(ImVec2(x - px(12), y - px(4)), ImVec2(x + ts.x + px(12), y + ts.y + px(4)),
                          col::dangerSoft, px(10));
        drawText(dl, font, ImVec2(x, y), col::dangerText, line.text, wrap);
    } else {
        drawText(dl, font, ImVec2(x, y), col::textFaint, line.text, wrap);
    }
}

void ChatApp::drawDaySeparator(uint64_t timestamp, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const std::string label = dayLabel(timestamp);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = px(30);
    ImGui::Dummy(ImVec2(width, h));

    const ImVec2 ts = textSize(fonts.caption, label);
    const float cy = p.y + h * 0.5f;
    const float tx = p.x + (width - ts.x) * 0.5f;
    dl->AddLine(ImVec2(p.x, cy), ImVec2(tx - px(12), cy), col::border);
    dl->AddLine(ImVec2(tx + ts.x + px(12), cy), ImVec2(p.x + width, cy), col::border);
    drawText(dl, fonts.caption, ImVec2(tx, cy - ts.y * 0.5f), col::textDim, label);
}

void ChatApp::drawNewMessagesPill(Conversation& c) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 ws = ImGui::GetWindowSize();
    const char* label = "New messages";
    const ImVec2 ts = textSize(fonts.caption, label);
    const ImVec2 size(ts.x + px(46), px(32));
    const ImVec2 p0(wp.x + (ws.x - size.x) * 0.5f, wp.y + ws.y - size.y - px(14));

    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                         ImGui::IsMouseHoveringRect(p0, p0 + size);
    dl->AddRectFilled(p0 + ImVec2(0, px(3)), p0 + size + ImVec2(0, px(3)), col::rgba(0, 0, 0, 90), size.y * 0.5f);
    dl->AddRectFilled(p0, p0 + size, hovered ? col::accentHover : col::accent, size.y * 0.5f);
    drawText(dl, fonts.caption, ImVec2(p0.x + px(16), p0.y + (size.y - ts.y) * 0.5f), col::white, label);

    const ImVec2 a(p0.x + size.x - px(20), p0.y + size.y * 0.5f);
    dl->AddTriangleFilled(ImVec2(a.x - px(5), a.y - px(2.5f)), ImVec2(a.x + px(5), a.y - px(2.5f)),
                          ImVec2(a.x, a.y + px(3.5f)), col::white);

    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            c.forceScroll = true;
    }
}

void ChatApp::drawTypingLine(ImVec2 pos) {
    const std::vector<std::string> names = typingNames(active_);
    if (names.empty() || conn_ != Conn::Online)
        return;

    std::string text;
    if (names.size() == 1)
        text = names[0] + " is typing";
    else if (names.size() == 2)
        text = names[0] + " and " + names[1] + " are typing";
    else
        text = "Several people are typing";

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float t = static_cast<float>(ImGui::GetTime());
    const float cy = pos.y + fonts.caption->FontSize * 0.5f + px(2);
    for (int i = 0; i < 3; ++i) {
        const float wave = 0.5f + 0.5f * std::sin(t * 6.0f - static_cast<float>(i) * 0.9f);
        dl->AddCircleFilled(ImVec2(pos.x + px(4) + static_cast<float>(i) * px(9), cy - wave * px(2)), px(3),
                            withAlpha(col::textDim, 0.35f + 0.65f * wave));
    }
    drawText(dl, fonts.caption, ImVec2(pos.x + px(32), pos.y + px(2)), col::textDim, text);
}

void ChatApp::drawComposer(float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool partnerOffline = !active_.empty() && !isOnline(active_);
    const bool canType = conn_ == Conn::Online && !partnerOffline;

    std::string hint;
    if (conn_ != Conn::Online)
        hint = "You're offline";
    else if (partnerOffline)
        hint = active_ + " is offline. Messages can't be delivered right now";
    else
        hint = active_.empty() ? "Message #general" : "Message " + active_;

    // Disabling a widget does not deactivate it; drop focus so the caret stops blinking.
    if (!canType && composerActive_) {
        ImGui::SetWindowFocus(nullptr);
        composerActive_ = false;
    }

    const float h = px(52);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, p + ImVec2(width, h), col::elevated, px(14));
    dl->AddRect(p, p + ImVec2(width, h), composerActive_ ? withAlpha(col::accent, 0.8f) : col::border, px(14), 0,
                px(1.2f));

    const ImVec2 button(px(78), h - px(14));
    const float fieldW = width - button.x - px(14);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, col::rgba(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, col::rgba(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, col::rgba(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(16), (h - ImGui::GetFontSize()) * 0.5f));
    ImGui::SetCursorScreenPos(p);
    ImGui::SetNextItemWidth(fieldW);
    if (focusInput_ && canType) {
        ImGui::SetKeyboardFocusHere();
        focusInput_ = false;
    }
    ImGui::BeginDisabled(!canType);
    const bool enter = ImGui::InputTextWithHint("##composer", hint.c_str(), input_, sizeof input_,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    const bool edited = ImGui::IsItemEdited();
    composerActive_ = ImGui::IsItemActive();
    ImGui::EndDisabled();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    const std::string text = chat::sanitizeText(input_);
    ImGui::SetCursorScreenPos(ImVec2(p.x + width - button.x - px(7), p.y + px(7)));
    const bool clicked = accentButton("Send", button, canType && !text.empty());
    if ((enter || clicked) && canType && !text.empty())
        sendCurrent(text);
    if (enter || clicked)
        focusInput_ = true;  // Enter deactivates the field; keep the user typing
    updateTyping(edited);

    const std::size_t length = std::strlen(input_);
    if (length > chat::kMaxTextLength - 200) {
        const std::string counter = std::to_string(length) + " / " + std::to_string(chat::kMaxTextLength);
        const ImVec2 cs = textSize(fonts.caption, counter);
        drawText(dl, fonts.caption, ImVec2(p.x + width - cs.x - px(8), p.y + h + px(6)),
                 length >= chat::kMaxTextLength ? col::dangerText : col::textFaint, counter);
    }
}

void ChatApp::updateWindowTitle() {
    int total = 0;
    for (const auto& entry : convs_)
        total += entry.second.unread;
    if (total == shownUnread_)
        return;
    shownUnread_ = total;
    const std::wstring title =
        total > 0 ? L"(" + std::to_wstring(total) + L") elazharyLearn" : std::wstring(L"elazharyLearn");
    SetWindowTextW(hwnd_, title.c_str());
}
