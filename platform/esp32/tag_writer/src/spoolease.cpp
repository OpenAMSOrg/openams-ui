// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on OpenRFID (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "spoolease.h"
#include "ndef.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

struct SpoolEaseExt {
    int bed_temp;
    int drying_temp;
    int drying_hours;
};

static const std::map<std::string, SpoolEaseExt> EXTENDED_DATA = {
    {"ABS",      {95,  80,  8}},
    {"ABS-GF",   {95,  80,  8}},
    {"ABS-S",    {95,  80,  8}},
    {"ASA",      {95,  80,  8}},
    {"ASA-AERO", {85,  80,  8}},
    {"ASA-CF",   {95,  80,  8}},
    {"BVOH",     {60,  60,  6}},
    {"EVA",      {55,  50,  6}},
    {"HIPS",     {100, 70,  6}},
    {"PA",       {110, 80,  10}},
    {"PA-CF",    {110, 80,  10}},
    {"PA-GF",    {110, 80,  10}},
    {"PA-S",     {110, 80,  10}},
    {"PA6-CF",   {110, 80,  10}},
    {"PC",       {100, 80,  8}},
    {"PCTG",     {80,  65,  6}},
    {"PE",       {60,  60,  6}},
    {"PE-CF",    {60,  60,  6}},
    {"PET-CF",   {85,  80,  10}},
    {"PETG",     {70,  65,  8}},
    {"PETG-CF",  {70,  65,  8}},
    {"PHA",      {60,  50,  6}},
    {"PLA",      {55,  50,  8}},
    {"PLA-AERO", {45,  50,  8}},
    {"PLA-CF",   {60,  55,  8}},
    {"PLA-S",    {45,  50,  8}},
    {"PP",       {90,  70,  12}},
    {"PP-CF",    {90,  70,  12}},
    {"PP-GF",    {90,  70,  12}},
    {"PPA-CF",   {110, 120, 10}},
    {"PPA-GF",   {110, 120, 10}},
    {"PPS",      {110, 120, 10}},
    {"PPS-CF",   {110, 120, 10}},
    {"PVA",      {45,  60,  8}},
    {"TPU",      {38,  70,  8}},
    {"TPU-AMS",  {33,  70,  8}},
};

static std::string get_alias(const std::string &m) {
    if (m == "ABS-S") return "ABS";
    if (m == "PA-S") return "PA";
    if (m == "PLA-S") return "PLA";
    if (m == "TPU-AMS") return "TPU";
    return m;
}

static ndef::Tag to_ndef_tag(const Tag &tag) {
    ndef::Tag ntag;
    if (tag.technology == TagTechnology::ISO14443A) {
        ntag.technology = ndef::Technology::ISO14443A;
    } else if (tag.technology == TagTechnology::ISO15693) {
        ntag.technology = ndef::Technology::ISO15693;
    } else {
        ntag.technology = ndef::Technology::Unknown;
    }

    switch (tag.tag_type) {
        case TagType::NTAG213: ntag.tag_type = ndef::TagType::NTAG213; break;
        case TagType::NTAG215: ntag.tag_type = ndef::TagType::NTAG215; break;
        case TagType::NTAG216: ntag.tag_type = ndef::TagType::NTAG216; break;
        case TagType::ICODE_SLIX2: ntag.tag_type = ndef::TagType::ICODE_SLIX2; break;
        case TagType::MIFARE_CLASSIC_1K:
        case TagType::MIFARE_CLASSIC_4K:
        case TagType::MIFARE_MINI:
            ntag.tag_type = ndef::TagType::MifareClassic; break;
        default:
            if (is_type2_tag(tag.tag_type)) {
                ntag.tag_type = ndef::TagType::Type2;
            } else if (is_type5_tag(tag.tag_type)) {
                ntag.tag_type = ndef::TagType::Type5;
            } else {
                ntag.tag_type = ndef::TagType::Unknown;
            }
            break;
    }

    ntag.memory = tag.memory;
    ntag.block_size = tag.block_size;
    ntag.user_first_block = tag.user_first_block >= 0 ? static_cast<size_t>(tag.user_first_block) : 0;
    ntag.user_last_block = tag.user_last_block >= 0 ? static_cast<size_t>(tag.user_last_block) : 0;
    return ntag;
}

