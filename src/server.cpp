#include "server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>

#include "web_page.h"

namespace cgdb {

namespace {

std::string json_escape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

// Serializes a node-id set + edge list in the /api/graph shape.
std::string subgraph_json(const Graph& g, std::vector<NodeId> nodes,
                          std::vector<std::pair<NodeId, NodeId>> edges,
                          const std::vector<EdgeKind>& edge_kinds) {
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
    std::string out = "{\"nodes\":[";
    bool first = true;
    for (NodeId id : nodes) {
        if (!first) out += ',';
        first = false;
        const Node& n = g.nodes()[id];
        out += "{\"id\":" + std::to_string(id) + ",\"label\":\"" +
               json_escape(n.name) + "\",\"kind\":\"" + to_string(n.kind) + "\"}";
    }
    out += "],\"edges\":[";
    first = true;
    for (size_t i = 0; i < edges.size(); ++i) {
        if (!first) out += ',';
        first = false;
        out += "{\"from\":" + std::to_string(edges[i].first) +
               ",\"to\":" + std::to_string(edges[i].second) + ",\"kind\":\"" +
               to_string(edge_kinds[i]) + "\"}";
    }
    out += "]}";
    return out;
}

bool parse_kind(const std::string& s, NodeKind& out) {
    if (s == "source") out = NodeKind::Source;
    else if (s == "header") out = NodeKind::Header;
    else if (s == "object") out = NodeKind::Object;
    else if (s == "flag") out = NodeKind::Flag;
    else return false;
    return true;
}

// Percent-decoding (+ -> space) for query parameters.
std::string url_decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < s.size()) {
            auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                return -1;
            };
            int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
            } else {
                out += c;
            }
        } else {
            out += c;
        }
    }
    return out;
}

std::map<std::string, std::string> parse_query(const std::string& q) {
    std::map<std::string, std::string> params;
    size_t pos = 0;
    while (pos < q.size()) {
        size_t amp = q.find('&', pos);
        std::string kv = q.substr(pos, amp == std::string::npos ? amp : amp - pos);
        size_t eq = kv.find('=');
        if (eq != std::string::npos)
            params[url_decode(kv.substr(0, eq))] = url_decode(kv.substr(eq + 1));
        pos = amp == std::string::npos ? q.size() : amp + 1;
    }
    return params;
}

void send_all(int fd, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::send(fd, data.data() + off, data.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return;  // client went away; this is a best-effort server
        off += static_cast<size_t>(n);
    }
}

void respond(int fd, int status, const std::string& status_text,
             const std::string& content_type, const std::string& body) {
    std::string head = "HTTP/1.1 " + std::to_string(status) + " " + status_text +
                       "\r\nContent-Type: " + content_type +
                       "\r\nContent-Length: " + std::to_string(body.size()) +
                       "\r\nConnection: close\r\n\r\n";
    send_all(fd, head);
    send_all(fd, body);
}

// Handles one connection: read request, route, respond, close.
void handle(int fd, const Graph& g) {
    std::string req;
    char buf[8192];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 65536) {
        ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0) break;
        req.append(buf, static_cast<size_t>(n));
    }
    // Request line: METHOD SP PATH SP VERSION
    size_t sp1 = req.find(' ');
    size_t sp2 = sp1 == std::string::npos ? sp1 : req.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) {
        respond(fd, 400, "Bad Request", "text/plain", "bad request\n");
        return;
    }
    std::string method = req.substr(0, sp1);
    std::string target = req.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string path = target, query;
    size_t q = target.find('?');
    if (q != std::string::npos) {
        path = target.substr(0, q);
        query = target.substr(q + 1);
    }

    if (method != "GET") {
        respond(fd, 405, "Method Not Allowed", "text/plain", "GET only\n");
        return;
    }
    if (path == "/") {
        respond(fd, 200, "OK", "text/html", kWebPage);
        return;
    }
    if (path == "/api/graph") {
        auto params = parse_query(query);
        auto it = params.find("kind");
        if (it == params.end())
            respond(fd, 200, "OK", "application/json", graph_to_json(g));
        else
            respond(fd, 200, "OK", "application/json",
                    graph_to_json(g, it->second));
        return;
    }
    if (path == "/api/neighborhood") {
        auto params = parse_query(query);
        auto it = params.find("path");
        std::string body;
        if (it == params.end() || !neighborhood_json(g, it->second, body)) {
            respond(fd, 404, "Not Found", "text/plain", "unknown path\n");
            return;
        }
        respond(fd, 200, "OK", "application/json", body);
        return;
    }
    if (path == "/api/cycles") {
        respond(fd, 200, "OK", "application/json", cycles_json(g));
        return;
    }
    respond(fd, 404, "Not Found", "text/plain", "not found\n");
}

}  // namespace

