#include "depfile.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cgdb {

namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

// Splits ONE logical line into words, resolving make-style escaping
// (\ , \\, \#, \:, $$) and skipping '#' comments.
std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> words;
    std::string cur;
    bool in_word = false;
    auto flush = [&]() {
        if (in_word) {
            words.push_back(cur);
            cur.clear();
            in_word = false;
        }
    };
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\\' && i + 1 < text.size()) {
            cur += text[++i];  // escaped character is literal
            in_word = true;
            continue;
        }
        if (c == '$' && i + 1 < text.size() && text[i + 1] == '$') {
            cur += '$';  // $$ is a literal dollar sign in make syntax
            ++i;
            in_word = true;
            continue;
        }
        if (is_space(c)) {
            flush();
            continue;
        }
        if (c == '#') break;  // unescaped '#' starts a comment
        cur += c;
        in_word = true;
    }
    flush();
    return words;
}

// Parses one logical line: targets up to the first word ending in ':',
// everything after is a dependency.
void parse_logical_line(const std::string& line, std::vector<DepStanza>& out) {
    std::vector<std::string> words = tokenize(line);
    DepStanza stanza;
    size_t i = 0;
    for (; i < words.size(); ++i) {
        std::string w = words[i];
        if (!w.empty() && w.back() == ':') {
            w.pop_back();
            if (!w.empty()) stanza.targets.push_back(w);
            ++i;
            break;
        }
        stanza.targets.push_back(w);
    }
    if (stanza.targets.empty()) return;  // no ':' on this line: not a rule
    for (; i < words.size(); ++i) stanza.deps.push_back(words[i]);
    out.push_back(std::move(stanza));
}

}  // namespace

std::vector<DepStanza> parse_depfile(const std::string& text) {
    std::vector<DepStanza> stanzas;
    std::string logical;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        bool last = (nl == std::string::npos);
        std::string line = text.substr(pos, last ? std::string::npos : nl - pos);
        pos = last ? text.size() + 1 : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Backslash at end of line = continuation: keep accumulating.
        if (!line.empty() && line.back() == '\\' &&
            (line.size() < 2 || line[line.size() - 2] != '\\')) {
            logical += line.substr(0, line.size() - 1);
            logical += ' ';
            continue;
        }
        logical += line;
        parse_logical_line(logical, stanzas);
        logical.clear();
        if (last) break;
    }
    return stanzas;
}

std::vector<DepStanza> parse_depfile_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse_depfile(ss.str());
}

}  // namespace cgdb
