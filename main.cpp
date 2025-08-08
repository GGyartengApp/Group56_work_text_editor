#include <wx/wx.h>
#include <wx/stc/stc.h>
#include <wx/aui/aui.h>
#include <wx/listctrl.h>
#include <wx/artprov.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <vector>
#include <thread>
#include <fstream>
#include <curl/curl.h>
#include <regex>
#include <set>
#include <wx/url.h>
#include <wx/sstream.h>
#include <wx/wfstream.h>
#include <filesystem>
#include <nlohmann/json.hpp> // Include JSON library (needs nlohmann_json)


class MyEditor : public wxStyledTextCtrl {
public:
    MyEditor(wxWindow* parent) : wxStyledTextCtrl(parent, wxID_ANY) {
        SetLexer(wxSTC_LEX_CPP);

/// Default text style: Menlo, regular
        wxFont editorFont(14, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Menlo");
        StyleSetFont(wxSTC_STYLE_DEFAULT, editorFont);
        StyleSetSize(wxSTC_STYLE_DEFAULT, 14);
        StyleClearAll(); // propagate to all styles

// Enable line numbers in margin 0
        SetMarginType(0, wxSTC_MARGIN_NUMBER);
        SetMarginWidth(0, 40);
        StyleSetBackground(wxSTC_STYLE_LINENUMBER, wxColour(245, 245, 245));
        StyleSetForeground(wxSTC_STYLE_LINENUMBER, *wxBLACK);

        // Keyword sets coloring and bold (Xcode style)
        // Xcode Blue: 21, 101, 192
        // Xcode Pink: 255, 45, 85
        StyleSetForeground(wxSTC_C_WORD, wxColour(21, 101, 192));       // Set 1: Xcode blue
        StyleSetForeground(wxSTC_C_WORD2, wxColour(255, 45, 85));       // Set 2: Xcode pink
        StyleSetForeground(wxSTC_C_GLOBALCLASS, wxColour(21, 101, 192));// Set 3: Xcode blue

        // Strings and characters in yellowish-orange (like Xcode)
        StyleSetForeground(wxSTC_C_STRING, wxColour(255, 200, 64));     // double-quoted string
        StyleSetForeground(wxSTC_C_CHARACTER, wxColour(255, 200, 64));  // single-quoted char

        // Numbers in light blue with light font
        StyleSetForeground(wxSTC_C_NUMBER, wxColour(173, 216, 230));  // light blue
        StyleSetWeight(wxSTC_C_NUMBER, wxFONTWEIGHT_LIGHT);           // lighter font weight

        // Comments in faded gray
        StyleSetForeground(wxSTC_C_COMMENT, wxColour(150, 150, 150));     // faded gray
        StyleSetForeground(wxSTC_C_COMMENTLINE, wxColour(150, 150, 150)); // faded gray
        StyleSetForeground(wxSTC_C_COMMENTDOC, wxColour(150, 150, 150));  // faded gray

        // Bold only for keywords
        StyleSetBold(wxSTC_C_WORD, true);
        StyleSetBold(wxSTC_C_WORD2, true);
        StyleSetBold(wxSTC_C_GLOBALCLASS, true);

        SetKeyWords(0, "class struct if else for while return switch case break continue void using");
        SetKeyWords(1, "public private protected virtual override const int bool float double string include namespace");


        // Indicator 0 for variables
        IndicatorSetStyle(0, wxSTC_INDIC_ROUNDBOX);
        IndicatorSetForeground(0, wxColour(255, 255, 128)); // light yellow highlight
        IndicatorSetAlpha(0, 80); // semi-transparent

        // Indicator 1 for functions
        IndicatorSetStyle(1, wxSTC_INDIC_ROUNDBOX);
        IndicatorSetForeground(1, wxColour(173, 216, 230)); // light blue highlight
        IndicatorSetAlpha(1, 80); // semi-transparent

        // Indicator 4 for errors (red squiggly underline)
        IndicatorSetStyle(4, wxSTC_INDIC_SQUIGGLE);
        IndicatorSetForeground(4, wxColour(255, 0, 0));  // Red underline
        IndicatorSetAlpha(4, 255);                       // Fully opaque

        // Real-time syntax highlighting + variable and error highlighting
        Bind(wxEVT_STC_CHANGE, [this](wxStyledTextEvent&) {
            Colourise(0, GetTextLength());
            HighlightVariables(); // Highlight variables dynamically
            HighlightErrors();    // Underline basic errors dynamically
        });

        // Enable automatic caret and line updates
        SetCaretForeground(*wxWHITE);
        SetUseHorizontalScrollBar(true);
        SetViewEOL(false);
        SetViewWhiteSpace(wxSTC_WS_INVISIBLE);
        SetWrapMode(wxSTC_WRAP_NONE);
        // Highlight current line

        // --- Auto indentation and brace completion ---
        SetTabWidth(4);
        SetUseTabs(false);
        SetIndent(4);
        SetBackSpaceUnIndents(true);  // backspace will unindent
        SetIndentationGuides(wxSTC_IV_LOOKBOTH);

        // Highlight matching braces
        Bind(wxEVT_STC_UPDATEUI, [this](wxStyledTextEvent&) {
            int caretPos = GetCurrentPos();
            char ch = GetCharAt(caretPos - 1);

            int braceAtCaret = -1;
            if (ch == '(' || ch == ')' || ch == '{' || ch == '}' ||
                ch == '[' || ch == ']') {
                braceAtCaret = caretPos - 1;
            }

            int braceOpposite = BraceMatch(braceAtCaret);
            if (braceAtCaret != -1 && braceOpposite != -1) {
                BraceHighlight(braceAtCaret, braceOpposite);
            } else {
                BraceBadLight(braceAtCaret);
            }
        });

        // --- Smart braces, auto completion and indentation ---
        Bind(wxEVT_CHAR, [this](wxKeyEvent& event) {
            int keyCode = event.GetKeyCode();

            if (isRecordingMacro) {
                int keyCode = event.GetKeyCode();
                if (keyCode >= 32 && keyCode <= 126) { // Printable ASCII
                    macroBuffer.push_back(std::string(1, static_cast<char>(keyCode)));
                }
            }

            // Handle Enter key for smart indentation
            if (keyCode == WXK_RETURN || keyCode == WXK_NUMPAD_ENTER) {
                int curLine = GetCurrentLine();
                int indent = 0;

                if (curLine >= 0)
                    indent = GetLineIndentation(curLine);

                // Get current caret position
                int pos = GetCurrentPos();

                // If last char is '{', insert a new indented line (double-indent) and closing brace aligned
                if (pos > 0 && GetCharAt(pos - 1) == '{') {
                    int innerIndent = indent + GetIndent();        // one level deeper than current line
                    int doubleIndent = innerIndent + GetIndent();  // components inside braces get an extra indent

                    // Insert newline with double indent for inner content
                    AddText("\n" + std::string(doubleIndent, ' '));

                    // Insert newline for closing brace aligned to the opening line's indent
                    AddText("\n" + std::string(indent, ' '));

                    // Move cursor to the double-indented line
                    GotoPos(pos + doubleIndent + 1);
                    return; // Skip default behavior
                }

                // Default indentation copies previous line
                AddText("\n" + std::string(indent, ' '));
                return;
            }

            // Handle auto-closing braces and quotes
            if (keyCode == '(') { AddText("()"); CharLeft(); }
            else if (keyCode == '{') { AddText("{}"); CharLeft(); }
            else if (keyCode == '[') { AddText("[]"); CharLeft(); }
            else if (keyCode == '\"') { AddText("\"\""); CharLeft(); }
            else if (keyCode == '\'') { AddText("''"); CharLeft(); }
            else {
                event.Skip();
                return;
            }

            event.Skip(false); // Prevent duplicate char
        });

        // --- Toggle line comment on Cmd/Ctrl + '/' ---
        Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
            bool isCmd = event.CmdDown();       // Cmd on macOS or Ctrl on Windows/Linux
            int keyCode = event.GetKeyCode();

            if (isCmd && keyCode == '/') {
                long start, end;
                GetSelection(&start, &end);

                // If nothing selected, comment the current line
                if (start == end) {
                    int line = GetCurrentLine();
                    std::string lineText = GetLine(line).ToStdString();

                    if (lineText.rfind("//", 0) == 0) {
                        // Uncomment
                        SetTargetStart(PositionFromLine(line));
                        SetTargetEnd(PositionFromLine(line) + 2);
                        ReplaceTarget("");
                    } else {
                        // Comment
                        InsertText(PositionFromLine(line), "//");
                    }
                } else {
                    // Multiple line selection: comment/uncomment each line
                    int startLine = LineFromPosition(start);
                    int endLine = LineFromPosition(end);

                    for (int line = startLine; line <= endLine; ++line) {
                        std::string lineText = GetLine(line).ToStdString();
                        if (lineText.rfind("//", 0) == 0) {
                            // Uncomment
                            SetTargetStart(PositionFromLine(line));
                            SetTargetEnd(PositionFromLine(line) + 2);
                            ReplaceTarget("");
                        } else {
                            // Comment
                            InsertText(PositionFromLine(line), "//");
                        }
                    }
                }

                return; // Consume event
            }

            event.Skip();
        });

