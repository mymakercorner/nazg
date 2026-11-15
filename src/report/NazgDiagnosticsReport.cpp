// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgDiagnosticsReport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <string_view>

#include "ui/NazgKeyHealthWords.h"

namespace nazg
{
    namespace
    {
        constexpr std::string_view c_ReadingsOpen  = "<script type=\"application/json\" id=\"nazg-readings\">";
        constexpr std::string_view c_ReadingsClose = "</script>";

        std::string Esc(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (const char c : text)
                switch (c)
                {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                default:  out += c; break;
                }
            return out;
        }

        // A number for SVG: one decimal at most.
        std::string N(double value)
        {
            char text[32];
            std::snprintf(text, sizeof(text), "%.1f", value);
            std::string out = text;
            if (out.size() > 2 && out.compare(out.size() - 2, 2, ".0") == 0)
                out.resize(out.size() - 2);
            return out;
        }

        std::string I(long long value) { return std::to_string(value); }

        // A time into the pass: 2:27.
        std::string Clock(double seconds)
        {
            const long long whole = std::max(0LL, std::llround(seconds));
            char            text[32];
            std::snprintf(text, sizeof(text), "%lld:%02lld", whole / 60, whole % 60);
            return text;
        }

        const char* ClassOf(KeyVerdict verdict)
        {
            switch (verdict)
            {
            case KeyVerdict::Failing: return "fail";
            case KeyVerdict::AtRisk:  return "risk";
            case KeyVerdict::ToCheck: return "check";
            case KeyVerdict::ToDo:    return "todo";
            case KeyVerdict::Good:    break;
            }
            return "good";
        }

        const char* TieOf(int left, const SafeMargins& margins)
        {
            return left <= 0 ? "fail" : left < margins.rest ? "risk" : "safe";
        }

        // What one report draws from: the input, and what is worked out once from it.
        class Page
        {
        public:
            explicit Page(const DiagnosticsReport& report) : r(report)
            {
                const size_t rows = r.info.details.controllerRows;
                levels.resize(r.keys.size());
                for (size_t k = 0; k < r.keys.size(); ++k)
                {
                    const ReportKey& key = r.keys[k];
                    if (!key.isMapped)
                        continue;
                    const size_t position = size_t{ key.controllerColumn } * rows + key.controllerRow;
                    if (position < r.traces.size())
                        for (const uint16_t level : r.traces[position])
                            levels[k].Add(level, key.threshold);
                }
            }

            std::string Html() const;

        private:
            const DiagnosticsReport& r;
            std::vector<ToolLevels>  levels;   // the tool's highest and lowest, per key

            const JudgedKey* Judged(size_t key) const { return key < r.health.keys.size() ? &r.health.keys[key] : nullptr; }
            KeyVerdict VerdictOf(size_t key) const { return Judged(key) ? Judged(key)->verdict : KeyVerdict::ToDo; }
            std::span<const uint16_t> TraceOf(size_t key) const;
            std::string MoverOf(size_t key) const;
            std::string Place(size_t key) const;
            std::string Tooltip(size_t key) const;
            const KeyCoupling* NamedCoupling(size_t key) const;

            std::string Style() const;
            std::string Script() const;
            std::string KeyboardPart() const;
            std::string Counts() const;
            std::string KeyText(size_t key, double x, double y, double w, double h, bool withLevels) const;
            std::string BoardSvg(bool withLevels) const;
            std::string GridSvg(bool withLevels) const;
            std::string BarsHeader() const;
            std::string Bar(size_t key) const;
            std::string List() const;
            std::string Dots() const;
            std::string FollowLine(size_t key) const;
            std::string Presses(size_t key) const;
            std::string MovedBy(size_t key) const;
            std::string Detail(size_t key) const;
            std::string Tables() const;
        };

        std::span<const uint16_t> Page::TraceOf(size_t key) const
        {
            const ReportKey& k        = r.keys[key];
            const size_t     position = size_t{ k.controllerColumn } * r.info.details.controllerRows + k.controllerRow;
            if (!k.isMapped || position >= r.traces.size())
                return {};
            return r.traces[position];
        }

        std::string Page::MoverOf(size_t key) const
        {
            const JudgedKey* judged = Judged(key);
            return judged && judged->movedBy && *judged->movedBy < r.keys.size() ? r.keys[*judged->movedBy].name : std::string();
        }

        std::string Page::Place(size_t key) const
        {
            const ReportKey& k    = r.keys[key];
            std::string      text = "row " + I(k.row) + ", column " + I(k.column);
            if (k.isMapped)
                text += " · connectors R" + I(k.controllerRow) + " C" + I(k.controllerColumn);
            return text;
        }

        // As the section's tooltip: name and place, verdict and reasons, the three levels.
        std::string Page::Tooltip(size_t key) const
        {
            const ReportKey& k      = r.keys[key];
            const JudgedKey* judged = Judged(key);
            std::string      text   = k.name + " · " + Place(key);
            if (r.info.thresholds.size() > 1)
                text += " · bin " + I(k.bin);
            if (judged)
            {
                text += "\n" + std::string(WordOf(judged->verdict));
                if (!judged->reasons.empty())
                    text += ": " + ReasonsOf(*judged, "; ", MoverOf(key));
                const KeyStats& st = judged->stats;
                text += "\nthreshold " + I(k.threshold) + " · at rest " + (st.restMedian ? I(*st.restMedian) : "-") +
                        " · firmest hold " + (st.pressMargin ? I(k.threshold + r.scale.direction * *st.pressMargin) : "-");
            }
            return text;
        }

        const KeyCoupling* Page::NamedCoupling(size_t key) const
        {
            const JudgedKey* judged = Judged(key);
            if (!judged || !judged->movedBy)
                return nullptr;
            for (const KeyCoupling& coupling : r.health.couplings)
                if (coupling.moved == key && coupling.pressed == *judged->movedBy)
                    return &coupling;
            return nullptr;
        }

        // --- The look ------------------------------------------------------------------------------

