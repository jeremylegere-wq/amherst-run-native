#include "world/map_file.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace amr {

float TerrainGrid::node(int i, int j) const {
    i = std::clamp(i, 0, nx - 1);
    j = std::clamp(j, 0, ny - 1);
    return h[static_cast<std::size_t>(j) * nx + i];
}

float TerrainGrid::height(float x, float y) const {
    if (nx == 0 || ny == 0) return 0.0f;
    const float fx = (x - x0) / step, fy = (y - y0) / step;
    const int i = static_cast<int>(std::floor(fx)), j = static_cast<int>(std::floor(fy));
    const float tx = fx - i, ty = fy - j;
    const float a = node(i, j), b = node(i + 1, j), c = node(i, j + 1), d = node(i + 1, j + 1);
    const float top = a + (b - a) * tx;
    return top + (c + (d - c) * tx - top) * ty;
}

float TerrainGrid::mesh_height(float x, float y) const {
    if (nx == 0 || ny == 0) return 0.0f;
    const float fx = (x - x0) / step, fy = (y - y0) / step;
    const int i = static_cast<int>(std::floor(fx)), j = static_cast<int>(std::floor(fy));
    const float tx = fx - i, ty = fy - j;
    const float a = node(i, j), b = node(i + 1, j), c = node(i + 1, j + 1), d = node(i, j + 1);
    return tx >= ty ? a + (b - a) * tx + (c - b) * ty : a + (d - a) * ty + (c - d) * tx;
}

namespace {

// Reads little-endian values from a chunk, remembering if it ever ran past the end.
class Reader {
public:
    Reader(const std::uint8_t* p, std::size_t n) : p_(p), n_(n) {}
    bool ok() const { return ok_; }
    bool done() const { return off_ == n_; }
    template <typename T>
    T get() {
        T v{};
        if (off_ + sizeof(T) > n_) {
            ok_ = false;
            return v;
        }
        std::memcpy(&v, p_ + off_, sizeof(T));  // the file is little-endian, like every target
        off_ += sizeof(T);
        return v;
    }
    std::string str() {
        const auto len = get<std::uint32_t>();
        if (!ok_ || off_ + len > n_) {
            ok_ = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(p_ + off_), len);
        off_ += len;
        return s;
    }
    // A count that must fit in what is left (each item at least `min_size` bytes): stops a bad file
    // from asking for gigabytes.
    std::uint32_t count(std::size_t min_size) {
        const auto c = get<std::uint32_t>();
        if (ok_ && min_size > 0 && c > (n_ - off_) / min_size) ok_ = false;
        return ok_ ? c : 0;
    }
    void points(std::vector<Point2>& out) {
        const auto n = count(8);
        out.resize(n);
        for (auto& p : out) {
            p.x = get<float>();
            p.y = get<float>();
        }
    }
    void tris(std::vector<Tri>& out, std::size_t npoints) {
        const auto n = count(6);
        out.resize(n);
        for (auto& t : out) {
            t.a = get<std::uint16_t>();
            t.b = get<std::uint16_t>();
            t.c = get<std::uint16_t>();
            if (t.a >= npoints || t.b >= npoints || t.c >= npoints) ok_ = false;
        }
    }
    Rgb rgb() {
        Rgb c;
        c.r = get<std::uint8_t>();
        c.g = get<std::uint8_t>();
        c.b = get<std::uint8_t>();
        return c;
    }

private:
    const std::uint8_t* p_;
    std::size_t n_, off_ = 0;
    bool ok_ = true;
};

bool read_chunk(const std::string& tag, Reader& r, MapData& m) {
    if (tag == "META") {
        m.x0 = r.get<float>(); m.y0 = r.get<float>(); m.x1 = r.get<float>(); m.y1 = r.get<float>();
        m.spawn_x = r.get<float>(); m.spawn_y = r.get<float>(); m.spawn_heading = r.get<float>();
        m.attribution = r.str();
        m.source = r.str();
    } else if (tag == "NAME") {
        const auto n = r.count(4);
        m.names.resize(n);
        for (auto& s : m.names) s = r.str();
    } else if (tag == "TERR") {
        auto& t = m.terrain;
        t.x0 = r.get<float>(); t.y0 = r.get<float>(); t.step = r.get<float>();
        t.nx = static_cast<int>(r.get<std::uint32_t>());
        t.ny = static_cast<int>(r.get<std::uint32_t>());
        const float base = r.get<float>(), unit = r.get<float>();
        if (!r.ok() || t.nx <= 0 || t.ny <= 0 || t.step <= 0) return false;
        t.h.resize(static_cast<std::size_t>(t.nx) * t.ny);
        for (auto& v : t.h) v = base + unit * r.get<std::uint16_t>();
    } else if (tag == "ROAD") {
        const auto n = r.count(20);
        m.roads.resize(n);
        for (auto& rd : m.roads) {
            rd.type = r.get<std::uint8_t>(); rd.service = r.get<std::uint8_t>();
            rd.flags = r.get<std::uint8_t>(); rd.lanes = r.get<std::uint8_t>();
            rd.width = r.get<float>(); rd.speed_kmh = r.get<float>();
            rd.name = r.get<std::uint32_t>();
            r.points(rd.points);
        }
    } else if (tag == "SPAN") {
        const auto n = r.count(16);
        m.spans.resize(n);
        for (auto& sp : m.spans) {
            sp.road = r.get<std::uint32_t>();
            sp.width = r.get<float>();
            sp.lift = r.get<float>();
            const auto np = r.count(12);
            sp.points.resize(np);
            for (auto& p : sp.points) {
                p.x = r.get<float>(); p.y = r.get<float>(); p.h = r.get<float>();
            }
        }
    } else if (tag == "BLDG") {
        const auto n = r.count(24);
        m.buildings.resize(n);
        for (auto& b : m.buildings) {
            b.height = r.get<float>(); b.roof_height = r.get<float>();
            b.roof = r.get<std::uint8_t>(); b.flags = r.get<std::uint8_t>();
            b.wall = r.rgb(); b.roof_colour = r.rgb();
            r.get<std::uint8_t>();  // pad
            r.points(b.points);
            r.tris(b.tris, b.points.size());
        }
    } else if (tag == "POLY") {
        const auto n = r.count(12);
        m.polys.resize(n);
        for (auto& p : m.polys) {
            p.kind = r.get<std::uint8_t>();
            p.colour = r.rgb();
            r.points(p.points);
            r.tris(p.tris, p.points.size());
        }
    } else if (tag == "PROP") {
        const auto nk = r.count(4);
        m.prop_kinds.resize(nk);
        for (auto& k : m.prop_kinds) k = r.str();
        const auto n = r.count(22);
        m.props.resize(n);
        for (auto& p : m.props) {
            p.kind = r.get<std::uint8_t>();
            p.c = r.get<std::uint8_t>();
            p.colour = r.rgb();
            p.segment = (r.get<std::uint8_t>() & 1) != 0;
            p.x = r.get<float>(); p.y = r.get<float>();
            p.h = r.get<std::uint16_t>() / 100.0f;
            p.r = r.get<std::uint16_t>() / 100.0f;
            p.cr = r.get<std::uint16_t>() / 100.0f;
            p.heading = r.get<std::int16_t>() / 1000.0f;
            if (p.segment) { p.x2 = r.get<float>(); p.y2 = r.get<float>(); }
            if (p.kind >= nk) return false;
        }
    } else {
        return true;  // a newer chunk this version does not know: skip it
    }
    return r.ok() && r.done();
}

}  // namespace