        // --- Ensure caret stays at end of line when selecting a whole line ---
        Bind(wxEVT_STC_UPDATEUI, [this](wxStyledTextEvent&) {
            long start, end;
            GetSelection(&start, &end);

            // If selection spans exactly one line from start to end-of-line
            if (LineFromPosition(start) == LineFromPosition(end) && start != end) {
                int line = LineFromPosition(start);
                int lineStart = PositionFromLine(line);
                int lineEnd = GetLineEndPosition(line);

                if (start == lineStart && end == lineEnd) {
                    // Move caret to end of the line
                    GotoPos(lineEnd);
                }
            }
        });
    }

    bool isRecordingMacro = false;
    std::vector<std::string> macroBuffer;

    void StartMacroRecording() {
        macroBuffer.clear();
        isRecordingMacro = true;
    }

    void StopMacroRecording() {
        isRecordingMacro = false;
    }

    void PlayMacro() {
        if (!macroBuffer.empty()) {
            for (auto& key : macroBuffer) {
                AddText(key);
            }
        }
    }
    void SetFilename(const wxString& filename) { m_filename = filename; }
    wxString GetFilename() const { return m_filename; }


    std::set<std::string> FetchVariables() {
        std::set<std::string> variables;
        std::string text = GetText().ToStdString();

        // Regex to match variable declarations with optional initialization and comma separation
        std::regex varDeclRegex(
                R"(\b(bool|int|float|double|string)\b\s+([a-zA-Z_][a-zA-Z0-9_]*)(\s*=\s*[^,;]+)?(\s*,\s*[a-zA-Z_][a-zA-Z0-9_]*(\s*=\s*[^,;]+)?)*\s*;)"
        );

        std::smatch match;
        auto it = text.cbegin();

        while (std::regex_search(it, text.cend(), match, varDeclRegex)) {
            std::string fullDecl = match.str();

            // Extract each variable name from the declaration
            std::regex nameRegex(R"([a-zA-Z_][a-zA-Z0-9_]*)");
            std::sregex_iterator nameIt(fullDecl.begin(), fullDecl.end(), nameRegex);
            std::sregex_iterator endIt;

            bool skipType = true; // Skip the first match (type)
            for (; nameIt != endIt; ++nameIt) {
                std::string varName = nameIt->str();

                if (skipType) {
                    skipType = false; // Skip the type name
                    continue;
                }

                variables.insert(varName); // Add variable name
            }

            it = match.suffix().first;
        }

        return variables;
    }

    void HighlightVariables() {
        // Fetch variable names
        auto variables = FetchVariables();
        std::string text = GetText().ToStdString();

        // Clear old highlights
        SetIndicatorCurrent(0);
        IndicatorClearRange(0, GetTextLength());
        SetIndicatorCurrent(1);
        IndicatorClearRange(0, GetTextLength());

        // Highlight variables (Indicator 0)
        SetIndicatorCurrent(0);
        for (const auto& var : variables) {
            size_t pos = text.find(var);
            while (pos != std::string::npos) {
                IndicatorFillRange(pos, var.size());
                pos = text.find(var, pos + var.size());
            }
        }

        // Highlight function names (Indicator 1)
        std::regex funcRegex(R"(\b([a-zA-Z_][a-zA-Z0-9_]*)\s*\()");
        std::smatch match;
        auto it = text.cbegin();

        while (std::regex_search(it, text.cend(), match, funcRegex)) {
            if (match.size() > 1) {
                std::string funcName = match[1];
                size_t pos = match.position(1) + std::distance(text.cbegin(), it);

                SetIndicatorCurrent(1);
                IndicatorFillRange(pos, funcName.size());
            }
            it = match.suffix().first;
        }
    }


