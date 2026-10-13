// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "spoolman.h"
#include "openspool.h"
#include "ndef.h"
#include <algorithm>
#include <cctype>
#include <cstring>

static std::string clean_color(const std::string &c) {
    if (c.empty()) return "";
    size_t start = 0;
    while (start < c.size() && std::isspace(static_cast<unsigned char>(c[start]))) {
        ++start;
    }
    if (start < c.size() && c[start] == '#') {
        ++start;
    }
    size_t end = c.size();
    while (end > start && std::isspace(static_cast<unsigned char>(c[end - 1]))) {
        --end;
    }
    std::string s = c.substr(start, end - start);
    if (s.size() < 6) return "";
    std::string out;
    for (size_t i = 0; i < 6; ++i) {
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(s[i])));
    }
    return out;
}

static std::string trim_str(const std::string &s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
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
        case TagType::TYPE5: ntag.tag_type = ndef::TagType::Type5; break;
        default:
            if (is_type2_tag(tag.tag_type)) ntag.tag_type = ndef::TagType::Type2;
            else if (is_type5_tag(tag.tag_type)) ntag.tag_type = ndef::TagType::Type5;
            else if (is_mifare_classic_tag(tag.tag_type)) ntag.tag_type = ndef::TagType::MifareClassic;
            else ntag.tag_type = ndef::TagType::Unknown;
            break;
    }
    ntag.memory = tag.memory;
    ntag.block_size = tag.block_size;
    ntag.user_first_block = tag.user_first_block >= 0 ? tag.user_first_block : 0;
    ntag.user_last_block = tag.user_last_block >= 0 ? tag.user_last_block : 0;
    return ntag;
}

SpoolmanCodec::SpoolmanCodec(const CodecOptions &options)
    : options_(options) {}

std::vector<std::string> SpoolmanCodec::tag_types() const {
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

bool SpoolmanCodec::can_encode(const Tag &tag) {
    bool match_type = false;
    std::string tname = tag_type_to_string(tag.tag_type);
    for (const auto &t : tag_types()) {
        if (t == tname) {
            match_type = true;
            break;
        }
    }
    if (!match_type &&
        tag.technology != TagTechnology::ISO14443A &&
        tag.technology != TagTechnology::ISO15693) {
        return false;
    }
    ndef::Tag ntag = to_ndef_tag(tag);
    ndef::NdefMessage msg;
    if (ndef::read_message(ntag, msg)) {
        if (msg.capacity < 20) {
            return false;
        }
    }
    if (tag.user_capacity() > 0 && tag.user_capacity() < 20) {
        return false;
    }
    return true;
}

// Case-insensitive ASCII prefix / equality, for the URI forms below.
static bool iequal_at(const std::string &s, size_t at, const char *lit) {
    size_t n = strlen(lit);
    if (at + n > s.size())
        return false;
    for (size_t i = 0; i < n; i++) {
        if (tolower(static_cast<unsigned char>(s[at + i])) !=
            tolower(static_cast<unsigned char>(lit[i])))
            return false;
    }
    return true;
}

static bool all_digits(const std::string &s, size_t from, size_t to) {
    if (from >= to)
        return false;
    for (size_t i = from; i < to; i++) {
        if (s[i] < '0' || s[i] > '9')
            return false;
    }
    return true;
}

static void set_uri_result(const std::string &uri, bool spool, const std::string &num,
                           DecodeResult &out) {
    out.recognized = true;
    out.format = "spoolman";
    if (spool)
        out.filament.spool_id = num;
    else
        out.filament.fields["filament_id"] = num;
    out.info["variant"] = "spoolman_uri";
    out.info["uri"] = uri;
}

// The two URI forms, matched case-insensitively and in full, as the reference
// regexes do: "web+spoolman:<s|f>-<digits>" and
// "http(s)://<anything>/<spool|filament>/show/<digits>[/]".
static bool parse_uri(const std::string &uri, DecodeResult &out) {
    std::string t = trim_str(uri);

    static const char SCHEME[] = "web+spoolman:";
    const size_t scheme_len = sizeof(SCHEME) - 1;
    if (iequal_at(t, 0, SCHEME) && t.size() > scheme_len + 2 && t[scheme_len + 1] == '-' &&
        all_digits(t, scheme_len + 2, t.size())) {
        char kind = static_cast<char>(tolower(static_cast<unsigned char>(t[scheme_len])));
        if (kind == 's' || kind == 'f') {
            set_uri_result(uri, kind == 's', t.substr(scheme_len + 2), out);
            return true;
        }
    }

    size_t rest;  // just past "://"
    if (iequal_at(t, 0, "http://"))
        rest = 7;
    else if (iequal_at(t, 0, "https://"))
        rest = 8;
    else
        return false;
    size_t end = t.size();
    if (end > rest && t[end - 1] == '/')
        end--;  // the optional trailing slash
    size_t digits = end;
    while (digits > rest && t[digits - 1] >= '0' && t[digits - 1] <= '9')
        digits--;
    if (digits == end)
        return false;  // no id
    static const char SHOW[] = "/show/";
    const size_t show_len = sizeof(SHOW) - 1;
    if (digits < rest + show_len || !iequal_at(t, digits - show_len, SHOW))
        return false;
    size_t kind_end = digits - show_len;
    std::string num = t.substr(digits, end - digits);
    if (kind_end >= rest + 6 && iequal_at(t, kind_end - 6, "/spool")) {
        set_uri_result(uri, true, num, out);
        return true;
    }
    if (kind_end >= rest + 9 && iequal_at(t, kind_end - 9, "/filament")) {
        set_uri_result(uri, false, num, out);
        return true;
    }
    return false;
}

static bool parse_companion(const std::string &text, DecodeResult &out) {
    std::string line;
    std::string spool_id;
    std::string filament_id;
    std::string material;
    std::map<std::string, std::string> raw_data;

    for (size_t pos = 0; pos <= text.size();) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos)
            nl = text.size();
        line = text.substr(pos, nl - pos);
        pos = nl + 1;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string k = trim_str(line.substr(0, colon));
        for (char &c : k) c = toupper(static_cast<unsigned char>(c));
        std::string v = trim_str(line.substr(colon + 1));
        raw_data[k] = v;
        // stoll would accept "12abc" (it stops at the first non-digit); the id
        // must be all digits to be a Spoolman id.
        if (k == "SPOOL") {
            if (!v.empty() && all_digits(v, 0, v.size()))
                spool_id = v;
        } else if (k == "FILAMENT") {
            if (!v.empty() && all_digits(v, 0, v.size()))
                filament_id = v;
        } else if (k == "MATERIAL" || k == "TYPE") {
            material = v;
            for (char &c : material) c = toupper(static_cast<unsigned char>(c));
        }
    }

    if (!spool_id.empty() || !filament_id.empty()) {
        out.recognized = true;
        out.format = "spoolman";
        if (!spool_id.empty()) {
            out.filament.spool_id = spool_id;
        }
        if (!filament_id.empty()) {
            out.filament.fields["filament_id"] = filament_id;
        }
        if (!material.empty()) {
            out.filament.material = material;
        }
        out.info = raw_data;
        out.info["variant"] = "spoolcompanion";
        return true;
    }

    return false;
}

