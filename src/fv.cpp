// An experimental file viewer using FTXUI.
//
// - it goes to a line when Ctrl+G is pressed then a line number is entered (Q. how to handle filtering?)
// - it quits when [Esc] is pressed
// - it supports multiple files
// - it supports highlighting strings with the mouse and assigning different colours to them

#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"

#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    int FindPreviousMatchingLine(const std::vector<std::string>& lines, int current, const std::string& pattern)
    {
        // If there is no previous line...
        if (current < 1)
        {
            return -1;
        }

        // Search backwards from the line before the current line.
        if (const auto it =
                    std::find_if(lines.crbegin() + ((lines.size() - 1) - (current - 1)), lines.crend(),
                                 [&pattern](const auto& line) { return line.find(pattern) != std::string::npos; });
            it != lines.crend())
        {
            return static_cast<int>(std::distance(it, lines.crend())) - 1;
        }

        return -1;
    }

    int FindNextMatchingLine(const std::vector<std::string>& lines, int current, const std::string& pattern)
    {
        // If there is no next line...
        if (current >= lines.size() - 1)
        {
            return -1;
        }

        // Search forwards from the line after the current line.
        if (const auto it =
                    std::find_if(lines.cbegin() + (current + 1), lines.cend(),
                                 [&pattern](const auto& line) { return line.find(pattern) != std::string::npos; });
            it != lines.cend())
        {
            return static_cast<int>(std::distance(lines.cbegin(), it));
        }

        return -1;
    }

    int LocatePreviousMatch(const std::vector<std::string>& lines, int current, const std::string& pattern)
    {
        if (!pattern.empty())
        {
            int line = FindPreviousMatchingLine(lines, current, pattern);
            if (line == -1)
            {
                // Wrap and start the search to include line size-1.
                line = FindPreviousMatchingLine(lines, static_cast<int>(lines.size()), pattern);
            }
            if (line != -1)
            {
                current = line;
            }
        }
        return current;
    }

    int LocateNextMatch(const std::vector<std::string>& lines, int current, const std::string& pattern)
    {
        if (!pattern.empty())
        {
            int line = FindNextMatchingLine(lines, current, pattern);
            if (line == -1)
            {
                // Wrap and start the search to include line zero.
                line = FindNextMatchingLine(lines, -1, pattern);
            }
            if (line != -1)
            {
                current = line;
            }
        }
        return current;
    }
} // namespace

/**
 * @brief Represents the lines of the document.
 */
class Document
{
private:
    std::vector<std::string> lines_;

public:
    Document(const std::string& filename);

    int FindPreviousMatchingLine(int current, const std::string& pattern) const;
    int FindNextMatchingLine(int current, const std::string& pattern) const;
    int LocatePreviousMatch(int current, const std::string& pattern) const;
    int LocateNextMatch(int current, const std::string& pattern) const;

    int Size() const;
    const std::string& operator[](int line) const;

    using Iterator = decltype(lines_)::iterator;

    Iterator begin()
    {
        return lines_.begin();
    }

    Iterator end()
    {
        return lines_.end();
    }
};

inline Document::Document(const std::string& filename)
{
    std::vector<std::string> lines;
    std::ifstream file(filename);
    for (std::string line; std::getline(file, line);)
    {
        lines_.push_back(std::move(line));
    }
}

inline int Document::FindPreviousMatchingLine(int current, const std::string& pattern) const
{
    return ::FindPreviousMatchingLine(lines_, current, pattern);
}

inline int Document::FindNextMatchingLine(int current, const std::string& pattern) const
{
    return ::FindNextMatchingLine(lines_, current, pattern);
}

inline int Document::LocatePreviousMatch(int current, const std::string& pattern) const
{
    return ::LocatePreviousMatch(lines_, current, pattern);
}

inline int Document::LocateNextMatch(int current, const std::string& pattern) const
{
    return ::LocateNextMatch(lines_, current, pattern);
}

inline int Document::Size() const
{
    return static_cast<int>(lines_.size());
}

const std::string& Document::operator[](int line) const
{
    return lines_[line];
}

using namespace ftxui;

namespace keyevents
{
    const auto Home = Event::Special({27, 91, 72});
    const auto End = Event::Special({27, 91, 70});
    const auto PgUp = Event::Special({27, 91, 53, 126});
    const auto PgDown = Event::Special({27, 91, 54, 126});

    const auto CtrlHome = Event::Special({27, 91, 49, 59, 53, 72});
    const auto CtrlEnd = Event::Special({27, 91, 49, 59, 53, 70});
    const auto CtrlPgUp = Event::Special({27, 91, 53, 59, 53, 126});
    const auto CtrlPgDown = Event::Special({27, 91, 54, 59, 53, 126});

    const auto CtrlL = Event::Special({12});
    const auto CtrlT = Event::Special({20});
} // namespace keyevents