static std::string url_decode(const std::string &in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '+') {
            out += ' ';
        } else if (in[i] == '%' && i + 2 < in.size() &&
                   std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
            char hex[3] = { in[i + 1], in[i + 2], '\0' };
            out += static_cast<char>(std::strtoul(hex, nullptr, 16));
            i += 2;
        } else {
            out += in[i];
        }
    }
    return out;
}

std::vector<std::string> SpoolEaseCodec::tag_types() const {
    return {
        TagTypes::NTAG213,
        TagTypes::NTAG215,
        TagTypes::NTAG216,
        TagTypes::ULTRALIGHT,
        TagTypes::ULTRALIGHT_EV1,
        TagTypes::TYPE2,
        TagTypes::ICODE_SLIX,
        TagTypes::ICODE_SLIX2,
        TagTypes::ICODE_SLIX_S,
        TagTypes::ICODE_SLIX_L,
        TagTypes::ICODE_DNA,
        TagTypes::TYPE5
    };
}

bool SpoolEaseCodec::decode(const Tag &tag, DecodeResult &out) {
    ndef::Tag ntag = to_ndef_tag(tag);
    ndef::NdefMessage msg;
    if (!ndef::read_message(ntag, msg)) {
        return false;
    }

    for (const auto &rec : msg.records) {
        std::string uri;
        if (rec.is_uri()) {
            uri = rec.uri();
        }
        if (uri.empty() && rec.is_text()) {
            std::string t = rec.text();
            size_t start = 0;
            while (start < t.size() && (t[start] == ' ' || t[start] == '\t' ||
                                        t[start] == '\r' || t[start] == '\n')) start++;
            size_t end = t.size();
            while (end > start && (t[end - 1] == ' ' || t[end - 1] == '\t' ||
                                   t[end - 1] == '\r' || t[end - 1] == '\n')) end--;
            t = t.substr(start, end - start);
            std::string lower = t;
            for (auto &c : lower) c = std::tolower(static_cast<unsigned char>(c));
            if (lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0) {
                uri = t;
            }
        }
        if (uri.empty()) {
            continue;
        }
        if (parse_uri(uri, out)) {
            return true;
        }
    }

    return false;
}