private:
    wxString m_filename;

    void HighlightErrors() {
        // Clear previous error highlights
        SetIndicatorCurrent(4);
        IndicatorClearRange(0, GetTextLength());

        std::string text = GetText().ToStdString();

        // 1. Missing semicolon check (simple heuristic)
        std::regex missingSemicolon(R"(\b(return|int|float|double|bool|string)\b[^;{}\n]*\n)");
        for (auto it = std::sregex_iterator(text.begin(), text.end(), missingSemicolon);
             it != std::sregex_iterator(); ++it) {
            size_t pos = it->position();
            size_t len = it->length();
            SetIndicatorCurrent(4);
            IndicatorFillRange(pos, len);
        }

        // 2. Mismatched quote detection
        size_t quoteCount = 0;
        for (size_t i = 0; i < text.size(); i++) {
            if (text[i] == '"' && (i == 0 || text[i-1] != '\\')) quoteCount++;
        }
        if (quoteCount % 2 != 0) {
            // Highlight last unmatched quote
            size_t pos = text.find_last_of('"');
            SetIndicatorCurrent(4);
            IndicatorFillRange(pos, 1);
        }

        // 3. Detect 'cout >>' misuse (should be <<)
        std::regex coutMisuse(R"(\bcout\s*>>)");
        for (auto it = std::sregex_iterator(text.begin(), text.end(), coutMisuse);
             it != std::sregex_iterator(); ++it) {
            size_t pos = it->position();
            size_t len = it->length();
            SetIndicatorCurrent(4);
            IndicatorFillRange(pos, len);
        }

        // 4. Detect 'return 0' without semicolon
        std::regex returnNoSemi(R"(\breturn\s+0\s*\n)");
        for (auto it = std::sregex_iterator(text.begin(), text.end(), returnNoSemi);
             it != std::sregex_iterator(); ++it) {
            size_t pos = it->position();
            size_t len = it->length();
            SetIndicatorCurrent(4);
            IndicatorFillRange(pos, len);
        }

        // 5. Highlight 'std::' usage if 'using namespace std;' is missing
        bool hasNamespaceStd = (text.find("using namespace std;") != std::string::npos);
        if (!hasNamespaceStd) {
            std::regex stdUsage(R"(\bstd::)");
            for (auto it = std::sregex_iterator(text.begin(), text.end(), stdUsage);
                 it != std::sregex_iterator(); ++it) {
                size_t pos = it->position();
                size_t len = it->length();
                SetIndicatorCurrent(4);
                IndicatorFillRange(pos, len);
            }
        }
    }
};