class FileViewer : public ComponentBase
{
    const int lineNumberMargin_ = 8;
    const int xFactor_ = 2;
    const int yFactor_ = 4;

    int topLine_ = 0;
    int leftEdge_ = 0;
    int matchingLine_ = -1;

    std::string pattern_ = "";
    bool isCapturing_ = false;
    bool showLineNumbers_ = true;
    bool isFiltering_ = false;

    std::shared_ptr<Document> doc_;

    void UpdateMatchFromPattern()
    {
        matchingLine_ = doc_->FindNextMatchingLine(topLine_, pattern_);
    }

    void CmdStartCapture()
    {
        isCapturing_ = true;
        pattern_.clear();
    }

    void CmdEndCapture()
    {
        if (isCapturing_)
        {
            isCapturing_ = false;
            topLine_ = matchingLine_;
        }
    }

    void CmdCancelCapture()
    {
        if (isCapturing_)
        {
            isCapturing_ = false;
            pattern_.clear();
            matchingLine_ = -1;
        }
    }

    void CmdBackspaceCapture()
    {
        if (isCapturing_)
        {
            if (pattern_.size() != 0)
            {
                pattern_.pop_back();
                UpdateMatchFromPattern();
            }
        }
    }

    void CmdPreviousMatch()
    {
        topLine_ = doc_->LocatePreviousMatch(topLine_, pattern_);
    }

    void CmdNextMatch()
    {
        topLine_ = doc_->LocateNextMatch(topLine_, pattern_);
    }

    void CmdPreviousLine()
    {
        topLine_ = topLine_ - 1;
    }

    void CmdNextLine()
    {
        topLine_ = topLine_ + 1;
    }

    void CmdPreviousColumn()
    {
        leftEdge_ = leftEdge_ - 1;
    }

    void CmdNextColumn()
    {
        leftEdge_ = leftEdge_ + 1;
    }

    void CmdStartOfDocument()
    {
        topLine_ = 0;
    }

    void CmdEndOfDocument()
    {
        topLine_ = doc_->Size() - (ViewHeight() - 1);
    }

    void CmdPreviousPage()
    {
        topLine_ -= ViewHeight() - 1;
    }

    void CmdNextPage()
    {
        topLine_ += ViewHeight() - 1;
    }

    void CmdPreviousFilteredPage()
    {
        int hits = 0;
        int line = topLine_;
        const int viewHeight = ViewHeight();
        while (line != -1 && line >= 0 && hits < viewHeight - 1)
        {
            line = doc_->FindPreviousMatchingLine(line - 1, pattern_);
            if (line != -1)
            {
                ++hits;
            }
        }
        topLine_ = (hits == (viewHeight - 1)) ? line : 0;
    }

    void CmdNextFilteredPage()
    {
        int hits = 0;
        int line = topLine_;
        const int viewHeight = ViewHeight();
        while (line != -1 && line < doc_->Size() && hits < (viewHeight - 1))
        {
            line = doc_->FindNextMatchingLine(line + 1, pattern_);
            if (line != -1)
            {
                ++hits;
            }
        }
        if (hits == (viewHeight - 1))
        {
            topLine_ = line;
        }
    }

    void CmdLeftmostColumn()
    {
        leftEdge_ = 0;
    }

    void CmdRightmostColumn()
    {
        int length = 0;
        int numLines = ViewHeight();
        for (int line = topLine_; numLines > 0 && line < doc_->Size(); ++line)
        {
            if (!isFiltering_ || !pattern_.empty() && (*doc_)[line].find(pattern_) != std::string::npos)
            {
                length = std::max(length, static_cast<int>((*doc_)[line].size()));
                numLines--;
            }
        }
        const int margin = showLineNumbers_ ? lineNumberMargin_ : 0;
        if (length > (ViewWidth() - margin))
        {
            leftEdge_ = length - ViewWidth() + margin;
        }
        else
        {
            leftEdge_ = 0;
        }
    }

    void CmdToggleLineNumbers()
    {
        showLineNumbers_ = !showLineNumbers_;
    }

    void CmdToggleFiltering()
    {
        isFiltering_ = !isFiltering_;
    }

public:
    FileViewer(std::shared_ptr<Document> doc) : doc_{doc}
    {
    }

    // Note: this returns the canvas size in "pixels".
    ftxui::Dimensions CanvasSize() const
    {
        const auto [x, y] = Terminal::Size();
        return {x * xFactor_, y * yFactor_};
    }

    int ViewWidth()
    {
        return Terminal::Size().dimx;
    }

    int ViewHeight()
    {
        return Terminal::Size().dimy;
    }