bool SpoolEaseCodec::parse_uri(const std::string &uri, DecodeResult &out) {
    size_t scheme_pos = uri.find("://");
    if (scheme_pos == std::string::npos) {
        return false;
    }
    size_t host_start = scheme_pos + 3;
    size_t path_start = uri.find('/', host_start);
    if (path_start == std::string::npos) {
        return false;
    }

    std::string host = uri.substr(host_start, path_start - host_start);
    size_t port_pos = host.find(':');
    if (port_pos != std::string::npos) {
        host = host.substr(0, port_pos);
    }
    for (auto &c : host) c = std::tolower(static_cast<unsigned char>(c));
    if (host != "tag.spoolease.io") {
        return false;
    }

    std::string path;
    std::string query_str;
    size_t q_pos = uri.find('?', path_start);
    if (q_pos != std::string::npos) {
        path = uri.substr(path_start, q_pos - path_start);
        query_str = uri.substr(q_pos + 1);
    } else {
        path = uri.substr(path_start);
    }

    if (path.rfind("/S1", 0) != 0) {
        return false;
    }

    std::map<std::string, std::vector<std::string>> query;
    size_t pos = 0;
    while (pos < query_str.size()) {
        size_t next = query_str.find('&', pos);
        std::string pair = (next == std::string::npos) ? query_str.substr(pos) : query_str.substr(pos, next - pos);
        pos = (next == std::string::npos) ? query_str.size() : next + 1;
        if (pair.empty()) continue;
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string k = url_decode(pair.substr(0, eq));
            std::string v = url_decode(pair.substr(eq + 1));
            if (!v.empty()) {
                query[k].push_back(v);
            }
        }
    }

    std::string raw_mat = (query.count("M") && !query["M"].empty() && !query["M"][0].empty())
                          ? query["M"][0] : "PLA";
    for (auto &c : raw_mat) c = std::toupper(static_cast<unsigned char>(c));
    std::string material = get_alias(raw_mat);

    std::string subtype = (query.count("MS") && !query["MS"].empty()) ? query["MS"][0] : "";
    std::string name = !subtype.empty() ? (subtype + " " + raw_mat) : raw_mat;

    std::string brand = (query.count("B") && !query["B"].empty() && !query["B"][0].empty())
                        ? query["B"][0] : "Generic";

    std::string color_field = (query.count("CC") && !query["CC"].empty() && !query["CC"][0].empty())
                              ? query["CC"][0] : "FFFFFFFF";
    std::vector<std::string> colors;
    size_t cpos = 0;
    while (cpos < color_field.size()) {
        size_t cnext = color_field.find(';', cpos);
        std::string c = (cnext == std::string::npos) ? color_field.substr(cpos) : color_field.substr(cpos, cnext - cpos);
        cpos = (cnext == std::string::npos) ? color_field.size() : cnext + 1;
        while (!c.empty() && (c.front() == ' ' || c.front() == '#')) c.erase(c.begin());
        while (!c.empty() && c.back() == ' ') c.pop_back();
        if (c.size() == 6 || c.size() == 8) {
            std::string col6 = c.substr(0, 6);
            for (auto &ch : col6) ch = std::toupper(static_cast<unsigned char>(ch));
            colors.push_back(col6);
        }
    }
    std::string color_hex = !colors.empty() ? colors[0] : "FFFFFF";
    if (colors.empty()) {
        colors.push_back(color_hex);
    }

    if (!query.count("NN") || query["NN"].empty() || query["NN"][0].empty() ||
        !query.count("NX") || query["NX"].empty() || query["NX"][0].empty()) {
        return false;
    }

    char *end1 = nullptr;
    char *end2 = nullptr;
    long nozzle_min = std::strtol(query["NN"][0].c_str(), &end1, 10);
    long nozzle_max = std::strtol(query["NX"][0].c_str(), &end2, 10);
    if (end1 == query["NN"][0].c_str() || end2 == query["NX"][0].c_str()) {
        return false;
    }

    if (nozzle_max < nozzle_min) {
        out.recognized = true;
        out.format = "spoolease";
        out.filament = Filament();
        out.info.clear();
        out.info["error"] = "invalid temperature range";
        return true;
    }

    double weight_g = 1000.0;
    if (query.count("WL") && !query["WL"].empty() && !query["WL"][0].empty()) {
        char *endw = nullptr;
        long w = std::strtol(query["WL"][0].c_str(), &endw, 10);
        if (endw != query["WL"][0].c_str()) {
            weight_g = static_cast<double>(w);
        }
    }

    int bed_min = 0, bed_max = 0;
    int drying_temp = 0, drying_time_min = 0;

    auto it_ext = EXTENDED_DATA.find(raw_mat);
    if (it_ext == EXTENDED_DATA.end()) {
        it_ext = EXTENDED_DATA.find(material);
    }
    if (it_ext != EXTENDED_DATA.end()) {
        bed_min = it_ext->second.bed_temp;
        bed_max = it_ext->second.bed_temp;
        drying_temp = it_ext->second.drying_temp;
        drying_time_min = it_ext->second.drying_hours * 60;
    }

    Filament fil;
    fil.brand = brand;
    fil.material = material;
    fil.name = name;
    fil.color_hex = color_hex;
    fil.colors = colors;
    fil.diameter_mm = 1.75;
    fil.weight_g = static_cast<int>(weight_g);
    fil.nozzle_min = static_cast<int>(nozzle_min);
    fil.nozzle_max = static_cast<int>(nozzle_max);
    fil.bed_min = bed_min;
    fil.bed_max = bed_max;
    fil.drying_temp = drying_temp;
    fil.drying_time_min = drying_time_min;

    out.info.clear();
    out.info["url"] = uri;
    out.info["raw_material"] = raw_mat;
    for (const auto &kv : query) {
        if (!kv.second.empty()) {
            out.info[kv.first] = kv.second[0];
        }
    }

    out.recognized = true;
    out.format = "spoolease";
    out.filament = fil;
    return true;
}