int MapData::prop_kind(const std::string& name) const {
    for (std::size_t i = 0; i < prop_kinds.size(); ++i)
        if (prop_kinds[i] == name) return static_cast<int>(i);
    return -1;
}

bool parse_map(const std::uint8_t* data, std::size_t size, MapData& out, std::string& error) {
    out = MapData{};
    if (size < 12 || std::memcmp(data, "AMR1", 4) != 0) {
        error = "not an Amherst Run map file";
        return false;
    }
    Reader head(data + 4, 8);
    const auto version = head.get<std::uint32_t>(), chunks = head.get<std::uint32_t>();
    if (version != kMapVersion) {
        error = "map file version " + std::to_string(version) + ", this game reads version " +
                std::to_string(kMapVersion) + " (bake it again, or update the game)";
        return false;
    }
    std::size_t off = 12;
    for (std::uint32_t c = 0; c < chunks; ++c) {
        if (off + 8 > size) {
            error = "map file is cut short";
            return false;
        }
        const std::string tag(reinterpret_cast<const char*>(data + off), 4);
        std::uint32_t len = 0;
        std::memcpy(&len, data + off + 4, 4);
        if (off + 8 + len > size) {
            error = "map file is cut short in " + tag;
            return false;
        }
        Reader r(data + off + 8, len);
        if (!read_chunk(tag, r, out)) {
            error = "bad " + tag + " chunk";
            return false;
        }
        off += 8 + len;
    }
    for (auto& sp : out.spans) {
        if (sp.road >= out.roads.size()) {
            error = "overpass refers to a missing road";
            return false;
        }
        const float half = sp.width / 2 + 0.3f;
        sp.x0 = sp.y0 = 1e30f;
        sp.x1 = sp.y1 = -1e30f;
        for (const auto& p : sp.points) {
            sp.x0 = std::min(sp.x0, p.x - half); sp.x1 = std::max(sp.x1, p.x + half);
            sp.y0 = std::min(sp.y0, p.y - half); sp.y1 = std::max(sp.y1, p.y + half);
        }
    }
    for (const auto& rd : out.roads)
        if (rd.name >= out.names.size() && !out.names.empty()) {
            error = "road name out of range";
            return false;
        }
    return true;
}

bool load_map(const std::string& path, MapData& out, std::string& error) {
    SDL_RWops* f = SDL_RWFromFile(path.c_str(), "rb");
    if (!f) {
        error = "cannot open " + path + ": " + SDL_GetError();
        return false;
    }
    const Sint64 size = SDL_RWsize(f);
    std::vector<std::uint8_t> buf(size > 0 ? static_cast<std::size_t>(size) : 0);
    const bool read_ok = size > 0 && SDL_RWread(f, buf.data(), 1, buf.size()) == buf.size();
    SDL_RWclose(f);
    if (!read_ok) {
        error = "cannot read " + path;
        return false;
    }
    return parse_map(buf.data(), buf.size(), out, error);
}

}  // namespace amr