    Element Render() override
    {
        const int marginWidth = (showLineNumbers_ ? lineNumberMargin_ : 0) * xFactor_;
        const auto [canvasWidth, canvasHeight] = CanvasSize();
        auto lineNumberArea = Canvas(marginWidth, canvasHeight - yFactor_);
        auto contentArea = Canvas(canvasWidth - marginWidth, canvasHeight - yFactor_);
        auto statusArea = Canvas(canvasWidth, yFactor_);

        const int contentHeight = ViewHeight() - 1;
        int row = 0;
        int lineNumber = topLine_ + 1;
        for (auto it = doc_->begin() + topLine_; row < contentHeight && it != doc_->end(); ++it, ++lineNumber)
        {
            const auto& line = *it;
            size_t where = pattern_.empty() ? std::string::npos : line.find(pattern_);

            if (!isFiltering_ || pattern_.empty() || where != std::string::npos)
            {
                // Draw the line number.
                const auto colour = (where != std::string::npos) ? Color::Cyan1 : Color::DarkCyan;
                lineNumberArea.DrawText(0, row * yFactor_, std::to_string(lineNumber), colour);

                // Draw this line's text.
                contentArea.DrawText(-leftEdge_ * xFactor_, row * yFactor_, line);

                // Highlight matches by overdrawing in a different colour.
                for (; where != std::string::npos; where = line.find(pattern_, where + pattern_.size()))
                {
                    int x = static_cast<int>(where) * xFactor_ - leftEdge_ * xFactor_;
                    contentArea.DrawText(x, row * yFactor_, line.substr(where, pattern_.size()), Color::Yellow1);
                }
                ++row;
            }
        }

        // Update the status area.
        const std::string lineIndicator = std::to_string(topLine_ + 1) + '/' + std::to_string(doc_->Size());
        statusArea.DrawText(0, 0, lineIndicator, Color::Aquamarine1);
        if (isCapturing_)
        {
            statusArea.DrawText(30, 0, "/", Color::DarkGoldenrod);
        }
        statusArea.DrawText(32, 0, pattern_, Color::DarkGoldenrod);

        return vbox(std::move(hbox(canvas(std::move(lineNumberArea)), canvas(std::move(contentArea)))),
                    std::move(canvas(std::move(statusArea))));
    }

    bool OnEvent(Event e) override
    {
        if (e.is_character())
        {
            const auto ch = e.character();
            if (isCapturing_)
            {
                pattern_ += ch;
                UpdateMatchFromPattern();
            }
            else
            {
                if (ch == "/")
                {
                    CmdStartCapture();
                }
                else if (ch == "n")
                {
                    CmdNextMatch();
                }
                else if (ch == "p")
                {
                    CmdPreviousMatch();
                }
            }
        }
        else // usually special
        {
            if (e == Event::Return)
            {
                CmdEndCapture();
            }
            else if (e == Event::Escape)
            {
                CmdCancelCapture();
            }
            else if (e == Event::Backspace)
            {
                CmdBackspaceCapture();
            }
            if (e == Event::ArrowUp)
            {
                if (isFiltering_ && !pattern_.empty())
                {
                    CmdPreviousMatch();
                }
                else
                {
                    CmdPreviousLine();
                }
            }
            else if (e == Event::ArrowDown)
            {
                if (isFiltering_ && !pattern_.empty())
                {
                    CmdNextMatch();
                }
                else
                {
                    CmdNextLine();
                }
            }
            else if (e == Event::ArrowLeft)
            {
                CmdPreviousColumn();
            }
            else if (e == Event::ArrowRight)
            {
                CmdNextColumn();
            }
            else if (e == keyevents::CtrlHome)
            {
                CmdStartOfDocument();
            }
            else if (e == keyevents::CtrlEnd)
            {
                CmdEndOfDocument();
            }
            else if (e == keyevents::PgUp)
            {
                if (!isFiltering_)
                {
                    CmdPreviousPage();
                }
                else
                {
                    CmdPreviousFilteredPage();
                }
            }
            else if (e == keyevents::PgDown)
            {
                if (!isFiltering_)
                {
                    CmdNextPage();
                }
                else
                {
                    CmdNextFilteredPage();
                }
            }
            else if (e == keyevents::Home)
            {
                CmdLeftmostColumn();
            }
            else if (e == keyevents::End)
            {
                CmdRightmostColumn();
            }
            else if (e == keyevents::CtrlL)
            {
                CmdToggleLineNumbers();
            }
            else if (e == keyevents::CtrlT)
            {
                CmdToggleFiltering();
            }
        }
        leftEdge_ = std::clamp(leftEdge_, 0, ViewWidth() - 1);
        topLine_ = std::clamp(topLine_, 0, doc_->Size() - 1);
        return false;
    }
};

int main(int argc, const char* argv[])
{
    std::string filename = argv[1];
    auto doc = std::make_shared<Document>(filename);

    auto screen = ScreenInteractive::Fullscreen();
    auto fv = std::make_shared<FileViewer>(doc);
    screen.Loop(fv);

    return 0;
}