class MyFrame : public wxFrame
{
public:
    MyFrame() : wxFrame(nullptr, wxID_ANY, "Group56 Project - Text Editor", wxDefaultPosition, wxSize(900, 600))
    {
        // --- Menu Bar ---
        wxMenu *fileMenu = new wxMenu;
        fileMenu->Append(wxID_NEW, "&New\tCtrl+N");
        fileMenu->Append(wxID_OPEN, "&Open\tCtrl+O");
        fileMenu->Append(wxID_SAVE, "&Save\tCtrl+S");
        fileMenu->Append(wxID_SAVEAS, "Save &As...\tCtrl+Shift+S");
        fileMenu->AppendSeparator();
        fileMenu->Append(wxID_EXIT, "E&xit\tCtrl+Q");

        wxMenuBar *menuBar = new wxMenuBar;
        menuBar->Append(fileMenu, "&File");

        // --- Edit Menu ---
        wxMenu *editMenu = new wxMenu;
        editMenu->Append(wxID_FIND, "&Find\tCtrl+F");
        editMenu->Append(wxID_REPLACE, "&Replace\tCtrl+H");
        menuBar->Append(editMenu, "&Edit");

        editMenu->AppendSeparator();
        int idStartMacro = wxWindow::NewControlId();
        int idStopMacro = wxWindow::NewControlId();
        int idPlayMacro = wxWindow::NewControlId();

        editMenu->Append(idStartMacro, "Start Macro Recording");
        editMenu->Append(idStopMacro, "Stop Macro Recording");
        editMenu->Append(idPlayMacro, "Play Macro");

        Bind(wxEVT_MENU, [this](wxCommandEvent&) {
            auto* editor = GetCurrentEditor();
            if (editor) editor->StartMacroRecording();
        }, idStartMacro);

        Bind(wxEVT_MENU, [this](wxCommandEvent&) {
            auto* editor = GetCurrentEditor();
            if (editor) editor->StopMacroRecording();
        }, idStopMacro);

        Bind(wxEVT_MENU, [this](wxCommandEvent&) {
            auto* editor = GetCurrentEditor();
            if (editor) editor->PlayMacro();
        }, idPlayMacro);


        // --- Plugins Menu ---
        wxMenu* pluginMenu = new wxMenu;
        int idMarketplace = wxWindow::NewControlId();
        pluginMenu->Append(idMarketplace, "&Marketplace\tCtrl+M");
        menuBar->Append(pluginMenu, "&Plugins");
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenMarketplace(); }, idMarketplace);

        SetMenuBar(menuBar);

        // --- Toolbar ---
        wxToolBar* toolbar = CreateToolBar(wxTB_HORIZONTAL | wxNO_BORDER | wxTB_FLAT);
        toolbar->SetToolBitmapSize(wxSize(24, 24));
        toolbar->AddTool(wxID_NEW, "New", wxArtProvider::GetBitmap(wxART_NEW, wxART_TOOLBAR));
        toolbar->AddTool(wxID_OPEN, "Open", wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_TOOLBAR));
        toolbar->AddTool(wxID_SAVE, "Save", wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_TOOLBAR));
        toolbar->Realize();

        // --- Notebook for Multi-Buffer Tabs ---
        notebook = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_NB_TOP | wxAUI_NB_TAB_MOVE | wxAUI_NB_CLOSE_ON_ALL_TABS);

        // --- Event Bindings ---
        Bind(wxEVT_MENU, &MyFrame::OnNew, this, wxID_NEW);
        Bind(wxEVT_MENU, &MyFrame::OnOpen, this, wxID_OPEN);
        Bind(wxEVT_MENU, &MyFrame::OnSave, this, wxID_SAVE);
        Bind(wxEVT_MENU, &MyFrame::OnSaveAs, this, wxID_SAVEAS);
        Bind(wxEVT_MENU, &MyFrame::OnExit, this, wxID_EXIT);

        Bind(wxEVT_MENU, &MyFrame::OnFind, this, wxID_FIND);
        Bind(wxEVT_MENU, &MyFrame::OnReplace, this, wxID_REPLACE);

        Bind(wxEVT_THREAD, [=](wxThreadEvent& e) {
            std::string msg = e.GetString().ToStdString();
            if (msg == "DOWNLOAD_FAILED") {
                wxMessageBox("Failed to download plugin.", "Error", wxOK | wxICON_ERROR);
            } else if (msg.rfind("DOWNLOAD_SUCCESS:", 0) == 0) {
                std::string path = msg.substr(strlen("DOWNLOAD_SUCCESS:"));
                wxMessageBox("Plugin installed to " + path, "Install Complete", wxOK | wxICON_INFORMATION);
            }
        });

    }