std::string graph_to_json(const Graph& g) {
    std::vector<NodeId> nodes(g.nodes().size());
    for (NodeId i = 0; i < nodes.size(); ++i) nodes[i] = i;
    std::vector<std::pair<NodeId, NodeId>> edges;
    std::vector<EdgeKind> kinds;
    g.for_each_edge([&](NodeId f, NodeId t, EdgeKind k) {
        edges.emplace_back(f, t);
        kinds.push_back(k);
    });
    return subgraph_json(g, nodes, edges, kinds);
}

std::string graph_to_json(const Graph& g, const std::string& kind) {
    NodeKind want;
    if (!parse_kind(kind, want)) return "{\"nodes\":[],\"edges\":[]}";
    const auto& nodes = g.nodes();
    std::vector<char> included(nodes.size(), 0);
    std::vector<NodeId> node_ids;
    for (NodeId i = 0; i < nodes.size(); ++i)
        if (nodes[i].kind == want) {
            included[i] = 1;
            node_ids.push_back(i);
        }
    std::vector<std::pair<NodeId, NodeId>> edges;
    std::vector<EdgeKind> kinds;
    g.for_each_edge([&](NodeId f, NodeId t, EdgeKind k) {
        if (nodes[f].kind != want && nodes[t].kind != want) return;
        edges.emplace_back(f, t);
        kinds.push_back(k);
        for (NodeId id : {f, t})
            if (!included[id]) {
                included[id] = 1;
                node_ids.push_back(id);
            }
    });
    return subgraph_json(g, node_ids, edges, kinds);
}

bool neighborhood_json(const Graph& g, const std::string& path, std::string& out) {
    const Node* n = g.find(path);
    if (!n) return false;
    NodeId center = g.id_of(path);
    std::vector<NodeId> node_ids{center};
    std::vector<std::pair<NodeId, NodeId>> edges;
    std::vector<EdgeKind> kinds;
    g.for_each_edge([&](NodeId f, NodeId t, EdgeKind k) {
        if (f != center && t != center) return;
        edges.emplace_back(f, t);
        kinds.push_back(k);
        node_ids.push_back(f);
        node_ids.push_back(t);
    });
    out = subgraph_json(g, node_ids, edges, kinds);
    return true;
}

std::string cycles_json(const Graph& g) {
    std::string out = "{\"cycles\":[";
    bool first = true;
    for (const auto& group : g.cycles()) {
        if (!first) out += ',';
        first = false;
        out += '[';
        bool first_member = true;
        for (NodeId id : group) {
            if (!first_member) out += ',';
            first_member = false;
            out += '"' + json_escape(g.nodes()[id].name) + '"';
        }
        out += ']';
    }
    out += "]}";
    return out;
}

void serve(const Graph& g, int port) {
    std::signal(SIGPIPE, SIG_IGN);

    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) throw std::runtime_error("socket: " + std::string(std::strerror(errno)));
    int one = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
        ::close(srv);
        throw std::runtime_error("bind port " + std::to_string(port) + ": " +
                                 std::strerror(errno));
    }
    if (::listen(srv, 16) < 0) {
        ::close(srv);
        throw std::runtime_error("listen: " + std::string(std::strerror(errno)));
    }
    while (true) {
        int fd = ::accept(srv, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EINTR) continue;
            continue;  // transient error: keep serving
        }
        handle(fd, g);
        ::close(fd);  // one request per connection (Connection: close)
    }
}

}  // namespace cgdb
