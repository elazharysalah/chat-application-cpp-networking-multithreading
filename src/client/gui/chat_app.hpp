#pragma once

#include "chat_client.hpp"
#include "settings.hpp"

#include <imgui.h>
#include <windows.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

// All UI state and rendering. frame() is called once per rendered frame on the
// UI thread; network events are applied at the start of each frame.
class ChatApp {
public:
    explicit ChatApp(HWND hwnd);
    void frame();

private:
    enum class Screen { Login, Chat };
    enum class Conn { Idle, Connecting, Online, Offline };

    struct Line {
        enum class Kind { Text, System, Error };
        Kind kind = Kind::Text;
        std::string from;
        std::string text;
        uint64_t timestamp = 0;
        bool mine = false;
    };

    struct Conversation {
        std::vector<Line> lines;
        int unread = 0;
        std::string draft;
        uint64_t version = 0;          // bumped on every change; drives auto-scroll
        uint64_t renderedVersion = 0;
        float renderedWidth = 0.0f;
        bool stickToBottom = true;
        bool forceScroll = true;
        bool newBelow = false;
    };

    // Networking -> state
    void processEvents();
    void onMessage(const chat::Message& m);
    void onConnectionLost(const std::string& reason);
    void addLine(const std::string& key, Line line, bool countUnread);
    void notify();

    // Actions
    void startConnect();
    void reconnect();
    void logout();
    void switchTo(const std::string& key);
    void sendCurrent(const std::string& text);
    void updateTyping(bool edited);

    // Rendering
    void drawLogin();
    void drawLoginForm();
    void drawChat();
    void drawSidebar();
    void sidebarRow(const std::string& key);
    void drawMain();
    void drawHeader(ImVec2 pos, float width, float height);
    float drawConnectionBanner(ImVec2 pos, float width, float y);
    void drawIntro(float width);
    void drawMessages(Conversation& c);
    void drawSystemLine(const Line& line, float width);
    void drawDaySeparator(uint64_t timestamp, float width);
    void drawNewMessagesPill(Conversation& c);
    void drawTypingLine(ImVec2 pos);
    void drawComposer(float width);
    void updateWindowTitle();

    bool isOnline(const std::string& user) const;
    std::vector<std::string> directMessageList() const;
    std::vector<std::string> typingNames(const std::string& key) const;

    HWND hwnd_;
    chat::ChatClient client_;
    Settings settings_;

    Screen screen_ = Screen::Login;
    Conn conn_ = Conn::Idle;

    // Login form
    char nameBuf_[chat::kMaxNameLength + 1] = {};
    char hostBuf_[128] = {};
    char portBuf_[6] = {};
    std::string loginError_;
    bool focusLogin_ = true;
    float loginCardHeight_ = 0.0f;

    // Session
    std::string me_;
    std::string host_;
    uint16_t port_ = 0;
    std::string offlineReason_;
    std::vector<std::string> users_;              // everyone online, including me
    std::map<std::string, Conversation> convs_;   // "" = #general, otherwise the other user's name
    std::string active_;
    std::optional<std::string> pendingSwitch_;
    bool logoutRequested_ = false;

    // Composer
    char input_[chat::kMaxTextLength + 1] = {};
    bool focusInput_ = false;
    bool composerActive_ = false;

    // Typing indicators: conversation -> (user -> expiry time in ImGui::GetTime() seconds)
    std::map<std::string, std::map<std::string, double>> typing_;
    bool typingSent_ = false;
    double typingSentAt_ = 0.0;
    double lastEditAt_ = 0.0;
    std::string typingTarget_;

    int shownUnread_ = -1;
};