private:
    wxAuiNotebook* notebook;

    MyEditor* GetCurrentEditor()
    {
        int sel = notebook->GetSelection();
        if (sel == wxNOT_FOUND) return nullptr;
        return dynamic_cast<MyEditor*>(notebook->GetPage(sel));
    }

    // --- Plugin Marketplace Feature ---
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
        output->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    static std::string fetchUrl(const std::string& url) {
        CURL* curl = curl_easy_init();
        std::string response;
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L); // verify SSL
            curl_easy_perform(curl);
            curl_easy_cleanup(curl);
        }
        return response;
    }

    void OpenMarketplace() {
        wxDialog dlg(this, wxID_ANY, "Plugin Marketplace", wxDefaultPosition, wxSize(500, 400));
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        wxListCtrl* listCtrl = new wxListCtrl(&dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                              wxLC_REPORT | wxLC_SINGLE_SEL);
        listCtrl->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 120);
        listCtrl->InsertColumn(1, "Version", wxLIST_FORMAT_LEFT, 70);
        listCtrl->InsertColumn(2, "Description", wxLIST_FORMAT_LEFT, 280);

        sizer->Add(listCtrl, 1, wxEXPAND | wxALL, 5);

        wxButton* installBtn = new wxButton(&dlg, wxID_OK, "Install Selected Plugin");
        sizer->Add(installBtn, 0, wxALIGN_CENTER | wxALL, 5);
        dlg.SetSizerAndFit(sizer);

        // Always use curl fetch
        wxString urlStr = "https://raw.githubusercontent.com/GGyartengApp/editor-plugin-marketplace/main/plugins.json";
        std::string jsonData = fetchUrl(urlStr.ToStdString());

        // Debug output
        wxMessageBox(jsonData.empty() ? "No data fetched" : jsonData, "Fetched JSON Data");

        if (jsonData.empty()) {
            wxMessageBox("Failed to fetch plugin marketplace data.", "Error", wxOK | wxICON_ERROR);
            dlg.ShowModal();
            return;
        }

        nlohmann::json plugins;
        try {
            plugins = nlohmann::json::parse(jsonData);
        } catch (...) {
            wxMessageBox("Failed to parse plugin marketplace JSON.", "Error", wxOK | wxICON_ERROR);
            dlg.ShowModal();
            return;
        }

        std::vector<nlohmann::json> pluginsVec;
        int index = 0;
        for (auto& plugin : plugins) {
            wxString name = plugin["name"].get<std::string>();
            wxString version = plugin["version"].get<std::string>();
            wxString description = plugin["description"].get<std::string>();
            listCtrl->InsertItem(index, name);
            listCtrl->SetItem(index, 1, version);
            listCtrl->SetItem(index, 2, description);
            pluginsVec.push_back(plugin);
            index++;
        }

        installBtn->Bind(wxEVT_BUTTON, [&, listCtrl](wxCommandEvent&) {
            long selected = listCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
            if (selected == -1) {
                wxMessageBox("Please select a plugin to install.", "No Selection", wxOK | wxICON_INFORMATION);
                return;
            }

            std::string pluginName = listCtrl->GetItemText(selected).ToStdString();
            std::string downloadUrl = pluginsVec[selected]["download_url"].get<std::string>();

            // Run download in background thread
            std::thread([downloadUrl, pluginName, &dlg]() {
                std::string pluginData = fetchUrl(downloadUrl);

                if (pluginData.empty()) {
                    auto evt = new wxThreadEvent(wxEVT_THREAD, wxID_ANY);
                    evt->SetString("DOWNLOAD_FAILED");
                    wxQueueEvent(wxTheApp->GetTopWindow()->GetEventHandler(), evt);
                    return;
                }

                std::filesystem::create_directory("plugins");
                std::string localPath = "plugins/" + pluginName + ".dll";
                std::ofstream out(localPath, std::ios::binary);
                out.write(pluginData.data(), pluginData.size());
                out.close();

                auto evt = new wxThreadEvent(wxEVT_THREAD, wxID_ANY);
                evt->SetString("DOWNLOAD_SUCCESS:" + localPath);

                // Queue event to main thread
                wxQueueEvent(wxTheApp->GetTopWindow()->GetEventHandler(), evt);

                // Close dialog on UI thread
                wxTheApp->CallAfter([&dlg]() {
                    dlg.EndModal(wxID_OK);
                });
            }).detach();
        });

        dlg.ShowModal();
    }

    void OnNew(wxCommandEvent&)
    {
        auto* editor = new MyEditor(notebook);
        notebook->AddPage(editor, "Untitled", true);

    }

    void OnOpen(wxCommandEvent&)
    {
        wxFileDialog openFileDialog(this, "Open file", "", "", "Text files (*.txt)|*.txt|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

        if (openFileDialog.ShowModal() == wxID_CANCEL)
            return;

        wxString path = openFileDialog.GetPath();
        auto* editor = new MyEditor(notebook);
        editor->LoadFile(path);
        editor->SetFilename(path);
        notebook->AddPage(editor, path.AfterLast('/'), true);
        editor->SetModified(false);
    }

    void OnSave(wxCommandEvent&)
    {
        auto* editor = GetCurrentEditor();
        if (!editor) return;

        wxString path = editor->GetFilename();
        if (path.IsEmpty() || !wxFileExists(path)) {
            wxCommandEvent evt;
            OnSaveAs(evt);
        } else {
            editor->SaveFile(path);
            editor->SetModified(false);
        }
    }

    void OnSaveAs(wxCommandEvent&)
    {
        auto* editor = GetCurrentEditor();
        if (!editor) return;

        wxFileDialog saveFileDialog(this, "Save file", "", "", "Text files (*.txt)|*.txt|All files (*.*)|*.*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

        if (saveFileDialog.ShowModal() == wxID_CANCEL)
            return;

        wxString path = saveFileDialog.GetPath();
        editor->SaveFile(path);
        editor->SetFilename(path);
        notebook->SetPageText(notebook->GetSelection(), path.AfterLast('/'));
        editor->SetModified(false);
    }

    void OnExit(wxCommandEvent&)
    {
        Close(true);
    }

    void OnFind(wxCommandEvent&) {
        auto* editor = GetCurrentEditor();
        if (!editor) return;

        wxTextEntryDialog dlg(this, "Enter text to find:", "Find");
        if (dlg.ShowModal() != wxID_OK) return;

        wxString query = dlg.GetValue();
        if (query.IsEmpty()) return;

        // Clear any previous indicator 3 highlights
        editor->SetIndicatorCurrent(3);
        editor->IndicatorClearRange(0, editor->GetTextLength());

        // Configure indicator 3 for find highlights
        editor->IndicatorSetStyle(3, wxSTC_INDIC_ROUNDBOX);
        editor->IndicatorSetForeground(3, wxColour(255, 255, 0)); // Yellow highlight
        editor->IndicatorSetAlpha(3, 80);

        std::string text = editor->GetText().ToStdString();
        std::string search = query.ToStdString();

        size_t pos = text.find(search, 0);
        bool foundAny = false;

        while (pos != std::string::npos) {
            editor->SetIndicatorCurrent(3);
            editor->IndicatorFillRange(pos, search.size());
            foundAny = true;
            pos = text.find(search, pos + search.size());
        }

        if (!foundAny) {
            wxMessageBox("Text not found.", "Find", wxOK | wxICON_INFORMATION);
        }
    }

    void OnReplace(wxCommandEvent&) {
        auto* editor = GetCurrentEditor();
        if (!editor) return;

        wxTextEntryDialog findDlg(this, "Enter text to find:", "Replace - Step 1");
        if (findDlg.ShowModal() != wxID_OK) return;

        wxString findText = findDlg.GetValue();
        if (findText.IsEmpty()) return;

        wxTextEntryDialog replaceDlg(this, "Enter replacement text:", "Replace - Step 2");
        if (replaceDlg.ShowModal() != wxID_OK) return;

        wxString replaceText = replaceDlg.GetValue();

        int count = 0;
        long pos = 0;
        while (true) {
            pos = editor->FindText(pos, editor->GetTextLength(), findText);
            if (pos == -1) break;
            editor->SetTargetStart(pos);
            editor->SetTargetEnd(pos + findText.length());
            editor->ReplaceTarget(replaceText);
            pos += replaceText.length();
            count++;
        }

        wxMessageBox(wxString::Format("Replaced %d occurrences.", count), "Replace", wxOK | wxICON_INFORMATION);
    }


};

class MyApp : public wxApp
{
public:
    bool OnInit() override
    {
        MyFrame* frame = new MyFrame();
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(MyApp);