static bool parse_filaman(const json_util::Value &obj, DecodeResult &out) {
    out.recognized = true;
    out.format = "spoolman";
    Filament &fil = out.filament;

    const auto *sm_id = obj.get("sm_id");
    if (sm_id) {
        std::string sid = trim_str(sm_id->as_string());
        if (!sid.empty() && sid != "0") {
            try {
                (void)std::stoll(sid);
                fil.spool_id = sid;
            } catch (...) {}
        }
    }

    const auto *b = obj.get("b");
    if (!b) b = obj.get("brand");
    if (b) fil.brand = b->as_string();

    const auto *t = obj.get("t");
    if (!t) t = obj.get("type");
    if (t) {
        fil.material = t->as_string();
        for (char &c : fil.material) c = toupper(static_cast<unsigned char>(c));
    }

    const auto *c_val = obj.get("c");
    if (!c_val) c_val = obj.get("color_hex");
    if (c_val) {
        fil.color_hex = clean_color(c_val->as_string());
        if (!fil.color_hex.empty()) {
            fil.colors.push_back(fil.color_hex);
        }
    }

    const auto *name_val = obj.get("cn");
    if (!name_val) name_val = obj.get("color_name");
    if (!name_val) name_val = obj.get("name");
    if (name_val) fil.name = name_val->as_string();

    const auto *et = obj.get("et");
    if (et && !trim_str(et->as_string()).empty()) {
        fil.nozzle_min = fil.nozzle_max = static_cast<int>(et->as_int64());
    }

    const auto *bt = obj.get("bt");
    if (bt && !trim_str(bt->as_string()).empty()) {
        fil.bed_min = fil.bed_max = static_cast<int>(bt->as_int64());
    }

    const auto *di = obj.get("di");
    if (di && !trim_str(di->as_string()).empty()) {
        fil.diameter_mm = di->as_double();
    }

    const auto *de = obj.get("de");
    if (de && !trim_str(de->as_string()).empty()) {
        fil.density = de->as_double();
    }

    const auto *sw = obj.get("sw");
    if (sw && !trim_str(sw->as_string()).empty()) {
        fil.spool_weight_g = static_cast<int>(sw->as_double());
    }

    const auto *an = obj.get("an");
    if (an) fil.sku = an->as_string();

    for (const auto &kv : obj.obj_val) {
        out.info[kv.first] = kv.second.as_string();
    }
    out.info["variant"] = "filaman";
    return true;
}

