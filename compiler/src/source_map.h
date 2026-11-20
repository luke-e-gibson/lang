// source_map.h
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct LineCol { uint32_t line, col; };   // both 1-based

class SourceMap {
public:
    SourceMap(std::string name, std::string_view src) : name_(std::move(name)), src_(src) {
        starts_.push_back(0);
        for (uint32_t i = 0; i < src.size(); ++i)
            if (src[i] == '\n') starts_.push_back(i + 1);
    }

    LineCol locate(uint32_t off) const {
        // last line start <= off
        auto it = std::upper_bound(starts_.begin(), starts_.end(), off);
        uint32_t line = uint32_t(it - starts_.begin());          // 1-based
        return {line, off - starts_[line - 1] + 1};
    }

    std::string_view lineText(uint32_t line) const {
        uint32_t b = starts_[line - 1];
        uint32_t e = line < starts_.size() ? starts_[line] : uint32_t(src_.size());
        while (e > b && (src_[e - 1] == '\n' || src_[e - 1] == '\r')) --e;
        return src_.substr(b, e - b);
    }

    // "file.lang:3:5: error: message" + source line + caret
    std::string format(uint32_t off, const std::string& msg) const {
        LineCol lc = locate(off);
        std::string out = name_ + ":" + std::to_string(lc.line) + ":" +
                          std::to_string(lc.col) + ": error: " + msg + "\n";
        std::string_view text = lineText(lc.line);
        std::string gutter = std::to_string(lc.line);
        out += " " + gutter + " | " + std::string(text) + "\n";
        out += " " + std::string(gutter.size(), ' ') + " | " +
               std::string(lc.col - 1, ' ') + "^\n";
        return out;
    }

private:
    std::string name_;
    std::string_view src_;            // source must outlive the map
    std::vector<uint32_t> starts_;    // byte offset where each line begins
};