        std::string Page::Style() const
        {
            return R"CSS(
:root{--window:#f3f3f5;--text:#1d1e22;--muted:#6b6e78;--control:#ffffff;--border:#c8cad2;--accent:#3b82f6;--panel:#f8f8fa;
--plate:#d3d4d8;--key:#ffffff;--keyline:#b4b7c2;--ok:#15803d;--warn:#c2410c;--err:#dc2626;--press:#3b82f6;--grid:#d9dbe1;
--risk:#fde2cc;--fail:#fbd0d0;--check:#e9ddfb;--checkline:#8b5cf6;--doc:#ffffff;--selected:rgba(59,130,246,.16);color-scheme:light}
@media screen and (prefers-color-scheme:dark){:root:not([data-theme=light]){--window:#0f0f11;--text:#ececf1;--muted:#80838f;--control:#20232b;
--border:#3a3d48;--accent:#4296fa;--panel:#16171b;--plate:#1c1d23;--key:#3a3c48;--keyline:#4b4e5c;--ok:#4ade80;--warn:#fb923c;--err:#f87171;
--press:#4296fa;--grid:#2c2e36;--risk:#5a3214;--fail:#5c1d1d;--check:#33255a;--checkline:#a78bfa;--doc:#18191d;--selected:rgba(66,150,250,.22);color-scheme:dark}}
@media screen{:root[data-theme=dark]{--window:#0f0f11;--text:#ececf1;--muted:#80838f;--control:#20232b;--border:#3a3d48;--accent:#4296fa;
--panel:#16171b;--plate:#1c1d23;--key:#3a3c48;--keyline:#4b4e5c;--ok:#4ade80;--warn:#fb923c;--err:#f87171;--press:#4296fa;--grid:#2c2e36;
--risk:#5a3214;--fail:#5c1d1d;--check:#33255a;--checkline:#a78bfa;--doc:#18191d;--selected:rgba(66,150,250,.22);color-scheme:dark}}
*{box-sizing:border-box}
body{margin:0;background:var(--window);color:var(--text);font-family:Arimo,"Segoe UI",Helvetica,Arial,sans-serif;font-size:13px}
.doc{margin:16px auto;max-width:1180px;background:var(--doc);border:1px solid var(--border);border-radius:8px;padding:28px 36px 40px;line-height:1.5}
h1{font-size:22px;margin:0}h2{font-size:16px;margin:26px 0 8px;padding-bottom:4px;border-bottom:1px solid var(--border)}
h3{font-size:14px;margin:18px 0 6px}
.titlebar{display:flex;align-items:center;gap:12px}.titlebar .seg{margin-left:auto}
.sub,.muted{color:var(--muted)}.sub{margin:2px 0 14px}
button{font:inherit;padding:2px 10px;border-radius:6px;border:1px solid var(--border);background:var(--control);color:var(--text);cursor:pointer}
button.on{background:var(--accent);border-color:var(--accent);color:#fff}
.seg{display:inline-flex}.seg button{border-radius:0;margin-left:-1px}.seg button:first-child{border-radius:6px 0 0 6px;margin-left:0}
.seg button:last-child{border-radius:0 6px 6px 0}
dl{display:grid;grid-template-columns:180px 1fr;gap:2px 12px;margin:0}dt{color:var(--muted)}dd{margin:0}
.tools{display:flex;align-items:center;gap:10px;margin:8px 0;flex-wrap:wrap}.tools .right{margin-left:auto;display:flex;gap:8px;align-items:center}
.counts{display:flex;gap:16px;flex-wrap:wrap}.chip{display:inline-flex;align-items:center;gap:5px}
.chip i{width:11px;height:11px;border-radius:3px;display:inline-block;border:1.5px solid}
.figure{display:flex;justify-content:center;margin:8px 0;overflow-x:auto}.caption{text-align:center;color:var(--muted);margin:2px 0 10px}
svg{display:block;overflow:visible}
.plate{fill:var(--plate)}.cap{fill:var(--key);stroke:var(--keyline)}.empty .cap{fill:none;stroke-dasharray:3 3;opacity:.55}
.key.risk .cap{fill:var(--risk);stroke:var(--warn);stroke-width:2}.key.fail .cap{fill:var(--fail);stroke:var(--err);stroke-width:2}
.key.check .cap{fill:var(--check);stroke:var(--checkline);stroke-width:1.5}.key.todo .cap{fill:url(#hatch);stroke:var(--muted);stroke-width:1.5}
.key[data-key]{cursor:pointer}.key.sel .cap{stroke:var(--text);stroke-width:3}
.key.tie-safe .cap{stroke:var(--muted);stroke-width:2.5;stroke-dasharray:5 3}.key.tie-risk .cap{stroke:var(--warn);stroke-width:2.5;stroke-dasharray:5 3}
.key.tie-fail .cap{stroke:var(--err);stroke-width:2.5;stroke-dasharray:5 3}
.lg{font:11px Arimo,Arial,sans-serif;fill:var(--text);text-anchor:middle;pointer-events:none}
.lv{font-family:Arimo,Arial,sans-serif;fill:var(--text);pointer-events:none;font-variant-numeric:tabular-nums}.lv.ex{fill:var(--muted)}.lv.bn{fill:var(--ok)}
.nm{font-family:Arimo,Arial,sans-serif;fill:var(--text);font-weight:600;pointer-events:none}
.ruler{font:11px Arimo,Arial,sans-serif;fill:var(--muted);text-anchor:middle}.ruler.flag{fill:var(--warn);font-weight:700}
.panel{border:1px solid var(--border);border-radius:6px;background:var(--panel);margin-top:10px}
.split{display:flex;gap:0}.split .left{flex:0 0 52%;padding:8px 12px;border-right:1px solid var(--grid);min-width:0}
.split .right{flex:1;padding:8px 12px;min-width:0;overflow-x:auto}
.row{display:flex;gap:8px;align-items:center;padding:2px 4px;border-radius:5px}.row[data-key]{cursor:pointer}.row.sel{background:var(--selected)}
.row .kn{min-width:150px;font-weight:600}.why{font-size:12px;color:var(--muted);margin:-2px 0 6px 162px;line-height:1.4}
.note{color:var(--warn);margin:4px 0}.todo-line{color:var(--muted);margin:6px 0}
.v-fail{color:var(--err)}.v-risk{color:var(--warn)}.v-check{color:var(--checkline)}.v-todo,.v-good{color:var(--muted)}
.follow{display:flex;align-items:center;gap:18px;padding:6px 0 8px;flex-wrap:wrap}
.follow .key-name{font-weight:700;font-size:18px;min-width:120px}.follow .place{display:block;font-size:11px;font-weight:400;color:var(--muted)}
.follow .num{display:flex;flex-direction:column;font-variant-numeric:tabular-nums}.follow .num b{font-size:18px;font-weight:600}
.follow .num span{font-size:11px;color:var(--muted)}
.detail{margin-bottom:12px}.detail .say{margin:4px 0}.detail h4{margin:10px 0 2px;font-size:13px}
#details{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.nojs #details .detail{border:1px solid var(--border);border-radius:6px;background:var(--panel);padding:8px 12px;break-inside:avoid}
.js #details{display:block}.js .detail:not(.on){display:none}.js .view:not(.on){display:none}.nojs .js-only{display:none}.js .nojs-only{display:none}
table{border-collapse:collapse;width:100%;font-size:12.5px}th{text-align:left;color:var(--muted);font-weight:600;border-bottom:1px solid var(--border);padding:4px 8px}
td{border-bottom:1px solid var(--grid);padding:4px 8px;vertical-align:top}td.n,th.n{text-align:right;font-variant-numeric:tabular-nums}
details{margin:12px 0}summary{cursor:pointer;color:var(--accent)}code{font-size:12px}
@media print{body{background:#fff}.doc{border:none;margin:0;max-width:none}.js-only{display:none!important}.view,.detail{display:block!important}
#details{display:block!important}.detail{break-inside:avoid}details{display:block}}
)CSS";
        }

        // Draws nothing: shows and hides what is drawn.
        std::string Page::Script() const
        {
            return R"JS(
(function () {
  var root = document.documentElement;
  root.classList.remove('nojs'); root.classList.add('js');
  function all(s) { return Array.prototype.slice.call(document.querySelectorAll(s)); }
  var theme = window.matchMedia && matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
  var levels = false, view = 'board';
  function show() {
    root.setAttribute('data-theme', theme);
    all('[data-theme-set]').forEach(function (b) { b.classList.toggle('on', b.getAttribute('data-theme-set') === theme); });
    all('[data-view-set]').forEach(function (b) { b.classList.toggle('on', b.getAttribute('data-view-set') === view); });
    all('[data-act=levels]').forEach(function (b) { b.classList.toggle('on', levels); });
    all('.view').forEach(function (v) {
      v.classList.toggle('on', v.getAttribute('data-view') === view && v.getAttribute('data-levels') === (levels ? 'on' : 'off'));
    });
  }
  var pane = document.getElementById('detail-pane'), details = document.getElementById('details');
  if (pane && details) pane.appendChild(details);
  function select(key) {
    all('.key.sel, .row.sel').forEach(function (e) { e.classList.remove('sel'); });
    all('.key').forEach(function (e) { e.classList.remove('tie-safe', 'tie-risk', 'tie-fail'); });
    all('.key[data-key="' + key + '"], .row[data-key="' + key + '"]').forEach(function (e) { e.classList.add('sel'); });
    all('.detail').forEach(function (d) { d.classList.toggle('on', d.id === 'key-' + key); });
    var detail = document.getElementById('key-' + key);
    var moves = detail ? (detail.getAttribute('data-moves') || '') : '';
    moves.split(',').forEach(function (m) {
      var p = m.split(':');
      if (p.length === 2) all('.key[data-key="' + p[0] + '"]').forEach(function (e) { e.classList.add('tie-' + p[1]); });
    });
  }
  document.addEventListener('click', function (e) {
    var t = e.target.closest('[data-key], [data-act], [data-view-set], [data-theme-set]');
    if (!t) return;
    if (t.hasAttribute('data-key')) select(t.getAttribute('data-key'));
    if (t.getAttribute('data-act') === 'levels') levels = !levels;
    if (t.hasAttribute('data-view-set')) view = t.getAttribute('data-view-set');
    if (t.hasAttribute('data-theme-set')) theme = t.getAttribute('data-theme-set');
    show();
  });
  show();
  select(root.getAttribute('data-first'));
})();
)JS";
        }

        // --- The keyboard ----------------------------------------------------------------------------

        std::string Page::KeyboardPart() const
        {
            const leydenjar::ControllerInfo& info = r.info;
            const leydenjar::Details&        d    = info.details;
            std::string thresholds, references;
            for (size_t bin = 0; bin < info.thresholds.size(); ++bin)
                thresholds += (bin ? ", " : "") + I(info.thresholds[bin]);
            for (size_t bin = 0; bin < info.referenceLevels.size(); ++bin)
                references += (bin ? ", " : "") + I(info.referenceLevels[bin]);

            std::string h = "<h2>The keyboard</h2><dl>";
            h += "<dt>Keyboard</dt><dd>" + Esc(r.title) + "</dd>";
            if (!r.usb.empty())
                h += "<dt>USB</dt><dd>" + Esc(r.usb) + "</dd>";
            h += "<dt>Controller</dt><dd>Leyden Jar, protocol " + I(info.version.major) + "." + I(info.version.mid) + "." +
                 I(info.version.minor) + ", " + (d.technology == leydenjar::SwitchTechnology::BeamSpring ? "Beam Spring" : "Model F") +
                 (info.isLeftHalf ? "" : ", right half") + "</dd>";
            h += "<dt>Matrix</dt><dd>QMK " + I(d.logicalRows) + " &times; " + I(d.logicalColumns) + ", controller " +
                 I(d.controllerRows) + " rows &times; " + I(d.controllerColumns) + " columns</dd>";
            h += "<dt>Bins</dt><dd>" + I(static_cast<long long>(info.thresholds.size())) + ": thresholds " + thresholds +
                 (references.empty() ? "" : ", resting references " + references) + "</dd></dl>";
            return h;
        }

        std::string Page::Counts() const
        {
            size_t counts[5] = {};
            for (const JudgedKey& judged : r.health.keys)
                ++counts[static_cast<int>(judged.verdict)];
            const auto chip = [&](KeyVerdict verdict, const char* face, const char* line)
            {
                return "<span class=\"chip\"><i style=\"background:" + std::string(face) + ";border-color:" + line + "\"></i>" +
                       I(static_cast<long long>(counts[static_cast<int>(verdict)])) + " " + WordOf(verdict) + "</span>";
            };
            std::string h = "<span class=\"counts\">" + chip(KeyVerdict::Good, "var(--key)", "var(--keyline)") +
                            chip(KeyVerdict::AtRisk, "var(--risk)", "var(--warn)") + chip(KeyVerdict::Failing, "var(--fail)", "var(--err)");
            if (counts[static_cast<int>(KeyVerdict::ToCheck)] > 0)
                h += chip(KeyVerdict::ToCheck, "var(--check)", "var(--checkline)");
            h += chip(KeyVerdict::ToDo, "repeating-linear-gradient(45deg,var(--muted) 0 1.5px,var(--key) 1.5px 4px)", "var(--muted)");
            return h + "</span>";
        }

        // --- The board and the controller's grid -----------------------------------------------------

        // With Levels: the name on the first line; the level at rest in bold, the highest at its
        // right; the bin, the lowest at its right (Rico, 2026-11-07) -- at rest where the section has now.
        std::string Page::KeyText(size_t key, double x, double y, double w, double h, bool withLevels) const
        {
            const ReportKey& k = r.keys[key];
            if (!withLevels)
                return "<text class=\"lg\" x=\"" + N(x + w / 2) + "\" y=\"" + N(y + h / 2 + 4) + "\">" + Esc(k.name) + "</text>";

            const double pad = 3.5, fs = 8.5, row = (h - 2 * pad) / 3;
            const auto   t   = [&](const char* cls, double tx, double ty, std::optional<int> value, bool end, bool bold)
            {
                if (!value)
                    return std::string();
                return "<text class=\"lv " + std::string(cls) + "\" x=\"" + N(tx) + "\" y=\"" + N(ty) + "\" style=\"font-size:" + N(fs) +
                       "px" + (bold ? ";font-weight:700" : "") + "\"" + (end ? " text-anchor=\"end\"" : "") + ">" + I(*value) + "</text>";
            };
            const JudgedKey*   judged = Judged(key);
            std::optional<int> rest, highest, lowest, bin;
            if (judged && judged->stats.restMedian)
                rest = *judged->stats.restMedian;
            if (levels[key].Max())
                highest = *levels[key].Max();
            if (levels[key].Min())
                lowest = *levels[key].Min();
            if (r.info.thresholds.size() > 1)
                bin = k.bin;
            return "<text class=\"nm\" x=\"" + N(x + pad) + "\" y=\"" + N(y + pad + row * 0.8) + "\" style=\"font-size:" + N(fs) + "px\">" +
                   Esc(k.name) + "</text>" + t("", x + pad, y + pad + row * 1.85, rest, false, true) +
                   t("ex", x + w - pad, y + pad + row * 1.85, highest, true, false) + t("bn", x + pad, y + h - pad, bin, false, false) +
                   t("ex", x + w - pad, y + h - pad, lowest, true, false);
        }

        std::string HatchPattern()
        {
            return "<defs><pattern id=\"hatch\" width=\"6\" height=\"6\" patternUnits=\"userSpaceOnUse\" patternTransform=\"rotate(45)\">"
                   "<rect width=\"6\" height=\"6\" fill=\"var(--key)\"/><line x1=\"0\" y1=\"0\" x2=\"0\" y2=\"6\" stroke=\"var(--muted)\" "
                   "stroke-width=\"2\" opacity=\".55\"/></pattern></defs>";
        }

        // A key's corners, rotated as drawn: for the board's bounds.
        void Extend(const via::DefinitionKey& g, double x, double y, double w, double h, double bounds[4])
        {
            const double radians = g.rotation * 3.14159265358979 / 180.0, c = std::cos(radians), s = std::sin(radians);
            for (const auto& [px, py] : { std::pair{ x, y }, { x + w, y }, { x, y + h }, { x + w, y + h } })
            {
                const double dx = px - g.rotationX, dy = py - g.rotationY;
                const double rx = g.rotation == 0.0f ? px : g.rotationX + dx * c - dy * s;
                const double ry = g.rotation == 0.0f ? py : g.rotationY + dx * s + dy * c;
                bounds[0] = std::min(bounds[0], rx);
                bounds[1] = std::min(bounds[1], ry);
                bounds[2] = std::max(bounds[2], rx);
                bounds[3] = std::max(bounds[3], ry);
            }
        }

        std::string Page::BoardSvg(bool withLevels) const
        {
            const double u = withLevels ? 46 : 40, margin = u * 0.35, gap = std::max(1.0, u * 0.06);
            double bounds[4] = { 1e9, 1e9, -1e9, -1e9 };
            for (const ReportShape& shape : r.shapes)
            {
                const via::DefinitionKey& g = shape.geometry;
                Extend(g, g.x, g.y, g.width, g.height, bounds);
                if (g.secondWidth > 0 && g.secondHeight > 0)
                    Extend(g, g.x + g.secondX, g.y + g.secondY, g.secondWidth, g.secondHeight, bounds);
            }
            if (r.shapes.empty())
                return {};
            const double width = (bounds[2] - bounds[0]) * u + 2 * margin, height = (bounds[3] - bounds[1]) * u + 2 * margin;

            std::string s = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + N(width) + "\" height=\"" + N(height) + "\">" + HatchPattern() +
                            "<rect class=\"plate\" width=\"100%\" height=\"100%\" rx=\"12\"/><g transform=\"translate(" +
                            N(margin - bounds[0] * u) + " " + N(margin - bounds[1] * u) + ")\">";
            for (const ReportShape& shape : r.shapes)
            {
                const via::DefinitionKey& g = shape.geometry;
                const std::string cls = shape.key ? ClassOf(VerdictOf(*shape.key)) : "plain";
                s += "<g class=\"key " + cls + "\"" + (shape.key ? " data-key=\"" + I(static_cast<long long>(*shape.key)) + "\"" : "") + ">";
                if (g.rotation != 0.0f)
                    s += "<g transform=\"rotate(" + N(g.rotation) + " " + N(g.rotationX * u) + " " + N(g.rotationY * u) + ")\">";
                const double x = g.x * u + gap, y = g.y * u + gap, w = g.width * u - 2 * gap, h = g.height * u - 2 * gap;
                const double rx = std::max(2.0, u * 0.1);
                s += "<rect class=\"cap\" x=\"" + N(x) + "\" y=\"" + N(y) + "\" width=\"" + N(w) + "\" height=\"" + N(h) + "\" rx=\"" + N(rx) + "\"/>";
                if (g.secondWidth > 0 && g.secondHeight > 0)
                    s += "<rect class=\"cap\" x=\"" + N((g.x + g.secondX) * u + gap) + "\" y=\"" + N((g.y + g.secondY) * u + gap) +
                         "\" width=\"" + N(g.secondWidth * u - 2 * gap) + "\" height=\"" + N(g.secondHeight * u - 2 * gap) + "\" rx=\"" +
                         N(rx) + "\"/>";
                if (shape.key)
                    s += "<title>" + Esc(Tooltip(*shape.key)) + "</title>" + KeyText(*shape.key, x, y, w, h, withLevels);
                else if (!shape.legend.empty())
                    s += "<text class=\"lg\" x=\"" + N(x + w / 2) + "\" y=\"" + N(y + h / 2 + 4) + "\">" + Esc(shape.legend) + "</text>";
                if (g.rotation != 0.0f)
                    s += "</g>";
                s += "</g>";
            }
            return s + "</g></svg>";
        }

        // The controller's connectors, its columns across, its rows down; a weak column's ruler flagged.
        std::string Page::GridSvg(bool withLevels) const
        {
            const leydenjar::Details& d = r.info.details;
            const double cw = withLevels ? 58 : 50, ch = withLevels ? 48 : 34, lab = 30;
            std::vector<std::optional<size_t>> at(size_t{ d.controllerColumns } * d.controllerRows);
            for (size_t k = 0; k < r.keys.size(); ++k)
                if (r.keys[k].isMapped)
                    at[size_t{ r.keys[k].controllerColumn } * d.controllerRows + r.keys[k].controllerRow] = k;

            std::string s = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + N(lab + d.controllerColumns * cw + 8) + "\" height=\"" +
                            N(lab + d.controllerRows * ch + 8) + "\">" + HatchPattern() + "<rect class=\"plate\" width=\"100%\" height=\"100%\" rx=\"10\"/>";
            for (int column = 0; column < d.controllerColumns; ++column)
            {
                bool weak = false;
                for (const WeakColumn& w : r.health.columns)
                    weak = weak || w.column == column;
                s += "<text class=\"ruler" + std::string(weak ? " flag" : "") + "\" x=\"" + N(lab + column * cw + cw / 2) + "\" y=\"" +
                     N(lab - 10) + "\">C" + I(column) + "</text>";
            }
            for (int row = 0; row < d.controllerRows; ++row)
                s += "<text class=\"ruler\" x=\"" + N(lab / 2 + 2) + "\" y=\"" + N(lab + row * ch + ch / 2 + 4) + "\">R" + I(row) + "</text>";
            for (int row = 0; row < d.controllerRows; ++row)
                for (int column = 0; column < d.controllerColumns; ++column)
                {
                    const std::optional<size_t> key = at[size_t(column) * d.controllerRows + row];
                    const double x = lab + column * cw + 2, y = lab + row * ch + 2, w = cw - 4, h = ch - 4;
                    if (!key)
                    {
                        s += "<g class=\"key empty\"><rect class=\"cap\" x=\"" + N(x) + "\" y=\"" + N(y) + "\" width=\"" + N(w) + "\" height=\"" +
                             N(h) + "\" rx=\"4\"/></g>";
                        continue;
                    }
                    s += "<g class=\"key " + std::string(ClassOf(VerdictOf(*key))) + "\" data-key=\"" + I(static_cast<long long>(*key)) +
                         "\"><rect class=\"cap\" x=\"" + N(x) + "\" y=\"" + N(y) + "\" width=\"" + N(w) + "\" height=\"" + N(h) +
                         "\" rx=\"4\"/><title>" + Esc(Tooltip(*key)) + "</title>" + KeyText(*key, x, y, w, h, withLevels) + "</g>";
                }
            return s + "</svg>";
        }

        // --- The list ------------------------------------------------------------------------------

        // The bars' geometry, as the section's (BarScaleOf()).
        struct Scale
        {
            double x0 = 110, perLevel = 1;
            double At(double x) const { return std::clamp(x, 4.0, 296.0); }
            double Rest(double margin) const { return At(x0 - margin * perLevel); }
            double Press(double margin) const { return At(x0 + margin * perLevel); }
        };

        Scale ScaleOf(int offset, int typical)
        {
            const double rest = std::max(1, offset) * 1.6, press = std::max(1, typical) * 1.15;
            Scale scale;
            scale.perLevel = (300.0 - 18.0) / (rest + press);
            scale.x0       = 8.0 + rest * scale.perLevel;
            return scale;
        }

        std::string Page::BarsHeader() const
        {
            const Scale  scale = ScaleOf(r.scale.offset, r.health.typical);
            const double x0 = scale.x0, xu = scale.Press(std::max(1, r.health.typical));
            bool rings = false;
            for (const JudgedKey& judged : r.health.keys)
                rings = rings || judged.movedBy.has_value();
            std::string s = "<div class=\"row\"><span class=\"kn\"></span><svg width=\"300\" height=\"36\" style=\"font:11px Arimo,Arial,sans-serif\">"
                            "<circle cx=\"" + N(x0 - 56) + "\" cy=\"8\" r=\"4\" fill=\"var(--muted)\"/><text x=\"" + N(x0 - 8) +
                            "\" y=\"12\" text-anchor=\"end\" fill=\"var(--muted)\">at rest</text><text x=\"" + N(x0 + 8) +
                            "\" y=\"12\" fill=\"var(--press)\">pressed</text><circle cx=\"" + N(x0 + 62) + "\" cy=\"8\" r=\"4.5\" fill=\"var(--press)\"/>"
                            "<line x1=\"" + N(x0) + "\" x2=\"" + N(x0) + "\" y1=\"2\" y2=\"16\" stroke=\"var(--grid)\"/><text x=\"" + N(x0) +
                            "\" y=\"27\" text-anchor=\"middle\" fill=\"var(--text)\">threshold</text><line x1=\"" + N(x0) + "\" x2=\"" + N(x0) +
                            "\" y1=\"30\" y2=\"35\" stroke=\"var(--text)\" stroke-width=\"1.5\"/><text x=\"" + N(xu) +
                            "\" y=\"27\" text-anchor=\"middle\" fill=\"var(--muted)\">most keys</text><line x1=\"" + N(xu) + "\" x2=\"" + N(xu) +
                            "\" y1=\"30\" y2=\"35\" stroke=\"var(--muted)\" stroke-dasharray=\"2 2\"/>";
            if (rings)
                s += "<circle cx=\"" + N(std::max(10.0, x0 - 108)) + "\" cy=\"23\" r=\"4\" fill=\"var(--doc)\" stroke=\"var(--warn)\" stroke-width=\"2\"/>"
                     "<text x=\"" + N(std::max(18.0, x0 - 100)) + "\" y=\"27\" fill=\"var(--warn)\">another key</text>";
            return s + "</svg></div>";
        }

        std::string Page::Bar(size_t key) const
        {
            const Scale      scale  = ScaleOf(r.scale.offset, r.health.typical);
            const JudgedKey& judged = r.health.keys[key];
            const KeyStats&  st     = judged.stats;
            const double     cy     = 8;
            std::string s = "<svg width=\"300\" height=\"16\"><line x1=\"4\" x2=\"296\" y1=\"8\" y2=\"8\" stroke=\"var(--grid)\" stroke-width=\"4\" "
                            "stroke-linecap=\"round\"/>";
            const double xu = scale.Press(std::max(1, r.health.typical));
            s += "<line x1=\"" + N(xu) + "\" x2=\"" + N(xu) + "\" y1=\"3\" y2=\"13\" stroke=\"var(--muted)\" stroke-dasharray=\"2 2\"/><line x1=\"" +
                 N(scale.x0) + "\" x2=\"" + N(scale.x0) + "\" y1=\"0\" y2=\"16\" stroke=\"var(--text)\" stroke-width=\"1.5\"/>";
            if (st.medianMargin)
            {
                const double a = scale.Rest(*st.medianMargin), spread = std::min(scale.x0 - 4, st.restSpread * scale.perLevel);
                s += "<rect x=\"" + N(a - spread / 2) + "\" y=\"5\" width=\"" + N(std::max(2.0, spread)) +
                     "\" height=\"6\" rx=\"3\" fill=\"var(--muted)\" opacity=\".35\"/>";
                std::optional<double> p;
                if (st.pressMedianMargin)
                {
                    const double x1 = scale.Press(st.pressBand->first), x2 = scale.Press(st.pressBand->second);
                    s += "<rect x=\"" + N(std::min(x1, x2)) + "\" y=\"5\" width=\"" + N(std::max(2.0, std::abs(x2 - x1))) +
                         "\" height=\"6\" rx=\"3\" fill=\"var(--press)\" opacity=\".3\"/>";
                    p = scale.Press(*st.pressMedianMargin);
                }
                else if (st.closest)
                {
                    p = scale.Press(-*st.closest);
                }
                if (p)
                {
                    const char* line = judged.verdict == KeyVerdict::Failing ? "var(--err)" : judged.verdict == KeyVerdict::AtRisk ? "var(--warn)" : "var(--press)";
                    s += "<line x1=\"" + N(a) + "\" x2=\"" + N(*p) + "\" y1=\"8\" y2=\"8\" stroke=\"" + line + "\" stroke-width=\"2\"/>";
                    s += st.pressMedianMargin
                             ? "<circle cx=\"" + N(*p) + "\" cy=\"8\" r=\"4.5\" fill=\"var(--press)\"><title>Pressed: usually " +
                                   I(*st.pressMedianMargin) + " past its threshold -- most keys " + I(r.health.typical) + "</title></circle>"
                             : "<circle cx=\"" + N(*p) + "\" cy=\"8\" r=\"4.5\" fill=\"var(--doc)\" stroke=\"var(--err)\" stroke-width=\"2\"/>";
                }
                if (const KeyCoupling* coupling = NamedCoupling(key))
                    s += "<circle cx=\"" + N(scale.Rest(coupling->left)) + "\" cy=\"8\" r=\"5\" fill=\"var(--doc)\" stroke=\"var(--" +
                         (coupling->left <= 0 ? "err" : "warn") + ")\" stroke-width=\"2.5\"><title>At rest while " + Esc(r.keys[coupling->pressed].name) +
                         " is pressed: " + (coupling->left <= 0 ? "past its threshold by " + I(-coupling->left) : I(coupling->left) + " from its threshold") +
                         "</title></circle>";
                s += "<circle cx=\"" + N(a) + "\" cy=\"8\" r=\"4\" fill=\"var(--muted)\"><title>At rest: " + I(*st.medianMargin) +
                     " from its threshold</title></circle>";
            }
            else if (st.stuck)
            {
                s += "<circle cx=\"" + N(scale.Press(r.health.typical * 0.8)) + "\" cy=\"8\" r=\"4.5\" fill=\"var(--err)\"/>";
            }
            (void)cy;
            return s + "</svg>";
        }

        // As the section's list (ListRows()): failing, a weak column's row, at risk and to check, the
        // weakest first -- the reasons written under each, nothing hovering on paper -- then the to-dos.
        std::string Page::List() const
        {
            const BoardHealth& health = r.health;
            std::vector<int>   weak;
            for (const WeakColumn& column : health.columns)
                weak.push_back(column.column);
            std::vector<size_t> loose;
            for (size_t k = 0; k < health.keys.size(); ++k)
            {
                const JudgedKey& judged = health.keys[k];
                const bool inWeak = judged.verdict == KeyVerdict::AtRisk && judged.reasons == std::vector<KeyReason>{ KeyReason::OnlyJustPast } &&
                                    std::find(weak.begin(), weak.end(), r.keys[k].controllerColumn) != weak.end();
                if (judged.verdict != KeyVerdict::Good && judged.verdict != KeyVerdict::ToDo && !inWeak)
                    loose.push_back(k);
            }
            std::stable_sort(loose.begin(), loose.end(), [&](size_t a, size_t b)
            {
                const JudgedKey& x = health.keys[a];
                const JudgedKey& y = health.keys[b];
                return x.verdict != y.verdict ? static_cast<int>(x.verdict) < static_cast<int>(y.verdict) : x.health < y.health;
            });

            std::string h = "<h3>The keys to look at <span class=\"muted\" style=\"font-weight:400\">the weakest first</span></h3>";
            for (const BoardNote& note : health.notes)
                h += "<div class=\"note\">" + I(static_cast<long long>(note.keys)) + " of " + I(static_cast<long long>(health.judged)) +
                     " keys " + Esc(PluralOf(note.reason)) + ".</div>";
            if (!loose.empty() || !health.columns.empty())
                h += BarsHeader();
            const auto keyRow = [&](size_t k)
            {
                const JudgedKey& judged = health.keys[k];
                return "<div class=\"row\" data-key=\"" + I(static_cast<long long>(k)) + "\"><span class=\"kn v-" + ClassOf(judged.verdict) + "\">" +
                       Esc(r.keys[k].name) + "</span>" + Bar(k) + "</div><div class=\"why\">" + Esc(ReasonsOf(judged, ". ", MoverOf(k))) + ".</div>";
            };
            for (size_t k : loose)
                if (health.keys[k].verdict == KeyVerdict::Failing)
                    h += keyRow(k);
            for (const WeakColumn& column : health.columns)
                h += "<div class=\"row\"><span class=\"kn v-risk\">Column C" + I(column.column) + " · " + I(static_cast<long long>(column.keys)) +
                     " keys</span><span class=\"muted\">its keys register more weakly than the rest of the keyboard</span></div>";
            for (size_t k : loose)
                if (health.keys[k].verdict != KeyVerdict::Failing)
                    h += keyRow(k);

            const std::pair<KeyToDo, const char*> kinds[] = { { KeyToDo::PressAgain, "Press again, firmly" },
                                                               { KeyToDo::Hold, "Hold a moment to measure" },
                                                               { KeyToDo::Press, "Not pressed" } };
            bool anyToDo = false;
            for (const auto& [kind, words] : kinds)
            {
                std::string names;
                for (size_t k = 0; k < health.keys.size(); ++k)
                    if (health.keys[k].verdict == KeyVerdict::ToDo && health.keys[k].toDo == kind)
                        names += (names.empty() ? "" : ", ") + Esc(r.keys[k].name);
                if (names.empty())
                    continue;
                anyToDo = true;
                h += "<div class=\"todo-line\"><b>" + std::string(words) + "</b>: " + names + ".</div>";
            }
            if (loose.empty() && health.columns.empty() && !anyToDo)
                h += "<div class=\"muted\">No key looks at risk.</div>";
            return h;
        }

        // One dot per key, at rest and pressed, the threshold between (DrawWholeBoard()).
        std::string Page::Dots() const
        {
            const double W = 480, left = 62, right = W, rowH = 46, inset = 8;
            double restMax = r.scale.offset * 1.6, pressMax = r.health.typical * 1.6;
            for (const JudgedKey& judged : r.health.keys)
            {
                restMax  = std::max(restMax, double(judged.stats.medianMargin.value_or(0)));
                pressMax = std::max(pressMax, double(judged.stats.pressMargin.value_or(0)));
            }
            restMax  = std::max(restMax, 1.0);
            pressMax = std::max(pressMax, 1.0);
            const double per = (right - left - 2 * inset) / (restMax + pressMax), x0 = left + inset + restMax * per;
            const auto xr = [&](double m) { return std::clamp(x0 - m * per, left + inset, right - inset); };
            const auto xp = [&](double m) { return std::clamp(x0 + m * per, left + inset, right - inset); };
            const double top = 18, yR = top + rowH / 2, yP = top + rowH + 10 + rowH / 2, bottom = yP + rowH / 2;

            std::string s = "<h3>The whole board <span class=\"muted\" style=\"font-weight:400\">one dot per key</span></h3>"
                            "<svg width=\"" + N(W) + "\" height=\"" + N(bottom + 22) + "\" style=\"font:11px Arimo,Arial,sans-serif\">"
                            "<rect x=\"" + N(xr(r.margins.rest)) + "\" y=\"" + N(yR - rowH / 2) + "\" width=\"" + N(x0 - xr(r.margins.rest)) +
                            "\" height=\"" + N(rowH) + "\" fill=\"var(--risk)\"/><rect x=\"" + N(x0) + "\" y=\"" + N(yP - rowH / 2) + "\" width=\"" +
                            N(xp(r.margins.press) - x0) + "\" height=\"" + N(rowH) + "\" fill=\"var(--risk)\"/><line x1=\"" + N(x0) + "\" x2=\"" + N(x0) +
                            "\" y1=\"" + N(top) + "\" y2=\"" + N(bottom + 4) + "\" stroke=\"var(--err)\" stroke-width=\"1.5\"/><text x=\"" + N(x0) +
                            "\" y=\"" + N(bottom + 17) + "\" text-anchor=\"middle\" fill=\"var(--err)\">threshold</text><text x=\"0\" y=\"" + N(yR + 4) +
                            "\" fill=\"var(--muted)\">at rest</text><text x=\"0\" y=\"" + N(yP + 4) + "\" fill=\"var(--muted)\">pressed</text>"
                            "<text x=\"" + N(x0 - 8) + "\" y=\"11\" text-anchor=\"end\" fill=\"var(--muted)\">&lt;- further from it is safer</text>"
                            "<text x=\"" + N(x0 + 8) + "\" y=\"11\" fill=\"var(--muted)\">further past it is safer -&gt;</text>";
            for (size_t k = 0; k < r.health.keys.size(); ++k)
            {
                const JudgedKey& judged = r.health.keys[k];
                const bool  good   = judged.verdict == KeyVerdict::Good;
                const double jitter = (double(int(k * 37 % 13)) - 6) * 2.6 * (good ? 1.0 : 0.4);
                const std::string colour = good ? "var(--muted)" : std::string("var(--") +
                                          (judged.verdict == KeyVerdict::Failing ? "err" : judged.verdict == KeyVerdict::AtRisk ? "warn" :
                                           judged.verdict == KeyVerdict::ToCheck ? "checkline" : "muted") + ")";
                const auto dot = [&](double x, double y, bool hollow)
                {
                    return "<circle data-key=\"" + I(static_cast<long long>(k)) + "\" cx=\"" + N(x) + "\" cy=\"" + N(y + jitter) + "\" r=\"" +
                           (good ? "3" : "4.5") + "\" fill=\"" + (hollow ? "var(--doc)" : colour) + "\" stroke=\"" + colour + "\" stroke-width=\"1.5\"" +
                           (good ? " opacity=\".55\"" : "") + " style=\"cursor:pointer\"><title>" + Esc(r.keys[k].name) + " · " +
                           WordOf(judged.verdict) + "</title></circle>";
                };
                const KeyStats& st = judged.stats;
                if (st.stuck)
                {
                    s += dot(std::min(x0 + 30, right - inset), yR, false);
                    continue;
                }
                if (st.medianMargin)
                    s += dot(xr(*st.medianMargin), yR, false);
                if (st.pressMargin)
                    s += dot(xp(*st.pressMargin), yP, false);
                else if (st.closest)
                    s += dot(xp(-std::max(0, *st.closest)) - (*st.closest <= 0 ? 6 : 0), yP, true);
            }
            return s + "</svg><div class=\"muted\" style=\"font-size:12px\">Healthy keys crowd far from the threshold on both rows. A dot in the "
                       "orange zone is at risk; a hollow dot on the pressed row never got past the threshold.</div>";
        }

        // --- A key's detail ----------------------------------------------------------------------------

        // As the section's line under its first (DrawFollowLine()): at rest where it has now.
        std::string Page::FollowLine(size_t key) const
        {
            const ReportKey&   k      = r.keys[key];
            const JudgedKey*   judged = Judged(key);
            std::optional<int> rest, highest, lowest;
            if (judged && judged->stats.restMedian)
                rest = *judged->stats.restMedian;
            if (levels[key].Max())
                highest = *levels[key].Max();
            if (levels[key].Min())
                lowest = *levels[key].Min();
            const auto num = [](std::optional<int> value, const char* word, const char* colour)
            {
                return "<div class=\"num\"><b" + (colour ? " style=\"color:var(" + std::string(colour) + ")\"" : std::string()) + ">" +
                       (value ? I(*value) : "–") + "</b><span>" + word + "</span></div>";
            };
            const KeyVerdict verdict = VerdictOf(key);
            std::string h = "<div class=\"follow\"><span class=\"key-name\">" + Esc(k.name) + " <span class=\"v-" + ClassOf(verdict) +
                            "\" style=\"font-size:13px;font-weight:400\">· " + WordOf(verdict) + "</span><span class=\"place\">" + Esc(Place(key)) +
                            "</span></span>" + num(rest, "at rest", nullptr) + num(highest, "highest", nullptr) + num(lowest, "lowest", nullptr) +
                            num(int(k.threshold), "threshold", "--warn");
            if (r.info.thresholds.size() > 1)
                h += num(int(k.bin), "bin", "--ok");

            // Its readings on one line: the resting band, the threshold, the highest ring, at rest the dot.
            if (rest && judged)
            {
                const int half = std::max(1, judged->stats.restSpread / 2);
                int lo = std::min({ int(k.threshold), *rest - half, lowest.value_or(*rest) }) - 8;
                int hi = std::max({ int(k.threshold), *rest + half, highest.value_or(*rest) }) + 8;
                const double W = 240;
                const auto   X = [&](int level) { return 6 + (W - 12) * double(level - lo) / double(hi - lo); };
                h += "<div style=\"margin-left:auto\"><svg width=\"" + N(W) + "\" height=\"24\"><line x1=\"6\" x2=\"" + N(W - 6) +
                     "\" y1=\"12\" y2=\"12\" stroke=\"var(--grid)\" stroke-width=\"2\"/><rect x=\"" + N(X(*rest - half)) + "\" y=\"7\" width=\"" +
                     N(std::max(2.0, X(*rest + half) - X(*rest - half))) + "\" height=\"10\" rx=\"2\" fill=\"var(--muted)\" opacity=\".5\"/><line x1=\"" +
                     N(X(k.threshold)) + "\" x2=\"" + N(X(k.threshold)) + "\" y1=\"1\" y2=\"23\" stroke=\"var(--warn)\" stroke-width=\"2\"/>" +
                     (highest ? "<circle cx=\"" + N(X(*highest)) + "\" cy=\"12\" r=\"4.5\" fill=\"none\" stroke=\"var(--press)\" stroke-width=\"2\"/>" : "") +
                     "<circle cx=\"" + N(X(*rest)) + "\" cy=\"12\" r=\"5\" fill=\"var(--text)\"/></svg><div class=\"muted\" style=\"font-size:11px\">"
                     "rest · threshold · highest ○ · at rest ●</div></div>";
            }
            return h + "</div>";
        }

        // Its last presses side by side (DrawKeyLevels()), from its own readings.
        std::string Page::Presses(size_t key) const
        {
            const ReportKey&                k      = r.keys[key];
            const JudgedKey*                judged = Judged(key);
            const std::span<const uint16_t> trace  = TraceOf(key);
            if (!judged || trace.empty())
                return "<div class=\"muted\">No readings.</div>";
            const KeyStats& st = judged->stats;
            const size_t    n  = trace.size();

            int lo = k.threshold, hi = k.threshold;
            for (const uint16_t level : trace)
                if (level > 0)
                {
                    lo = std::min<int>(lo, level);
                    hi = std::max<int>(hi, level);
                }
            if (k.bin < r.info.referenceLevels.size())
            {
                lo = std::min<int>(lo, r.info.referenceLevels[k.bin]);
                hi = std::max<int>(hi, r.info.referenceLevels[k.bin]);
            }
            lo -= 5;
            hi += 5;

            const double lineH = 14, top = 14, bottom = 3 * lineH + 8, H = top + 90 + bottom, wordsY = H - bottom + 16;
            const auto   Y     = [&](int v) { return top + (1.0 - double((v > 0 ? v : lo) - lo) / (hi - lo)) * (H - top - bottom); };
            const auto   dashed = [&](double x1, double x2)
            {
                return "<line x1=\"" + N(x1) + "\" x2=\"" + N(x2) + "\" y1=\"" + N(Y(k.threshold)) + "\" y2=\"" + N(Y(k.threshold)) +
                       "\" stroke=\"var(--err)\" stroke-dasharray=\"5 3\"/>";
            };

            std::vector<bool> inPress(n);
            for (const PressAttempt& press : st.presses)
                for (size_t x = press.first; x <= press.last && x < n; ++x)
                    inPress[x] = true;
            std::optional<int> rLo, rHi;
            for (size_t i = 0; i < n; ++i)
                if (!inPress[i])
                {
                    rLo = std::min<int>(rLo.value_or(trace[i]), trace[i]);
                    rHi = std::max<int>(rHi.value_or(trace[i]), trace[i]);
                }

            std::string s = "<text x=\"4\" y=\"10\" fill=\"var(--muted)\">at rest</text>";
            if (rLo && st.restMedian)
                s += "<rect x=\"10\" y=\"" + N(std::min(Y(*rHi), Y(*rLo))) + "\" width=\"28\" height=\"" + N(std::abs(Y(*rHi) - Y(*rLo)) + 2) +
                     "\" fill=\"var(--muted)\" opacity=\".35\"/><line x1=\"10\" x2=\"38\" y1=\"" + N(Y(*st.restMedian)) + "\" y2=\"" +
                     N(Y(*st.restMedian)) + "\" stroke=\"var(--muted)\" stroke-width=\"2\"/><text x=\"24\" y=\"" + N(wordsY) +
                     "\" text-anchor=\"middle\" fill=\"var(--muted)\">" + I(*rLo) + "-" + I(*rHi) + "</text>";
            s += dashed(6, 42);

            const std::vector<PressAttempt>& presses = st.stuck ? std::vector<PressAttempt>{} : st.presses;
            double width = 380;
            if (presses.empty())
            {
                s += "<text x=\"60\" y=\"" + N(H / 2) + "\" fill=\"var(--muted)\">" +
                     std::string(st.stuck ? "Seen pressed all the time: no press to show" : "Not pressed") + "</text>";
            }
            else
            {
                const size_t first = presses.size() > 4 ? presses.size() - 4 : 0;
                const double pw = 108, gap = 8, x0 = 52;
                width = x0 + double(presses.size() - first) * (pw + gap);
                for (size_t j = first; j < presses.size(); ++j)
                {
                    const PressAttempt& press = presses[j];
                    const size_t a = press.first >= 2 ? press.first - 2 : 0, z = std::min(n - 1, press.last + 2);
                    const double left = x0 + double(j - first) * (pw + gap);
                    const auto   X    = [&](size_t i) { return left + double(i - a) / double(std::max<size_t>(1, z - a)) * pw; };
                    s += "<rect x=\"" + N(left - 2) + "\" y=\"" + N(top - 4) + "\" width=\"" + N(pw + 4) + "\" height=\"" + N(H - top - bottom + 8) +
                         "\" rx=\"4\" fill=\"none\" stroke=\"var(--grid)\"/>" + dashed(left - 2, left + pw + 2) +
                         "<polyline fill=\"none\" stroke=\"" + (press.registered ? "var(--press)" : "var(--muted)") + "\" stroke-width=\"1.6\" points=\"";
                    for (size_t i = a; i <= z; ++i)
                        s += N(X(i)) + "," + N(Y(trace[i])) + " ";
                    s += "\"/>";
                    for (size_t i = a; i <= z; ++i)
                        if (std::find(st.outliers.begin(), st.outliers.end(), i) != st.outliers.end())
                            s += "<circle cx=\"" + N(X(i)) + "\" cy=\"" + N(Y(trace[i])) + "\" r=\"4\" fill=\"none\" stroke=\"var(--checkline)\" stroke-width=\"1.5\"/>";
                    const std::string what = !press.registered ? "not across"
                                             : press.held      ? "held " + I(r.scale.direction * (*press.held - k.threshold)) + " past"
                                                               : "too short to measure";
                    const auto centred = [&](const std::string& text, double y, const char* colour)
                    {
                        return "<text x=\"" + N(left + pw / 2) + "\" y=\"" + N(y) + "\" text-anchor=\"middle\" fill=\"" + colour + "\">" + text + "</text>";
                    };
                    s += centred(what, wordsY, press.registered ? "var(--ok)" : "var(--muted)");
                    std::vector<int> before;
                    for (size_t i = press.first; i > 0 && before.size() < 5;)
                        if (!inPress[--i])
                            before.push_back(trace[i]);
                    for (size_t i = press.last + 1; before.empty() && i < n && i <= press.last + 5; ++i)
                        if (!inPress[i])
                            before.push_back(trace[i]);
                    if (!before.empty())
                    {
                        std::sort(before.begin(), before.end());
                        const int margin = r.scale.direction * (k.threshold - before[before.size() / 2]);
                        s += centred("rest " + I(margin) + (r.scale.direction > 0 ? " below" : " above"), wordsY + lineH, "var(--muted)");
                    }
                    if (press.first < r.times.size())
                        s += centred("at " + Clock(r.times[press.first]), wordsY + 2 * lineH, "var(--muted)");
                }
            }

            std::string caption = judged->verdict == KeyVerdict::Good ? std::string() : ReasonsOf(*judged, ". ", MoverOf(key));
            if (!caption.empty())
                caption += ". ";
            if (!presses.empty())
                caption += "The dashed line: its threshold, " + I(k.threshold) + ". The band at rest: its resting readings, the line its usual level.";
            if (!st.outliers.empty())
                caption += " Circled: the spring's overshoot, left out.";
            return "<svg width=\"" + N(width) + "\" height=\"" + N(H) + "\" style=\"font:11px Arimo,Arial,sans-serif\">" + s + "</svg><div class=\"muted\" "
                   "style=\"font-size:12px;line-height:1.45\">" + Esc(caption) + "</div>";
        }

        // What the other keys' presses do to it, and what its own moves (DrawMovedBy()).
        std::string Page::MovedBy(size_t key) const
        {
            const JudgedKey* judged = Judged(key);
            if (!judged)
                return {};
            std::string h;

            // Its own press, in words: on paper the dashes on the board are not there.
            std::string moves;
            for (const KeyCoupling& coupling : r.health.couplings)
                if (coupling.pressed == key)
                    moves += (moves.empty() ? "" : ", ") + Esc(r.keys[coupling.moved].name);
            if (judged->pressesAlone < c_CoupledPresses)
                h += "<div class=\"say muted\">Pressed alone " + std::string(judged->pressesAlone == 0 ? "never" : "once") +
                     ": what its press moves is said from its second press.</div>";
            else if (!moves.empty())
                h += "<div class=\"say\">Its press moves " + moves + " -- dashed on the board.</div>";
            else
                h += "<div class=\"say muted\">Its press moves no other key.</div>";

            std::vector<const KeyCoupling*> moving;
            for (const KeyCoupling& coupling : r.health.couplings)
                if (coupling.moved == key)
                    moving.push_back(&coupling);
            h += "<h4>While other keys are pressed</h4>";
            const KeyStats& st = judged->stats;
            if (moving.empty() || !st.medianMargin)
                return h + "<div class=\"say muted\">No other key's press moves it.</div>";

            const KeyCoupling& worst = *moving.front();
            const std::string  by    = Esc(r.keys[worst.pressed].name);
            if (worst.left <= 0)
                h += "<div class=\"say\">It goes past its threshold each time " + by + " is pressed: it types by itself.</div>";
            else if (worst.left < r.margins.rest)
                h += "<div class=\"say\">" + by + " brings it close to its threshold: it may type by itself while " + by + " is held.</div>";
            else
            {
                std::string names;
                for (size_t i = 0; i < moving.size(); ++i)
                    names += (i == 0 ? "" : i + 1 == moving.size() ? " and " : ", ") + Esc(r.keys[moving[i]->pressed].name);
                h += "<div class=\"say muted\">" + names + " move" + (moving.size() > 1 ? "" : "s") + " it a little: it stays well short of its threshold.</div>";
            }

            const double rowH = 20, labelW = 110, width = 380, top = 4, height = top + rowH * double(moving.size() + 1);
            const int    far = *st.medianMargin + std::max(3, st.restSpread) + 3, past = 4;
            const auto   X   = [&](int margin)
            {
                const int clamped = std::clamp(margin, -past, far);
                return labelW + (width - labelW - 10) * double(far - clamped) / double(far + past);
            };
            std::string s = "<rect x=\"" + N(X(r.margins.rest)) + "\" y=\"" + N(top) + "\" width=\"" + N(X(0) - X(r.margins.rest)) + "\" height=\"" +
                            N(height - top) + "\" fill=\"var(--warn)\" opacity=\".12\"/><line x1=\"" + N(X(0)) + "\" x2=\"" + N(X(0)) +
                            "\" y1=\"0\" y2=\"" + N(height) + "\" stroke=\"var(--warn)\" stroke-width=\"2\" stroke-dasharray=\"4 3\"/>";
            double y = top + rowH / 2;
            const int half = std::max(1, st.restSpread / 2);
            s += "<text x=\"" + N(labelW - 8) + "\" y=\"" + N(y + 4) + "\" text-anchor=\"end\" fill=\"var(--muted)\">nothing pressed</text><rect x=\"" +
                 N(X(*st.medianMargin + half)) + "\" y=\"" + N(y - 5) + "\" width=\"" + N(std::max(3.0, X(*st.medianMargin - half) - X(*st.medianMargin + half))) +
                 "\" height=\"10\" rx=\"2\" fill=\"var(--muted)\" opacity=\".45\"/>";
            for (const KeyCoupling* coupling : moving)
            {
                y += rowH;
                const std::string tie    = TieOf(coupling->left, r.margins);
                const std::string colour = tie == "safe" ? "var(--text)" : tie == "risk" ? "var(--warn)" : "var(--err)";
                const double      from = X(*st.medianMargin), to = X(coupling->left);
                s += "<text x=\"" + N(labelW - 8) + "\" y=\"" + N(y + 4) + "\" text-anchor=\"end\" fill=\"var(--text)\">" +
                     Esc(r.keys[coupling->pressed].name) + " pressed</text><line x1=\"" + N(from) + "\" x2=\"" + N(to - 5) + "\" y1=\"" + N(y) +
                     "\" y2=\"" + N(y) + "\" stroke=\"" + colour + "\" stroke-width=\"2\"/><path d=\"M" + N(to) + " " + N(y) + " l-7 -4 v8 z\" fill=\"" +
                     colour + "\"/>";
            }
            s += "<text x=\"" + N(X(0)) + "\" y=\"" + N(height + 14) + "\" text-anchor=\"middle\" fill=\"var(--warn)\">threshold</text>";
            return h + "<svg width=\"" + N(width) + "\" height=\"" + N(height + 18) + "\" style=\"font:11px Arimo,Arial,sans-serif\">" + s + "</svg>";
        }

        std::string Page::Detail(size_t key) const
        {
            std::string moves;
            for (const KeyCoupling& coupling : r.health.couplings)
                if (coupling.pressed == key)
                    moves += (moves.empty() ? "" : ",") + I(static_cast<long long>(coupling.moved)) + ":" + TieOf(coupling.left, r.margins);
            return "<section class=\"detail\" id=\"key-" + I(static_cast<long long>(key)) + "\" data-moves=\"" + moves + "\">" + FollowLine(key) +
                   Presses(key) + MovedBy(key) + "</section>";
        }

        // --- The numbers ----------------------------------------------------------------------------

        std::string Page::Tables() const
        {
            std::string h = "<details><summary>Every key's numbers</summary><table><tr><th>Key</th><th>Matrix</th><th>Controller</th>"
                            "<th class=\"n\">Bin</th><th class=\"n\">Threshold</th><th class=\"n\">At rest</th><th class=\"n\">Lowest</th>"
                            "<th class=\"n\">Highest</th><th class=\"n\">Firmest hold</th><th class=\"n\">Presses</th><th>Verdict</th></tr>";
            const auto opt = [](std::optional<int> v) { return v ? I(*v) : std::string("–"); };
            for (size_t k = 0; k < r.keys.size(); ++k)
            {
                const ReportKey& key    = r.keys[k];
                const JudgedKey* judged = Judged(k);
                std::optional<int> rest, firmest, highest, lowest;
                size_t presses = 0;
                if (judged)
                {
                    if (judged->stats.restMedian)
                        rest = *judged->stats.restMedian;
                    if (judged->stats.pressMargin)
                        firmest = key.threshold + r.scale.direction * *judged->stats.pressMargin;
                    presses = judged->stats.presses.size();
                }
                if (levels[k].Max())
                    highest = *levels[k].Max();
                if (levels[k].Min())
                    lowest = *levels[k].Min();
                h += "<tr><td>" + Esc(key.name) + "</td><td>R" + I(key.row) + " C" + I(key.column) + "</td><td>" +
                     (key.isMapped ? "R" + I(key.controllerRow) + " C" + I(key.controllerColumn) : "–") + "</td><td class=\"n\">" + I(key.bin) +
                     "</td><td class=\"n\">" + I(key.threshold) + "</td><td class=\"n\">" + opt(rest) + "</td><td class=\"n\">" + opt(lowest) +
                     "</td><td class=\"n\">" + opt(highest) + "</td><td class=\"n\">" + opt(firmest) + "</td><td class=\"n\">" +
                     I(static_cast<long long>(presses)) + "</td><td class=\"v-" + ClassOf(VerdictOf(k)) + "\">" + WordOf(VerdictOf(k)) + "</td></tr>";
            }
            h += "</table></details>";

            if (!r.health.couplings.empty())
            {
                h += "<details><summary>Keys moved by another key's press: " + I(static_cast<long long>(r.health.couplings.size())) +
                     ", the closest to their threshold first</summary><table><tr><th>At rest</th><th>Pressed</th><th class=\"n\">From its threshold, "
                     "untouched</th><th class=\"n\">Usual move</th><th class=\"n\">Most</th><th class=\"n\">Left</th><th class=\"n\">Presses</th></tr>";
                for (const KeyCoupling& c : r.health.couplings)
                {
                    const JudgedKey* moved = Judged(c.moved);
                    const std::string tie  = TieOf(c.left, r.margins);
                    h += "<tr><td><b>" + Esc(r.keys[c.moved].name) + "</b></td><td>" + Esc(r.keys[c.pressed].name) + "</td><td class=\"n\">" +
                         (moved && moved->stats.medianMargin ? I(*moved->stats.medianMargin) : "–") + "</td><td class=\"n\">" + I(c.move) +
                         "</td><td class=\"n\">" + I(c.most) + "</td><td class=\"n" + (tie == "safe" ? "" : " v-" + tie) + "\">" +
                         (c.left < 0 ? "past by " + I(-c.left) : I(c.left)) + "</td><td class=\"n\">" + I(static_cast<long long>(c.presses)) + "</td></tr>";
                }
                h += "</table></details>";
            }
            return h;
        }

        // --- The page ------------------------------------------------------------------------------

        std::string Page::Html() const
        {
            // The key shown first: the first to look at, else the first judged.
            size_t first = 0;
            for (size_t k = 0; k < r.health.keys.size(); ++k)
                if (r.health.keys[k].verdict == KeyVerdict::Failing || r.health.keys[k].verdict == KeyVerdict::AtRisk)
                {
                    first = k;
                    break;
                }

            const double seconds = r.times.empty() ? 0.0 : r.times.back();
            std::string  h = "<!DOCTYPE html>\n<html lang=\"en\" class=\"nojs\" data-first=\"" + I(static_cast<long long>(first)) +
                            "\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                            "<title>Leyden Jar diagnostics: " + Esc(r.title) + "</title><style>" + Style() + "</style></head><body><div class=\"doc\">";
            h += "<div class=\"titlebar\"><h1>Leyden Jar diagnostics: " + Esc(r.title) + "</h1><span class=\"seg js-only\">"
                 "<button data-theme-set=\"light\">Light</button><button data-theme-set=\"dark\">Dark</button></span></div>";
            h += "<p class=\"sub\">Saved by " + Esc(r.savedBy) + " on " + Esc(r.savedAt) + " · " + I(static_cast<long long>(r.times.size())) +
                 " level readings over " + (seconds < 90 ? I(std::llround(seconds)) + " seconds" : I(std::llround(seconds / 60)) + " minutes") +
                 " · threshold margins " + I(r.margins.rest) + " at rest, " + I(r.margins.press) + " pressed</p>";
            h += KeyboardPart();

            h += "<h2>Signal levels</h2><p class=\"muted js-only\">Click a key, on the board or in the list, to see its levels; the keys its press "
                 "moves are outlined in dashes. Levels puts each key's numbers on it.</p>";
            h += "<div class=\"tools\">" + Counts() + "<span class=\"right js-only\"><button data-act=\"levels\">Levels</button><span class=\"seg\">"
                 "<button data-view-set=\"board\">Board</button><button data-view-set=\"controller\">Controller</button></span></span></div>";
            const auto view = [&](const char* name, bool withLevels, const std::string& svg, const char* caption)
            {
                return "<div class=\"view\" data-view=\"" + std::string(name) + "\" data-levels=\"" + (withLevels ? "on" : "off") + "\"><div class=\"figure\">" +
                       svg + "</div><div class=\"caption nojs-only\">" + caption + "</div></div>";
            };
            h += view("board", false, BoardSvg(false), "The keyboard: the problems tinted, a key not pressed hatched.");
            h += view("board", true, BoardSvg(true), "Each key's numbers: its name; its level at rest in bold, its highest at the right; "
                                                      "its bin, its lowest at the right.");
            h += view("controller", false, GridSvg(false), "The same on the controller's connectors, its columns across, its rows down.");
            h += view("controller", true, GridSvg(true), "The controller's connectors with each key's numbers.");

            h += "<div class=\"panel\"><div class=\"split\"><div class=\"left\">" + List() + Dots() + "</div><div class=\"right js-only\" "
                 "id=\"detail-pane\"></div></div></div>";
            h += "<h2 class=\"nojs-only\">Every key</h2><div id=\"details\">";
            for (size_t k = 0; k < r.keys.size(); ++k)
                h += Detail(k);
            h += "</div>";

            h += "<h2>The numbers</h2>" + Tables();
            if (!r.readings.empty())
            {
                std::string json;
                json.reserve(r.readings.size());
                for (size_t i = 0; i < r.readings.size(); ++i)   // a "</" would end the script element
                {
                    if (r.readings[i] == '<' && i + 1 < r.readings.size() && r.readings[i + 1] == '/')
                    {
                        json += "<\\/";
                        ++i;
                        continue;
                    }
                    json += r.readings[i];
                }
                h += "<h2>The readings</h2><p class=\"muted\">Every level reading of this pass, " + I(static_cast<long long>(r.times.size())) +
                     " of them, is kept in this file as JSON (<code>&lt;script id=\"nazg-readings\"&gt;</code>): Nazg's tests replay it.</p>";
                h += std::string(c_ReadingsOpen) + json + std::string(c_ReadingsClose);
            }
            h += "</div><script>" + Script() + "</script></body></html>\n";
            return h;
        }
    }

    std::string DiagnosticsReportHtml(const DiagnosticsReport& report)
    {
        return Page(report).Html();
    }

    std::string ReadingsOfReport(const std::string& html)
    {
        const size_t open = html.find(c_ReadingsOpen);
        if (open == std::string::npos)
            return {};
        const size_t start = open + c_ReadingsOpen.size();
        const size_t close = html.find(c_ReadingsClose, start);
        if (close == std::string::npos)
            return {};
        std::string json = html.substr(start, close - start);
        for (size_t at = json.find("<\\/"); at != std::string::npos; at = json.find("<\\/", at + 2))
            json.replace(at, 3, "</");
        return json;
    }
}