static bool parse_spoolscan(const json_util::Value &obj, DecodeResult &out) {
    out.recognized = true;
    out.format = "spoolman";
    Filament &fil = out.filament;

    const auto *brand = obj.get("brand");
    if (brand) fil.brand = brand->as_string();

    const auto *mat = obj.get("material");
    if (mat) {
        fil.material = mat->as_string();
        for (char &c : fil.material) c = toupper(static_cast<unsigned char>(c));
    }

    const auto *c_hex = obj.get("color_hex");
    if (c_hex) {
        fil.color_hex = clean_color(c_hex->as_string());
        if (!fil.color_hex.empty()) {
            fil.colors.push_back(fil.color_hex);
        }
    }

    const auto *pt = obj.get("print_temp");
    if (pt) {
        fil.nozzle_min = fil.nozzle_max = static_cast<int>(pt->as_int64());
    }

    const auto *wt = obj.get("weight_total");
    if (wt) {
        fil.weight_g = static_cast<int>(wt->as_double());
    }

    const auto *wr = obj.get("weight_remaining");
    if (wr) {
        fil.fields["remaining_g"] = wr->as_string();
    }

    for (const auto &kv : obj.obj_val) {
        out.info[kv.first] = kv.second.as_string();
    }
    out.info["variant"] = "spoolscan";
    return true;
}

bool SpoolmanCodec::decode(const Tag &tag, DecodeResult &out) {
    ndef::Tag ntag = to_ndef_tag(tag);
    ndef::NdefMessage msg;
    if (!ndef::read_message(ntag, msg)) {
        return false;
    }

    for (const auto &rec : msg.records) {
        std::string uri_str = rec.uri();
        if (uri_str.empty() && rec.is_text()) {
            std::string t = trim_str(rec.text());
            std::string t_lower = t;
            for (char &c : t_lower) c = tolower(static_cast<unsigned char>(c));
            if (t_lower.rfind("web+spoolman:", 0) == 0 ||
                t_lower.rfind("http://", 0) == 0 ||
                t_lower.rfind("https://", 0) == 0) {
                uri_str = t;
            }
        }
        if (!uri_str.empty()) {
            if (parse_uri(uri_str, out)) {
                return true;
            }
        }

        if (rec.is_text()) {
            if (parse_companion(rec.text(), out)) {
                return true;
            }
        }

        json_util::Value obj;
        bool is_json = false;
        if (rec.is_mime("application/json")) {
            std::string payload_str(rec.payload.begin(), rec.payload.end());
            if (json_util::parse(payload_str, obj) && obj.is_object()) {
                is_json = true;
            }
        } else if (rec.is_text()) {
            std::string t = trim_str(rec.text());
            if (!t.empty() && t.front() == '{' && t.back() == '}') {
                if (json_util::parse(t, obj) && obj.is_object()) {
                    is_json = true;
                }
            }
        }

        if (is_json) {
            const auto *prot = obj.get("protocol");
            if (prot && prot->as_string() == "openspool") {
                continue;
            }
            if (obj.get("sm_id")) {
                return parse_filaman(obj, out);
            }
            const auto *std_val = obj.get("standard");
            if (std_val && std_val->as_string() == "openprinttag") {
                return parse_spoolscan(obj, out);
            }
        }
    }

    return false;
}

bool SpoolmanCodec::encode(Tag &tag, const Filament &fields, std::string &err) {
    if (fields.spool_id.empty()) {
        err = "spool_id is required";
        return false;
    }
    if (!all_digits(fields.spool_id, 0, fields.spool_id.size())) {
        err = "spool_id must be a number";
        return false;
    }

    std::string text;
    auto it_fil = fields.fields.find("filament_id");
    if (it_fil != fields.fields.end() && !it_fil->second.empty()) {
        text = "SPOOL:" + fields.spool_id + "\nFILAMENT:" + it_fil->second + "\n";
    } else {
        text = "SPOOL:" + fields.spool_id + "\n";
    }

    ndef::NdefRecord rec = ndef::text_record(text, "en", &err);
    ndef::Tag ntag = to_ndef_tag(tag);
    std::vector<std::pair<size_t, std::vector<uint8_t>>> patches;
    if (!ndef::message_patches(ntag, {rec}, patches, &err)) {
        return false;
    }

    for (const auto &p : patches) {
        if (p.first + p.second.size() > tag.memory.size()) {
            err = "patch exceeds tag memory";
            return false;
        }
        std::memcpy(&tag.memory[p.first], p.second.data(), p.second.size());
        size_t b_start = p.first / tag.block_size;
        size_t b_end = (p.first + p.second.size() - 1) / tag.block_size;
        for (size_t b = b_start; b <= b_end && b < tag.readable.size(); ++b) {
            tag.readable[b] = true;
        }
    }
    return true;
}
